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
//! \file Page.cpp
//! \brief Class to wrap a page of a NAND.
///////////////////////////////////////////////////////////////////////////////

#include "Page.h"
#include "Block.h"
#include "drivers/media/buffer_manager/media_buffer_manager.h"
#include "ddi_nand_ddi.h"

using namespace nand;

///////////////////////////////////////////////////////////////////////////////
// Source
///////////////////////////////////////////////////////////////////////////////

#if !defined(__ghs__)
#pragma mark --Page--
#endif

Page::Page()
:   PageAddress(),
    m_nand(NandHal::getFirstNand()),
    m_pageBuffer(),
    m_auxBuffer(),
    m_metadata()
{
}

Page::Page(const PageAddress & addr)
:   PageAddress(addr),
    m_pageBuffer(),
    m_auxBuffer(),
    m_metadata()
{
    m_nand = PageAddress::getNand();
}

Page::Page(const BlockAddress & addr)
:   PageAddress(addr),
    m_pageBuffer(),
    m_auxBuffer(),
    m_metadata()
{
    m_nand = PageAddress::getNand();
}

Page::Page(const Page & other)
:   PageAddress(other),
    m_pageBuffer(other.m_pageBuffer),
    m_auxBuffer(other.m_auxBuffer),
    m_metadata()
{
    m_nand = PageAddress::getNand();
    
    if (m_auxBuffer.hasBuffer())
    {
        m_metadata.setBuffer(m_auxBuffer);
    }
}

Page & Page::operator = (const Page & other)
{
    m_address = other.m_address;
    m_nand = other.m_nand;
    
    // We must cast to non-const SECTOR_BUFFER since the other's buffer objects
    // are const in this case.
    setBuffers((SECTOR_BUFFER *)other.m_pageBuffer.getBuffer(), (SECTOR_BUFFER *)other.m_pageBuffer.getBuffer());
    return *this;
}

Page & Page::operator = (const PageAddress & addr)
{
    PageAddress::operator=(addr);
    m_nand = PageAddress::getNand();
    return *this;
}

Page & Page::operator = (const BlockAddress & addr)
{
    PageAddress::operator=(addr);
    m_nand = PageAddress::getNand();
    return *this;
}

Page::~Page()
{
    releaseBuffers();
}

//! \brief Change the address.
void Page::set(const PageAddress & addr)
{
    PageAddress::set(addr);
    m_nand = PageAddress::getNand();
}

Page & Page::operator ++ ()
{
    PageAddress::operator++();
    m_nand = PageAddress::getNand();
    return *this;
}
 
Page & Page::operator -- ()
{
    PageAddress::operator -- ();
    m_nand = PageAddress::getNand();
    return *this;
}

Page & Page::operator += (uint32_t amount)
{
    PageAddress::operator += (amount);
    m_nand = PageAddress::getNand();
    return *this;
}

Page & Page::operator -= (uint32_t amount)
{
    PageAddress::operator -= (amount);
    m_nand = PageAddress::getNand();
    return *this;
}

void Page::setBuffers(SECTOR_BUFFER * pageBuffer, SECTOR_BUFFER * auxBuffer)
{
    // Changing the buffer values will release previous buffers if necessary.
    m_pageBuffer = pageBuffer;
    m_auxBuffer = auxBuffer;
    
    // Updated related pointers.
    buffersDidChange();
}

RtStatus_t Page::allocateBuffers(bool page, bool aux)
{
    RtStatus_t status;
    
    if (page)
    {
        status = m_pageBuffer.acquire(kMediaBufferType_Sector);
        if (status != SUCCESS)
        {
            return status;
        }
    }
    if (aux)
    {
        status = m_auxBuffer.acquire(kMediaBufferType_Auxiliary);
        if (status != SUCCESS)
        {
            if (page)
            {
                m_pageBuffer.release();
            }
            return status;
        }
    }
    
    // Call the virtual method to update buffers to let subclasses update any of
    // their own pointers.
    buffersDidChange();
    
    return SUCCESS;
}

void Page::releaseBuffers()
{
    m_pageBuffer.release();
    m_auxBuffer.release();
}

RtStatus_t Page::read(NandEccCorrectionInfo_t * eccInfo)
{
    assert(m_pageBuffer.hasBuffer());
    assert(m_auxBuffer.hasBuffer());
    return m_nand->readPage(getRelativePage(), m_pageBuffer, m_auxBuffer, eccInfo);
}

RtStatus_t Page::readMetadata(NandEccCorrectionInfo_t * eccInfo)
{
    assert(m_auxBuffer.hasBuffer());
    return m_nand->readMetadata(getRelativePage(), m_auxBuffer, eccInfo);
}

RtStatus_t Page::write()
{
    assert(m_pageBuffer.hasBuffer());
    assert(m_auxBuffer.hasBuffer());
    return m_nand->writePage(getRelativePage(), m_pageBuffer, m_auxBuffer);
}

RtStatus_t Page::writeAndMarkOnFailure()
{
    RtStatus_t status = write();

    // If error is type ERROR_DDI_NAND_HAL_WRITE_FAILED, then the block we try to write
    // to is BAD. We need to mark it physically as such.
    if (status == ERROR_DDI_NAND_HAL_WRITE_FAILED)
    {
        tss_logtext_Print(LOGTEXT_VERBOSITY_ALL | LOGTEXT_EVENT_DDI_NAND_GROUP, "*** Write failed: new bad block %u! ***\n", getBlock().get());
            
        // Mark the block bad
        Block(*this).markBad();
    }

    return status;
}

bool Page::isErased(RtStatus_t * status)
{
    RtStatus_t localStatus = readMetadata();
    bool pageIsErased = m_metadata.isErased();
    if (status)
    {
        *status = localStatus;
    }
    return pageIsErased;
}

void Page::buffersDidChange()
{
    m_metadata.setBuffer(m_auxBuffer);
}

#if !defined(__ghs__)
#pragma mark --BootPage--
#endif

BootPage::BootPage()
:   Page(),
    m_doRawWrite(false)
{
}

//! \brief Constructor taking a page address.
BootPage::BootPage(const PageAddress & addr)
:   Page(addr),
    m_doRawWrite(false)
{
}

//! \brief Write the page contents.
RtStatus_t BootPage::write()
{
    // Page buffer is required for both raw and ECC writes.
    assert(m_pageBuffer.hasBuffer());

    // Perform the write.
    if (m_doRawWrite)
    {
        return m_nand->writeRawData(getRelativePage(), 0, m_nand->pNANDParams->pageTotalSize, m_pageBuffer);
    }
    else
    {
        assert(m_auxBuffer.hasBuffer());
        return m_nand->writeFirmwarePage(getRelativePage(), m_pageBuffer, m_auxBuffer);
    }
}


////////////////////////////////////////////////////////////////////////////////
// End of file
////////////////////////////////////////////////////////////////////////////////
