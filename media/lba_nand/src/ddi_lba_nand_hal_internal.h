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
//! \file ddi_lba_nand_hal_internal.h
//! \brief Internal declarations for the HAL interface for LBA-NAND devices.
////////////////////////////////////////////////////////////////////////////////
#if !defined(__ddi_nand_hal_lba_internal_h__)
#define __ddi_nand_hal_lba_internal_h__

#include "ddi_lba_nand_hal.h"
#include "drivers/media/nand/gpmi/ddi_nand_gpmi.h"
#include "drivers/media/nand/gpmi/ddi_nand_gpmi_dma.h"
#include "drivers/media/ddi_media_errordefs.h"
#include "drivers/media/ddi_media.h"
#include "drivers/media/sectordef.h"
#include "drivers/media/include/ddi_media_timers.h"
#include "components/telemetry/tss_logtext.h"
#include "os/dmi/os_dmi_api.h"
#include "circular_array.h"
#include "access_history_entry.h"

////////////////////////////////////////////////////////////////////////////////
// Definitions
////////////////////////////////////////////////////////////////////////////////

//! \name Build options
//@{

//! Set to 1 to turn on recording of average times for certain operations.
#define LBA_HAL_STATISTICS 0

//! Set to 1 to use the ElapsedTimeHistogram class instead of AverageTime for tracking time statistics.
#define LBA_HAL_USE_HISTOGRAM 0

//! Set to 1 to enable recording of read/write sector history per partition.
#define LBA_HAL_RECORD_HISTORY 0

//! Set to 1 to enable recording of all commands sent to the device.
#define LBA_HAL_RECORD_COMMAND_HISTORY 0

//! Set this macro to 1 to enable printing of read/write sequence steps.
#define LBA_HAL_LOG_RW_SEQUENCE 0

//! Set to 1 to print a message when entering and exiting power save mode.
#define LBA_HAL_LOG_POWER_SAVE_MODE 0

//! Set to 1 to enable sequential reads and writes.
#define LBA_HAL_USE_SEQUENTIAL_TRANSFERS 1

#if !defined(LBA_HAL_HISTORY_RECORD_COUNT)
    //! Number of history records to save at once.
    #define LBA_HAL_HISTORY_RECORD_COUNT (1000)
#endif

#if !defined(LBA_HAL_COMMAND_HISTORY_RECORD_COUNT)
    //! Number of history records to save at once.
    #define LBA_HAL_COMMAND_HISTORY_RECORD_COUNT (1000)
#endif

//@}

//! Event and verbosity mask to use for TSS logtext prints.
#define LBA_LOGTEXT_MASK (LOGTEXT_EVENT_ALL)

//! \brief Timeout Constants
//!
//! The following constants describe how much patience we have when waiting for
//! particular operations to finish.
//!
//! \todo Adjust timeouts to match data sheet and add timeouts for other commands.
enum _lba_nand_hal_timeouts
{
    kLbaNandTimeout_Reset = 5000000,                    //!< The time, in microseconds, to wait for a reset to finish. (5 seconds)
    kLbaNandTimeout_ReadPage = 1500000,                 //!< The time, in microseconds, to wait for a page read to finish. (1 second)
    kLbaNandTimeout_WritePage = 1500000,                //!< The time, in microseconds, to wait for a page write to finish. (1 second)
    kLbaNandTimeout_SetVfpSize = 40000000              //!< The time, in microseconds, to wait while changing the firmware partition size. (40 seconds)
};

//! \brief Various LBA-NAND constants.
enum
{
    //! Size of an LBA-NAND's logical sector.
    kLbaNandBaseSectorSize = 512,
    
    //! Number of 512-byte logical sectors to read or write in one transfer, forming
    //! the LBA-NAND transfer unit. Can be either 1, 4, or 8.
    //!
    //! \note If you change the sector multiple here, you must also change \a kLbaNandDefaultTransferProtocol1.
    kLbaNandSectorMultiple = 8,
    
    //! The sector size we use for the MDP and VFP partitions. This is the size of the
    //! transfer unit in LBA-NAND terms.
    kLbaNandSectorSize = kLbaNandBaseSectorSize * kLbaNandSectorMultiple,
    
    //! We only support 8-bit devices (not that there are any 16-bit LBA-NANDs out there).
    kLbaNandBusWidth = 8,
    
    //! Number of bytes the device sends in response to a device attribute read.
    kLbaNandDeviceAttributeResponseLength = 512,
    
    //! Maximum number of GB for an LBA-NAND to use the small addressing scheme (i.e., 3 row bytes).
    kLbaNandSmallDeviceMaximumGB = 8,
    
    //! 8GB and smaller devices use 3 row address bytes.
    kLbaNandSmallDeviceRowByteCount = 3,
    
    //! Devices 16GB and greater use 4 row address bytes.
    kLbaNandLargeDeviceRowByteCount = 4,
    
    //! Number of bytes returned from the Read ID 2 command.
    kLbaNandReadId2ResponseLength = 5,
    
    //! Number of 512-byte sectors to transfer in one sequence. Cannot be larger than 0x10000.
    kLbaNandSequentialTransferBaseSectorCount = 8192, //32768,
    
    //! Maximum number of sectors that can be read or written in one sequence. In other words,
    //! the maximum count that can be specified for a read/write command. This value is in full
    //! sized sectors (i.e., #kLbaNandSectorSize).
    kLbaNandMaxReadWriteSectorCount = kLbaNandSequentialTransferBaseSectorCount / kLbaNandSectorMultiple,
};

//! \brief LBA-NAND command codes.
enum _lba_nand_commands
{
    kLbaNandCommand_ReadId2 = 0x92,
    kLbaNandCommand_ReadStatus1 = 0x70,
    kLbaNandCommand_ReadStatus2 = 0x71,
    kLbaNandCommand_RebootDevice = 0xfd,
    kLbaNandCommand_ReadPageFirst = 0x00,
    kLbaNandCommand_ReadPageSecond = 0x30,
    kLbaNandCommand_SerialDataInput = 0x80,
    kLbaNandCommand_WritePage = 0x10,
    kLbaNandCommand_GeneralFirst = 0x00,
    kLbaNandCommand_GeneralSecond = 0x57,
    kLbaNandCommand_ModeChangeToMdp = 0xfc,
    kLbaNandCommand_ModeChangeToVfp = 0xbe,
    kLbaNandCommand_ModeChangeToBcm = 0xbf,
    kLbaNandCommand_CacheFlush = 0xf9,
    kLbaNandCommand_GetMdpSize = 0xb0,
    kLbaNandCommand_SetVfpSize = 0x22,
    kLbaNandCommand_GetVfpSize = 0xb5,
    kLbaNandCommand_ExSetVfpSize = 0x24,
    kLbaNandCommand_ExGetVfpSize = 0xb7,
    kLbaNandCommand_ExGetVfpSizeVariation = 0xb8,
    kLbaNandCommand_ChangePassword = 0x21,
    kLbaNandCommand_SetTransferProtocol1 = 0xa2,
    kLbaNandCommand_SetTransferProtocol2 = 0xb2,
    kLbaNandCommand_GetTransferProtocol1 = 0xa3,
    kLbaNandCommand_GetTransferProtocol2 = 0xb3,
    kLbaNandCommand_SetMinimumBusyTime = 0xa4,
    kLbaNandCommand_GetMinimumBusyTime = 0xb4,
    kLbaNandCommand_EnablePowerSaveMode = 0xba,
    kLbaNandCommand_DisablePowerSaveMode = 0xbb,
    kLbaNandCommand_EnableHighSpeedWriteMode = 0xbc,
    kLbaNandCommand_DisableHighSpeedWriteMode = 0xbd,
    kLbaNandCommand_DeviceAttributeStart = 0x9e,
    kLbaNandCommand_DeviceAttributeClose = 0x9f,
    kLbaNandCommand_GarbageAreaSetStart = 0x5e,
    kLbaNandCommand_GarbageAreaSetClose = 0x5f,
    kLbaNandCommand_TerminateReadWrite = 0xfb
};

//! \brief VFP constants
enum _lba_nand_vfp_constants
{
    kLbaNandDefaultVfpPassword = 0xffff,                //!< Default password for LBA-NAND
    kLbaNandVfpMinSize = 0x4000,                        //!< Minimum non-zero size of the VFP in base/physical sectors
    kLbaNandVfpMaxSize = 0x10000,                       //!< Maximum size of the VFP in base/physical sectors
    kLbaNandVfpStepSize = 0x0200,                       //!< Allocation unit step size of the VFP in base/physical sectors
    kLbaNandVfpZeroSizeValue = 0x2020,                  //!< VFP get size return value for zero size VFP
    kLbaNandVfpExSizeValue = 0x3fff,                    //!< VFP get size return value for VFP size setby EX_ command
    kLbaNandVfpExCapacityModelCategory = 0x10,          //!< VFP EX_ Capacity Model Type Catagory
    kLbaNandVfpExCapacityModelType = 0x11,              //!< VFP EX_ Capacity Model Type
    kLbaNandVfpExCapacityModelTypeMax = 0x13,           //!< VFP EX_ Capacity Model Type Maximum
    kLbaNandVfpExCapacityModelUnitSectors = (0x1000000 / kLbaNandSectorSize)    //!< VFP EX_ Capacity Model Unit size in logical sectors
};

//! \brief Transfer protocol bitmask constants.
enum _lba_nand_transfer_protocol
{
    kLbaNandTransferProtocol_SectorMultiple1 = 1 << 0,
    kLbaNandTransferProtocol_SectorMultiple4 = 1 << 1,
    kLbaNandTransferProtocol_SectorMultiple8 = 1 << 2,
    kLbaNandTransferProtocol_SectorSize512 = 0,
    kLbaNandTransferProtocol_SectorSize528 = 1 << 5,
    kLbaNandTransferProtocol_NoTransferCheck = 0,
    kLbaNandTransferProtocol_TransferCheckCRC16 = 1 << 6,
    kLbaNandTransferProtocol_TransferCheckECC = 1 << 7,
    kLbaNandTransferProtocol_TransferCorrectECC = (1 << 6 | 1 << 7),
    
    kLbaNandTransferProtocol_ReadTypeA = 0,
    kLbaNandTransferProtocol_ReadTypeB = 2,
    kLbaNandTransferProtocol_ReadTypeC = 3,
    kLbaNandTransferProtocol_WriteTypeA = 0,
    kLbaNandTransferProtocol_WriteTypeB = 4,
    
    //! Value to set for transfer protocol 1.
    kLbaNandDefaultTransferProtocol1 = kLbaNandTransferProtocol_SectorMultiple8 | kLbaNandTransferProtocol_SectorSize512,
    
    //! Value to use for transfer protocol 2.
    kLbaNandDefaultTransferProtocol2 = kLbaNandTransferProtocol_ReadTypeA | kLbaNandTransferProtocol_WriteTypeA
};

//! \brief boot mode constants.
enum _lba_nand_boot_mode_code
{
    kLbaNandBootMode1Code = 0x11,
    kLbaNandBootMode2Code = 0x22,
    kLbaNandBootMode3Code = 0x33,
    kLbaNandBootMode5Code = 0x55,
    kLbaNandBootMode6Code = 0x66,
    kLbaNandBootMode7Code = 0x77
};

//! \brief "reboot command change" constants.
enum _lba_nand_reboot_cmd_change_code
{
    kLbaNandRebootCmd_FDh = 0xad,
    kLbaNandRebootCmd_FFh = 0xaf,
};

/*!
 * \brief Utility structure for parsing a response from the Status_1_Read command.
 */
struct LbaNandStatus1Response
{
    //! The actual byte returned from the status command.
    uint8_t m_response;

    //! \brief Bitmasks for the status 1 command responses.
    enum
    {
        kFailureMask = 1 << 0,
        kSectorWriteTransferErrorMask = 1 << 2,
        kNewCommandStartMask = 1 << 5,
        kReadyBusyMask = 1 << 6
    };
    
    //! \brief Default constructor.
    LbaNandStatus1Response() : m_response(0) {}
    
    //! \brief Constructor taking the response value.
    LbaNandStatus1Response(uint8_t data) : m_response(data) {}
    
    inline bool failure() const { return (m_response & kFailureMask) != 0; }
    inline bool sectorWriteTransferError() const { return (m_response & kSectorWriteTransferErrorMask) != 0; }
    inline bool newCommandStart() const { return (m_response & kNewCommandStartMask) != 0; }
    inline bool busy() const { return (m_response & kReadyBusyMask) == 0; }
};

/*!
 * \brief Utility structure for parsing a response from the Status_2_Read command.
 */
struct LbaNandStatus2Response
{
    //! The actual byte returned from the status command.
    uint8_t m_response;

    //! \brief Bitmasks for the status 2 command responses.
    enum
    {
        kPowerSaveModeMask = 1 << 0,
        kCurrentPartitionMask = (1 << 1 | 1 << 2),
        kHighSpeedWriteModeMask = 1 << 3,
        kAddressOutOfRangeMask = 1 << 4,
        kSpareBlocksExhaustedMask = 1 << 5,
        kCommandParameterErrorMask = 1 << 6
    };
    
    //! \brief Default constructor.
    LbaNandStatus2Response() : m_response(0) {}
    
    //! \brief Constructor taking the response value.
    LbaNandStatus2Response(uint8_t data) : m_response(data) {}
    
    inline bool powerSaveMode() const { return (m_response & kPowerSaveModeMask) != 0; }
    inline unsigned currentPartition() const { return m_response & kCurrentPartitionMask; }
    inline bool highSpeedWriteMode() const { return (m_response & kHighSpeedWriteModeMask) != 0; }
    inline bool addressOutOfRange() const { return (m_response & kAddressOutOfRangeMask) != 0; }
    inline bool spareBlocksExhausted() const { return (m_response & kSpareBlocksExhaustedMask) != 0; }
    inline bool commandParameterError() const { return (m_response & kCommandParameterErrorMask) != 0; }
};

/*!
 * Information about an LBA-NAND device attribute.
 */
struct DeviceAttributeInfo
{
    uint32_t m_address; //!< Address of the attribute.
    uint32_t m_length;  //!< The attribute's length, starting from the first byte of the returned data buffer.
};

typedef CircularArray<AccessHistoryEntry> AccessHistory;

typedef enum _lba_nand_commands LbaNandCommand_t;
typedef CircularArray<LbaNandCommand_t> CommandHistory;

// Select which class to use for time statistics based on whether we want histogramming.
// Both classes have the same programmatic interface, so they are interchangeable.
#if LBA_HAL_USE_HISTOGRAM
typedef ElapsedTimeHistogram LbaElapsedTime;
#else
typedef AverageTime LbaElapsedTime;
#endif // LBA_HAL_USE_HISTOGRAM

/*!
 * \brief Concrete class for an LBA-NAND.
 *
 * This class provides a concrete implementation of the purely abstract LbaNandPhysicalMedia
 * class. Its inner classes provide the implementation for the LbaPartition class, one
 * for each of the three partition types in an LBA-NAND.
 *
 * Many of the LBA commands are implemented as methods of this class. These include commands
 * to set the device mode, reboot the device, and so on.
 */
class LbaTypeNand : public LbaNandPhysicalMedia
{
public:
    
    //! \brief Modes that the LBA-NAND can be put into.
    //!
    //! These constants are also the pre-shifted values for the Current Partition field,
    //! bits 1 and 2, of the Status_2_Read command. See LbaNandStatus2Response::currentPartition().
    typedef enum {
        kPnpMode = 0,   //!< Read-only plain NAND mode. This mode is only entered when the device is freshly rebooted. If you ask to switch to this mode, the device will actually be placed into BCM mode.
        kBcmMode = 2,   //!< Read-write plain NAND mode.
        kVfpMode = 4,   //!< Vendor firmware partition access mode.
        kMdpMode = 6    //!< Multimedia data partition access mode (also called LBA mode).
    } LbaNandMode_t;

    /*!
     * \brief Concrete subclass of LbaPartition.
     *
     * This class provides most of the implementation for each of the three
     * LBA-NAND partition types. The read and write sector commands are implemented here.
     */
    class LbaPartitionBase : public LbaPartition
    {
    public:
        
        virtual RtStatus_t init(LbaTypeNand * parentDevice);
        
        virtual void cleanup();
        
        virtual inline LbaNandPhysicalMedia * getDevice() { return m_device; }
        
        virtual inline uint32_t getSectorCount() { return m_sectorCount; }
        
        virtual inline uint32_t getSectorSize() { return m_sectorSize; }

        virtual RtStatus_t readSector(uint32_t sectorNumber, SECTOR_BUFFER * buffer);

        virtual RtStatus_t writeSector(uint32_t sectorNumber, const SECTOR_BUFFER * buffer);
        
        virtual RtStatus_t eraseSectors(uint32_t startSectorNumber, uint32_t sectorCount);
        
        virtual RtStatus_t flushCache();
        
        //! \brief Let the partition prepare for switching the device to another mode.
        virtual RtStatus_t exitPartition();

        virtual RtStatus_t startTransferSequence(uint32_t sectorCount);

    protected:
        LbaTypeNand * m_device; //!< The encompassing LBA-NAND device.
        uint32_t m_sectorCount; //!< Size in sectors of this partition.
        uint32_t m_sectorSize;  //!< Size of a sector of this partition in bytes.
        LbaNandMode_t m_partitionMode;   //!< Mode to use for this partition.
        bool m_hasUnflushedChanges;    //!< Whether there are unflushed writes on this partition.
        
        //! \name Transfer sequence info
        //@{
        uint32_t m_remainingSectors;    //!< Count of sectors remaining in the current read/write transaction sequence. The device is within an active read/write sequence if this count is nonzero. Units are sector multiple sectors.
        uint32_t m_nextSectorInSequence;       //!< Address of the next sequential sector to be read. Units are sector multiple sectors.
        bool m_isReading;   //!< True if the I/O sequence is a read sequence, false if a write sequence.
        uint32_t m_Next512Count; //!< Expected sector count of the current transfer sequence
        //@}

#if DEBUG
        //! \name Last transfer info
        //@{
        uint32_t m_startSector;
        uint32_t m_startCount;
        uint32_t m_lastStartSector;
        uint32_t m_lastSectorCount;
        bool m_isLastRead;
        //@}
#endif

#if LBA_HAL_STATISTICS
        //! \name Command statistics
        //@{
        LbaElapsedTime m_partitionWriteTime;   //!< Average Write time
        LbaElapsedTime m_partitionReadTime;   //!< Average Read time
        LbaElapsedTime m_flushCacheTime;   //!< Average Read time
        LbaElapsedTime m_terminateReadTime;   //! Average time to terminate a read sequence.
        LbaElapsedTime m_terminateWriteTime;   //! Average time to terminate a write sequence.
        //@}
#endif //#if LBA_HAL_STATISTICS

#if LBA_HAL_RECORD_HISTORY
        AccessHistoryEntry m_currentEntry;    //!< History entry for the current operation sequence.
#endif // LBA_HAL_RECORD_HISTORY
        
        //! \brief Put the device into the mode for this partition type.
        RtStatus_t setModeForThisPartition();
        
        //! \brief Terminate a sequential read or write sequence.
        RtStatus_t terminateReadWrite();
    };
    
    /*!
     * \brief Firmware partition.
     */
    class VendorFirmwarePartition : public LbaPartitionBase
    {
    public:
        virtual RtStatus_t init(LbaTypeNand * parentDevice);
    };
    
    /*!
     * \brief Data partition.
     */
    class MultimediaDataPartition : public LbaPartitionBase
    {
    public:
        virtual RtStatus_t init(LbaTypeNand * parentDevice);
    };
    
    /*!
     * \brief Boot partition.
     *
     * When the LBA-NAND device is powered up, it starts in a "plain NAND" mode where
     * it emulates a 2K-page SLC device. This allows for easy booting using existing
     * code. Just like the other partitions, the boot partition class presents a
     * read/write interface with 2048-byte sectors. The difference is that the data
     * is read and written using 4-bit Reed-Solomon ECC, just like a raw NAND with a
     * 2112-byte page.
     *
     * The boot partition always has a fixed size of 256 pages of 2112 bytes each.
     * The partition size is not affected by adjusting the VFP size, and it is not
     * possible to remove the boot partition entirely.
     */
    class PlainNandPartition : public LbaPartitionBase
    {
    public:
        enum
        {
            //! The plain NAND boot partition always has a fixed sector count.
            kPnpSectorCount = 256,
            
            //! Fixed sector size for the PNP.
            kPnpSectorSize = LARGE_SECTOR_DATA_SIZE,
            
            //! Size of reads and writes for the PNP partition.
            kPnpTransferSize = LARGE_SECTOR_TOTAL_SIZE,
            
            //! Number of address bytes, rows + columns, to use for PNP reads and writes.
            kPnpAddressByteCount = 5
        };
        
        virtual RtStatus_t init(LbaTypeNand * parentDevice);

        virtual RtStatus_t readSector(uint32_t sectorNumber, SECTOR_BUFFER * buffer);

        virtual RtStatus_t writeSector(uint32_t sectorNumber, const SECTOR_BUFFER * buffer);
        
        virtual RtStatus_t eraseSectors(uint32_t startSectorNumber, uint32_t sectorCount);
        
        //! \brief Let the partition prepare for switching the device to another mode.
        virtual RtStatus_t exitPartition();
    };
    
    //! \brief Initialize the device instance.
    RtStatus_t init(unsigned chipSelect);
    
    //! \brief Clean up and shut down the device.
    void cleanup();
    
    RtStatus_t rebootDevice();

    inline virtual LbaPartition * getFirmwarePartition() { return &m_vfp; }
    inline virtual LbaPartition * getDataPartition() { return &m_mdp; }
    inline virtual LbaPartition * getBootPartition() { return &m_pnp; }
    
    inline LbaNandMode_t getMode() const { return m_mode; }
    RtStatus_t setMode(LbaNandMode_t mode);
    
    RtStatus_t exitCurrentPartition();

    //! \name LbaNandPhysicalMedia implementation
    //@{
    virtual unsigned getChipSelectNumber() { return m_chipSelect; }

    virtual RtStatus_t getReadIdResults(LbaNandId2Response * responseData);
    
    virtual unsigned getVfpMaxSize(void) { return  m_vfpMaxSize; }
    virtual unsigned getVfpMinSize(void) { return  (kLbaNandVfpMinSize / kLbaNandSectorMultiple); }
    virtual RtStatus_t setVfpSize(uint32_t newSectorCount);
    
    virtual RtStatus_t enablePowerSaveMode(bool enable);
    
    virtual RtStatus_t enableHighSpeedWrites(bool enable);

    virtual RtStatus_t readDeviceAttribute(DeviceAttributeName_t which, void * data, unsigned length, unsigned * actualLength);

    virtual RtStatus_t changeRebootCommand ();
    //@}
    
    //! \name Status
    //@{
    RtStatus_t readStatus1(LbaNandStatus1Response * response);
    RtStatus_t readStatus2(LbaNandStatus2Response * response);
    //@}
    
    //! \brief Returns the number of row bytes needed for this device.
    inline unsigned getRowByteCount() const { return m_rowByteCount; }
    
protected:
    VendorFirmwarePartition m_vfp;  //!< The firmware partition for this device.
    MultimediaDataPartition m_mdp;  //!< The data partition for this device.
    PlainNandPartition m_pnp;       //!< The boot partition for this device.
    LbaNandMode_t m_mode;   //!< The current mode of the LBA-NAND device.
    uint32_t m_vfpMaxSize;  //!< Max size in sectors of VFP for this device.
    unsigned m_chipSelect;  //!< Chip select number for this device.
    unsigned m_rowByteCount; //!< Number of row bytes needed to access all sectors of this device.
    unsigned m_PowerSavedEnabled; //!< power saving status for this device

#if LBA_HAL_STATISTICS
    LbaElapsedTime m_modeSwitchTime;   //!< Average Write time
#endif //#if LBA_HAL_STATISTICS

#if LBA_HAL_RECORD_HISTORY
    AccessHistory m_history;    //!< History of recent read and write operations.
#endif // LBA_HAL_RECORD_HISTORY
#if LBA_HAL_RECORD_COMMAND_HISTORY
    CommandHistory m_commandHistory;  //!< History of recent commands sent to this device.
#endif // LBA_HAL_RECORD_COMMAND_HISTORY

    //! \name Internal LBA commands
    //!
    //! Member functions for sending various commands used only internally by this class.
    //@{
    RtStatus_t readId2(uint8_t * data);
    
    RtStatus_t modeChangeToMdp();
    RtStatus_t modeChangeToVfp();
    RtStatus_t modeChangeToBcm();
    
    RtStatus_t verifyMode(LbaNandMode_t mode);

    RtStatus_t getMdpSize(uint32_t * sectorCount);
    RtStatus_t getVfpSize(uint32_t * sectorCount);

    RtStatus_t readMaxVfpSize(uint32_t * sectorCount);
    
    RtStatus_t setTransferProtocol1(uint8_t protocol);
    RtStatus_t getTransferProtocol1(uint8_t * protocol);
    
    RtStatus_t setTransferProtocol2(uint8_t protocol);
    RtStatus_t getTransferProtocol2(uint8_t * protocol);
    
    RtStatus_t setMinimumBusyTime(uint8_t value);
    RtStatus_t getMinimumBusyTime(uint8_t * value);
    
    RtStatus_t sendRebootCommandChange(uint8_t value);
    RtStatus_t persistentFunctionGet(uint8_t *u8BootMode, uint8_t *u8RebootCmd);
    RtStatus_t sendBootModeChange (uint8_t value);
    
    RtStatus_t flushCache();
    //@}
    
    //! \name Command helpers
    //!
    //! These member functions provide an interface for building and firing a DMA descriptor
    //! for the different classes of command structures that an LBA-NAND uses. The read and
    //! write command types are handled directly by the various read and write methods
    //! in the concrete partition classes.
    //@{
    RtStatus_t sendResetTypeCommand(uint8_t commandCode, bool waitForReady=true);
    RtStatus_t sendGeneralCommand(uint8_t commandCode, uint8_t dataBytes[4], unsigned responseLength, uint8_t * responseData, uint32_t timeout=kLbaNandTimeout_ReadPage, bool waitForReady=true);
    RtStatus_t sendGenericReadCommand(uint8_t firstCommandCode, uint8_t secondCommandCode, uint8_t addressBytes[5], unsigned responseLength, uint8_t * responseData, uint32_t timeout=kLbaNandTimeout_ReadPage, bool waitForReady=true);
    RtStatus_t sendReadStatusCommand(uint8_t statusCommand, uint8_t * responseData);
    //@}
    
    inline uint16_t getVfpPassword(void) { return kLbaNandDefaultVfpPassword; }
};

/*!
 * \brief Global context for the LBA-NAND HAL.
 *
 * All global data for the LBA-NAND HAL is stored in this structure. The \a m_dataBuffer
 * array must be the first member, so that it will be aligned properly when the context
 * global itself is aligned.
 *
 * The DMA descriptor objects that are members of this structure are reused for almost all
 * DMAs issued by the LBA-NAND HAL. The \a m_readDma and \a m_writeDma objects are prebuilt
 * at HAL init time and are only modified as necessary for each read or write operation.
 */
struct LbaNandHalContext
{
    uint8_t m_dataBuffer[32];   //!< Shared data buffer that is cache line aligned and sized.
    unsigned m_deviceCount;     //!< Number of LBA-NAND devices discovered during init.
    LbaTypeNand * m_devices[MAX_NAND_DEVICES];  //!< Array of pointers to the device objects. Only the first \a m_deviceCount entries are valid.
    TX_MUTEX m_mutex;           //!< The mutex used to protect this global context.
    
    //! \name DMA objects
    //@{
    NandDma::Reset m_resetDma;  //!< Shared DMA for reset type commands.
    NandDma::ReadRawData m_readDma; //!< Shared read DMA.
    NandDma::WriteRawData m_writeDma;   //!< Shared write DMA.
    NandDma::ReadStatus m_readStatusDma;    //!< Shared DMA for read status commands.
    NandDma::ReadRawData m_genericReadDma;  //!< Shared read DMA used for general commands.
    //@}
};

////////////////////////////////////////////////////////////////////////////////
// Externs
////////////////////////////////////////////////////////////////////////////////

#pragma alignvar(32)
extern LbaNandHalContext g_lbaNandHal;

// This class definition has to come after the g_lbaNandHal extern because it
// uses it.
/*!
 * \brief Utility class to lock and unlock the HAL's serialization mutex.
 */
class LbaNandHalLocker
{
public:
    //! \brief Constructor. Acquire the HAL serialization mutex.
    inline LbaNandHalLocker()
    {
        tx_mutex_get(&g_lbaNandHal.m_mutex, TX_WAIT_FOREVER);
    }
    
    //! \brief Destructor. Release the mutex protecting the HAL.
    inline ~LbaNandHalLocker()
    {
        tx_mutex_put(&g_lbaNandHal.m_mutex);
    }
};

#endif // __ddi_nand_hal_lba_internal_h__
// EOF
//! @}
