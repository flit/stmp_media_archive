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
////////////////////////////////////////////////////////////////////////////////
//! \file
//! \brief Implementation of multisector transactions for the data drive.
////////////////////////////////////////////////////////////////////////////////

#include <string.h>
#include <stdio.h>
#include <memory>
#include "types.h"
#include "MultiTransaction.h"
#include "components/telemetry/tss_logtext.h"
#include "Mapper.h"
#include "ddi_nand_data_drive.h"
#include "drivers/media/nand/hal/ddi_nand_hal.h"
#include "NonsequentialSectorsMap.h"
#include "VirtualBlock.h"

using namespace nand;

//! Set to 1 to disable use of multipage read/write calls to the HAL.
#define USE_SINGLE_PLANE_R_OPS 0

//! When set to 1, transactions will call back into the data drive instead of directly using
//! the HAL.
#define USE_DATA_DRIVE_R_OPS 0

#define USE_SINGLE_PLANE_W_OPS 0

#define USE_DATA_DRIVE_W_OPS 0

////////////////////////////////////////////////////////////////////////////////
// Code
////////////////////////////////////////////////////////////////////////////////

#if !defined(__ghs__)
#pragma mark --DataDrive--
#endif

//! This method cannot acquire the NAND mutex at least until after the transaction is opened,
//! because the calling thread may block on the transaction semaphore.
//! 
//! \todo It might be nice to track the owning thread of the transaction semaphore and return an
//!     error instead of causing a deadlock by trying to get the semaphore again.
RtStatus_t DataDrive::openMultisectorTransaction(uint32_t start, uint32_t count, bool isRead)
{
    // Make sure we're initialized
    if (!m_bInitialized)
    {
        return ERROR_DDI_LDL_LDRIVE_NOT_INITIALIZED;
    }
    
    // Get the semaphore so we block if another transaction is already open.
    tx_semaphore_get(&m_transactionSem, TX_WAIT_FOREVER);

    // Cannot open a transaction if another one is still outstanding. This should never happen,
    // because we are protected by the semaphore.
    if (m_transaction)
    {
        tx_semaphore_put(&m_transactionSem);
        return ERROR_GENERIC;
    }
    
    // Lock the driver for the rest of the open.
    DdiNandLocker locker;
    
    // Create the appropriate transaction instance, placing it in the buffer we have already
    // allocated for this purpose. This lets us use subclasses without the overhead of dynamically
    // allocated memory.
    assert(m_transactionStorage);
    if (isRead)
    {
        m_transaction = new(m_transactionStorage) ReadTransaction(this);
    }
    else
    {
        m_transaction = new(m_transactionStorage) WriteTransaction(this);
    }
    assert(m_transaction);
    
    // Start the new transaction.
    RtStatus_t status = m_transaction->open(start, count);
    if (status != SUCCESS)
    {
        // Opening the transaction failed, so clean up so we don't leave a zombie transaction
        // hanging around.
        m_transaction->~MultiTransaction();
        m_transaction = NULL;
        tx_semaphore_put(&m_transactionSem);
    }
    
    return status;
}

RtStatus_t DataDrive::commitMultisectorTransaction()
{
    // Lock the NAND driver during the commit so no other threads can interfere.
    DdiNandLocker lock;
    
    // Make sure we have an active transaction.
    if (!m_transaction)
    {
        return ERROR_GENERIC;
    }
    
    // Complete the transaction.
    RtStatus_t status = m_transaction->commit();
    
    // Delete the transaction object by explicitly calling its destructor.
    m_transaction->~MultiTransaction();
    m_transaction = NULL;
    
    // Release the transaction semaphore so another thread can open a transaction.
    tx_semaphore_put(&m_transactionSem);
    
    return status;
}

#if !defined(__ghs__)
#pragma mark --MultiTransaction--
#endif

MultiTransaction::MultiTransaction(DataDrive * drive)
:   m_drive(drive),
    m_isLive(false),
    m_sectorCount(0),
    m_startLogicalSector(0),
    m_sectorMap(NULL),
    m_nand(NULL),
    m_mustAbort(false)
{
    memset(&m_sectors, 0, sizeof(m_sectors));
    memset(&m_sectorInfo, 0, sizeof(m_sectorInfo));
}

MultiTransaction::~MultiTransaction()
{
}

RtStatus_t MultiTransaction::open(uint32_t start, uint32_t count)
{
    // Make sure we won't go out of bounds.
    if (start + count >= m_drive->m_u32NumberOfSectors)
    {
        return ERROR_DDI_LDL_LDRIVE_SECTOR_OUT_OF_BOUNDS;
    }

    // Init transaction.
    m_startLogicalSector = start;
    
    uint32_t logicalSectorInRegion;
    uint32_t logicalSectorOffset;
    DataRegion * sectorRegion;
    RtStatus_t status;
    
    // Convert logical sector to be region relative. Then find the NSSM for this virtual block.
    // If it isn't already in memory, the physical block(s) will be scanned in order to build it.
    status = m_drive->getSectorMapForLogicalSector(
        start,
        &logicalSectorInRegion,
        &logicalSectorOffset,
        &m_sectorMap,
        &sectorRegion);
    if (status != SUCCESS)
    {
        return status;
    }
    assert(m_sectorMap);
    
//    m_sectorMap->retain();
    
    // The transaction must be for exactly the number of planes in a virtual block to be worth
    // handling. If it's not, then we won't make the transaction live, and the read/write calls
    // will just operate as normal. But we still have to act like there is a transaction in
    // progress.
    VirtualBlock & vblock = m_sectorMap->getVirtualBlock();
    unsigned planeCount = VirtualBlock::getPlaneCount();
    unsigned freePages = m_sectorMap->getFreePagesInBlock();
    m_isLive = (planeCount > 1
        && count == planeCount
        && (!isWrite()          // Not writing, so no worries about splitting/merging.
            || freePages == 0   // Will split/merge on first page.
            || freePages >= count));    // Room to write without needing to split/merge.
    if (!m_isLive)
    {
//        m_sectorMap->release();
        return SUCCESS;
    }
    
    // Make sure the range doesn't extend beyond this one region. This check may not be
    // actually necessary, due to the check below to ensure that the range doesn't cross a
    // virtual block boundary.
    uint32_t totalSectorsInRegion = sectorRegion->getNand()->blockToPage(sectorRegion->getLogicalBlockCount());
    if (logicalSectorInRegion + count >= totalSectorsInRegion)
    {
        m_isLive = false;
//        m_sectorMap->release();
        return SUCCESS;
    }
    
    // Make sure the transaction sector range does not cross a virtual block boundary.
    if (logicalSectorOffset + count >= VirtualBlock::getVirtualPagesPerBlock())
    {
        m_isLive = false;
//        m_sectorMap->release();
        return SUCCESS;
    }
    
    // Save this virtual block.
    m_virtualBlockAddress = vblock;
    
    return SUCCESS;
}

RtStatus_t MultiTransaction::commit()
{
    // If this wasn't a live transaction then just exit; we don't have anything else to do.
    if (!m_isLive)
    {
        return SUCCESS;
    }
    
    // Verify that all the required sectors have been provided.
    if (m_sectorCount != VirtualBlock::getPlaneCount())
    {
        return ERROR_GENERIC;
    }

    RtStatus_t status;
    
    // First compute the physical pages from the logical sectors.
    status = computePhysicalPages();
    
    // If that succeeded, then do the actual read or write operation.
    if (status == SUCCESS)
    {
        if (m_mustAbort)
        {
            status = abortCommit();
        }
        else
        {
            status = multiplaneCommit();
        }
    }
    
    // Release the NSSM instance so it can be reused.
//    m_sectorMap->release();
    
    // Release the auxiliary buffers.
    for (int i = 0; i < m_sectorCount; ++i)
    {
        if (m_sectors[i].m_auxiliaryBuffer)
        {
            media_buffer_release(m_sectors[i].m_auxiliaryBuffer);
        }
    }
    
    // Transaction is completed.
    m_isLive = false;
    
    return status;
}

void MultiTransaction::pushSector(uint32_t logicalSector, uint32_t logicalOffset, SECTOR_BUFFER * dataBuffer, SECTOR_BUFFER * auxBuffer)
{
    assert(m_sectorCount < VirtualBlock::kMaxPlanes);
    
    // Save the logical sector number in case we have to recover.
    SectorInfo & info = m_sectorInfo[m_sectorCount];
    info.m_logicalSector = logicalSector;
    info.m_logicalOffset = logicalOffset;
    info.m_virtualOffset = 0;
    
    // Record the buffer and address information.
    NandPhysicalMedia::MultiplaneParamBlock & tpb = m_sectors[m_sectorCount];
    tpb.m_address = 0;
    tpb.m_buffer = dataBuffer;
    tpb.m_auxiliaryBuffer = auxBuffer;
    tpb.m_eccInfo = &info.m_eccInfo;
    tpb.m_resultStatus = 0;
    
    // Update number of sectors we've recorded.
    ++m_sectorCount;
}

bool MultiTransaction::isSectorPartOfTransaction(uint32_t logicalSector)
{
    return true;
}

#if !defined(__ghs__)
#pragma mark --ReadTransaction--
#endif

ReadTransaction::ReadTransaction(DataDrive * drive)
:   MultiTransaction(drive)
{
}

RtStatus_t ReadTransaction::computePhysicalPages()
{
    assert(m_sectorMap);
    
    RtStatus_t status;

    // Look up the physical page for each sector and save the information in the structures
    // passed into the NAND HAL.
    for (int i = 0; i < m_sectorCount; ++i)
    {
        NandPhysicalMedia::MultiplaneParamBlock & pb = m_sectors[i];
        SectorInfo & info = m_sectorInfo[i];

        PageAddress physicalPageAddress;
        status = m_sectorMap->getPhysicalPageForLogicalOffset(
            info.m_logicalOffset,
            physicalPageAddress,
            &info.m_isOccupied,
            &info.m_virtualOffset);
            
        if (status == SUCCESS)
        {
            // Save the NAND relative physical page address.
            pb.m_address = physicalPageAddress.getRelativePage();
            
            // Save the NAND object from the first page we look at.
            if (!m_nand)
            {
                m_nand = physicalPageAddress.getNand();
            }
            // And check that other pages belong to the same NAND.
            else if (m_nand != physicalPageAddress.getNand())
            {
                // This page is on a different NAND, so we must use the abort commit.
                m_mustAbort = true;
            }
        }
        else if (status == ERROR_DDI_NAND_MAPPER_INVALID_PHYADDR)
        {
            // This sector has never been written, so we cannot use the standard commit.
            m_mustAbort = true;
        }
        else
        {
            // Some unexpected error occurred, so just exit immediately.
            return status;
        }
    }
    
    return SUCCESS;
}

RtStatus_t ReadTransaction::multiplaneCommit()
{
#if USE_DATA_DRIVE_R_OPS
    return abortCommit();
#else
    RtStatus_t status = SUCCESS;
    
    // Handle unoccupied case.
    if (m_mustAbort)
    {
        return abortCommit();
    }
    
    // Perform the multiplane read.
    assert(m_nand);
#if !USE_SINGLE_PLANE_R_OPS
    status = m_nand->readMultiplePages(m_sectors, m_sectorCount);
    if (status != SUCCESS)
    {
        return status;
    }
#endif // !USE_SINGLE_PLANE_R_OPS
    
    // Review results. The result status starts off SUCCESS (because we can only get here if the
    // read call above succeeded), and will be set to an error if any of the page reads failed.
    bool needsRewrite = false;
    for (int i = 0; i < m_sectorCount; ++i)
    {
        NandPhysicalMedia::MultiplaneParamBlock & pb = m_sectors[i];
        
#if USE_SINGLE_PLANE_R_OPS
        pb.m_resultStatus = m_nand->readPage(pb.m_address, pb.m_buffer, pb.m_auxiliaryBuffer, pb.m_eccInfo);
#endif // USE_SINGLE_PLANE_R_OPS
        
        if (is_read_status_error_excluding_ecc(pb.m_resultStatus))
        {
            // Set the return value for this method.
            status = pb.m_resultStatus;
        }
        else if (pb.m_resultStatus == ERROR_DDI_NAND_HAL_ECC_FIXED)
        {
            // This error simply indicates that there were correctable bit errors.
        }
        else if (pb.m_resultStatus == ERROR_DDI_NAND_HAL_ECC_FIX_FAILED)
        {
            // There were uncorrectable bit errors in the data, so there's nothing we can do
            // except return an error.
            status = pb.m_resultStatus;
        }
        else if (pb.m_resultStatus == ERROR_DDI_NAND_HAL_ECC_FIXED_REWRITE_SECTOR)
        {
            // Rewrite this virtual block to another location.
            needsRewrite = true;
        }
    }

    if (needsRewrite)
    {
        // The ECC hit the threshold, so we must rewrite the block contents to a different
        // physical block, thus refreshing the data. Create a task to do it in the background.
        RelocateVirtualBlockTask * task = new RelocateVirtualBlockTask(m_drive->m_media->getNssmManager(), m_virtualBlockAddress);
        assert(task);
        if (task)
        {
            m_drive->m_media->getDeferredQueue()->post(task);
        }
    }
    
    return status;
#endif // USE_DATA_DRIVE_R_OPS
}

RtStatus_t ReadTransaction::abortCommit()
{
    assert(m_isLive);
    
    RtStatus_t returnStatus = SUCCESS;
    
    // Disable this transaction temporarily, so the read sector call will work normally.
    m_isLive = false;

    for (int i = 0; i < m_sectorCount; ++i)
    {
        NandPhysicalMedia::MultiplaneParamBlock & pb = m_sectors[i];
        SectorInfo & info = m_sectorInfo[i];
        
        RtStatus_t thisStatus = m_drive->readSector(info.m_logicalSector, pb.m_buffer);
        
        if (thisStatus != SUCCESS)
        {
            returnStatus = thisStatus;
        }
    }
    
    // Turn this transaction back on.
    m_isLive = true;
    
    return returnStatus;
}

#if !defined(__ghs__)
#pragma mark --WriteTransaction--
#endif

WriteTransaction::WriteTransaction(DataDrive * drive)
:   MultiTransaction(drive)
{
}

RtStatus_t WriteTransaction::computePhysicalPages()
{
    assert(m_sectorMap);
    
    RtStatus_t status;
    
    for (int i = 0; i < m_sectorCount; ++i)
    {
        NandPhysicalMedia::MultiplaneParamBlock & pb = m_sectors[i];
        SectorInfo & info = m_sectorInfo[i];

        // Convert the logical offset into a virtual offset and a real physical page address. If
        // the physical block has not yet been allocated, then this method will allocate one for us.
        PageAddress physicalPageAddress;
        status = m_sectorMap->getNextPhysicalPage(info.m_logicalOffset, physicalPageAddress, &info.m_virtualOffset);
            
        if (status == SUCCESS)
        {
            // Save the NAND relative physical page address.
            pb.m_address = physicalPageAddress.getRelativePage();
            
            // Save the NAND object from the first page we look at.
            if (!m_nand)
            {
                m_nand = physicalPageAddress.getNand();
            }
            // And check that other pages belong to the same NAND.
            else if (m_nand != physicalPageAddress.getNand())
            {
                // This page is on a different NAND, so we must use the abort commit.
                m_mustAbort = true;
            }
        }
        else
        {
            // Some unexpected error occurred, so just exit immediately.
            return status;
        }
        
        // Update metadata for this page.
        prepareMetadata(pb, info);

#if !USE_DATA_DRIVE_W_OPS
        // We have to go ahead and insert the entries in the NSSM's sector map, since this
        // will increment the next virtual offset. It also allows for tracking whether the block
        // is in logical order.
        //! \todo How to deal with write errors that cause this information to be invalid?
        //! \todo Must handle undoing this if the next page causes us to use abort commit.
        m_sectorMap->addEntry(info.m_logicalOffset, info.m_virtualOffset);
#endif // !USE_DATA_DRIVE_W_OPS
    }
    
    return SUCCESS;
}

void WriteTransaction::prepareMetadata(NandPhysicalMedia::MultiplaneParamBlock & pb, SectorInfo & info)
{
    assert(m_sectorMap);
    VirtualBlock & vblock = m_sectorMap->getVirtualBlock();
    
    // See if the whole block is written in logical order, so we know whether to set the
    // is-in-order flag in the page metadata.
    bool isInLogicalOrder = false;
    if (info.m_logicalOffset == VirtualBlock::getVirtualPagesPerBlock() - 1)
    {
        isInLogicalOrder = m_sectorMap->isInLogicalOrder();
    }

    // Initialize the redundant area. Up until now, we have ignored u32LogicalSectorOffset.
    // We write the logical sector offset into redundant area so that NSSM may be reconstructed
    // from physical block. The block number stored in the metadata is the value that is passed
    // to the mapper to look up the physical block, which is the virtual block number plus the
    // plane index for the virtual sector offset.
    assert(pb.m_auxiliaryBuffer);
    Metadata md(pb.m_auxiliaryBuffer);
    md.prepare(vblock.getMapperKeyFromVirtualOffset(info.m_virtualOffset), info.m_logicalOffset);
    
    // If this drive is a hidden data drive, then we need to set the RA flag indicating so.
    if (m_drive->m_Type == kDriveTypeHidden)
    {
        // Clear the flag bit to set it. All metadata flags are set when the bit is 0.
        md.setFlag(Metadata::kIsHiddenBlockFlag);
    }
    
    // The pages of this block are written in logical order, we set kIsInLogicalOrderFlag 
    if (isInLogicalOrder)
    {
        md.setFlag(Metadata::kIsInLogicalOrderFlag);
    }
}

RtStatus_t WriteTransaction::multiplaneCommit()
{
#if USE_DATA_DRIVE_W_OPS
    return abortCommit();
#else
    assert(m_nand);
    assert(m_sectorMap);
    
    RtStatus_t status = SUCCESS;
    
#if !USE_SINGLE_PLANE_W_OPS
    // Perform the multiplane write.
    status = m_nand->writeMultiplePages(m_sectors, m_sectorCount);
    if (status != SUCCESS)
    {
        return status;
    }
#endif // !USE_SINGLE_PLANE_W_OPS
    
    // This first loop reviews the results from each page that was written. For each successful
    // page write, the NSSM is updated with the new logical and virtual offsets. This loop also
    // checks for failed writes, which will be handled in the second loop, below.
    int i;
    bool hadFailedWrites = false;
    for (i = 0; i < m_sectorCount; ++i)
    {
        NandPhysicalMedia::MultiplaneParamBlock & pb = m_sectors[i];
        SectorInfo & info = m_sectorInfo[i];

#if USE_SINGLE_PLANE_W_OPS
        pb.m_resultStatus = m_nand->writePage(pb.m_address, pb.m_buffer, (const SECTOR_BUFFER *)pb.m_auxiliaryBuffer);
#endif // USE_SINGLE_PLANE_W_OPS

        if (pb.m_resultStatus == SUCCESS)
        {
            // Don't have to do anything special.
        }
        else if (pb.m_resultStatus == ERROR_DDI_NAND_HAL_WRITE_FAILED)
        {
            // Recover from the failed write by rewriting the sector using a single write.
            tss_logtext_Print(LOGTEXT_VERBOSITY_ALL | LOGTEXT_EVENT_DDI_NAND_GROUP, "*** Multi write failed: new bad vblock %u (voffset %u)! ***\n", m_virtualBlockAddress.get(), info.m_virtualOffset);
            
            hadFailedWrites = true;
        }
        else if (pb.m_resultStatus != SUCCESS)
        {
            // Some other error occurred, so just save the result.
            status = pb.m_resultStatus;
        }
    }

    // If one or more writes failed, then we handle the failure here.
    if (hadFailedWrites)
    {
        bool didRecoverFromFailedWrite = false;
        for (i = 0; i < m_sectorCount; ++i)
        {
            NandPhysicalMedia::MultiplaneParamBlock & pb = m_sectors[i];
            SectorInfo & info = m_sectorInfo[i];

            // Recover from the failed write by rewriting the sector using a single write.
            if (pb.m_resultStatus == ERROR_DDI_NAND_HAL_WRITE_FAILED)
            {
                // We only want to do the initial recover a single time.
                if (!didRecoverFromFailedWrite)
                {
                    // Try to recover by copying data into a new block. We must skip the logical
                    // sector that we were going to write.
                    status = m_sectorMap->recoverFromFailedWrite(info.m_virtualOffset, info.m_logicalOffset);
                    if (status != SUCCESS)
                    {
                        tss_logtext_Print(LOGTEXT_VERBOSITY_ALL | LOGTEXT_EVENT_DDI_NAND_GROUP, "Recovery from failed write (sector %u) failed with error %u\n", info.m_logicalSector, status);
                        return status;
                    }
                    
                    didRecoverFromFailedWrite = true;
                }
                
                // Rewrite this page using the standard sector write API.
                status = m_drive->writeSector(info.m_logicalSector, pb.m_buffer);
            }
        }
    }
    
    return status;
#endif // USE_DATA_DRIVE_W_OPS
}

RtStatus_t WriteTransaction::abortCommit()
{
    assert(m_isLive);
    
    RtStatus_t returnStatus = SUCCESS;
    
    // Disable this transaction temporarily, so the read sector call will work normally.
    m_isLive = false;

    for (int i = 0; i < m_sectorCount; ++i)
    {
        NandPhysicalMedia::MultiplaneParamBlock & pb = m_sectors[i];
        SectorInfo & info = m_sectorInfo[i];
        
        RtStatus_t thisStatus = m_drive->writeSector(info.m_logicalSector, pb.m_buffer);
        
        if (thisStatus != SUCCESS)
        {
            returnStatus = thisStatus;
        }
    }
    
    // Turn this transaction back on.
    m_isLive = true;
    
    return returnStatus;
}

////////////////////////////////////////////////////////////////////////////////
// End of file
////////////////////////////////////////////////////////////////////////////////
