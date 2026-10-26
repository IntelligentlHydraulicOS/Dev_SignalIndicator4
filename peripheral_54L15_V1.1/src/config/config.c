#include <zephyr/drivers/flash.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>

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
static void config_flash_read()
{
    const struct flash_area *fa;
    // int rc = flash_area_open(FIXED_PARTITION_ID(storage_partition), &fa);
    int rc = flash_area_open(PARTITION_ID(storage_partition), &fa);
    if(rc != 0)
    {
        printk("[%s %d] fail to open flash\n", __func__, __LINE__);
        return;
    }

    printk("[%s %d] start 0x%lx all size 0x%x", __func__, __LINE__, fa->fa_off, fa->fa_size);

    flash_area_read(fa, 1, &nrf54l15_config, sizeof(nrf54L15_config_t));

    if(nrf54l15_config.uiTxpower > 8)
    {
        nrf54l15_config.uiTxpower = 8;
    }

    if(nrf54l15_config.uiConnectType > 0xF)
    {
        nrf54l15_config.uiConnectType = 0xF;
    }

    if(nrf54l15_config.uiCentral_number > 0xF)
    {
        nrf54l15_config.uiConnectType = 0xF;
    }

    printk("[%s %d] config: uiLocalID(%d), uiTxpower(%d), uiConnectType(%d), uiCentral_number(%d)\n", __func__, __LINE__, nrf54l15_config.uiLocalID, nrf54l15_config.uiTxpower, \
        nrf54l15_config.uiConnectType, nrf54l15_config.uiCentral_number);

    flash_area_close(fa);
}

static void config_flash_write()
{
    int iRet = 0;
    const struct flash_area *fa;
    // iRet = flash_area_open(FIXED_PARTITION_ID(storage_partition), &fa);
    iRet = flash_area_open(PARTITION_ID(storage_partition), &fa);

    if(iRet != 0)
    {
        printk("[%s %d] fail to open flash\n", __func__, __LINE__);
        return;
    }

    iRet = flash_area_write(fa, 1, &nrf54l15_config, sizeof(nrf54L15_config_t));

    if(iRet == 0)
    {
        printk("[%s %d] success to write flash\n", __func__, __LINE__);
    }
    else
    {
        printk("[%s %d] fail to write flash\n", __func__, __LINE__);
    }

    flash_area_close(fa);

}

void nrfConfigInit()
{
    config_flash_read();
    // nrf54l15_config.uiLocalID = 120;
    // nrf54l15_config.uiTxpower = 8;
    // nrf54l15_config.uiCentral_number = 10;
    // nrf54l15_config.uiConnectType = 0xF;
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
    config_flash_write();
}
