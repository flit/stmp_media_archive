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
//! \file
//! \brief Classes to support multisector transactions for the data drive.
///////////////////////////////////////////////////////////////////////////////
#if !defined(__multitransaction_h__)
#define __multitransaction_h__

#include "ddi_nand_ddi.h"
#include "auto_free.h"
#include "VirtualBlock.h"

///////////////////////////////////////////////////////////////////////////////
// Types
///////////////////////////////////////////////////////////////////////////////

namespace nand {

// Forward declaration.
class DataDrive;
class NonsequentialSectorsMap;

/*!
 * \name Multiplane transaction.
 */
class MultiTransaction
{
public:
    //! \brief Constructor.
    MultiTransaction(DataDrive * drive);
    
    //! \brief Destructor.
    virtual ~MultiTransaction();
    
    RtStatus_t open(uint32_t start, uint32_t count);
    RtStatus_t commit();
    
    void pushSector(uint32_t logicalSector, uint32_t logicalOffset, SECTOR_BUFFER * dataBuffer, SECTOR_BUFFER * auxBuffer);
    
    bool isLive() const { return m_isLive; }
    virtual bool isWrite() const=0;
    const BlockAddress & getVirtualBlockAddress() const { return m_virtualBlockAddress; }
    
    bool isSectorPartOfTransaction(uint32_t logicalSector);
    
protected:

    /*!
     * \brief Information for a sector of the transaction.
     */
    struct SectorInfo
    {
        uint32_t m_logicalSector;   //!< Logical sector number.
        uint32_t m_logicalOffset;   //!< Logical offset of the sector within its virtual block.
        uint32_t m_virtualOffset;   //!< Virtual offset of the sector.
        bool m_isOccupied;          //!< Whether the logical sector has been written yet.
        NandEccCorrectionInfo m_eccInfo;    //!< ECC correction results for reads.
    };
    
    DataDrive * m_drive;    //!< Our parent drive.
    bool m_isLive;  //!< Whether the current transaction is valid, or false if it should just be ignored.
    unsigned m_sectorCount;  //!< Next sector number in this transaction.
    uint32_t m_startLogicalSector;   //!< First logical sector number for this transaction.
    BlockAddress m_virtualBlockAddress;  //!< Virtual block address for this transaction.
    NonsequentialSectorsMap * m_sectorMap;  //!< NSSM instance for the virtual block.
    NandPhysicalMedia::MultiplaneParamBlock m_sectors[VirtualBlock::kMaxPlanes];  //!< Multisector transaction sector details.
    SectorInfo m_sectorInfo[VirtualBlock::kMaxPlanes];  //!< Details of sectors in the transaction.
    NandPhysicalMedia * m_nand;  //!< Nand containing the transaction's blocks.
    bool m_mustAbort;   //!< Indicates if the abort commit must be used for some reason.
    
    //! \name Operations
    //@{
    virtual RtStatus_t multiplaneCommit()=0;
    virtual RtStatus_t abortCommit()=0;
    virtual RtStatus_t computePhysicalPages()=0;
    //@}

};

/*!
 * \name Multiplane read transaction.
 */
class ReadTransaction : public MultiTransaction
{
public:
    //! \brief Constructor.
    ReadTransaction(DataDrive * drive);

    virtual bool isWrite() const { return false; }
    
protected:

    //! \name Read operations
    //@{
    virtual RtStatus_t multiplaneCommit();
    virtual RtStatus_t abortCommit();
    virtual RtStatus_t computePhysicalPages();
    //@}

};

/*!
 * \name Multiplane write transaction.
 */
class WriteTransaction : public MultiTransaction
{
public:
    //! \brief Constructor.
    WriteTransaction(DataDrive * drive);

    virtual bool isWrite() const { return true; }
    
protected:

    //! \name Write operations
    //@{
    virtual RtStatus_t multiplaneCommit();
    virtual RtStatus_t abortCommit();
    virtual RtStatus_t computePhysicalPages();
    //@}
    
    void prepareMetadata(NandPhysicalMedia::MultiplaneParamBlock & pb, SectorInfo & info);

};

} // namespace nand

#endif // __multitransaction_h__
////////////////////////////////////////////////////////////////////////////////
// End of file
////////////////////////////////////////////////////////////////////////////////
