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
//! \file Block.cpp
//! \brief Class to wrap a block of a NAND.
///////////////////////////////////////////////////////////////////////////////

#include "Block.h"
#include "Page.h"
#include "drivers/media/buffer_manager/media_buffer.h"
#include "ddi_nand_media.h"

using namespace nand;

///////////////////////////////////////////////////////////////////////////////
// Source
///////////////////////////////////////////////////////////////////////////////

Block::Block()
:   BlockAddress()
{
    m_nand = NandHal::getFirstNand();
}

Block::Block(const BlockAddress & addr)
:   BlockAddress(addr)
{
    m_nand = BlockAddress::getNand();
}

Block & Block::operator = (const Block & other)
{
    m_nand = other.m_nand;
    m_address = other.m_address;
    return *this;
}

Block & Block::operator = (const BlockAddress & addr)
{
    BlockAddress::operator=(addr);
    m_nand = BlockAddress::getNand();
    return *this;
}

Block & Block::operator = (const PageAddress & page)
{
    BlockAddress::operator=(page);
    m_nand = BlockAddress::getNand();
    return *this;
}

void Block::set(const BlockAddress & addr)
{
    BlockAddress::set(addr);
    m_nand = BlockAddress::getNand();
}

Block & Block::operator ++ ()
{
    BlockAddress::operator ++ ();
    m_nand = BlockAddress::getNand();
    return *this;
}
    
Block & Block::operator -- ()
{
    BlockAddress::operator -- ();
    m_nand = BlockAddress::getNand();
    return *this;
}

Block & Block::operator += (uint32_t amount)
{
    BlockAddress::operator += (amount);
    m_nand = BlockAddress::getNand();
    return *this;
}

Block & Block::operator -= (uint32_t amount)
{
    BlockAddress::operator -= (amount);
    m_nand = BlockAddress::getNand();
    return *this;
}

RtStatus_t Block::readPage(unsigned pageOffset, SECTOR_BUFFER * buffer, SECTOR_BUFFER * auxBuffer, NandEccCorrectionInfo_t * eccInfo)
{
    return m_nand->readPage(PageAddress(m_address, pageOffset).getRelativePage(), buffer, auxBuffer, eccInfo);
}

RtStatus_t Block::readMetadata(unsigned pageOffset, SECTOR_BUFFER * buffer, NandEccCorrectionInfo_t * eccInfo)
{
    return m_nand->readMetadata(PageAddress(m_address, pageOffset).getRelativePage(), buffer, eccInfo);
}

RtStatus_t Block::writePage(unsigned pageOffset, const SECTOR_BUFFER * buffer, SECTOR_BUFFER * auxBuffer)
{
    return m_nand->writePage(PageAddress(m_address, pageOffset).getRelativePage(), buffer, auxBuffer);
}

RtStatus_t Block::erase()
{
    return m_nand->eraseBlock(getRelativeBlock());
}

bool Block::isMarkedBad(SECTOR_BUFFER * auxBuffer, RtStatus_t * status)
{
    // Allocate a temp buffer if one was not provided.
    AuxiliaryBuffer tempBuffer;
    if (!auxBuffer)
    {
        RtStatus_t status = tempBuffer.acquire();
        if (status != SUCCESS)
        {
            // We couldn't allocate the buffer. In debug builds we want to call attention to this,
            // but in release builds we just treat the block as if it's good since that is the
            // most common case.
            assert(false);
            return false;
        }
        
        auxBuffer = tempBuffer;
    }

    uint32_t relativeAddress = getRelativeBlock();
    
#if defined(STMP378x)
    // Skip NCB1 on NAND0 or NCB2 on NAND1. We must only skip if the NCB is already in place,
    // otherwise we might misreport a truly bad block as good. However, none of this
    // even needs to be done if the page size is larger than 2KB since
    BootBlocks & bootBlocksInfo = g_nandMedia->getBootBlocks();
    if (NandHal::getParameters().pageTotalSize > LARGE_SECTOR_TOTAL_SIZE && bootBlocksInfo.hasValidNCB())
    {
        unsigned nandNumber = m_nand->wChipNumber;
        if (bootBlocksInfo.m_ncb1.doesAddressMatch(nandNumber, relativeAddress) || bootBlocksInfo.m_ncb2.doesAddressMatch(nandNumber, relativeAddress))
        {
            return false;
        }
    }
#endif // defined(STMP378x)

    // Don't check factory markings.
    return m_nand->isBlockBad(relativeAddress, auxBuffer, false, status);
}

RtStatus_t Block::markBad()
{
    MediaBuffer buffer;
    RtStatus_t status;
    if ((status = buffer.acquire(kMediaBufferType_NANDPage)) != SUCCESS)
    {
        return status;
    }

    AuxiliaryBuffer auxBuffer;
    if ((status = auxBuffer.acquire()) != SUCCESS)
    {
        return status;
    }
    
    return m_nand->markBlockBad(getRelativeBlock(), buffer, auxBuffer);
}

RtStatus_t Block::eraseAndMarkOnFailure()
{
    RtStatus_t status = erase();
    if (status != SUCCESS)
    {
        tss_logtext_Print(LOGTEXT_VERBOSITY_ALL | LOGTEXT_EVENT_DDI_NAND_GROUP, "*** Erase failed: new bad block %u! ***\n", get());
            
        markBad();
    }
    return status;
}

bool Block::isErased()
{
    Page firstPage(*this);
    firstPage.allocateBuffers(false, true); // Allocate only aux buffer.
    if (firstPage.readMetadata() != SUCCESS)
    {
        return false;
    }
    return firstPage.getMetadata().isErased();
}

////////////////////////////////////////////////////////////////////////////////
// End of file
////////////////////////////////////////////////////////////////////////////////
