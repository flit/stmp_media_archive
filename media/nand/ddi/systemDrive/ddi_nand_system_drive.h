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
//! \addtogroup ddi_nand_system_drive
//! @{
//! \file ddi_nand_system_drive.h
//! \brief Definitions and types for the NAND system drive.
///////////////////////////////////////////////////////////////////////////////
#ifndef _NAND_SYSTEM_DRIVE_H
#define _NAND_SYSTEM_DRIVE_H

#include "ddi_nand_ddi.h"

///////////////////////////////////////////////////////////////////////////////
// Typedefs
///////////////////////////////////////////////////////////////////////////////

namespace nand {

/*!
 * \brief NAND system drive.
 */
class SystemDrive : public LogicalDrive
{
public:
    
    //! \brief Default constructor.
    SystemDrive(Media * media, Region * region);
    
    //! \brief Destructor.
    virtual ~SystemDrive() {}
    
    //! \name Logical drive API
    //@{
    virtual RtStatus_t init();
    virtual RtStatus_t shutdown();
    virtual RtStatus_t getInfo(uint32_t infoSelector, void * value);
    virtual RtStatus_t setInfo(uint32_t infoSelector, const void * value);
    virtual RtStatus_t readSector(uint32_t sector, SECTOR_BUFFER * buffer);
    virtual RtStatus_t writeSector(uint32_t sector, const SECTOR_BUFFER * buffer);
    virtual RtStatus_t erase();
    virtual RtStatus_t flush();
    virtual RtStatus_t repair() { return ERROR_DDI_LDL_UNIMPLEMENTED; }
    //@}

    bool isRecoverable();
    bool isPrimaryFirmware();
    bool isSecondaryFirmware();
    bool isMasterFirmware();
    
    SystemDrive * getMasterDrive();
    SystemDrive * getBackupDrive();
    
    bool isBeingRewritten() const { return m_isBeingRewritten; }
    void setIsBeingRewritten(bool isIt) { m_isBeingRewritten = isIt; }
    
    RtStatus_t readSectorWithRecovery(uint32_t wSectorNumber, SECTOR_BUFFER * pSectorData);
    
    //! \brief Erases and rewrites a logical block by copying from another system drive.
    void refreshLogicalBlock(uint32_t logicalBlock, SystemDrive * sourceDrive);
    
protected:

    #pragma alignvar(32)
    //! \brief Static auxiliary buffer used for system drive reads and writes.
    static SECTOR_BUFFER s_auxBuffer[];

    Media * m_media;        //!< Our parent logical media object.
    uint32_t m_wStartSector;       //!< Drive 1st sector related to the chip.
    SystemRegion * m_pRegion;  //!< System drive has only one region.
    bool m_isBeingRewritten;    //!< True if the entire drive is being rewritten and should not be read from. Read from the backup instead.
    int m_logicalBlockBeingRefreshed;   //!< If not -1, the this is the logical block number for a block that is being refreshed. If set to -1, then no block is being refreshed.

    uint32_t skipBadBlocks(uint32_t wLogicalBlockNumber);

    RtStatus_t recoverFromFailedRead(uint32_t wSectorNumber, SECTOR_BUFFER * pSectorData);

};

} // namespace nand

#endif // #ifndef NANDSystemDrive
////////////////////////////////////////////////////////////////////////////////
// End of file
////////////////////////////////////////////////////////////////////////////////
//! @}

