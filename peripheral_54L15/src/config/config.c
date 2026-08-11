#include <zephyr/drivers/flash.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>


#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(config);
#include "config.h"


#ifdef CONFIG_BT_MAX_CONN
#define MAX_CENTRAL_NUMBER (CONFIG_BT_MAX_CONN - 1)
#else
#define MAX_CENTRAL_NUMBER 0x0A
#endif

typedef struct tag_nrf54L15_config_t
{
    uint8_t uiLocalID;
    uint8_t uiTxpower;
    uint8_t uiConnectType;
    uint8_t uiCentral_number;
}nrf54L15_config_t;

static nrf54L15_config_t nrf54l15_config = {0};
/*
const struct device *flash_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_flash_controller));
const struct flash_area *fa;
int rc = flash_area_open(FIXED_PARTITION_ID(storage_partition), &fa);
flash_area_read(fa, 0, buf, len);
flash_area_write(fa, 0, data, len);
flash_area_close(fa);
*/
void config_flash_read()
{
    const struct flash_area *fa;
    uint8_t uiData[32] = {0};
    int rc = flash_area_open(FIXED_PARTITION_ID(storage_partition), &fa);

    if(rc != 0)
    {
        LOG_INF("fail to open flash");
        return;
    }

    LOG_INF("start 0x%lx all size 0x%x", fa->fa_off, fa->fa_size);

    flash_area_read(fa, 1, uiData, 32 * sizeof(uint8_t));

    LOG_INF("0x%x 0x%x 0x%x 0x%x 0x%x 0x%x", uiData[0], uiData[1], uiData[2], uiData[3], uiData[4], uiData[5]);

    flash_area_close(fa);

    // const struct device *flash_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_flash_controller));
    // // uint8_t write_buf[] = {0x01, 0x02, 0x03, 0x04};
    // // uint8_t read_buf[4] = {0};

    // if (!device_is_ready(flash_dev)) {
    //     LOG_INF("Flash device not ready");
    //     return;
    // }

    // size_t page_size = flash_get_write_block_size(flash_dev);
    // LOG_INF("============================= 0x%x", page_size);
}

void config_flash_write()
{
    const struct flash_area *fa;
    uint8_t uiData[32] = {0};
    int rc = flash_area_open(FIXED_PARTITION_ID(storage_partition), &fa);

    if(rc != 0)
    {
        LOG_INF("fail to open flash");
        return;
    }

    for(int i = 0; i < 32; i++)
    {
        uiData[i] = i & 0xff;
    }
    LOG_INF("start 0x%lx all size 0x%x", fa->fa_off, fa->fa_size);

    flash_area_write(fa, 1, uiData, 32 * sizeof(uint8_t));

    LOG_INF("0x%x 0x%x 0x%x 0x%x 0x%x 0x%x", uiData[0], uiData[1], uiData[2], uiData[3], uiData[4], uiData[5]);

    flash_area_close(fa);

}

void nrfConfigInit()
{
    nrf54l15_config.uiLocalID = 120;
    nrf54l15_config.uiTxpower = 8;
    nrf54l15_config.uiCentral_number = 10;
    nrf54l15_config.uiConnectType = 0xF;
}

uint8_t uiGetMaxCentralNumber()
{
    return min(MAX_CENTRAL_NUMBER, nrf54l15_config.uiCentral_number);
}

void uiSetMaxCentralNumber(uint8_t uiCentralNumber)
{
    nrf54l15_config.uiCentral_number = uiCentralNumber;
}

uint8_t uiGetLocalID()
{
    return nrf54l15_config.uiLocalID;
}

void uiSetLocalID(uint8_t uiLocalID)
{
    nrf54l15_config.uiLocalID = uiLocalID;
}

uint8_t uiGetBLE_Txpower()
{
    return nrf54l15_config.uiTxpower;
}

void uiSetBLE_Txpower(uint8_t uiTxpower)
{
    nrf54l15_config.uiTxpower = uiTxpower;
}

uint8_t uiGetSensorConnectType()
{
    return nrf54l15_config.uiConnectType;
}

void uiSetSensorConnectType(uint8_t uiConnectType)
{
    nrf54l15_config.uiConnectType = uiConnectType;
}

void saveConfig()
{

}
