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
//! \addtogroup ddi_nand_data_drive
//! @{
//! \file ddi_nand_data_drive.h
//! \brief Definitions of the NAND data drive class.
///////////////////////////////////////////////////////////////////////////////
#ifndef _NANDDATADRIVE_H
#define _NANDDATADRIVE_H

#include "ddi_nand_ddi.h"
#include "auto_free.h"

///////////////////////////////////////////////////////////////////////////////
// Types
///////////////////////////////////////////////////////////////////////////////

namespace nand {

// Forward declaration.
class NonsequentialSectorsMap;
class MultiTransaction;

/*!
 * \brief NAND data drive.
 *
 * This data drive class is used for both the primary data drive and all hidden data drives. It
 * provides full dynamic wear leveling.
 *
 * \sa nand::Mapper
 * \sa nand::NonsequentialSectorsMap
 */
class DataDrive : public LogicalDrive
{
public:
    
    //! \brief Default constructor.
    DataDrive(Media * media, Region * region);
    
    //! \brief Destructor.
    virtual ~DataDrive();

    void addRegion(Region * region);
    
    //! \name Logical drive API
    //@{
    virtual RtStatus_t init();
    virtual RtStatus_t shutdown();
    virtual RtStatus_t getInfo(uint32_t infoSelector, void * value);
    virtual RtStatus_t setInfo(uint32_t infoSelector, const void * value);
    virtual RtStatus_t readSector(uint32_t sector, SECTOR_BUFFER * buffer);
    virtual RtStatus_t writeSector(uint32_t sector, const SECTOR_BUFFER * buffer);
    virtual RtStatus_t openMultisectorTransaction(uint32_t start, uint32_t count, bool isRead);
    virtual RtStatus_t commitMultisectorTransaction();
    virtual RtStatus_t erase();
    virtual RtStatus_t flush();
    virtual RtStatus_t repair();
    //@}

protected:

    Media * m_media;            //!< The NAND media object that we belong to.
    uint32_t m_u32NumRegions;   //!< Total number of data drive regions belonging to this drive.
    Region ** m_ppRegion;       //!< Array of pointers to the regions belonging to this drive. Allocated with new[].
    
    //! \name Transaction members
    //@{
    auto_free<char> m_transactionStorage;        //!< Buffer that holds the current transaction object. This memory is reused for all transaction object instantiations to be more efficient.
    MultiTransaction * m_transaction; //!< The object that manages multisector transactions. When there is not an open transaction, the value is NULL. The transaction object is placed in the memory pointed to by m_transactionStorage.
    TX_SEMAPHORE m_transactionSem;     //!< Transaction ownership semaphore.
    //@}

    //! \name Init helpers
    //@{
    void processRegions(Region ** regionArray, unsigned * regionCount);
    void buildRegionsList();
    //@}

    //! \name Repair
    //@{
    bool shouldRepairEraseBlock(uint32_t u32BlockFirstPage, NandPhysicalMedia *pNandDesc);
    bool isBlockHidden(uint32_t u32BlockPhysAddr);
    //@}
    
    //! \name Address conversion
    //@{
    DataRegion * getRegionForLogicalSector(uint32_t logicalSector, uint32_t & logicalSectorInRegion);
    RtStatus_t getSectorMapForLogicalSector(uint32_t logicalSector, uint32_t * logicalSectorInRegion, uint32_t * logicalOffset, NonsequentialSectorsMap ** map, DataRegion ** region);
    //@}
    
    //! \name Internal implementations
    //@{
    RtStatus_t readSectorInternal(uint32_t u32LogicalSectorNumber, SECTOR_BUFFER * pSectorData);
    RtStatus_t writeSectorInternal(uint32_t u32LogicalSectorNumber, const SECTOR_BUFFER * pSectorData);
    //@}
    
    friend class MultiTransaction;
    friend class ReadTransaction;
    friend class WriteTransaction;
};

///////////////////////////////////////////////////////////////////////////////
// Prototypes
///////////////////////////////////////////////////////////////////////////////

void log_ecc_failures(uint32_t wPhysicalBlockNumber, uint32_t wSectorOffset, NandEccCorrectionInfo_t * correctionInfo);

} // namespace nand

#endif // #ifndef _NANDDATADRIVE_H
////////////////////////////////////////////////////////////////////////////////
// End of file
////////////////////////////////////////////////////////////////////////////////
//! @}
