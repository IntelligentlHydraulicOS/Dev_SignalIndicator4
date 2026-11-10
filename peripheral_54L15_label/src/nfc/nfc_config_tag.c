#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include <nfc_t4t_lib.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "config.h"
#include "nfc_config_tag.h"
#include "fuel_gauge.h"
#include "dw3000_node.h"
#include "power_mgr.h"

#define NFC_NDEF_FILE_SIZE 256U
#define NFC_UPDATE_QUEUE_DEPTH 2
#define NFC_CONFIG_PIN CONFIG_APP_NFC_CONFIG_PIN

typedef struct {
	uint16_t length;
	uint8_t data[NFC_NDEF_FILE_SIZE];
} nfc_update_t;

K_MSGQ_DEFINE(nfc_update_queue, sizeof(nfc_update_t), NFC_UPDATE_QUEUE_DEPTH, 4);
K_THREAD_STACK_DEFINE(nfc_config_stack, 1536);
static struct k_thread nfc_config_thread;

static uint8_t ndef_files[2][NFC_NDEF_FILE_SIZE];
static uint8_t active_ndef_file;
static char device_id[17];
static bool nfc_config_tag_ready;
K_MUTEX_DEFINE(ndef_file_mutex);

static bool nfc_pin_is_configured(void)
{
	size_t length = strlen(NFC_CONFIG_PIN);

	if (length < 4 || length > 12) {
		return false;
	}

	for (size_t i = 0; i < length; i++) {
		if (NFC_CONFIG_PIN[i] < '0' || NFC_CONFIG_PIN[i] > '9') {
			return false;
		}
	}

	return true;
}

static int ndef_file_encode_text(uint8_t *file, size_t file_size,
				 const char *text)
{
	size_t text_length = strlen(text);
	size_t payload_length = text_length + 3;
	size_t record_length = payload_length + 4;

	if (payload_length > UINT8_MAX || record_length + 2 > file_size) {
		return -EMSGSIZE;
	}

	file[0] = (uint8_t)(record_length >> 8);
	file[1] = (uint8_t)record_length;
	file[2] = 0xD1;
	file[3] = 1;
	file[4] = (uint8_t)payload_length;
	file[5] = 'T';
	file[6] = 0x02;
	file[7] = 'e';
	file[8] = 'n';
	memcpy(&file[9], text, text_length);

	return 0;
}

static int ndef_file_set_info_impl(void)
{
	uwb_settings_t settings;
	fuel_gauge_data_t battery;
	uint8_t mac[6];
	char text[224];
	uint8_t next_file = active_ndef_file ^ 1U;

	int ret = uwb_settings_get(&settings);
	if (ret != 0) {
		return ret;
	}

	get_LocalMacAddr(mac);
	fuel_gauge_data_get(&battery);

	ret = snprintf(text, sizeof(text),
		       "INFO1;device_id=%s;node_id=%u;"
		       "mac=%02X:%02X:%02X:%02X:%02X:%02X;"
		       "uwb_start_node_id=%u;uwb_end_node_id=%u;"
		       "min_interval_ms=%u;max_interval_ms=%u;"
		       "active_window_s=%u;deep_sleep_s=%u;"
		       "battery_level=%.1f;battery_health=%.1f",
		       device_id,
		       (unsigned int)dw3000_node_get_current_tag_node_id(),
		       mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
		       (unsigned int)settings.start_node_id,
		       (unsigned int)settings.end_node_id,
		       (unsigned int)settings.min_interval_ms,
		       (unsigned int)settings.max_interval_ms,
		       (unsigned int)settings.active_window_s,
		       (unsigned int)settings.deep_sleep_s,
		       (double)battery.soc, (double)battery.soh);
	if (ret < 0 || (size_t)ret >= sizeof(text)) {
		return -EMSGSIZE;
	}

	ret = ndef_file_encode_text(ndef_files[next_file], sizeof(ndef_files[next_file]), text);
	if (ret != 0) {
		return ret;
	}

	unsigned int key = irq_lock();
	ret = nfc_t4t_ndef_rwpayload_set(ndef_files[next_file],
					 sizeof(ndef_files[next_file]));
	irq_unlock(key);
	if (ret != 0) {
		return ret;
	}

	active_ndef_file = next_file;
	return 0;
}

static int ndef_file_set_info(void)
{
	k_mutex_lock(&ndef_file_mutex, K_FOREVER);
	int ret = ndef_file_set_info_impl();
	k_mutex_unlock(&ndef_file_mutex);
	return ret;
}

int nfc_config_tag_refresh_info(void)
{
	if (!nfc_config_tag_ready) {
		return -EAGAIN;
	}

	return ndef_file_set_info();
}

static int ndef_text_get(const uint8_t *message, size_t message_length,
			 char *text, size_t text_size)
{
	if (message == NULL || message_length < 7 || text_size == 0 ||
	    message[0] != 0xD1 || message[1] != 1 || message[3] != 'T') {
		printk("NFC: ndef_text_get: invalid NDEF record header or input "
		       "(message=%p len=%u text_size=%u)\n",
		       message, (unsigned int)message_length,
		       (unsigned int)text_size);
		return -EINVAL;
	}

	size_t payload_length = message[2];
	if (message_length != payload_length + 4 || payload_length < 3) {
		printk("NFC: ndef_text_get: invalid payload length (%u, expected %u)\n",
		       (unsigned int)message_length,
		       (unsigned int)(payload_length + 4));
		return -EINVAL;
	}

	uint8_t status = message[4];
	size_t language_length = status & 0x3FU;
	if ((status & 0xC0U) != 0 || payload_length < language_length + 1) {
		printk("NFC: ndef_text_get: invalid text status or language length\n");
		return -EINVAL;
	}

	size_t text_length = payload_length - language_length - 1;
	if (text_length == 0 || text_length >= text_size) {
		printk("NFC: ndef_text_get: invalid text length (%u, capacity %u)\n",
		       (unsigned int)text_length, (unsigned int)text_size);
		return -EMSGSIZE;
	}

	const uint8_t *text_data = &message[5 + language_length];
	if (memchr(text_data, '\0', text_length) != NULL) {
		const uint8_t *nul = memchr(text_data, '\0', text_length);
		printk("NFC: ndef_text_get: embedded NUL at text offset %u "
		       "(record length %u, payload length %u)\n",
		       (unsigned int)(nul - text_data),
		       (unsigned int)message_length,
		       (unsigned int)payload_length);
		return -EINVAL;
	}

	memcpy(text, text_data, text_length);
	text[text_length] = '\0';
	return 0;
}

static int parse_u8(const char *value, uint8_t maximum, uint8_t *result)
{
	if (value == NULL || *value == '\0') {
		return -EINVAL;
	}

	unsigned int parsed = 0;
	for (const char *p = value; *p != '\0'; p++) {
		if (*p < '0' || *p > '9') {
			return -EINVAL;
		}
		unsigned int digit = (unsigned int)(*p - '0');
		if (digit > maximum || parsed > (maximum - digit) / 10U) {
			return -ERANGE;
		}
		parsed = parsed * 10U + digit;
	}

	*result = (uint8_t)parsed;
	return 0;
}

static int parse_u16(const char *value, uint16_t maximum, uint16_t *result)
{
	if (value == NULL || *value == '\0') {
		return -EINVAL;
	}

	uint32_t parsed = 0;
	for (const char *p = value; *p != '\0'; p++) {
		if (*p < '0' || *p > '9') {
			return -EINVAL;
		}
		uint32_t digit = (uint32_t)(*p - '0');
		if (digit > maximum || parsed > (maximum - digit) / 10U) {
			return -ERANGE;
		}
		parsed = parsed * 10U + digit;
	}

	*result = (uint16_t)parsed;
	return 0;
}

static int ndef_config_parse(char *text)
{
	enum {
		SEEN_PIN = BIT(0),
		SEEN_START_NODE_ID = BIT(1),
		SEEN_END_NODE_ID = BIT(2),
		SEEN_MIN_INTERVAL = BIT(3),
		SEEN_MAX_INTERVAL = BIT(4),
		SEEN_ACTIVE_WINDOW = BIT(5),
		SEEN_DEEP_SLEEP = BIT(6),
	};

	char *saveptr = NULL;
	char *token = strtok_r(text, ";", &saveptr);
	if (token == NULL || strcmp(token, "CFG1") != 0) {
		printk("NFC: ndef_config_parse: missing or invalid CFG1 header\n");
		return -EINVAL;
	}

	uwb_settings_t settings;
	int ret = uwb_settings_get(&settings);
	if (ret != 0) {
		printk("NFC: ndef_config_parse: failed to get UWB settings: %d\n", ret);
		return ret;
	}

	uint32_t seen = 0;
	while ((token = strtok_r(NULL, ";", &saveptr)) != NULL) {
		char *separator = strchr(token, '=');
		if (separator == NULL || separator == token || separator[1] == '\0') {
			printk("NFC: ndef_config_parse: malformed field '%s'\n", token);
			return -EINVAL;
		}

		*separator = '\0';
		const char *value = separator + 1;
		uint32_t field = 0;
		uint8_t node_id;
		uint16_t interval_ms;

		if (strcmp(token, "pin") == 0) {
			field = SEEN_PIN;
			if (!nfc_pin_is_configured() || strcmp(value, NFC_CONFIG_PIN) != 0) {
				printk("NFC: ndef_config_parse: PIN is not configured or does not match\n");
				return -EACCES;
			}
		} else if (strcmp(token, "uwb_start_node_id") == 0) {
			field = SEEN_START_NODE_ID;
			ret = parse_u8(value, UWB_MAX_TAG_NODE_ID, &node_id);
			if (ret == 0) {
				settings.start_node_id = node_id;
			}
		} else if (strcmp(token, "uwb_end_node_id") == 0) {
			field = SEEN_END_NODE_ID;
			ret = parse_u8(value, UWB_MAX_TAG_NODE_ID, &node_id);
			if (ret == 0) {
				settings.end_node_id = node_id;
			}
		} else if (strcmp(token, "min_interval_ms") == 0) {
			field = SEEN_MIN_INTERVAL;
			ret = parse_u16(value, UWB_MAX_INTERVAL_MS,
					&interval_ms);
			if(value < UWB_MIN_INTERVAL_MS) {
				printk("NFC: ndef_config_parse: min_interval_ms is too small\n");
				ret = -EINVAL;
			}
			if (ret == 0) {
				settings.min_interval_ms = interval_ms;
			}
		} else if (strcmp(token, "max_interval_ms") == 0) {
			field = SEEN_MAX_INTERVAL;
			ret = parse_u16(value, UWB_MAX_INTERVAL_MS,
					&interval_ms);
			if(value < UWB_MIN_INTERVAL_MS) {
				printk("NFC: ndef_config_parse: min_interval_ms is too small\n");
				ret = -EINVAL;
			}
			if (ret == 0) {
				settings.max_interval_ms = interval_ms;
			}
		} else if (strcmp(token, "active_window_s") == 0) {
			field = SEEN_ACTIVE_WINDOW;
			ret = parse_u16(value, UWB_MAX_ACTIVE_WINDOW_S,
					&interval_ms);
			if (ret == 0) {
				settings.active_window_s = interval_ms;
			}
		} else if (strcmp(token, "deep_sleep_s") == 0) {
			field = SEEN_DEEP_SLEEP;
			ret = parse_u16(value, UWB_MAX_DEEP_SLEEP_S,
					&interval_ms);
			if (ret == 0) {
				settings.deep_sleep_s = interval_ms;
			}
		} else {
			printk("NFC: ndef_config_parse: unknown field '%s'\n", token);
			return -EINVAL;
		}

		if ((seen & field) != 0) {
			printk("NFC: ndef_config_parse: duplicate field '%s'\n", token);
			return -EINVAL;
		}
		seen |= field;

		if (field != SEEN_PIN && ret != 0) {
			printk("NFC: ndef_config_parse: invalid value for '%s': '%s' (%d)\n",
			       token, value, ret);
			return ret;
		}
	}

	if ((seen & SEEN_PIN) == 0) {
		printk("NFC: ndef_config_parse: required PIN field is missing\n");
		return -EINVAL;
	}
	if ((seen & ~(uint32_t)SEEN_PIN) == 0) {
		return 0;
	}

	ret = uwb_settings_set(&settings);
	if (ret != 0) {
		printk("NFC: ndef_config_parse: failed to save UWB settings: %d\n", ret);
		return ret;
	}

	ret = dw3000_node_set_tag_node_range(settings.start_node_id,
					     settings.end_node_id);
	if (ret != 0) {
		printk("NFC: ndef_config_parse: failed to apply node range: %d\n", ret);
		return ret;
	}

	power_mgr_settings_changed();
	return ret;
}

static void nfc_callback(void *context, nfc_t4t_event_t event,
			 const uint8_t *data, size_t data_length, uint32_t flags)
{
	ARG_UNUSED(context);
	ARG_UNUSED(flags);

	if (event != NFC_T4T_EVENT_NDEF_UPDATED || data_length == 0) {
		return;
	}

	if (data == NULL || data_length > NFC_NDEF_FILE_SIZE - 2U) {
		printk("NFC: rejected oversized or empty NDEF update\n");
		nfc_update_t rejected = {0};
		int ret = k_msgq_put(&nfc_update_queue, &rejected, K_NO_WAIT);
		if (ret != 0) {
			printk("NFC: update queue full; tag data will be restored "
			       "after queued updates\n");
		}
		return;
	}

	/*
	 * The T4T callback reports NLEN as data_length, while the supplied
	 * buffer starts with the two-byte NLEN field.
	 */
	size_t update_length = data_length + 2U;
	nfc_update_t update = {
		.length = (uint16_t)update_length,
	};
	memcpy(update.data, data, update_length);

	printk("NFC write:[");
	for (size_t i = 0; i < update_length; i++)
	{
		printk("%02X ", data[i]);
	}
	printk("]\r\n");

	int ret = k_msgq_put(&nfc_update_queue, &update, K_NO_WAIT);
	if (ret != 0) {
		printk("NFC: update queue full; NDEF update was not applied\n");
	}
}

static void nfc_config_worker(void *arg1, void *arg2, void *arg3)
{
	ARG_UNUSED(arg1);
	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);

	nfc_update_t update;
	char text[NFC_NDEF_FILE_SIZE];

	for (;;) {
		k_msgq_get(&nfc_update_queue, &update, K_FOREVER);

		const uint8_t *ndef_message = update.data;
		size_t ndef_length = update.length;

		if (ndef_length >= 2 && ndef_message[2] == 0xD1) {
			uint16_t nlen = ((uint16_t)ndef_message[0] << 8) |
					ndef_message[1];
			if (nlen == 0) {
				continue;
			}
			if ((size_t)nlen + 2U > ndef_length) {
				printk("NFC: invalid NLEN %u for update length %u\n",
				       (unsigned int)nlen, (unsigned int)ndef_length);
				continue;
			}
			ndef_message += 2;
			ndef_length = nlen;
		}

		int ret = ndef_text_get(ndef_message, ndef_length,
					text, sizeof(text));
		if (ret == 0) {
			ret = ndef_config_parse(text);
			if (ret == 0) {
				printk("NFC: configuration updated and saved\n");
			}
		}

		ret = ndef_file_set_info();
		if (ret != 0) {
			printk("NFC: failed to refresh tag data: %d\n", ret);
		}
	}
}

int nfc_config_tag_init(void)
{
	uint8_t id[8];
	uwb_settings_t settings;
	int ret;
	ssize_t id_length = hwinfo_get_device_id(id, sizeof(id));
	if (id_length <= 0 || (size_t)id_length > sizeof(id)) {
		printk("NFC: could not read chip unique ID: %d\n", (int)id_length);
		return id_length < 0 ? (int)id_length : -EIO;
	}

	for (ssize_t i = 0; i < id_length; i++) {
		snprintf(&device_id[i * 2], sizeof(device_id) - (size_t)i * 2,
			 "%02X", id[i]);
	}
	device_id[id_length * 2] = '\0';

	ret = uwb_settings_get(&settings);
	if (ret != 0) {
		printk("NFC: could not get UWB settings: %d\n", ret);
		return ret;
	}
	ret = dw3000_node_set_tag_node_range(settings.start_node_id,
					     settings.end_node_id);
	if (ret != 0) {
		printk("NFC: could not apply tag node range: %d\n", ret);
		return ret;
	}

	ret = nfc_t4t_setup(nfc_callback, NULL);
	if (ret != 0) {
		printk("NFC: Type 4 Tag setup failed: %d\n", ret);
		return ret;
	}

	ret = ndef_file_set_info();
	if (ret != 0) {
		printk("NFC: could not set initial NDEF payload: %d\n", ret);
		return ret;
	}

	ret = nfc_t4t_emulation_start();
	if (ret != 0) {
		printk("NFC: Type 4 Tag emulation start failed: %d\n", ret);
		return ret;
	}

	nfc_config_tag_ready = true;
	k_thread_create(&nfc_config_thread, nfc_config_stack,
			K_THREAD_STACK_SIZEOF(nfc_config_stack), nfc_config_worker,
			NULL, NULL, NULL, K_PRIO_PREEMPT(7), 0, K_NO_WAIT);
	k_thread_name_set(&nfc_config_thread, "nfc_config");

	if (!nfc_pin_is_configured()) {
		printk("NFC: tag is readable; configuration writes are disabled until "
		       "APP_NFC_CONFIG_PIN is set to 4-12 digits\n");
	} else {
		printk("NFC: writable Type 4 tag started\n");
	}

	return 0;
}
