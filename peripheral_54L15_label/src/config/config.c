#include <errno.h>
#include <stddef.h>

#include <zephyr/drivers/flash.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/sys/printk.h>

#include "config.h"

#define CONFIG_RECORD_MAGIC 0x53434647U
#define CONFIG_RECORD_VERSION 3U
#define CONFIG_RECORD_VERSION_V2 2U
#define CONFIG_RECORD_VERSION_LEGACY 1U
#define CONFIG_RECORD_SIZE 32U

#define MAX_CENTRAL_NUMBER 0x0A

static app_config_t nrf54l15_config;
K_MUTEX_DEFINE(uwb_settings_mutex);
static uwb_settings_t uwb_settings = {
    .start_node_id = UWB_DEFAULT_START_NODE_ID,
    .end_node_id = UWB_DEFAULT_END_NODE_ID,
    .min_interval_ms = UWB_DEFAULT_MIN_INTERVAL_MS,
    .max_interval_ms = UWB_DEFAULT_MAX_INTERVAL_MS,
    .active_window_s = UWB_DEFAULT_ACTIVE_WINDOW_S,
    .deep_sleep_s = UWB_DEFAULT_DEEP_SLEEP_S,
};

typedef struct {
    uint32_t magic;
    uint8_t version;
    app_config_t config;
    uint8_t reserved[CONFIG_RECORD_SIZE - sizeof(uint32_t) - sizeof(uint8_t) -
                     sizeof(app_config_t)];
} config_record_t;

BUILD_ASSERT(sizeof(config_record_t) == CONFIG_RECORD_SIZE);

static void config_flash_read(void)
{
    nrf54l15_config = (app_config_t) {
        .max_connections = MAX_CENTRAL_NUMBER,
    };

    const struct flash_area *fa;
    int rc = flash_area_open(PARTITION_ID(storage_partition), &fa);

    if (rc != 0) {
        printk("Config: failed to open storage partition: %d\n", rc);
        return;
    }

    config_record_t record;
    rc = flash_area_read(fa, 0, &record, sizeof(record));
    if (rc == 0 && record.magic == CONFIG_RECORD_MAGIC &&
        (record.version == CONFIG_RECORD_VERSION ||
         record.version == CONFIG_RECORD_VERSION_V2 ||
         record.version == CONFIG_RECORD_VERSION_LEGACY)) {
        nrf54l15_config = record.config;
        if (record.version >= CONFIG_RECORD_VERSION_V2) {
            uwb_settings.start_node_id = record.reserved[0];
            uwb_settings.end_node_id = record.reserved[1];
            uwb_settings.min_interval_ms =
                (uint16_t)record.reserved[2] |
                ((uint16_t)record.reserved[3] << 8);
            uwb_settings.max_interval_ms =
                (uint16_t)record.reserved[4] |
                ((uint16_t)record.reserved[5] << 8);
        }
        if (record.version == CONFIG_RECORD_VERSION) {
            uwb_settings.active_window_s =
                (uint16_t)record.reserved[6] |
                ((uint16_t)record.reserved[7] << 8);
            uwb_settings.deep_sleep_s =
                (uint16_t)record.reserved[8] |
                ((uint16_t)record.reserved[9] << 8);
        }
        flash_area_close(fa);
    } else {
        if (rc != 0) {
            printk("Config: current record read failed; checking legacy data: %d\n",
                   rc);
        }

        struct {
            uint8_t local_id;
            uint8_t tx_power;
            uint8_t connect_type;
            uint8_t max_connections;
        } legacy_config;

        rc = flash_area_read(fa, 1, &legacy_config, sizeof(legacy_config));
        if (rc != 0) {
            printk("Config: failed to read storage partition: %d\n", rc);
            flash_area_close(fa);
            return;
        }

        if (legacy_config.tx_power <= 8 && legacy_config.connect_type <= 0xF &&
            legacy_config.max_connections <= 0xF) {
            nrf54l15_config = (app_config_t) {
                .local_id = legacy_config.local_id,
                .tx_power = legacy_config.tx_power,
                .connect_type = legacy_config.connect_type,
                .max_connections = legacy_config.max_connections,
                .device_number = 0,
            };
        } else {
            nrf54l15_config = (app_config_t) {
                .max_connections = MAX_CENTRAL_NUMBER,
            };
        }

        flash_area_close(fa);
    }

    if (nrf54l15_config.tx_power > 8) {
        nrf54l15_config.tx_power = 8;
    }
    if (nrf54l15_config.connect_type > 0xF) {
        nrf54l15_config.connect_type = 0xF;
    }
    if (nrf54l15_config.max_connections > 0xF) {
        nrf54l15_config.max_connections = 0xF;
    }
    if (uwb_settings.start_node_id < 1U ||
        uwb_settings.start_node_id > UWB_MAX_TAG_NODE_ID ||
        uwb_settings.end_node_id < uwb_settings.start_node_id ||
        uwb_settings.end_node_id > UWB_MAX_TAG_NODE_ID ||
        uwb_settings.min_interval_ms == 0U ||
        uwb_settings.min_interval_ms > uwb_settings.max_interval_ms ||
        uwb_settings.max_interval_ms > UWB_MAX_INTERVAL_MS ||
        uwb_settings.active_window_s < UWB_MIN_ACTIVE_WINDOW_S ||
        uwb_settings.active_window_s > UWB_MAX_ACTIVE_WINDOW_S ||
        uwb_settings.deep_sleep_s < UWB_MIN_DEEP_SLEEP_S ||
        uwb_settings.deep_sleep_s > UWB_MAX_DEEP_SLEEP_S) {
        uwb_settings = (uwb_settings_t) {
            .start_node_id = UWB_DEFAULT_START_NODE_ID,
            .end_node_id = UWB_DEFAULT_END_NODE_ID,
            .min_interval_ms = UWB_DEFAULT_MIN_INTERVAL_MS,
            .max_interval_ms = UWB_DEFAULT_MAX_INTERVAL_MS,
            .active_window_s = UWB_DEFAULT_ACTIVE_WINDOW_S,
            .deep_sleep_s = UWB_DEFAULT_DEEP_SLEEP_S,
        };
        printk("Config: invalid UWB settings; using defaults\n");
    }
}

static int config_flash_write(void)
{
    const struct flash_area *fa;
    int ret = flash_area_open(PARTITION_ID(storage_partition), &fa);

    if (ret != 0) {
        printk("Config: failed to open storage partition: %d\n", ret);
        return ret;
    }

    config_record_t record __aligned(16) = {
        .magic = CONFIG_RECORD_MAGIC,
        .version = CONFIG_RECORD_VERSION,
        .config = nrf54l15_config,
        .reserved = {
            uwb_settings.start_node_id,
            uwb_settings.end_node_id,
            (uint8_t)(uwb_settings.min_interval_ms & 0xFFU),
            (uint8_t)(uwb_settings.min_interval_ms >> 8),
            (uint8_t)(uwb_settings.max_interval_ms & 0xFFU),
            (uint8_t)(uwb_settings.max_interval_ms >> 8),
            (uint8_t)(uwb_settings.active_window_s & 0xFFU),
            (uint8_t)(uwb_settings.active_window_s >> 8),
            (uint8_t)(uwb_settings.deep_sleep_s & 0xFFU),
            (uint8_t)(uwb_settings.deep_sleep_s >> 8),
        },
    };
    ret = flash_area_erase(fa, 0, fa->fa_size);
    if (ret != 0) {
        printk("Config: failed to erase storage partition: %d\n", ret);
        flash_area_close(fa);
        return ret;
    }

    ret = flash_area_write(fa, 0, &record, sizeof(record));
    flash_area_close(fa);
    if (ret != 0) {
        printk("Config: failed to write storage partition: %d\n", ret);
    }
    return ret;
}

void nrfConfigInit(void)
{
    config_flash_read();
    // nrf54l15_config.uiLocalID = 120;
    // nrf54l15_config.uiTxpower = 8;
    // nrf54l15_config.uiCentral_number = 10;
    // nrf54l15_config.uiConnectType = 0xF;
}

uint8_t uiGetMaxCentralNumber(void)
{
    return min(MAX_CENTRAL_NUMBER, nrf54l15_config.max_connections);
}

void uiSetMaxCentralNumber(uint8_t uiCentralNumber)
{
    nrf54l15_config.max_connections = uiCentralNumber;
}

uint8_t uiGetLocalID(void)
{
    return nrf54l15_config.local_id;
}

void uiSetLocalID(uint8_t uiLocalID)
{
    nrf54l15_config.local_id = uiLocalID;
}

uint8_t uiGetBLE_Txpower(void)
{
    return nrf54l15_config.tx_power;
}

void uiSetBLE_Txpower(uint8_t uiTxpower)
{
    nrf54l15_config.tx_power = uiTxpower;
}

uint8_t uiGetSensorConnectType(void)
{
    return nrf54l15_config.connect_type;
}

void uiSetSensorConnectType(uint8_t uiConnectType)
{
    nrf54l15_config.connect_type = uiConnectType;
}

int nrfConfigGet(app_config_t *config)
{
    if (config == NULL) {
        return -EINVAL;
    }

    *config = nrf54l15_config;
    return 0;
}

int nrfConfigSet(const app_config_t *config)
{
    if (config == NULL || config->tx_power > 8 || config->connect_type > 0xF ||
        config->max_connections > 0xF) {
        return -EINVAL;
    }

    app_config_t previous = nrf54l15_config;
    nrf54l15_config = *config;

    int ret = config_flash_write();
    if (ret != 0) {
        nrf54l15_config = previous;
    }
    return ret;
}

int uwb_settings_get(uwb_settings_t *settings)
{
    if (settings == NULL) {
        return -EINVAL;
    }

    k_mutex_lock(&uwb_settings_mutex, K_FOREVER);
    *settings = uwb_settings;
    k_mutex_unlock(&uwb_settings_mutex);
    return 0;
}

int uwb_settings_set(const uwb_settings_t *settings)
{
    if (settings == NULL ||
        settings->start_node_id < 1U ||
        settings->start_node_id > UWB_MAX_TAG_NODE_ID ||
        settings->end_node_id < settings->start_node_id ||
        settings->end_node_id > UWB_MAX_TAG_NODE_ID ||
        settings->min_interval_ms < UWB_MIN_INTERVAL_MS ||
        settings->min_interval_ms > settings->max_interval_ms ||
        settings->max_interval_ms < settings->min_interval_ms ||
        settings->max_interval_ms > UWB_MAX_INTERVAL_MS ||
        settings->active_window_s < UWB_MIN_ACTIVE_WINDOW_S ||
        settings->active_window_s > UWB_MAX_ACTIVE_WINDOW_S ||
        settings->deep_sleep_s < UWB_MIN_DEEP_SLEEP_S ||
        settings->deep_sleep_s > UWB_MAX_DEEP_SLEEP_S) {
        printk("Config: invalid UWB settings [%u-%u, %u-%u, %u-%u, %u-%u]\n",
               settings->start_node_id, settings->end_node_id,
               settings->min_interval_ms, settings->max_interval_ms,
               settings->active_window_s, settings->deep_sleep_s);
        return -EINVAL;
    }

    k_mutex_lock(&uwb_settings_mutex, K_FOREVER);
    uwb_settings_t previous = uwb_settings;
    uwb_settings = *settings;

    int ret = config_flash_write();
    if (ret != 0) {
        uwb_settings = previous;
    }
    k_mutex_unlock(&uwb_settings_mutex);
    return ret;
}

void saveConfig(void)
{
    int ret = config_flash_write();
    if (ret != 0) {
        printk("Config: save failed: %d\n", ret);
    }
}
