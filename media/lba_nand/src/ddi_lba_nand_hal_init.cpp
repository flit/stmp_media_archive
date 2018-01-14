///////////////////////////////////////////////////////////////////////////////
// Copyright (c) Freescale Semiconductor, Inc. All rights reserved.
// 
// Freescale Semiconductor, Inc.
// Proprietary & Confidential
// 
// This source code and the algorithms implemented therein constitute
// confidential information and may comprise trade secrets of Freescale Semiconductor, Inc.
// or its associates, and any use thereof is subject to the terms and
// conditions of the Confidential Disclosure Agreement pursual to which this
// source code was originally received.
///////////////////////////////////////////////////////////////////////////////
//! \addtogroup ddi_media_lba_nand_hal_internal
//! @{
//! \file ddi_lba_nand_hal_init.cpp
//! \brief Init code for the HAL interface for LBA-NAND devices.
////////////////////////////////////////////////////////////////////////////////

#include "ddi_lba_nand_hal_internal.h"
#include "os/thi/os_thi_api.h"
#include "hw/otp/hw_otp.h"
#include "drivers/media/sectordef.h"
#include <string.h>
#include "hw/core/vmemory.h"

////////////////////////////////////////////////////////////////////////////////
// Types
////////////////////////////////////////////////////////////////////////////////

/*!
 * \brief Timings for a particular LBA-NAND type.
 */
struct LbaNandDeviceType
{
    uint8_t m_deviceCode;       //!< The unique device code for this device.
    NAND_Timing2_struct_t m_timings;    //!< The timing characteristics for this device type.
};

////////////////////////////////////////////////////////////////////////////////
// Prototypes
////////////////////////////////////////////////////////////////////////////////

static RtStatus_t ddi_lba_nand_hal_init_chip_select(int wChipNumber, NAND_Timing2_struct_t * pTimings);
static const LbaNandDeviceType * ddi_lba_nand_hal_find_device_type(LbaTypeNand * nand);

////////////////////////////////////////////////////////////////////////////////
// Variables
////////////////////////////////////////////////////////////////////////////////

#pragma alignvar(32)
//! Global LBA-NAND HAL context information.
LbaNandHalContext g_lbaNandHal;

//! Table of unique LBA-NAND devices supported by this library.
static const LbaNandDeviceType s_lbaNandDeviceTypes[] = {
        { 0x21, /*NAND_FAILSAFE_TIMINGS*/ MK_NAND_TIMINGS_DYNAMIC( 0, AVG_TSAMPLE_TIME, 10, 5, 25, 5, 25 ) },
        { 0 }   // List terminator
    };

////////////////////////////////////////////////////////////////////////////////
// Code
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
// See ddi_lba_nand_hal.h for documentation.
////////////////////////////////////////////////////////////////////////////////
__INIT_TEXT RtStatus_t ddi_lba_nand_hal_init()
{
    // For some reason, using the zero initializer caused GHS MULTI to generate a
    // memcpy() instead of memset(), and for some reason that caused an abort
    // in the ROM implementation of memcpy(). Manually calling memset() works fine.
    NAND_Timing2_struct_t timings;// = {0};
    unsigned i;
    RtStatus_t status;
    
    memset(&timings, 0, sizeof(timings));

    // Grab the number of NAND chips.
    g_lbaNandHal.m_deviceCount = hw_otp_NandNumberChips();

    // Ask the HAL to initialize its synchronization objects.
    status = os_thi_ConvertTxStatus(tx_mutex_create(&g_lbaNandHal.m_mutex, "LBA-NAND_HAL_MUTEX", TX_INHERIT));
    if (status != SUCCESS)
    {
        return status;
    }
    
    // Construct DMA objects in place. Normally, the static initializer for g_lbaNandHal would
    // do this for us, but our paging apps don't invoke the static initializer chain. So
    // we must do this manually, to ensure that the vtbl pointer is set correctly.
    new (&g_lbaNandHal.m_resetDma) NandDma::Reset;
    new (&g_lbaNandHal.m_readDma) NandDma::ReadRawData;
    new (&g_lbaNandHal.m_writeDma) NandDma::WriteRawData;
    new (&g_lbaNandHal.m_readStatusDma) NandDma::ReadStatus;
    new (&g_lbaNandHal.m_genericReadDma) NandDma::ReadRawData;

    // Initialize each of the chip selects and figure out how many there are.
    for (i=0; i < g_lbaNandHal.m_deviceCount; i++)
    {
        status = ddi_lba_nand_hal_init_chip_select(i, &timings);
        if (status != SUCCESS)
        {
            // If chip AFTER first fails, then set m_deviceCount to good count.
            if (i > 0)
            {
                g_lbaNandHal.m_deviceCount = i;
                status = SUCCESS;
                break;
            }
            // If the first chip failed, return Hardware error.
            else
            {
                return ERROR_DDI_LDL_LMEDIA_HARDWARE_FAILURE;
            }
        }
    }

    // For Nand2 and Nand4, relax timing to allow for
    // signal distortion due to higher capacitance.
    if (g_lbaNandHal.m_deviceCount > 2)
    {
        ddi_gpmi_relax_timings_by_amount(&timings, 10);
    }
    else if (g_lbaNandHal.m_deviceCount > 1)
    {
        ddi_gpmi_relax_timings_by_amount(&timings, 5);
    }

    // This will set the GPMI timings to the composite timings for the set of NANDs available.
    ddi_gpmi_set_timings(&timings, true /* bWriteToTheDevice */);


    // Pre-build the shared read DMA chain.
    unsigned addressByteCount = 2 + g_lbaNandHal.m_devices[0]->getRowByteCount();
    
    g_lbaNandHal.m_readDma.init(
        0, // chipSelect,
        kLbaNandCommand_ReadPageFirst, // command1,
        NULL, // addressBytes,
        addressByteCount, // addressByteCount,
        kLbaNandCommand_ReadPageSecond, // command2,
        NULL, // dataBuffer,
        0, // dataReadSize,
        NULL, // auxBuffer,
        0); // auxReadSize

    g_lbaNandHal.m_writeDma.init(
        0, // chipSelect
        kLbaNandCommand_SerialDataInput, // command1
        NULL, // addressBytes
        addressByteCount, // addressByteCount
        kLbaNandCommand_WritePage, // command2
        NULL, // dataBuffer
        0, // dataReadSize
        NULL, // auxBuffer
        0); // auxReadSize

    return status;
}

RtStatus_t ddi_lba_nand_hal_shutdown()
{
    // Destroy synchronization objects.
    tx_mutex_delete(&g_lbaNandHal.m_mutex);
    
    // Dispose of device objects.
    unsigned i;
    for (i=0; i < g_lbaNandHal.m_deviceCount; i++)
    {
        if (g_lbaNandHal.m_devices[i])
        {
            g_lbaNandHal.m_devices[i]->cleanup();
            delete g_lbaNandHal.m_devices[i];
            g_lbaNandHal.m_devices[i] = NULL;
        }
    }

    // Disable the GPMI block.
    ddi_gpmi_disable();
    
    return SUCCESS;
}

__STATIC_TEXT unsigned ddi_lba_nand_hal_get_device_count()
{
    return g_lbaNandHal.m_deviceCount;
}

__STATIC_TEXT LbaNandPhysicalMedia * ddi_lba_nand_hal_get_device(unsigned chipSelect)
{
    assert(chipSelect < g_lbaNandHal.m_deviceCount);
    return g_lbaNandHal.m_devices[chipSelect];
}

__INIT_TEXT RtStatus_t ddi_lba_nand_hal_init_chip_select(int wChipNumber, NAND_Timing2_struct_t * pTimings)
{
    RtStatus_t status;
    LbaTypeNand * nand;
    
    assert(pTimings != NULL);

    // Initialize the pins for the GPMI interface to the NANDs.
    status = ddi_gpmi_init(false, wChipNumber, false, false, hw_otp_NandEnableInternalPullups());
    if (status != SUCCESS)
    {
        return status;
    }

    // Initialize the timings to safe vaules.
    NAND_Timing2_struct_t safeTimings;
    ddi_gpmi_get_safe_timings(&safeTimings);
    ddi_gpmi_set_timings(&safeTimings, true /* bWriteToTheDevice */);
    
    // Instantiate the LBA-NAND object.
    nand = new LbaTypeNand;
    if (!nand)
    {
        return ERROR_GENERIC;
    }
    
    // Let the object init itself.
    status = nand->init(wChipNumber);
    if (status != SUCCESS)
    {
        delete nand;
        return status;
    }
    
    // Save the device object in our global context.
    g_lbaNandHal.m_devices[wChipNumber] = nand;
    
    // Look up this device in our table and get its timings.
    const LbaNandDeviceType * deviceType = ddi_lba_nand_hal_find_device_type(nand);
    if (!deviceType)
    {
        delete nand;
        return ERROR_DDI_LBA_NAND_UNKNOWN_DEVICE_TYPE;
    }

    // Adjust the passed in timings to be suitable for this part.
    ddi_gpmi_set_most_relaxed_timings(pTimings, &deviceType->m_timings);

    return SUCCESS;
}

__INIT_TEXT const LbaNandDeviceType * ddi_lba_nand_hal_find_device_type(LbaTypeNand * nand)
{
    // Get this device's ID code from its read ID results. This will actually
    // cause a second read ID command to be sent, but that's not a big deal.
    LbaNandId2Response readIdResults;
    nand->getReadIdResults(&readIdResults);
    
    unsigned i;
    for (i=0; ; i++)
    {
        const LbaNandDeviceType * deviceType = &s_lbaNandDeviceTypes[i];
        
        // Exit the loop if we reach the terminating empty entry.
        if (deviceType->m_deviceCode == 0)
        {
            break;
        }
        
        // Match the device code.
        if (deviceType->m_deviceCode == readIdResults.m_deviceCode)
        {
            return deviceType;
        }
    }
    
    // If we exit the loop here then we didn't find a matching device type.
    return NULL;
}

// EOF
//! @}
