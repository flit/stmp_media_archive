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
//! \file   ddi_lba_nand_hal.cpp
//! \brief  Implementation of the HAL interface for LBA-NAND devices.
////////////////////////////////////////////////////////////////////////////////

#include "ddi_lba_nand_hal_internal.h"
#include "hw/core/mmu.h"
#include "hw/profile/hw_profile.h"
#include "drivers/media/buffer_manager/media_buffer.h"
#include "drivers/media/include/ddi_media_timers.h"
#include "os/dmi/os_dmi_api.h"
#include <string.h>
#include <stdio.h>
#include <algorithm>
#include <memory>
#include "auto_free.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////
// Variables
////////////////////////////////////////////////////////////////////////////////

//! Information about each of the supported device attributes.
const DeviceAttributeInfo k_lbaNandAttributeInfo[] = {
        { 0x000000, 16 },   // kUniqueId,
        { 0x000010, 10 },   // kControllerFirmwareVersion,
        { 0x000020, 10 }    // kDeviceHardwareVersion
    };

//! \brief Table of valid VFP Capacity Parameters
const uint8_t k_vfpCapacityParameterTable[] = {
        3,
        4,
        6,
        8,
        12,
        16,
        24,
        32,
        48,
        64,
        96,
        128,
        192
    };

const uint8_t k_vfpCapacityParameterTableSize = sizeof(k_vfpCapacityParameterTable);

////////////////////////////////////////////////////////////////////////////////
// Code
////////////////////////////////////////////////////////////////////////////////

unsigned LbaNandId2Response::getDeviceSizeInGB()
{
    switch (m_deviceSize)
    {
        case kSize_2GB:
            return 2;
        case kSize_4GB:
            return 4;
        case kSize_8GB:
            return 8;
        case kSize_16GB:
            return 16;
        default:
            return 0;
    }
}

#if !defined(__ghs__)
#pragma mark -LbaTypeNand-
#endif

__INIT_TEXT RtStatus_t LbaTypeNand::init(unsigned chipSelect)
{
    RtStatus_t status;

#if LBA_HAL_RECORD_HISTORY
    m_history.init(LBA_HAL_HISTORY_RECORD_COUNT);
#endif // LBA_HAL_RECORD_HISTORY
#if LBA_HAL_RECORD_COMMAND_HISTORY
    m_commandHistory.init(LBA_HAL_COMMAND_HISTORY_RECORD_COUNT);
#endif // LBA_HAL_RECORD_COMMAND_HISTORY

#if LBA_HAL_STATISTICS && LBA_HAL_USE_HISTOGRAM
    m_modeSwitchTime.init(ElapsedTimeHistogram::kLinear, 0, 15000, 10);
#endif
    
    m_chipSelect = chipSelect;
    m_mode = kPnpMode;
    
    // Reset device to place it into a known state.
    status = rebootDevice();
    
    LbaNandId2Response idResponse;
    if (status == SUCCESS)
    {
        status = readId2((uint8_t *)&idResponse);
    }
    
    // Validate the ID response.
    if (status == SUCCESS)
    {
        // Check maker code and signatures.
        if (!(idResponse.m_makerCode == LbaNandId2Response::kToshibaMakerCode && idResponse.m_signature1 == LbaNandId2Response::kSignature1 && idResponse.m_signature2 == LbaNandId2Response::kSignature2))
        {
            return ERROR_DDI_LBA_NAND_UNKNOWN_DEVICE_TYPE;
        }
        
        // Check device code range.
        if (!(idResponse.m_deviceCode >= LbaNandId2Response::kDeviceCodeRangeStart && idResponse.m_deviceCode <= LbaNandId2Response::kDeviceCodeRangeEnd))
        {
            return ERROR_DDI_LBA_NAND_UNKNOWN_DEVICE_TYPE;
        }
    }
    
    // Determine the number of row address bytes. All devices larger than 8GB have 4 row bytes.
    if (idResponse.getDeviceSizeInGB() > kLbaNandSmallDeviceMaximumGB)
    {
        m_rowByteCount = kLbaNandLargeDeviceRowByteCount;
    }
    else
    {
        m_rowByteCount = kLbaNandSmallDeviceRowByteCount;
    }

    if(status == SUCCESS)
    {
        // Allow <FFh> functions as Device Reboot in MDP, VFP and BCM
        status = changeRebootCommand();
    }
    
    // Put the device into LBA mode before continuing. We do it directly instead of
    // calling setMode() to avoid using a partition object.
    if (status == SUCCESS)
    {
        status = modeChangeToMdp();
        m_mode = kMdpMode;
    }
    
    // Init the partition objects.
    if (status == SUCCESS)
    {
        status = m_mdp.init(this);
    }
    
    if (status == SUCCESS)
    {
        status = m_vfp.init(this);
    }
    
    if (status == SUCCESS)
    {
        status = m_pnp.init(this);
    }
    
    // Set the transfer protocol 1.
    if (status == SUCCESS)
    {
        status = setTransferProtocol1(kLbaNandDefaultTransferProtocol1);
    }
    
    // Set the transfer protocol 2.
    if (status == SUCCESS)
    {
        status = setTransferProtocol2(kLbaNandDefaultTransferProtocol2);
    }

    // Set the default power state for power save mode disabled but no high speed writes.
    if (status == SUCCESS)
    {
        m_PowerSavedEnabled = true;
        status = enablePowerSaveMode(false);
    }

    if (status == SUCCESS)
    {
        status = enableHighSpeedWrites(false);
    }
    
    // Read the Max VFP size info
    if (status == SUCCESS)
    {
        status = readMaxVfpSize(&m_vfpMaxSize);
    }

    return status;
}

void LbaTypeNand::cleanup()
{
    // Close out the partitions.
    exitCurrentPartition();
    m_pnp.cleanup();
    m_vfp.cleanup();
    m_mdp.cleanup();
    
    // Then reboot the device back into PNP mode. This is necessary so that the
    // device is in the mode that the boot ROM expects, otherwise we won't be
    // able to boot!
    rebootDevice();

#if LBA_HAL_STATISTICS && LBA_HAL_USE_HISTOGRAM
    m_modeSwitchTime.cleanup();
#endif

#if LBA_HAL_RECORD_HISTORY
    m_history.cleanup();
#endif // LBA_HAL_RECORD_HISTORY
#if LBA_HAL_RECORD_COMMAND_HISTORY
    m_commandHistory.cleanup();
#endif // LBA_HAL_RECORD_COMMAND_HISTORY
}

__STATIC_TEXT RtStatus_t LbaTypeNand::exitCurrentPartition()
{
    switch (m_mode)
    {
        case kPnpMode:
        case kBcmMode:
            return m_pnp.exitPartition();
        
        case kVfpMode:
            return m_vfp.exitPartition();
        
        case kMdpMode:
            return m_mdp.exitPartition();
        
        default:
            return ERROR_GENERIC;
    }
}

__STATIC_TEXT RtStatus_t LbaTypeNand::setMode(LbaNandMode_t mode)
{
    RtStatus_t status;
    
    // Lock the HAL during the mode change.
    LbaNandHalLocker locker;
    
#if 0 //DEBUG
    // Check that the device is in the expected mode.
    status = verifyMode(m_mode);
    if (status != SUCCESS)
    {
        return status;
        
    }
#endif // DEBUG

    // Make sure we really need to change modes before continuing.
    if (m_mode == mode)
    {
        return SUCCESS;
    }

    // Let the current partition do whatever it needs to do to cleanup.
    status = exitCurrentPartition();
    if (status != SUCCESS)
    {
        return status;
    }
    
#if LBA_HAL_STATISTICS
    SimpleTimer cTimer;
#endif //#if LBA_HAL_STATISTICS
    
    // To get to the VFP partition from PNP mode, we must first be in LBA mode.
    if (mode == kVfpMode && m_mode == kPnpMode)
    {
        status = modeChangeToMdp();
    
#if DEBUG
        // Make sure we're in LBA mode before continuing.
        if (status == SUCCESS)
        {
            status = verifyMode(kMdpMode);
        }
#endif // DEBUG
    }
    
    if (status == SUCCESS)
    {
        // Switch to the new mode.
        switch (mode)
        {
            case kPnpMode:
            case kBcmMode:
                // We're actually going into BCM mode even if someone asks for PNP mode.
                status = modeChangeToBcm();
                mode = kBcmMode;
                break;
            
            case kVfpMode:
                status = modeChangeToVfp();
                break;
            
            case kMdpMode:
                status = modeChangeToMdp();
                break;
            
            default:
                status = ERROR_GENERIC;
        }
    }
    
#if DEBUG
    // Check that the device is has actually been placed into the desired mode.
    if (status == SUCCESS)
    {
        status = verifyMode(mode);
    }
#endif // DEBUG

    if (status == SUCCESS)
    {
        // Save current mode.
        m_mode = mode;
    }

#if LBA_HAL_STATISTICS
    m_modeSwitchTime += cTimer;
#endif //#if LBA_HAL_STATISTICS
    
    return status;
}

__INIT_TEXT RtStatus_t LbaTypeNand::getReadIdResults(LbaNandId2Response * responseData)
{
    return readId2(reinterpret_cast<uint8_t *>(responseData));
}

__STATIC_TEXT RtStatus_t LbaTypeNand::flushCache()
{
    return sendResetTypeCommand(kLbaNandCommand_CacheFlush);
}

RtStatus_t LbaTypeNand::setVfpSize(uint32_t newSectorCount)
{
    // Lock the HAL because we don't want any other commands to possibly
    // interrupt changing the VFP size.
    LbaNandHalLocker locker;
    
    RtStatus_t status;
    uint32_t commandData;
    uint8_t commandCode = kLbaNandCommand_SetVfpSize;

    if (newSectorCount == 0)
    {
        // VFP of zero size uses a special size 
        // value to indicate this
        commandData = kLbaNandVfpZeroSizeValue;
    }
    else if (newSectorCount <= (kLbaNandVfpMaxSize / kLbaNandSectorMultiple))
    {
        // Standard size VFP partition

        // Convert from logical sector size to base/physical sector size
        commandData = newSectorCount * kLbaNandSectorMultiple;

        // use the greater of commandData and the minimum non-zero VFP size
        commandData = std::max(commandData, (uint32_t)kLbaNandVfpMinSize);

        // round up to the next valid VFP size
        commandData = ROUND_UP(commandData, kLbaNandVfpStepSize);

        // Convert the resulting commandData to logical sector size
        newSectorCount = commandData / kLbaNandSectorMultiple;
        
        if (commandData == (kLbaNandVfpMaxSize))
        {
            // In set VFP size command use 0 for the maximum size.
            commandData = 0;
        }
    }
    else
    {
        // EX_ size VFP partition
        commandCode = kLbaNandCommand_ExSetVfpSize;

        // Find the valid EX_ VFP size
        uint32_t index = 0;
        uint32_t sizeFromTable;
        uint8_t tableValue;
        do
        {
            tableValue = k_vfpCapacityParameterTable[index];
            sizeFromTable = tableValue * kLbaNandVfpExCapacityModelUnitSectors;
            index++;
        } while ((sizeFromTable < newSectorCount) && (index < k_vfpCapacityParameterTableSize));

        // error if valid VFP is not large enough
        if (sizeFromTable < newSectorCount)
        {
             return ERROR_DDI_LBA_NAND_VFP_SIZE_TOO_LARGE;
        }
        newSectorCount = sizeFromTable;

        commandData = (kLbaNandVfpExCapacityModelType) | (tableValue << 8);
    }

    // Format sector count in format used by command
    uint8_t dataBytes[4];
    dataBytes[0] = commandData & 0xff;                              // byte 0 - Least Significant Byte
    dataBytes[1] = (commandData >> 8) & 0xff;                       // byte 1 - Most Significant Byte
    dataBytes[2] = (~commandData) & 0xff;                           // byte 2 - Inversion of Least Significant Byte
    dataBytes[3] = ((~commandData) >> 8) & 0xff;                    // byte 3 - Inversion of Most Significant Byte

    // This command only works in VFP mode
    status = setMode(kVfpMode);
    
#if DEBUG
    SimpleTimer timer;
#endif
    
    // Send the command, but don't let the DMA perform the wait for ready. This
    // is because we need to wait longer than the GPMI peripheral's maximum
    // timeout when GPMI_CLK is at 96MHz.
    status = sendGeneralCommand(commandCode, dataBytes, 0, NULL, kLbaNandTimeout_ReadPage, false);
    
    // Do a software controlled wait for ready if the DMA was completed successfully.
    if (status == SUCCESS)
    {
        status = ddi_gpmi_wait_for_ready(m_chipSelect, kLbaNandTimeout_SetVfpSize);
    }
    
#if DEBUG
    uint64_t elapsed = timer;
    tss_logtext_Print(LBA_LOGTEXT_MASK, "SetVFPSize[0x%02x] returned 0x%08x (%u ms)\n", commandCode, status, elapsed/1000);
#endif
    
    if (status == SUCCESS)
    {
        // Read the new sector count.
        uint32_t actualSectorCount;
        status = getVfpSize(&actualSectorCount);
        if (status != SUCCESS)
        {
            return status;
        }
        
        // Return an error if changing the size failed.
        if (actualSectorCount != newSectorCount)
        {
            status = ERROR_DDI_LBA_NAND_SET_VFP_SIZE_FAILED;
            
#if DEBUG
            tss_logtext_Print(LBA_LOGTEXT_MASK, "SetVFPSize[0x%02x] failed to change the VFP size as expected (current=%u, expected=%u)\n", commandCode, actualSectorCount, newSectorCount);
#endif
        }
    }
    
    // Re-Init the affected partition objects.
    if (status == SUCCESS)
    {
        status = m_mdp.init(this);
    }
    if (status == SUCCESS)
    {
        status = m_vfp.init(this);
    }

    return status;
}

__INIT_TEXT RtStatus_t LbaTypeNand::readMaxVfpSize(uint32_t * sectorCount)
{
    // Lock because we're using the shared data buffer.
    LbaNandHalLocker locker;
    
    uint8_t * responseBytes = g_lbaNandHal.m_dataBuffer;
    RtStatus_t status;
    uint8_t dataBytes[4];

    *sectorCount = (kLbaNandVfpMaxSize / kLbaNandSectorMultiple);

    dataBytes[0] = kLbaNandVfpExCapacityModelCategory;
    status = sendGeneralCommand(kLbaNandCommand_ExGetVfpSizeVariation, dataBytes, 2, responseBytes);

    if (SUCCESS == status)
    {
        // Check that VFP Capacity Model Type is supported
        if ((responseBytes[0] >= kLbaNandVfpExCapacityModelType) && (responseBytes[0] <= kLbaNandVfpExCapacityModelTypeMax))
        {
            // check that VFP Capacity parameter is valid
            int index = 0;
            do
            {
                // if VFP Capacity parameter is valid calculate max sector count
                if (k_vfpCapacityParameterTable[index] == responseBytes[1])
                {
                    *sectorCount = responseBytes[1] * kLbaNandVfpExCapacityModelUnitSectors;
                    break;
                }
            } while(++index < k_vfpCapacityParameterTableSize);
            
        }
    }
    
    // Always return sucsess because even if the 
    // EX_ command fails, there is the standard 32MB
    status = SUCCESS;

    return status;
}

RtStatus_t LbaTypeNand::enablePowerSaveMode(bool enable)
{
    LbaNandHalLocker locker;

    if(m_PowerSavedEnabled == enable)
    {
        return SUCCESS;
    }
    m_PowerSavedEnabled = enable;
        
#if LBA_HAL_LOG_POWER_SAVE_MODE
    tss_logtext_Print(LBA_LOGTEXT_MASK, "Setting power save mode to %d\n", (int)enable);
#endif
    
    // We have to be in LBA mode to change power save mode. If we're already in
    // LBA mode then we still have to terminate any in progress read or write sequence.
    if (m_mode == kPnpMode || m_mode == kBcmMode)
    {
        setMode(kMdpMode);
    }
    else
    {
        exitCurrentPartition();
    }
    
    // Send the appropriate command.
    uint8_t commandCode = (enable ? kLbaNandCommand_EnablePowerSaveMode : kLbaNandCommand_DisablePowerSaveMode);
    RtStatus_t status = sendGeneralCommand(commandCode, NULL, 0, NULL, kLbaNandTimeout_WritePage);
    
#if DEBUG
    // For debug builds, verify that the mode state changed to what we expect.
    if (status == SUCCESS)
    {
        LbaNandStatus2Response response;
        status = readStatus2(&response);
        
        if (status == SUCCESS && response.powerSaveMode() != enable)
        {
            tss_logtext_Print(LBA_LOGTEXT_MASK, "Warning: enablePowerSaveMode failed! (desired=%d, actual=%d, status=0x%02x)\n", (int)enable, (int)response.powerSaveMode(), response.m_response);
        }
    }
#endif
    
    return status;
}

RtStatus_t LbaTypeNand::enableHighSpeedWrites(bool enable)
{
    LbaNandHalLocker locker;
    
    // We have to be in LBA mode to send this command. If we're already in
    // LBA mode then we still have to terminate any in progress read or write sequence.
    if (m_mode == kPnpMode || m_mode == kBcmMode)
    {
        setMode(kMdpMode);
    }
    else
    {
        exitCurrentPartition();
    }
    
    // Send the appropriate command.
    uint8_t commandCode = (enable ? kLbaNandCommand_EnableHighSpeedWriteMode : kLbaNandCommand_DisableHighSpeedWriteMode);
    RtStatus_t status = sendGeneralCommand(commandCode, NULL, 0, NULL, kLbaNandTimeout_WritePage);
    
#if DEBUG
    // For debug builds, verify that the mode state changed to what we expect.
    if (status == SUCCESS)
    {
        LbaNandStatus2Response response;
        status = readStatus2(&response);
        
        if (status == SUCCESS && response.highSpeedWriteMode() != enable)
        {
            tss_logtext_Print(LBA_LOGTEXT_MASK, "Warning: enableHighSpeedWrites failed! (desired=%d, actual=%d, status=0x%02x)\n", (int)enable, (int)response.highSpeedWriteMode(), response.m_response);
        }
    }
#endif
    
    return status;
}

__INIT_TEXT RtStatus_t LbaTypeNand::readId2(uint8_t * data)
{
    // Lock because we're using the shared data buffer.
    LbaNandHalLocker locker;

#if LBA_HAL_RECORD_COMMAND_HISTORY
    m_commandHistory.insert(kLbaNandCommand_ReadId2);
#endif // LBA_HAL_RECORD_COMMAND_HISTORY
    
    uint8_t * responseBytes = g_lbaNandHal.m_dataBuffer;
    RtStatus_t status;
    
    NandDma::ReadId readIdDma(m_chipSelect, kLbaNandCommand_ReadId2, 0, responseBytes);

    // Invalidate and clean the data cache before starting the read DMA.
    hw_core_invalidate_clean_DCache();

    status = readIdDma.startAndWait(kLbaNandTimeout_ReadPage);
    
    if (status == SUCCESS)
    {
        // Copy the response bytes into the caller's buffer.
        memcpy(data, responseBytes, kLbaNandReadId2ResponseLength);
    }

    return status;
}

__STATIC_TEXT RtStatus_t LbaTypeNand::readStatus1(LbaNandStatus1Response * response)
{
    return sendReadStatusCommand(kLbaNandCommand_ReadStatus1, &response->m_response);
}

__STATIC_TEXT RtStatus_t LbaTypeNand::readStatus2(LbaNandStatus2Response * response)
{
    return sendReadStatusCommand(kLbaNandCommand_ReadStatus2, &response->m_response);
}

__STATIC_TEXT RtStatus_t LbaTypeNand::modeChangeToMdp()
{
    return sendResetTypeCommand(kLbaNandCommand_ModeChangeToMdp);
}

__STATIC_TEXT RtStatus_t LbaTypeNand::modeChangeToVfp()
{
    uint16_t u16Password = getVfpPassword();
    uint8_t dataBytes[4];
    dataBytes[0] = u16Password & 0xff;                         // byte 0 - Least Significant Byte 
    dataBytes[1] = (u16Password >> 8) & 0xff;                    // byte 1 - Most Significant Byte
    dataBytes[2] = (~u16Password) & 0xff;
    dataBytes[3] = ((~u16Password) >> 8) & 0xff;

    return sendGeneralCommand(kLbaNandCommand_ModeChangeToVfp, dataBytes, 0, NULL);
}

__STATIC_TEXT RtStatus_t LbaTypeNand::modeChangeToBcm()
{
    return sendGeneralCommand(kLbaNandCommand_ModeChangeToBcm, NULL, 0, NULL);
}

__INIT_TEXT RtStatus_t LbaTypeNand::rebootDevice()
{
    RtStatus_t status;
    
    // Lock so that nobody else grabs the GPMI peripheral while we're doing
    // the software wait for ready.
    LbaNandHalLocker locker;

    // Send the reboot command but don't wait for it to finish.
    status = sendResetTypeCommand(kLbaNandCommand_RebootDevice, false);
            
    // Do a software controlled wait for ready if the DMA was completed successfully.
    // We wait for the command to finish outside of the DMA because the GPMI wait
    // for ready timeout is limited to a time shorter than the reboot command can take.
    if (status == SUCCESS)
    {
        status = ddi_gpmi_wait_for_ready(m_chipSelect, kLbaNandTimeout_Reset);
    }
    
    if (status == SUCCESS)
    {
        // We're now in PNP mode.
        m_mode = kPnpMode;
    }

    return status;
}

__STATIC_TEXT RtStatus_t LbaTypeNand::verifyMode(LbaNandMode_t mode)
{
    RtStatus_t status = SUCCESS;

    LbaNandStatus2Response statusResponse;
    status = readStatus2(&statusResponse);

    if ((status == SUCCESS) && ( mode != statusResponse.currentPartition() ))
    {
        status = ERROR_DDI_LBA_NAND_MODE_NOT_SET;
    }

    return status;
}

__INIT_TEXT RtStatus_t LbaTypeNand::getMdpSize(uint32_t * sectorCount)
{
    // Lock because we're using the shared data buffer.
    LbaNandHalLocker locker;
    
    uint8_t * responseBytes = g_lbaNandHal.m_dataBuffer;
    RtStatus_t status = sendGeneralCommand(kLbaNandCommand_GetMdpSize, NULL, 5, responseBytes);
    if (status == SUCCESS)
    {
        // Construct a uint32_t from the 5 byte response. We assume that the sector count
        // won't be larger than a 32-bit value can hold, for now. The assert verifies that
        // assumption.
        assert(responseBytes[4] == 0);
        
        *sectorCount = (responseBytes[0] | (responseBytes[1] << 8) | (responseBytes[2] << 16) | (responseBytes[3] << 24)) / kLbaNandSectorMultiple;
    }
    
    return status;
}

__INIT_TEXT RtStatus_t LbaTypeNand::getVfpSize(uint32_t * sectorCount)
{
    // Lock because we're using the shared data buffer.
    LbaNandHalLocker locker;
    
    uint8_t * responseBytes = g_lbaNandHal.m_dataBuffer;
    RtStatus_t status = sendGeneralCommand(kLbaNandCommand_GetVfpSize, NULL, 2, responseBytes);
    if (status == SUCCESS)
    {
        // Construct a uint32_t from the 2 byte response.

        uint32_t size = 0;
        size = (responseBytes[0] | (responseBytes[1] << 8));

        if (size == 0)
        {
            // a size of zero indicates the max sector value
            *sectorCount = (kLbaNandVfpMaxSize / kLbaNandSectorMultiple);
        }
        else if (size == kLbaNandVfpZeroSizeValue)
        {
            // VFP of zero size uses a special size 
            // value to indicate this
            *sectorCount = 0;
        }
        else if (size == kLbaNandVfpExSizeValue)
        {
            // VFP size set using the EX_ command.  Use
            // the EX_ command for get size

            status = sendGeneralCommand(kLbaNandCommand_ExGetVfpSize, NULL, 2, responseBytes);

            if (status == SUCCESS)
            {
                if ((responseBytes[0] == 0) || (responseBytes[1] == 0))
                {
                    // size of zero from the EX_ get command indicates 
                    // the VFP size was not set using the EX_ command.
                    // This is an error condition due to the standard
                    // VFP get size reporting to use EX_ get size
//                    status = ERROR_DDI_LBA_NAND_VFP_SIZE_PARADOX;

                    // This state shouldn't happen, but it seems to sometimes. So we just
                    // set the sector count to 0 and continue.
                    *sectorCount = 0;
                    
#if DEBUG
                    tss_logtext_Print(LBA_LOGTEXT_MASK, "Warning: encountered VFP size paradox!\n");
#endif
                }
                else if (responseBytes[0] != kLbaNandVfpExCapacityModelType)
                {
                    // VFP EX_ Capacity Model Type is incorrect
                    status = ERROR_DDI_LBA_NAND_UNKNOWN_VFP_CAPACITY_MODEL_TYPE;
                }
                else
                {
                    // no error, translate response to a sector count
                    *sectorCount = responseBytes[1] * kLbaNandVfpExCapacityModelUnitSectors;
                }
            }
        }
        else
        {
            // if VFP size is not zero, not max, not EX_
            *sectorCount = (size / kLbaNandSectorMultiple);
        }

    }
    
    return status;
}

// <00h>(A2h)(Data)(XXh)(XXh)(XXh)<57h> B2R
__INIT_TEXT RtStatus_t LbaTypeNand::setTransferProtocol1(uint8_t protocol)
{
    uint8_t dataBytes[4];
    dataBytes[0] = protocol;
    return sendGeneralCommand(kLbaNandCommand_SetTransferProtocol1, dataBytes, 0, NULL);
}

RtStatus_t LbaTypeNand::getTransferProtocol1(uint8_t * protocol)
{
    return sendGeneralCommand(kLbaNandCommand_GetTransferProtocol1, NULL, 1, protocol);
}

// <00h>(A3h)(Data)(XXh)(XXh)(XXh)<57h> B2R
__INIT_TEXT RtStatus_t LbaTypeNand::setTransferProtocol2(uint8_t protocol)
{
    uint8_t dataBytes[4];
    dataBytes[0] = protocol;
    return sendGeneralCommand(kLbaNandCommand_SetTransferProtocol2, dataBytes, 0, NULL);
}

RtStatus_t LbaTypeNand::getTransferProtocol2(uint8_t * protocol)
{
    return sendGeneralCommand(kLbaNandCommand_GetTransferProtocol2, NULL, 1, protocol);
}

RtStatus_t LbaTypeNand::setMinimumBusyTime(uint8_t value)
{
    uint8_t dataBytes[4];
    dataBytes[0] = value;
    return sendGeneralCommand(kLbaNandCommand_SetMinimumBusyTime, dataBytes, 0, NULL);
}

RtStatus_t LbaTypeNand::getMinimumBusyTime(uint8_t * value)
{
    return sendGeneralCommand(kLbaNandCommand_GetMinimumBusyTime, NULL, 1, value);
}

RtStatus_t LbaTypeNand::readDeviceAttribute(DeviceAttributeName_t which, void * data, unsigned length, unsigned * actualLength)
{
    RtStatus_t status;
    const DeviceAttributeInfo & info = k_lbaNandAttributeInfo[which];
    
    // Return in the actual length.
    if (actualLength)
    {
        *actualLength = info.m_length;
    }
    
    // If the caller didn't provide any data buffer then just return.
    if (!data)
    {
        return SUCCESS;
    }
    
    // Get a temporary buffer to hold the data coming from the device.
    SectorBuffer buffer;
    if (buffer.didFail())
    {
        return buffer.getStatus();
    }
    
    // Send the start command.
    status = sendGeneralCommand(kLbaNandCommand_DeviceAttributeStart, NULL, 0, NULL);
    if (status != SUCCESS)
    {
        return status;
    }
    
    // Send the command to read the attribute.
    uint8_t addressBytes[5] = {1, 0};
    addressBytes[2] = info.m_address & 0xff;
    addressBytes[3] = (info.m_address >> 8) & 0xff;
    addressBytes[4] = (info.m_address >> 16) & 0xff;
    status = sendGenericReadCommand(kLbaNandCommand_ReadPageFirst, kLbaNandCommand_ReadPageSecond, addressBytes, kLbaNandDeviceAttributeResponseLength, buffer);
    
    // Send the close command regardless of whether the read command succeeded.
    RtStatus_t closeStatus = sendGeneralCommand(kLbaNandCommand_DeviceAttributeClose, NULL, 0, NULL);
    if (closeStatus != SUCCESS && status == SUCCESS)
    {
        // The close command failed, but everything else succeeded, so return the close failure.
        status = closeStatus;
    }
    
    if (status == SUCCESS)
    {
        // Copy the response data into the caller's buffer.
        memcpy(data, buffer, std::min(length, info.m_length));
    }
    
    return status;
}

__STATIC_TEXT RtStatus_t LbaTypeNand::sendResetTypeCommand(uint8_t commandCode, bool waitForReady)
{
    // Have to lock since we're sending a DMA.
    LbaNandHalLocker locker;

#if LBA_HAL_RECORD_COMMAND_HISTORY
    m_commandHistory.insert((LbaNandCommand_t)commandCode);
#endif // LBA_HAL_RECORD_COMMAND_HISTORY

    g_lbaNandHal.m_resetDma.init(m_chipSelect, commandCode);

    if (!waitForReady)
    {
        g_lbaNandHal.m_resetDma.skipPostWait();
    }
    
    // Invalidate and clean the data cache before starting the DMA.
    hw_core_invalidate_clean_DCache();

    // Kick it off.
    return g_lbaNandHal.m_resetDma.startAndWait(kLbaNandTimeout_ReadPage);
}

__STATIC_TEXT RtStatus_t LbaTypeNand::sendGeneralCommand(uint8_t commandCode, uint8_t dataBytes[4], unsigned responseLength, uint8_t * responseData, uint32_t timeout, bool waitForReady)
{
    // Put the general command in the first address byte and the four data bytes in the
    // remaining address bytes.
    uint8_t addressBytes[5];// = {0}; // !zero initializer problem!
    addressBytes[0] = commandCode;
    if (dataBytes)
    {
        addressBytes[1] = dataBytes[0];
        addressBytes[2] = dataBytes[1];
        addressBytes[3] = dataBytes[2];
        addressBytes[4] = dataBytes[3];
    }
    else
    {
        addressBytes[1] = 0;
        addressBytes[2] = 0;
        addressBytes[3] = 0;
        addressBytes[4] = 0;
    }

    RtStatus_t status = sendGenericReadCommand(kLbaNandCommand_GeneralFirst, kLbaNandCommand_GeneralSecond, addressBytes, responseLength, responseData, timeout, waitForReady);

#if LBA_HAL_RECORD_COMMAND_HISTORY
    // Insert the actual command code after the generic command 0x00-0x57 sequence. This way, the
    // 0x00-0x57 sequence can be used as a marker in the command history.
    m_commandHistory.insert((LbaNandCommand_t)commandCode);
#endif // LBA_HAL_RECORD_COMMAND_HISTORY

    return status;
}

__STATIC_TEXT RtStatus_t LbaTypeNand::sendGenericReadCommand(uint8_t firstCommandCode, uint8_t secondCommandCode, uint8_t addressBytes[5], unsigned responseLength, uint8_t * responseData, uint32_t timeout, bool waitForReady)
{
    // Have to lock since we're sending a DMA.
    LbaNandHalLocker locker;

#if LBA_HAL_RECORD_COMMAND_HISTORY
    m_commandHistory.insert((LbaNandCommand_t)firstCommandCode);
    m_commandHistory.insert((LbaNandCommand_t)secondCommandCode);
#endif // LBA_HAL_RECORD_COMMAND_HISTORY

    g_lbaNandHal.m_genericReadDma.init(
        m_chipSelect, // chipSelect,
        firstCommandCode, // command1,
        addressBytes, // addressBytes,
        5, // addressByteCount,
        secondCommandCode, // command2,
        responseData, // dataBuffer,
        responseLength, // dataReadSize,
        NULL, // auxBuffer,
        0); // auxReadSize
    
    if (!waitForReady)
    {
        // Cannot read any data if we're skipping the wait for ready stage.
        assert(responseLength == 0);
        
        // Relink to remove the wait for ready.
        g_lbaNandHal.m_genericReadDma.m_cle2 >> g_lbaNandHal.m_genericReadDma.m_done;
    }
    
    // Invalidate and clean the data cache before starting the read DMA.
    hw_core_invalidate_clean_DCache();

    // Kick off the DMA.
    return g_lbaNandHal.m_genericReadDma.startAndWait(timeout);
}

__STATIC_TEXT RtStatus_t LbaTypeNand::sendReadStatusCommand(uint8_t statusCommand, uint8_t * responseData)
{
    // Have to lock since we're sending a DMA.
    LbaNandHalLocker locker;

#if LBA_HAL_RECORD_COMMAND_HISTORY
    m_commandHistory.insert((LbaNandCommand_t)statusCommand);
#endif // LBA_HAL_RECORD_COMMAND_HISTORY

    // Build the status command DMA.
    g_lbaNandHal.m_readStatusDma.init(m_chipSelect, statusCommand, g_lbaNandHal.m_dataBuffer);

    // Flush the entire data cache before starting the DMA.
    hw_core_invalidate_clean_DCache();
    
    RtStatus_t retCode = g_lbaNandHal.m_readStatusDma.startAndWait(kLbaNandTimeout_ReadPage);

    *responseData = g_lbaNandHal.m_dataBuffer[0];

    return retCode;
}

// <00h>(XXh)(XXh)(XXh)(ADh/AFh)(XXh)<30h> B2R
__INIT_TEXT RtStatus_t LbaTypeNand::sendRebootCommandChange(uint8_t value)
{
    uint8_t addressBytes[5];
    
    addressBytes[3] = value;
    return sendGenericReadCommand(kLbaNandCommand_ReadPageFirst, kLbaNandCommand_ReadPageSecond, addressBytes, 0, NULL, kLbaNandTimeout_Reset, true);
}

__INIT_TEXT RtStatus_t LbaTypeNand::changeRebootCommand ()
{
    RtStatus_t status;
    uint8_t u8BootMode, u8RebootCmd;
    
    // Lock the HAL during the mode change.
    LbaNandHalLocker locker;

    // Since it is not sure whether we are in LBA mode or PNR mode, 
    // Let's switch to MDP first, and then back to Bcm
    status = modeChangeToMdp();
    if(status != SUCCESS)
    {
        return status;
    }
    
    status = modeChangeToBcm();
    if(status != SUCCESS)
    {
        return status;
    }

    m_mode = kBcmMode;

    status = persistentFunctionGet(&u8BootMode, &u8RebootCmd);
    if(status != SUCCESS)
    {
        return status;
    }

    // If reboot command is already changed to 0xFF, return here
    if(u8RebootCmd == kLbaNandRebootCmd_FFh && u8BootMode == kLbaNandBootMode7Code)
    {
        return SUCCESS;
    }        

    // Allow <FFh> functions as Device Reboot in MDP, VFP and BCM
    status = sendRebootCommandChange(kLbaNandRebootCmd_FFh);
    if(status != SUCCESS)
    {
        return status;
    }

    // Change LBA to bootmode 7
    status = sendBootModeChange(kLbaNandBootMode7Code);
    if(status != SUCCESS)
    {
        return status;
    }

    // Flush cache in BCM
    status = flushCache();
    if(status != SUCCESS)
    {
        return status;
    }
    
    return  rebootDevice();
}

// <00h>(XXh)(XXh)(XXh)(99h)(XXh)<30h> B2R
__INIT_TEXT RtStatus_t LbaTypeNand::persistentFunctionGet(uint8_t *u8BootMode, uint8_t *u8RebootCmd)
{
    RtStatus_t status;
    uint8_t addressBytes[5];
        
    addressBytes[3] = 0x99;
    
    status = sendGenericReadCommand(kLbaNandCommand_ReadPageFirst, kLbaNandCommand_ReadPageSecond, addressBytes, 6, g_lbaNandHal.m_dataBuffer, kLbaNandTimeout_Reset, true);
    if(status == SUCCESS)
    {
        *u8BootMode = g_lbaNandHal.m_dataBuffer[0];
        *u8RebootCmd = g_lbaNandHal.m_dataBuffer[1];
    }
    return status;
}

// <00h>(XXh)(XXh)(XXh)(boot mode)(XXh)<30h> B2R
__INIT_TEXT RtStatus_t LbaTypeNand::sendBootModeChange (uint8_t value)
{
    uint8_t addressBytes[5];
    
    addressBytes[3] = value;
    return sendGenericReadCommand(kLbaNandCommand_ReadPageFirst, kLbaNandCommand_ReadPageSecond, addressBytes, 0, NULL, kLbaNandTimeout_Reset, true);
}


#if !defined(__ghs__)
#pragma mark -LbaPartitionBase-
#endif

__INIT_TEXT RtStatus_t LbaTypeNand::LbaPartitionBase::init(LbaTypeNand * parentDevice)
{
    // Init member variables.
    m_device = parentDevice;
    m_sectorSize = 0;
    m_sectorCount = 0;
    m_hasUnflushedChanges = false;
    m_remainingSectors = 0;
    m_nextSectorInSequence = 0;
    m_isReading = false;
    m_Next512Count = kLbaNandSequentialTransferBaseSectorCount;


#if LBA_HAL_STATISTICS && LBA_HAL_USE_HISTOGRAM
    m_partitionWriteTime.init(ElapsedTimeHistogram::kLinear, 700, 16000, 30);
    m_partitionReadTime.init(ElapsedTimeHistogram::kLinear, 500, 2000, 10);
    m_flushCacheTime.init(ElapsedTimeHistogram::kLinear, 0, 500000, 10);
    m_terminateReadTime.init(ElapsedTimeHistogram::kLinear, 0, 250, 10);
    m_terminateWriteTime.init(ElapsedTimeHistogram::kLinear, 0, 30000, 10);
#endif

#if DEBUG
    m_lastStartSector = 0;
    m_lastSectorCount = 0;
    m_isLastRead = false;
#endif
    
    return SUCCESS;
}

__STATIC_TEXT void LbaTypeNand::LbaPartitionBase::cleanup()
{
    // Need to flush the cache before shutting down to make sure all data
    // has been committed to media.
    flushCache();
    
#if LBA_HAL_STATISTICS && LBA_HAL_USE_HISTOGRAM
    m_partitionWriteTime.cleanup();
    m_partitionReadTime.cleanup();
    m_flushCacheTime.cleanup();
    m_terminateReadTime.cleanup();
    m_terminateWriteTime.cleanup();
#endif
}

__STATIC_TEXT RtStatus_t LbaTypeNand::LbaPartitionBase::exitPartition()
{
    // Terminate any active read or write sequence before switching to
    // another partition mode.
    return terminateReadWrite();
}

__STATIC_TEXT RtStatus_t LbaTypeNand::LbaPartitionBase::startTransferSequence(uint32_t sectorCount)
{
    RtStatus_t  status;

    // Lock the HAL.
    LbaNandHalLocker locker;
    
    if (m_remainingSectors)
    {
        status = terminateReadWrite();
        if (status != SUCCESS)
        {
            return status;
        }
    }

    sectorCount *= kLbaNandSectorMultiple;  
    if( sectorCount==0 || sectorCount>kLbaNandSequentialTransferBaseSectorCount)
    {
        sectorCount = kLbaNandSequentialTransferBaseSectorCount;
    }
    
    m_Next512Count = sectorCount;

    return SUCCESS;
}


__STATIC_TEXT RtStatus_t LbaTypeNand::LbaPartitionBase::readSector(uint32_t sectorNumber, SECTOR_BUFFER * buffer)
{
    assert(m_device);
    
    RtStatus_t status;

    // Lock the HAL.
    LbaNandHalLocker locker;
    
    // First switch to the correct mode for this partition.
    status = setModeForThisPartition();
    if (status != SUCCESS)
    {
        return status;
    }

#if LBA_HAL_RECORD_COMMAND_HISTORY
    m_device->m_commandHistory.insert(kLbaNandCommand_ReadPageFirst);
    m_device->m_commandHistory.insert(kLbaNandCommand_ReadPageSecond);
#endif // LBA_HAL_RECORD_COMMAND_HISTORY
    
    unsigned column;
    
#if !LBA_HAL_USE_SEQUENTIAL_TRANSFERS
    // Fill in the column bytes with the sector multiple.
    column = kLbaNandSectorMultiple;
#else
    uint32_t u32Expected512Count;

    // Terminate an in progress write sequence or out of order sector number.    
    if (m_remainingSectors && (!m_isReading || m_nextSectorInSequence != sectorNumber))
    {
        // terminateReadWrite() terminates the current read sequence and resets m_remainingSectors to 0 
        // so the if statement below will fill in m_remainingSectors with the correct start sector count
        status = terminateReadWrite();
        if (status != SUCCESS)
        {
            return status;
        }
    }
    
    if( !m_remainingSectors)
    {
        u32Expected512Count = m_Next512Count;
        m_Next512Count = kLbaNandSequentialTransferBaseSectorCount;
    }
    else
    {
        u32Expected512Count = (m_remainingSectors*kLbaNandSectorMultiple);
    }
            
    // Fill in the column bytes with the expected sector count.
    column = u32Expected512Count;
    
    // Update read sequence info.
#if LBA_HAL_LOG_RW_SEQUENCE
    bool isNewSequence = false;
#endif
    if (!m_remainingSectors)
    {
        // Starting a sequence, so reset the remaining count.
        m_remainingSectors = (u32Expected512Count/kLbaNandSectorMultiple);
        m_nextSectorInSequence = sectorNumber;  // Set to current sector since we increment just below.
        m_isReading = true;

#if DEBUG
        m_startSector = sectorNumber;
        m_startCount = m_remainingSectors;
#endif

#if LBA_HAL_RECORD_HISTORY
        m_currentEntry = AccessHistoryEntry(m_partitionMode, AccessHistoryEntry::kRead, sectorNumber, 1);
#endif

#if LBA_HAL_LOG_RW_SEQUENCE
        isNewSequence = true;
#endif
    }
    m_remainingSectors--;
    m_nextSectorInSequence++;
#if LBA_HAL_LOG_RW_SEQUENCE
    if (this == &m_device->m_mdp) tss_logtext_Print(LBA_LOGTEXT_MASK, "Read: new=%c rem=%u cur=%u\n", isNewSequence?'Y':'N', m_remainingSectors, sectorNumber);
#endif
#endif // !LBA_HAL_USE_SEQUENTIAL_TRANSFERS

    // Multiply the given sector number by the sector multiple. Since we present 2K
    // sectors instead of 512 byte ones, we have to adjust the sector number appropriately.
    sectorNumber *= kLbaNandSectorMultiple;

    // Update DMA descriptors.
    g_lbaNandHal.m_readDma.setChipSelect(m_device->m_chipSelect);
    g_lbaNandHal.m_readDma.setAddress(column, sectorNumber);
    g_lbaNandHal.m_readDma.setBuffers(buffer, m_sectorSize, NULL, 0);
    
    // Invalidate and clean the data cache before starting the read DMA.
    hw_core_invalidate_clean_DCache();

#if LBA_HAL_STATISTICS || DEBUG
    SimpleTimer cTimer;
#endif //#if LBA_HAL_STATISTICS

    // Kick off the DMA.
    status = g_lbaNandHal.m_readDma.startAndWait(kLbaNandTimeout_ReadPage);
    
#if LBA_HAL_STATISTICS
    m_partitionReadTime += cTimer;
#endif //#if LBA_HAL_STATISTICS

#if LBA_HAL_RECORD_HISTORY
    m_currentEntry.m_time += cTimer;
#endif // LBA_HAL_RECORD_HISTORY

    return status;
}

RtStatus_t LbaTypeNand::LbaPartitionBase::writeSector(uint32_t sectorNumber, const SECTOR_BUFFER * buffer)
{
    assert(m_device);
    
    RtStatus_t status;

    // Lock the HAL.
    LbaNandHalLocker locker;
    
    // First switch to the correct mode for this partition.
    status = setModeForThisPartition();
    if (status != SUCCESS)
    {
        return status;
    }

#if LBA_HAL_RECORD_COMMAND_HISTORY
    m_device->m_commandHistory.insert(kLbaNandCommand_SerialDataInput);
    m_device->m_commandHistory.insert(kLbaNandCommand_WritePage);
    m_device->m_commandHistory.insert(kLbaNandCommand_ReadStatus1);
#endif // LBA_HAL_RECORD_COMMAND_HISTORY

    unsigned column;
    
#if !LBA_HAL_USE_SEQUENTIAL_TRANSFERS
    // Fill in the column bytes with the sector multiple.
    column = kLbaNandSectorMultiple;
#else
    uint32_t u32Expected512Count;

    // Terminate an in progress sequence if it's a read sequence or out of order sector number.
    if (m_remainingSectors && (m_isReading || m_nextSectorInSequence != sectorNumber))
    {
        // terminateReadWrite() terminates the current read sequence and resets m_remainingSectors to 0 
        // so the if statement below will fill in m_remainingSectors with the correct start sector count
        status = terminateReadWrite();
        if (status != SUCCESS)
        {
            return status;
        }
    }

    if( !m_remainingSectors)
    {
        u32Expected512Count = m_Next512Count;
        m_Next512Count = kLbaNandSequentialTransferBaseSectorCount;
    }
    else
    {
        u32Expected512Count = (m_remainingSectors*kLbaNandSectorMultiple);
    }

    // Fill in the column bytes with the maximum sector count.
    column = u32Expected512Count;
    
    // Update read sequence info.
#if LBA_HAL_LOG_RW_SEQUENCE
    bool isNewSequence = false;
#endif
    if (!m_remainingSectors)
    {
        // Starting a sequence, so reset the remaining count.
        m_remainingSectors = (u32Expected512Count/kLbaNandSectorMultiple);
        m_nextSectorInSequence = sectorNumber;  // Set to current sector since we increment just below.
        m_isReading = false;

#if DEBUG
        m_startSector = sectorNumber;
        m_startCount = m_remainingSectors;
#endif

#if LBA_HAL_RECORD_HISTORY
    m_currentEntry = AccessHistoryEntry(m_partitionMode, AccessHistoryEntry::kWrite, sectorNumber, 1);
#endif
        
#if LBA_HAL_LOG_RW_SEQUENCE
        isNewSequence = true;
#endif
    }
    m_remainingSectors--;
    m_nextSectorInSequence++;
#if LBA_HAL_LOG_RW_SEQUENCE
    if (this == &m_device->m_mdp) tss_logtext_Print(LBA_LOGTEXT_MASK, "Write: new=%c rem=%u cur=%u\n", isNewSequence?'Y':'N', m_remainingSectors, sectorNumber);
#endif
#endif // !LBA_HAL_USE_SEQUENTIAL_TRANSFERS

    // Multiply the given sector number by the sector multiple. Since we present larger
    // sectors than 512 byte ones, we have to adjust the sector number appropriately.
    sectorNumber *= kLbaNandSectorMultiple;
    
    // Update the write DMA descriptors.
    g_lbaNandHal.m_writeDma.setChipSelect(m_device->m_chipSelect);
    g_lbaNandHal.m_writeDma.setAddress(column, sectorNumber);
    g_lbaNandHal.m_writeDma.setBuffers(buffer, m_sectorSize, NULL, 0);
    
    // Flush the entire data cache before starting the write. Because our buffers are larger
    // than the cache line size, this is faster than walking the buffer a cache line at a time.
    // Also, note that we do not need to invalidate for writes.
    hw_core_clean_DCache();

#if LBA_HAL_STATISTICS || DEBUG
    SimpleTimer cTimer;
#endif //#if LBA_HAL_STATISTICS

    // Start the DMA.
    status = g_lbaNandHal.m_writeDma.startAndWait(kLbaNandTimeout_WritePage);

#if LBA_HAL_STATISTICS
        m_partitionWriteTime += cTimer;
#endif //#if LBA_HAL_STATISTICS

#if LBA_HAL_RECORD_HISTORY
        m_currentEntry.m_time += cTimer;
#endif // LBA_HAL_RECORD_HISTORY
    
    if (status == SUCCESS)
    {
        // When finished, examine the status byte.
        LbaNandStatus1Response statusResponse;
        m_device->readStatus1(&statusResponse);

        // And check to see if the write failed.
        if (statusResponse.failure())
        {
            // Read status 2 to see what if we can figure out why the error occurred.
            LbaNandStatus2Response response;
            m_device->readStatus2(&response);
            
#if DEBUG
            // Save the remaining since it'll be zeroed in the terminate call.
            uint32_t saveRemaining = m_remainingSectors;
#endif
            
            // Terminate this write sequence since we had a failure.
            terminateReadWrite();
            
            // Read status 1 again after the terminate command.
            LbaNandStatus1Response status1Response;
            m_device->readStatus1(&status1Response);
            
#if DEBUG
            tss_logtext_Print(LBA_LOGTEXT_MASK, "write error: status 1=0x%02x, status 2=0x%02x, status 1 after terminate=0x%02x (remaining=%u, start=%u, cur=%u) (last: start=%u, count=%u, op=%c)\n", statusResponse.m_response, response.m_response, status1Response.m_response, saveRemaining, m_startSector, sectorNumber/kLbaNandSectorMultiple, m_lastStartSector, m_lastSectorCount, m_isLastRead?'r':'w');
#endif
            
            if (response.addressOutOfRange())
            {
                status = ERROR_DDI_LBA_NAND_ADDRESS_OUT_OF_RANGE;
            }
            else if (response.spareBlocksExhausted())
            {
                status = ERROR_DDI_LBA_NAND_SPARE_BLOCKS_EXHAUSTED;
            }
            else
            {
                status = ERROR_DDI_LBA_NAND_WRITE_FAILED;
            }
        }
        else
        {
            // The write suceeded, so remember that there have been changes since
            // the last time we flushed.
            m_hasUnflushedChanges = true;
        }
    }

    return status;
}

RtStatus_t LbaTypeNand::LbaPartitionBase::eraseSectors(uint32_t startSectorNumber, uint32_t sectorCount)
{
    assert(m_device);
    
    RtStatus_t status;
    
    // Nothing to do if there are no sectors to erase.
    if (sectorCount == 0)
    {
        return SUCCESS;
    }

    // Lock the HAL.
    LbaNandHalLocker locker;
    
    // First switch to the correct mode for this partition.
    status = setModeForThisPartition();
    if (status != SUCCESS)
    {
        return status;
    }
    
    // Stop a read or write sequence.
    status = terminateReadWrite();
    if (status != SUCCESS)
    {
        return status;
    }
    
    // Send the start command.
    status = m_device->sendGeneralCommand(kLbaNandCommand_GarbageAreaSetStart, NULL, 0, NULL);
    if (status != SUCCESS)
    {
        return status;
    }

#if LBA_HAL_RECORD_COMMAND_HISTORY
    m_device->m_commandHistory.insert(kLbaNandCommand_SerialDataInput);
    m_device->m_commandHistory.insert(kLbaNandCommand_WritePage);
    m_device->m_commandHistory.insert(kLbaNandCommand_ReadStatus1);
#endif // LBA_HAL_RECORD_COMMAND_HISTORY

    // Multiply the given sector number and count by the sector multiple. Since we present 2K
    // sectors instead of 512 byte ones, we have to adjust the sector number appropriately.
    sectorCount *= kLbaNandSectorMultiple;
    startSectorNumber *= kLbaNandSectorMultiple;
    
    // Update the write DMA descriptors.
    g_lbaNandHal.m_writeDma.setChipSelect(m_device->m_chipSelect);
    g_lbaNandHal.m_writeDma.setAddress(sectorCount, startSectorNumber);
    g_lbaNandHal.m_writeDma.setBuffers(NULL, 0, NULL, 0);
    
    // Flush the entire data cache before starting the write. Because our buffers are larger
    // than the cache line size, this is faster than walking the buffer a cache line at a time.
    // Also, note that we do not need to invalidate for writes.
    hw_core_clean_DCache();

    // Start the DMA.
    status = g_lbaNandHal.m_writeDma.startAndWait(kLbaNandTimeout_WritePage);
    
    // Send the close command regardless of whether the read command succeeded.
    RtStatus_t closeStatus = m_device->sendGeneralCommand(kLbaNandCommand_GarbageAreaSetClose, NULL, 0, NULL);
    if (closeStatus != SUCCESS && status == SUCCESS)
    {
        // The close command failed, but everything else succeeded, so return the close failure.
        status = closeStatus;
    }
    
    if (status == SUCCESS)
    {
        m_hasUnflushedChanges = true;
    }
    
    return status;
}

__STATIC_TEXT RtStatus_t LbaTypeNand::LbaPartitionBase::flushCache()
{
    assert(m_device);
    
    LbaNandHalLocker locker;
    
    // No need to flush if there haven't been any changes to this partition.
    if (!m_hasUnflushedChanges)
    {
        return SUCCESS;
    }
    
    // Must stop a read/write sequence before sending any other command.
    RtStatus_t status = terminateReadWrite();
    if (status != SUCCESS)
    {
        return status;
    }
    
    // Switch to this partition before flushing.
    status = setModeForThisPartition();
    if (status != SUCCESS)
    {
        return status;
    }

#if LBA_HAL_RECORD_HISTORY
    m_currentEntry = AccessHistoryEntry(m_partitionMode, AccessHistoryEntry::kFlush, 0, 0);
#endif

#if LBA_HAL_STATISTICS || LBA_HAL_RECORD_HISTORY
    SimpleTimer cTimer;
#endif //#if LBA_HAL_STATISTICS

    // Send the flush command.
    status = m_device->flushCache();

#if LBA_HAL_STATISTICS
    m_flushCacheTime += cTimer;
#endif //#if LBA_HAL_STATISTICS

#if LBA_HAL_RECORD_HISTORY
    m_currentEntry.m_time += cTimer;
    m_device->m_history.insert(m_currentEntry);
#endif
    
    // Clear the unflushed changes flag.
    if (status == SUCCESS)
    {
        m_hasUnflushedChanges = false;
    }
    
    return status;
}

__STATIC_TEXT RtStatus_t LbaTypeNand::LbaPartitionBase::setModeForThisPartition()
{
    assert(m_device);
    return m_device->setMode(m_partitionMode);
}

__STATIC_TEXT RtStatus_t LbaTypeNand::LbaPartitionBase::terminateReadWrite()
{
    assert(m_device);
    
#if LBA_HAL_USE_SEQUENTIAL_TRANSFERS
    // If there are no more sectors in the read/write sequence, or if we're not
    // in the middle of a read/write sequence, then we don't need to terminate.
    if (m_remainingSectors)
    {
#if DEBUG
        m_isLastRead = m_isReading;
        m_lastStartSector = m_startSector;
        m_lastSectorCount = m_startCount - m_remainingSectors;

#if LBA_HAL_RECORD_HISTORY
        // Update count before inserting entry into history.
        m_currentEntry.m_count = m_lastSectorCount;
        m_device->m_history.insert(m_currentEntry);
#endif // LBA_HAL_RECORD_HISTORY
#endif // DEBUG

#if LBA_HAL_STATISTICS
        SimpleTimer timer;
#endif
        
        RtStatus_t status = m_device->sendResetTypeCommand(kLbaNandCommand_TerminateReadWrite);
        if (status != SUCCESS)
        {
            return status;
        }

#if LBA_HAL_STATISTICS
        if (m_isReading)
        {
            m_terminateReadTime += timer;
        }
        else
        {
            m_terminateWriteTime += timer;
        }
#endif
        
        // Reset sequence information.
        m_remainingSectors = 0;
        m_nextSectorInSequence = 0;
    }
#endif // LBA_HAL_USE_SEQUENTIAL_TRANSFERS
    
    return SUCCESS;
}

__INIT_TEXT RtStatus_t LbaTypeNand::VendorFirmwarePartition::init(LbaTypeNand * parentDevice)
{
    RtStatus_t status = LbaPartitionBase::init(parentDevice);
    if (status == SUCCESS)
    {
        m_partitionMode = kVfpMode;
        m_sectorSize = kLbaNandSectorSize;
        status = m_device->getVfpSize(&m_sectorCount);
    }
    
    return status;
}

__INIT_TEXT RtStatus_t LbaTypeNand::MultimediaDataPartition::init(LbaTypeNand * parentDevice)
{
    RtStatus_t status = LbaPartitionBase::init(parentDevice);
    if (status == SUCCESS)
    {
        m_partitionMode = kMdpMode;
        m_sectorSize = kLbaNandSectorSize;
        status = m_device->getMdpSize(&m_sectorCount);
    }
    
    return status;
}

#if !defined(__ghs__)
#pragma mark -PlainNandPartition-
#endif

__INIT_TEXT RtStatus_t LbaTypeNand::PlainNandPartition::init(LbaTypeNand * parentDevice)
{
    RtStatus_t status = LbaPartitionBase::init(parentDevice);
    if (status == SUCCESS)
    {
        m_partitionMode = kBcmMode;
        m_sectorSize = kPnpSectorSize;
        m_sectorCount = kPnpSectorCount;
    }
    
    return status;
}

RtStatus_t LbaTypeNand::PlainNandPartition::readSector(uint32_t sectorNumber, SECTOR_BUFFER * buffer)
{
    assert(m_device);
    
    RtStatus_t status;
    
    // Allocate a temporary auxiliary buffer before we lock the HAL.
    AuxiliaryBuffer auxBuffer;
    if (auxBuffer.didFail())
    {
        return auxBuffer.getStatus();
    }

    // Lock the HAL.
    LbaNandHalLocker locker;
    
    // First switch to the correct mode for this partition.
    status = setModeForThisPartition();
    if (status != SUCCESS)
    {
        return status;
    }

#if LBA_HAL_RECORD_COMMAND_HISTORY
    m_device->m_commandHistory.insert(kLbaNandCommand_ReadPageFirst);
    m_device->m_commandHistory.insert(kLbaNandCommand_ReadPageSecond);
#endif // LBA_HAL_RECORD_COMMAND_HISTORY
    
    {
        NandEccDescriptor_t ecc = { kNandEccType_RS4 };
        const EccTypeInfo * eccInfo = ecc.getTypeInfo();
        assert(eccInfo);
        
        auto_delete<NandDma::ReadWriteBase> readDma;
        auto_delete<EccTypeInfo::TransactionWrapper> eccWrapper;
        bool useEcc = true;
        
        // For the 3780, we disable ECC on the first PNP sector, which happens to be where the NCB
        // resides. All earlier chips use ECC on all sectors of the PNP.
#if defined(STMP378x)
        if (sectorNumber == 0)
        {
            useEcc = false;
            readDma = new NandDma::ReadRawData(
                m_device->m_chipSelect, // chipSelect,
                kLbaNandCommand_ReadPageFirst, // command1,
                NULL, // addressBytes,
                kPnpAddressByteCount, // addressByteCount,
                kLbaNandCommand_ReadPageSecond, // command2,
                NULL, // dataBuffer,
                0, // dataReadSize,
                NULL, // auxBuffer,
                0); // auxReadSize
        }
        else
#endif
        {
            // Prepare ECC information.
            uint32_t eccMask = ecc.computeMask(kPnpTransferSize, kPnpTransferSize, kEccOperationRead, kEccTransferFullPage, NULL, NULL);
            
            eccWrapper = new EccTypeInfo::TransactionWrapper(ecc, m_device->m_chipSelect, kPnpTransferSize, kEccOperationRead);
            
            readDma = new NandDma::ReadEccData(
                m_device->m_chipSelect, // chipSelect
                kLbaNandCommand_ReadPageFirst, // command1
                NULL, // addressBytes
                kPnpAddressByteCount, // addressByteCount
                kLbaNandCommand_ReadPageSecond, // command2
                buffer, // dataBuffer
                auxBuffer, // auxBuffer
                kPnpTransferSize, // readSize
                ecc, // ecc
                eccMask); // eccMask
        }
        
        // Fill in the row and column addresses.
        readDma->setAddress(0, sectorNumber);
        
        // Invalidate and clean the data cache before starting the read DMA.
        hw_core_invalidate_clean_DCache();
    
#if LBA_HAL_STATISTICS
        SimpleTimer cTimer;
#endif //#if LBA_HAL_STATISTICS
    
        // Kick off the DMA.
        status = readDma->startAndWait(kLbaNandTimeout_ReadPage);
    
        if (status == SUCCESS && useEcc)
        {
            // Pass-through to the abstract ECC correction function.
            status = eccInfo->correctEcc(NULL, NULL);
        }

#if LBA_HAL_STATISTICS
        m_partitionReadTime += cTimer;
#endif //#if LBA_HAL_STATISTICS
    }
    
    if (status != SUCCESS)
    {
        tss_logtext_Print(LBA_LOGTEXT_MASK, "PNP read error = 0x%08x\n", status);
    }

    return status;
}

RtStatus_t LbaTypeNand::PlainNandPartition::writeSector(uint32_t sectorNumber, const SECTOR_BUFFER * buffer)
{
    assert(m_device);
    
    RtStatus_t status;

    // Allocate a temporary auxiliary buffer before we lock the HAL.
    AuxiliaryBuffer auxBuffer;
    if (auxBuffer.didFail())
    {
        return auxBuffer.getStatus();
    }

    // Lock the HAL.
    LbaNandHalLocker locker;
    
    // First switch to the correct mode for this partition.
    status = setModeForThisPartition();
    if (status != SUCCESS)
    {
        return status;
    }
    
    // Clear the metadata from the aux buffer since we don't have any information to put into the metadata.
    auxBuffer.fill(0xff);

#if LBA_HAL_RECORD_COMMAND_HISTORY
    m_device->m_commandHistory.insert(kLbaNandCommand_SerialDataInput);
    m_device->m_commandHistory.insert(kLbaNandCommand_WritePage);
    m_device->m_commandHistory.insert(kLbaNandCommand_ReadStatus1);
#endif // LBA_HAL_RECORD_COMMAND_HISTORY
    
    {
        NandEccDescriptor_t ecc = { kNandEccType_RS4 };
        auto_delete<NandDma::ReadWriteBase> writeDma;
        auto_delete<EccTypeInfo::TransactionWrapper> eccWrapper;
        
        // For the 3780, we disable ECC on the first PNP sector, which happens to be where the NCB
        // resides. All earlier chips use ECC on all sectors of the PNP.
#if defined(STMP378x)    
        if (sectorNumber == 0)
        {
            writeDma = new NandDma::WriteRawData(
                m_device->m_chipSelect, // chipSelect
                kLbaNandCommand_SerialDataInput, // command1
                NULL, // addressBytes
                kPnpAddressByteCount, // addressByteCount
                kLbaNandCommand_WritePage, // command2
                buffer, // dataBuffer
                LARGE_SECTOR_DATA_SIZE, // dataReadSize
                auxBuffer, // auxBuffer
                LARGE_SECTOR_REDUNDANT_SIZE); // auxReadSize
        }
        else
#endif
        {
            // Prepare ECC information.
            const EccTypeInfo * eccInfo = ecc.getTypeInfo();
            uint32_t dataSize;
            uint32_t leftoverSize;
            uint32_t eccMask = ecc.computeMask(kPnpTransferSize, kPnpTransferSize, kEccOperationWrite, kEccTransferFullPage, &dataSize, &leftoverSize);
            
            eccWrapper = new EccTypeInfo::TransactionWrapper(ecc, m_device->m_chipSelect, kPnpTransferSize, kEccOperationWrite);
            
            writeDma = new NandDma::WriteEccData(
                m_device->m_chipSelect, // chipSelect,
                kLbaNandCommand_SerialDataInput, // command1,
                NULL, // addressBytes,
                kPnpAddressByteCount, // addressByteCount,
                kLbaNandCommand_WritePage, // command2,
                buffer, // dataBuffer,
                auxBuffer, // auxBuffer,
                kPnpTransferSize, // sendSize,
                dataSize, // dataSize,
                leftoverSize, // leftoverSize,
                ecc,
                eccMask);
        }
        
        // Fill in the row and column addresses.
        writeDma->setAddress(0, sectorNumber);
        
        // Flush the entire data cache before starting the write. Because our buffers are larger
        // than the cache line size, this is faster than walking the buffer a cache line at a time.
        // Also, note that we do not need to invalidate for writes.
        hw_core_clean_DCache();
    
    #if LBA_HAL_STATISTICS
        SimpleTimer cTimer;
    #endif //#if LBA_HAL_STATISTICS
    
        // Start the DMA.
        status = writeDma->startAndWait(kLbaNandTimeout_WritePage);
            
#if LBA_HAL_STATISTICS
        m_partitionWriteTime += cTimer;
#endif //#if LBA_HAL_STATISTICS
    
        if (status == SUCCESS)
        {
            // When finished, grab the status.
            LbaNandStatus1Response statusResponse;
            m_device->readStatus1(&statusResponse);
    
            // And check to see if the write failed.
            if (statusResponse.failure())
            {
                // Read status 2 to see what if we can figure out why the error occurred.
                LbaNandStatus2Response response;
                m_device->readStatus2(&response);
                
#if DEBUG
                tss_logtext_Print(LBA_LOGTEXT_MASK, "write error, status 2 = 0x%02x\n", response.m_response);
#endif
    
                if (response.addressOutOfRange())
                {
                    status = ERROR_DDI_LBA_NAND_ADDRESS_OUT_OF_RANGE;
                }
                else if (response.spareBlocksExhausted())
                {
                    status = ERROR_DDI_LBA_NAND_SPARE_BLOCKS_EXHAUSTED;
                }
                else
                {
                    status = ERROR_DDI_LBA_NAND_WRITE_FAILED;
                }
            }
            else
            {
                m_hasUnflushedChanges = true;
            }
        }
    }
    
    if (status != SUCCESS)
    {
        tss_logtext_Print(LBA_LOGTEXT_MASK, "PNP write error = 0x%08x\n", status);
    }
    
    return status;
}

RtStatus_t LbaTypeNand::PlainNandPartition::eraseSectors(uint32_t startSectorNumber, uint32_t sectorCount)
{
    // Allocate a temporary data buffer.
    SectorBuffer buffer;
    if (buffer.didFail())
    {
        return buffer.getStatus();
    }
    
    // Fill the buffer with all ffs.
    buffer.fill(0xff);

    // Write the empty sector over only the first sector the caller asked us to erase.
    return writeSector(startSectorNumber, buffer);
}

__STATIC_TEXT RtStatus_t LbaTypeNand::PlainNandPartition::exitPartition()
{
    // Let our superclass do its thing.
    RtStatus_t status = LbaPartitionBase::exitPartition();
    if (status != SUCCESS)
    {
        return status;
    }
    
    // Next, flush the cache if there have been writes since the last flush.
    return flushCache();
}

// EOF
//! @}
