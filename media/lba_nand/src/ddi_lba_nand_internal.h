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
//! \addtogroup ddi_lba_nand_media
//! @{
//! \file ddi_lba_nand_ddi.h
//! \brief Internal declarations for the LBA NAND media layer.
///////////////////////////////////////////////////////////////////////////////
#if !defined(_ddi_lba_nand_ddi_h_)
#define _ddi_lba_nand_ddi_h_

///////////////////////////////////////////////////////////////////////////////
// Includes
///////////////////////////////////////////////////////////////////////////////

#include "drivers/media/include/ddi_media_internal.h"
#include "ddi_lba_nand_hal.h"
#include "drivers/media/buffer_manager/media_buffer.h"
#include "os/threadx/tx_api.h"

///////////////////////////////////////////////////////////////////////////////
// Definitions
///////////////////////////////////////////////////////////////////////////////
#define INTERNAL_MANAGED_BLOCK_LENGTH    1

//! \name Drive Limits
//! @{

//! Maximum number of physical devices.
const unsigned k_uMaxPhysicalMedia = 4;

//! \brief Maximum number of regions per drive.
//!
//! A drive can have no more than one region per physical device.
const unsigned k_uMaxRegions = k_uMaxPhysicalMedia;

//! \brief Maximum number of bootlet drives.
//!
//! The bootlet goes in the PNP
//! of the first device. It is a fixed size.
const unsigned k_uMaxBootletDrives = 1;

//! \brief Maximum number of system drives.
//!
//! The system drives include the bootable firmware
//! and the backup copies.
const unsigned k_uMaxSystemDrives = 3;

//! \brief Maximum number of hidden data drives.
//!
//! The hidden drives plus the data drive must fit in the MBR
//! partition table (4 entries). The system drives do not appear
//! in the MBR since they are in the VFP.
const unsigned k_uMaxHiddenDrives = 2;

//! Maximum number of data drives.
const unsigned k_uMaxDataDrives = 1;

//! Maximum total number of drives.
const unsigned k_uMaxDrives = k_uMaxBootletDrives +
                              k_uMaxSystemDrives +
                              k_uMaxHiddenDrives +
                              k_uMaxDataDrives;

//! @}

/*!
 * \brief Interface for an LBA-NAND Media.
 *
 * This class abstracts the media as a collection of drives.
 * A data drive may span multiple physical devices (chip selects).
 * The sector locations and sector count of each drive are held in
 * internal Region objects.
 *
 * The following types of drives are supported:
 *
 * Bootlet Drive - Contains Bootlet firmware used by the ROM.
 *      Stored on the Pure Nand Partition (PNP) of the first
 *      physical device.
 *
 * Main Firmware System Drive - Contains the bootable firmware
 *      image. Stored on the Vendor Firmware Partition (VFP)
 *      of the first physical device.
 *
 * Secondary Firmware System Drive - Contains the backup
 *      firmware image. Stored on the VFP of the first physical
 *      device.
 *
 * Hidden Drive One - Stored on the Multimedia Data Partition
 *      (MDP) of the first physical device. Pointed to by the
 *      first partition entry in the MBR.
 *
 * Hidden Drive Two - Stored on the MDP of the first physical
 *      device. Pointed to by the second partitino entry in the MBR.
 *
 * Data Drive - Stored on the MDP. Pointed to by the third
 *      partition entry in the MBR. Starts on the MDP of the first
 *      physical device but automatically spans the MDP of
 *      all remaining devices.
 *
 * The VFP of the first device also contains a Config Block used
 * by the ROM to find the firmware drive sizes and locations. The MDP
 * of the first device contains a standard MBR that describes the
 * drive partitions.
 *
 * \todo Get away from using constructors and destructors and use init
 *      and cleanup methods instead, since constructors cannot return
 *      errors without exceptions, which we do not have enabled.
 */
class LbaNandMedia
{
public:
    /*!
     * \brief Interface for an LBA-NAND Drive.
     *
     * This class describes a drive as a collection of Regions.
     */
    class Drive
    {
    public:
        //! \name Initialization and deletion methods.
        //! @{

        //! \brief Constructor that sets the drive type and tag.
        Drive(LbaNandMedia * media, LogicalDriveType_t eType, DriveTag_t Tag);

        //! \brief Destructor that deletes the regions.
        virtual ~Drive();

        //! \brief Create and add a new Region.
        void addRegion(LbaNandPhysicalMedia *pPhysicalMedia,
                       LbaNandPhysicalMedia::LbaPartition *pPartition,
                       uint32_t u32FirstSectorNumber,
                       uint32_t u32SectorCount);

        //! @}

        //! \name Accessors.
        //! @{
        
        //! \brief Returns the parent media object of this drive.
        inline LbaNandMedia * getMedia() const { return m_media; }

        //! \brief Returns the total number of sectors in all regions.
        inline uint32_t getSectorCount() const { return m_u32SectorCount; }

        //! \brief Returns the sector size of the first region.
        uint32_t getSectorSize() const;

        //! \brief Returns the first sector number of the first region.
        uint32_t getFirstSectorNumber() const;

        //! \brief Accessor for the drive type.
        inline LogicalDriveType_t getType() const { return m_eType; }

        //! \brief Accessor for the drive tag.
        inline DriveTag_t getTag() const { return m_Tag; }

        //! @}

        //! \name Drive data access methods.
        //! @{

        //! \brief Writes a sector to the drive.
        RtStatus_t writeSector(uint32_t u32SectorNumber, const SECTOR_BUFFER *pBuffer);

        //! \brief Reads a sector from the drive.
        RtStatus_t readSector(uint32_t u32SectorNumber, SECTOR_BUFFER *pBuffer);

        //! \brief Flushes cached data to the drive.
        RtStatus_t flush();

        //! \brief Erases the entire drive.
        RtStatus_t erase();
        //! @}

    private:
        //! \brief Private copy constructor, so copies are not allowed.
        Drive(const Drive & other) {}

    protected:
        /*!
         * \brief Interface for an LBA-NAND Region.
         *
         * This class holds region information which includes which physical media and
         * partition a group of sectors is on.
         */
        class Region
        {
        public:
            //! \name Initialization and deletion methods.
            //! @{

            //! \brief Constructor that sets all attributes.
            Region(LbaNandPhysicalMedia *pPhysicalMedia,
                   LbaNandPhysicalMedia::LbaPartition *pPartition,
                   uint32_t u32FirstSectorNumber,
                   uint32_t u32SectorCount);

            //! @}

            //! \name Accessors.
            //! @{

            //! \brief Accessor for the physical media pointer.
            inline LbaNandPhysicalMedia * getPhysicalMedia() { return m_pPhysicalMedia; }

            //! \brief Accessor for the partition pointer.
            inline LbaNandPhysicalMedia::LbaPartition * getPartition() { return m_pPartition; }

            //! \brief Returns the partition sector size.
            inline uint32_t getSectorSize() const { return m_pPartition->getSectorSize(); }

            //! \brief Accessor for the sector count.
            inline uint32_t getSectorCount() const { return m_u32SectorCount; }

            //! \brief Accessor for the first sector number.
            inline uint32_t getFirstSectorNumber() const { return m_u32FirstSectorNumber; }

            //! @}

            //! \name Region data access methods.
            //! @{

            //! \brief Writes a sector to the region.
            RtStatus_t writeSector(uint32_t u32SectorNumber, const SECTOR_BUFFER *pBuffer);

            //! \brief Reads a sector from the region.
            RtStatus_t readSector(uint32_t u32SectorNumber, SECTOR_BUFFER *pBuffer);

            //! \brief Flushes cached data to the region.
            RtStatus_t flush();

            //! \brief Erases the entire region.
            RtStatus_t erase();
            
            //! \brief Notify the region for an expected transfer sequence
            RtStatus_t startTransferSequence(uint32_t u32SectorCount);
            //! @}

#if INTERNAL_MANAGED_BLOCK_LENGTH
        enum
        {
            // enum for the last operation of this region
            kActivityRead = 0,
            kActivityWrite = 1,
            
            // in sequence count threshold to start a block sequence
            kInSequenceThreshold = 2,

            kRegionInvalidSector = 0xFFFFFFFFL
        };
#endif

        private:
            LbaNandPhysicalMedia *m_pPhysicalMedia;             //!< Used to flush data.
            LbaNandPhysicalMedia::LbaPartition *m_pPartition;   //!< Access to data on physical media.
            uint32_t m_u32FirstSectorNumber;                    //!< Starting sector number on partition.
            uint32_t m_u32SectorCount;                          //!< Number of sectors used on partition.
#if INTERNAL_MANAGED_BLOCK_LENGTH
            uint32_t m_lastAccessSector;
            unsigned m_lastOperation;
            unsigned m_inSequenceCounter;
#endif
        };

    protected:
        Region *regionForSector(uint32_t *pu32SectorNumber) const;

    protected:
        LbaNandMedia * m_media;     //!< Pointer to parent media object.
        unsigned m_uNumRegions;             //!< Number of regions.
        Region *m_pRegion[k_uMaxRegions];   //!< Only the data drive uses more than one region.
        LogicalDriveType_t m_eType;         //!< Drive type.
        DriveTag_t m_Tag;                   //!< Drive tag.
        uint32_t m_u32SectorCount;          //!< Total number of sectors in all regions.
    };

    /*!
     * \brief Represents a bootlet drive on the PNP partition.
     */
    class BootletDrive : public Drive
    {
    public:
        
        enum
        {
            //! NCB1, LDLB1, DBBT1
            //!
            //! The boot ROM never sees the secondary boot blocks because pages are read
            //! sequentially and supposedly there will never be corruption.
            kBootBlockCount = 3,
            
            kNcbSectorNumber = 0,
            kLdlbSectorNumber = 1,
            kDbbtSectorNumber = 2,
            kFirmwareSectorNumber = 3   //!< Starting sector number for the firmare in the PNP.
        };
        
        //! \brief Default constructor.
        BootletDrive(LbaNandMedia * media) : Drive(media, kDriveTypeSystem, DRIVE_TAG_BOOTLET_S) {}
        
        //! \brief Initialze the drive and create the sole region.
        virtual RtStatus_t init(LbaNandPhysicalMedia * nand);
        
        //! \brief Write the blocks that the boot ROM needs to boot.
        RtStatus_t writeBootBlocks(SectorBuffer & buffer);
        
    protected:
        RtStatus_t writeNCB(LbaNandPhysicalMedia::LbaPartition * partition, SectorBuffer & buffer);
        RtStatus_t writeLDLB(LbaNandPhysicalMedia::LbaPartition * partition, SectorBuffer & buffer);
        RtStatus_t writeDBBT(LbaNandPhysicalMedia::LbaPartition * partition, SectorBuffer & buffer);
    };

    /*!
     * \brief Interface for an LBA-NAND Drive Iterator.
     *
     * Use this class to interate over the drives objects contained on the
     * logical media. This is meant to be a lightweight object, not to be
     * held beyond a single function.
     */
    class DriveIterator
    {
    public:
        //! \brief Constructor.
        //! \note Do not delete the pMedia object while using this drive iterator.
        DriveIterator(const LbaNandMedia *pMedia);

        //! \brief Returns the next drive.
        //! \note Do not call resetDrives on the pMedia object while referencing a
        //!    drive returned by this method.
        Drive *next();

    private:
        const LbaNandMedia *m_pMedia;   //!< Media object referenced every time next is called
        unsigned m_uCurrentIndex;       //!< Current drive index.
    };

public:
    //! \name Initialization and deletion methods.
    //! @{

    //! \brief Default constructor.
    LbaNandMedia();

    //! \brief Destructor that deletes drive objects.
    virtual ~LbaNandMedia();

    //! \brief Associates a physical media object with this logical media.
    RtStatus_t addPhysicalMedia(LbaNandPhysicalMedia *pPhysicalMedia);

    //! \brief Deletes current drive objects an prepares the object for new drives.
    //!
    //! Use this method to make sure the object is ready for drive allocation or
    //! drive discovery.
    void resetDrives();

    //! \brief Turns on automatic management of power save mode.
    void enablePowerSaveManagement(bool isEnabled);

    //! @}

    //! \name Accessors.
    //! @{

    //! \brief Returns the total size in bytes of all partitions on all devices.
    inline uint64_t getSizeInBytes() const { return m_u64SizeInBytes; }

    //! \brief Accessor for number of physical media devices.
    inline unsigned getPhysicalMediaCount() const { return m_uNumPhysicalMedia; }

    //! \brief Accessor for the expected transfer activity type
    inline int getTransferActivityType() const { return (int)m_TransferActivityType; }

    //! \brief Assign the expected transfer activity type
    inline RtStatus_t setTransferActivityType(TransferActivityType_t eTransferActivityType){
        m_TransferActivityType = eTransferActivityType;
        return SUCCESS;
    }
    //! @}

    //! \brief Flushes cached data to all devices.
    RtStatus_t flush();

    //! \brief Erases all devices.
    //!
    //! Tries to preserve hidden drives if \a u8DoNotEraseHidden is true.
    RtStatus_t erase(uint8_t u8DoNotEraseHidden);

    //! \name Drive allocation methods.
    //! @{

    //! \brief Adds a Bootlet drive.
    RtStatus_t addBootletDrive();

    //! \brief Adds a System drive.
    RtStatus_t addSystemDrive(uint64_t u64SizeInBytes, DriveTag_t tag);

    //! \brief Adds a Hidden drive.
    RtStatus_t addHiddenDrive(uint64_t u64SizeInBytes, uint64_t *pu64AllocatedSize, DriveTag_t tag);

    //! \brief Adds a Data drive.
    RtStatus_t addDataDrive(uint64_t *pu64AllocatedSize);

    //! \brief Commits System drives to the media.
    //!
    //! Writes the configuration block header to the VFP.
    RtStatus_t commitSystemDrives();

    //! \brief Commits Data and Hidden drives to the media.
    //!
    //! Writes the MBR to the MDP.
    RtStatus_t commitDataDrives();

    //! @}

    //! \name Drive discovery methods.
    //! @{

    //! \brief Reads drive information from the media and creates discovered drive objects.
    RtStatus_t loadDrives();

    //! @}

    //! \brief Returns the drive at the given index.
    //! \note This method should only be used by the DriveIterator class.
    Drive *getDriveAtIndex(unsigned uIndex) const;

private:
    static inline uint32_t roundBytesToSectors(uint64_t u64NumBytes, unsigned uBytesPerSector);
    static inline uint8_t sysIdForSize(uint64_t u64ByteCount);

    //! \name Drive configuration methods.
    //! @{
    RtStatus_t writeConfigBlock(SectorBuffer & buffer);
    RtStatus_t readConfigBlock(SectorBuffer & buffer);
    
    RtStatus_t writeMbr(SectorBuffer & buffer);
    RtStatus_t readMbr(SectorBuffer & buffer);
    
    //! @}

    Drive *getDriveForTag(DriveTag_t Tag) const;
    RtStatus_t readDataDriveInfo(uint32_t *pu32StartSector) const;
    
    //! \name Power save
    //@{
    //! \brief DPC callback to enable power save mode for all devices.
    static void enterPowerSaveModeDpc(uint32_t param);
    
    //! \brief Callback for the power save timer.
    static void enterPowerSaveModeTimer(uint32_t param);
    
    //! \brief Enable or disable power save mode for all devices.
    void enableAllPowerSaveMode(bool isEnabled);

    //! \brief Disables power save mode and manages the power save timer.
    void exitPowerSaveMode();
    //@}

    //! \brief Private copy constructor, so copies are not allowed.
    LbaNandMedia(const LbaNandMedia & other) {}

private:
    unsigned m_uNumPhysicalMedia;       //!< Number of physical media (devices).
    LbaNandPhysicalMedia *m_pPhysicalMedia[k_uMaxPhysicalMedia];    //!< Devices added by addPhysicalMedia.
    unsigned m_uNumDrives;              //!< Number of drives.
    Drive *m_pDrive[k_uMaxDrives];      //!< Drive objects added by addDrive methods.
    BootletDrive * m_bootletDrive;      //!< The single bootlet drive. Also present in the m_pDrive array.
    uint64_t m_u64SizeInBytes;          //!< Size calclulated by addPhysicalMedia.
    unsigned m_uNumSystemDrives;        //!< Number of System drives.
    unsigned m_uNumHiddenDrives;        //!< Number of Hidden drives.
    unsigned m_uNumDataDrives;          //!< Number of Data drives.
    unsigned m_uVfpSectorsAllocated;    //!< Next available sector on VFP.
    unsigned m_uMdpSectorsAllocated;    //!< Next available sector on MDP.
    TX_TIMER m_powerSaveTimer;  //!< ThreadX timer used for managing power modes.
    bool m_powerSaveEnabled;    //!< True if power save mode is currently enabled.
    bool m_managePowerSave;     //!< Whether to manage power save mode or leave it fixed.
    TransferActivityType_t m_TransferActivityType; //!< Expected transfer activity type.
};

class LbaNandMediaInfo
{
public:
    
    //! \brief Constructor. Assign the LBA Nand media configurable paramater to default value
    inline LbaNandMediaInfo()
    {
        m_shouldExitPowerSaveOnTransfer = true;
    }

    //! \brief Accessor for m_shouldExitPowerSaveOnTransfer
    inline  void setExitPowerSaveOnTransfer (bool bExitPowerSaveOnTransfer){
        m_shouldExitPowerSaveOnTransfer = bExitPowerSaveOnTransfer;
    }

    //! \brief Accessor for m_shouldExitPowerSaveOnTransfer
    inline  bool shouldExitPowerSaveOnTransfer (){
        return m_shouldExitPowerSaveOnTransfer;
    }

private:
    bool    m_shouldExitPowerSaveOnTransfer;    //! \brief Whether to exit power saving mode when read
};

///////////////////////////////////////////////////////////////////////////////
// Global variables
///////////////////////////////////////////////////////////////////////////////
         
extern const LogicalMediaApi_t g_LbaNandMediaApi;
extern const LogicalDriveApi_t g_LbaNandDriveApi;

extern TX_MUTEX g_LbaNandMediaMutex;

// This class definition has to come after the g_LbaNandMediaMutex extern because it
// uses it.
/*!
 * \brief Utility class to lock and unlock the LBA-NAND media mutex.
 */
class LbaNandMediaLocker
{
public:
    //! \brief Constructor. Acquire the LBA-NAND media mutex.
    inline LbaNandMediaLocker()
    {
        tx_mutex_get(&g_LbaNandMediaMutex, TX_WAIT_FOREVER);
    }
    
    //! \brief Destructor. Release the mutex.
    inline ~LbaNandMediaLocker()
    {
        tx_mutex_put(&g_LbaNandMediaMutex);
    }
};

extern TX_SEMAPHORE g_LbaNandMediaSemaphore;

extern LbaNandMediaInfo g_LbaNandMediaInfo;

#endif // _ddi_lba_nand_ddi_h_

///////////////////////////////////////////////////////////////////////////////
// End of file
///////////////////////////////////////////////////////////////////////////////
//! @}
