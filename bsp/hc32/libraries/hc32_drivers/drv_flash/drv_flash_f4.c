/*
 * Copyright (C) 2022-2024, Xiaohua Semiconductor Co., Ltd.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date             Author      Notes
 * 2022-04-28       CDT         First version
 * 2024-06-14       CDT         Fixed sector number calculation
 * 2024-06-18       CDT         Support HC32F460,HC32F448,HC32F472
 * 2025-07-16       CDT         Support HC32F334
 */

#include "board.h"

#if defined(BSP_USING_ON_CHIP_FLASH)
#include "drv_config.h"
#include "drv_flash.h"

#if defined(RT_USING_FAL)
    #include "fal.h"
#endif

//#define DRV_DEBUG
#define LOG_TAG                 "drv.flash"
#include <drv_log.h>

/**
 * @brief  Gets the sector number.
 * @param  addr                         flash address
 * @param  size                         erase bytes size
 * @retval Sector number.
 */
static rt_uint32_t GetSectorNum(rt_uint32_t addr, size_t size)
{
    rt_uint32_t firstSector = 0, lastSector = 0;
    rt_uint32_t NumOfSectors = 0;

    firstSector = addr / EFM_SECTOR_SIZE;
    lastSector = (addr + size - 1U) / EFM_SECTOR_SIZE;
    NumOfSectors = lastSector - firstSector + 1U;

    return NumOfSectors;
}

/**
 * @brief Read data from flash.
 * @param addr                          flash address
 * @param buf                           buffer to store read data
 * @param size                          read bytes size
 * @return result
 */
int hc32_flash_read(rt_uint32_t addr, rt_uint8_t *buf, size_t size)
{
    if ((addr + size) > HC32_FLASH_END_ADDRESS)
    {
        LOG_E("read outrange flash size! addr is (0x%p)", (void *)(addr + size));
        return -1;
    }

    if (LL_OK != EFM_ReadByte(addr, buf, size))
    {
        return -RT_ERROR;
    }

    return size;
}

/**
 * @brief  Write data to flash.
 * @param  addr                         flash address
 * @param  buf                          the write data buffer
 * @param  size                         write bytes size
 * @return result
 * @note   This operation's units is word.
 * @note   This operation must after erase.
 */
int hc32_flash_write(rt_uint32_t addr, const rt_uint8_t *buf, size_t size)
{
    uint8_t u8MemBuf[HC32_FLASH_WRITE_GRANULARITY];
    rt_err_t result = RT_EOK;
    rt_uint32_t newAddr = addr, offsetVal = 0;
    rt_uint32_t index = 0, u32Cnt = 0;
#if defined (HC32F4A0) || defined (HC32F472) || defined (HC32F448) || defined (HC32F4A8) || defined (HC32F334)
    rt_uint32_t FirstSector = 0, NumOfSectors = 0;
#endif

    if ((addr + size) > HC32_FLASH_END_ADDRESS)
    {
        LOG_E("write outrange flash size! addr is (0x%p)", (void *)(addr + size));
        return -RT_EINVAL;
    }
    if (size < 1)
    {
        return -RT_EINVAL;
    }

    for (u32Cnt = 0; u32Cnt < HC32_FLASH_WRITE_GRANULARITY; u32Cnt++)
    {
        u8MemBuf[u32Cnt] = 0xFF;
    }
    /* EFM_FWMC write enable */
    EFM_FWMC_Cmd(ENABLE);
#if defined (HC32F4A0) || defined (HC32F472) || defined (HC32F448) || defined (HC32F4A8) || defined (HC32F334)
    /* calculate sector information */
    FirstSector = addr / EFM_SECTOR_SIZE,
    NumOfSectors = GetSectorNum(addr, size);
    /* Sectors disable write protection */
    EFM_SequenceSectorOperateCmd(FirstSector, NumOfSectors, ENABLE);
#endif
    /* Word align */
    if (0U != (addr % HC32_FLASH_WRITE_GRANULARITY))
    {
        newAddr = (addr / HC32_FLASH_WRITE_GRANULARITY + 1U) * HC32_FLASH_WRITE_GRANULARITY;
        offsetVal = newAddr - addr;
        if (offsetVal >= size)
        {
            result = -RT_ERROR;
            index = HC32_FLASH_WRITE_GRANULARITY - offsetVal;
            if (LL_OK == EFM_ReadByte(newAddr - HC32_FLASH_WRITE_GRANULARITY, u8MemBuf, index))
            {
                for (u32Cnt = 0; u32Cnt < size; u32Cnt++)
                {
                    u8MemBuf[index + u32Cnt] = buf[u32Cnt];
                }
                /* program */
                if (LL_OK == EFM_Program(newAddr - HC32_FLASH_WRITE_GRANULARITY, u8MemBuf, HC32_FLASH_WRITE_GRANULARITY))
                {
                    result = RT_EOK;
                }
            }
            goto __exit;
        }
        else
        {
            size = size - offsetVal;
        }
    }
    /* program */
    if (LL_OK != EFM_Program(newAddr, (uint8_t *)&buf[offsetVal], size))
    {
        result = -RT_ERROR;
        goto __exit;
    }

__exit:
#if defined (HC32F4A0) || defined (HC32F472) || defined (HC32F448) || defined (HC32F4A8) || defined (HC32F334)
    /* Sectors enable write protection */
    EFM_SequenceSectorOperateCmd(FirstSector, NumOfSectors, DISABLE);
#endif
    EFM_FWMC_Cmd(DISABLE);

    if (result != RT_EOK)
    {
        return result;
    }

    return size;
}

/**
 * @brief  Erase data on flash.
 * @param  addr                         flash address
 * @param  size                         erase bytes size
 * @return result
 * @note   This operation is irreversible.
 */
int hc32_flash_erase(rt_uint32_t addr, size_t size)
{
    rt_err_t result = RT_EOK;
    rt_uint32_t NumOfSectors = 0;
    rt_uint32_t SectorVal = 0, u32Addr = addr;
#if defined (HC32F4A0) || defined (HC32F472) || defined (HC32F448) || defined (HC32F4A8) || defined (HC32F334)
    rt_uint32_t FirstSector = 0;
#endif

    if ((addr + size) > HC32_FLASH_END_ADDRESS)
    {
        LOG_E("ERROR: erase outrange flash size! addr is (0x%p)\n", (void *)(addr + size));
        return -RT_EINVAL;
    }
    if (size < 1)
    {
        return -RT_EINVAL;
    }

    /* EFM_FWMC write enable */
    EFM_FWMC_Cmd(ENABLE);
    /* calculate sector information */
    NumOfSectors = GetSectorNum(addr, size);
#if defined (HC32F4A0) || defined (HC32F472) || defined (HC32F448) || defined (HC32F4A8) || defined (HC32F334)
    FirstSector = addr / EFM_SECTOR_SIZE,
    /* Sectors disable write protection */
    EFM_SequenceSectorOperateCmd(FirstSector, NumOfSectors, ENABLE);
#endif
    /* Erase sector */
    for (SectorVal = 0U; SectorVal < NumOfSectors; SectorVal++)
    {
        if (LL_OK != EFM_SectorErase(u32Addr))
        {
            result = -RT_ERROR;
            break;
        }
        u32Addr += EFM_SECTOR_SIZE;
    }
#if defined (HC32F4A0) || defined (HC32F472) || defined (HC32F448) || defined (HC32F4A8) || defined (HC32F334)
    /* Sectors enable write protection */
    EFM_SequenceSectorOperateCmd(FirstSector, NumOfSectors, DISABLE);
#endif
    EFM_FWMC_Cmd(DISABLE);

    if (result != RT_EOK)
    {
        return result;
    }

    LOG_D("erase done: addr (0x%p), size %d", (void *)addr, size);
    return size;
}

#if defined(RT_USING_FAL)
static int fal_flash_read(long offset, rt_uint8_t *buf, size_t size);
static int fal_flash_write(long offset, const rt_uint8_t *buf, size_t size);
static int fal_flash_erase(long offset, size_t size);

const struct fal_flash_dev hc32_onchip_flash =
{
    .name       = "onchip_flash",
    .addr       = HC32_FLASH_START_ADDRESS,
    .len        = HC32_FLASH_SIZE,
    .blk_size   = HC32_FLASH_ERASE_GRANULARITY,
    .ops        = {NULL, fal_flash_read, fal_flash_write, fal_flash_erase},
    .write_gran = HC32_FLASH_WRITE_GRANULARITY
};

static int fal_flash_read(long offset, rt_uint8_t *buf, size_t size)
{
    return hc32_flash_read(hc32_onchip_flash.addr + offset, buf, size);
}

static int fal_flash_write(long offset, const rt_uint8_t *buf, size_t size)
{
    return hc32_flash_write(hc32_onchip_flash.addr + offset, buf, size);
}

static int fal_flash_erase(long offset, size_t size)
{
    return hc32_flash_erase(hc32_onchip_flash.addr + offset, size);
}
#endif

#endif /* BSP_USING_ON_CHIP_FLASH */
