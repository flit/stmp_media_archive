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
//! \addtogroup ddi_media_lba_nand_hal
//! @{
//! \file   ddi_lba_nand_hal.h
//! \brief  Declaration of the HAL interface for LBA-NAND devices.
////////////////////////////////////////////////////////////////////////////////
#if !defined(__ddi_nand_hal_lba_h__)
#define __ddi_nand_hal_lba_h__

#include "types.h"
#include "errordefs.h"

////////////////////////////////////////////////////////////////////////////////
// Definitions
////////////////////////////////////////////////////////////////////////////////

#pragma pack(1)
/*!
 * \brief Structure of the response from an ID_2_Read command.
 */
struct LbaNandId2Response
{
    //! \brief Constants for the ID_2_Read command response.
    enum _lba_nand_id_2_responses
    {
        kToshibaMakerCode = 0x98,
        kDeviceCodeRangeStart = 0x21,
        kDeviceCodeRangeEnd = 0x2f,
        
        kSize_2GB = 0x01,
        kSize_4GB = 0x02,
        kSize_8GB = 0x03,
        kSize_16GB = 0x04,
        
        kSignature1 = 0x55,
        kSignature2 = 0xaa
    };

    union
    {
        struct
        {
            //! \name Response bytes
            //@{
            uint8_t m_makerCode;    //!< Toshiba maker code: #kToshibaMakerCode.
            uint8_t m_deviceCode;   //!< LBA-NAND device codes in the range of 0x21-0x2f.
            uint8_t m_deviceSize;   //!< See #_lba_nand_id_2_responses for possible values.
            uint8_t m_signature1;   //!< See #kSignature1.
            uint8_t m_signature2;   //!< See #kSignature2.
            //@}
        };
        uint8_t m_data[5];  //!< Raw byte array.
    };
    
    //! \brief Returns the number of GB large the device is.
    //! \return An integer number of gigbytes, or zero if the ID size value returned from the device is unrecognized.
    unsigned getDeviceSizeInGB();
};
#pragma pack()

/*!
 * \brief Interface for an LBA-NAND.
 *
 * \todo Document me.
 */
class LbaNandPhysicalMedia
{
public:

    /*!
     * \brief Common interface for partitions of an LBA-NAND.
     * \todo Document me.
     */
    class LbaPartition
    {
    public:
        
        virtual LbaNandPhysicalMedia * getDevice() = 0;
        
        virtual uint32_t getSectorCount() = 0;
        
        virtual uint32_t getSectorSize() = 0;

        virtual RtStatus_t readSector(uint32_t sectorNumber, SECTOR_BUFFER * buffer) = 0;

        virtual RtStatus_t writeSector(uint32_t sectorNumber, const SECTOR_BUFFER * buffer) = 0;
        
        virtual RtStatus_t eraseSectors(uint32_t startSectorNumber, uint32_t sectorCount) = 0;
    
        virtual RtStatus_t flushCache() = 0;

        virtual RtStatus_t startTransferSequence(uint32_t sectorCount) = 0;
    };
    
    //! \name Partition access
    //@{
    virtual LbaPartition * getFirmwarePartition() = 0;
    virtual LbaPartition * getDataPartition() = 0;
    virtual LbaPartition * getBootPartition() = 0;
    //@}
    
    virtual unsigned getChipSelectNumber() = 0;
    
    virtual RtStatus_t getReadIdResults(LbaNandId2Response * responseData) = 0;
    
    //! \name VFP size
    //@{
    virtual unsigned getVfpMaxSize() = 0;
    virtual unsigned getVfpMinSize() = 0;
    virtual RtStatus_t setVfpSize(uint32_t newSectorCount) = 0;
    //@}
    
    //! \name Power control
    //@{
    virtual RtStatus_t enablePowerSaveMode(bool enable) = 0;
    virtual RtStatus_t enableHighSpeedWrites(bool enable) = 0;
    //@}
    
    //! \brief Known device attributes.
    typedef enum {
        kUniqueId,
        kControllerFirmwareVersion,
        kDeviceHardwareVersion
    } DeviceAttributeName_t;
    
    //! \name Device attributes
    //@{
    virtual RtStatus_t readDeviceAttribute(DeviceAttributeName_t which, void * data, unsigned length, unsigned * actualLength) = 0;
    //@}

    // misc utilities
    virtual RtStatus_t changeRebootCommand () = 0;
};

//! \brief Initialize the LBA-NAND HAL layer.
//!
//! This function initializes hardware to interface with the LBA-NAND devices,
//! sets timings, scans for devices and verifies that they are indeed LBA-NANDs,
//! and creates the instances of LbaNandPhysicalMedia.
//!
//! \retval SUCCESS The HAL was initialized successfully and there is at least
//!     one LBA-NAND present.
RtStatus_t ddi_lba_nand_hal_init();

//! \brief Shut down and clean up the LBA-NAND HAL layer.
//!
//! \retval SUCCESS
RtStatus_t ddi_lba_nand_hal_shutdown();

//! \brief Returns the total number of LBA-NAND devices.
//!
//! \return A count of LBA-NAND devices found by the HAL. This count will always
//!     be greater than 0 if ddi_lba_nand_hal_init() returned successfully. Before
//!     the HAL is initialized, this function will return 0.
unsigned ddi_lba_nand_hal_get_device_count();

//! \brief Return the LBA-NAND object for a given chip select.
//!
//! \param chipSelect The chip select number for the device to return.
//!     This value should range from 0 to the number returned from ddi_lba_nand_get_device_count()
//!     minus one.
//! \return An instance of LbaNandPhysicalMedia for \a chipSelect.
LbaNandPhysicalMedia * ddi_lba_nand_hal_get_device(unsigned chipSelect);

#endif // __ddi_nand_hal_lba_h__
// EOF
//! @}
