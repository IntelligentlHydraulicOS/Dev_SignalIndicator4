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
#define CONFIG_RECORD_VERSION 1U
#define CONFIG_RECORD_SIZE 16U

#define MAX_CENTRAL_NUMBER 0x0A

static app_config_t nrf54l15_config;

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
        record.version == CONFIG_RECORD_VERSION) {
        nrf54l15_config = record.config;
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
    };
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

void saveConfig(void)
{
    int ret = config_flash_write();
    if (ret != 0) {
        printk("Config: save failed: %d\n", ret);
    }
}
