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
//! \addtogroup ddi_nand_mapper
//! @{
//! \file ZoneMapSectionPage.cpp
//! \brief Class to wrap a section of the zone map.
///////////////////////////////////////////////////////////////////////////////

#include "ZoneMapSectionPage.h"
#include <algorithm>

using namespace nand;

///////////////////////////////////////////////////////////////////////////////
// Source
///////////////////////////////////////////////////////////////////////////////

ZoneMapSectionPage::ZoneMapSectionPage()
:   Page(),
    m_header(NULL),
    m_entrySize(0),
    m_metadataSignature(0),
    m_mapType(0)
{
}

ZoneMapSectionPage::ZoneMapSectionPage(const PageAddress & addr)
:   Page(addr),
    m_header(NULL),
    m_entrySize(0),
    m_metadataSignature(0),
    m_mapType(0)
{
}

ZoneMapSectionPage & ZoneMapSectionPage::operator = (const PageAddress & other)
{
    Page::operator =(other);
    return *this;
}

//! This overridden version of setBuffers() also sets the header struct pointer.
//!
void ZoneMapSectionPage::buffersDidChange()
{
    Page::buffersDidChange();
    m_header = (NandMapSectionHeader_t *)m_pageBuffer.getBuffer();
    m_sectionData = (uint8_t *)m_pageBuffer.getBuffer() + sizeof(*m_header);
}

uint32_t ZoneMapSectionPage::getSectionNumber()
{
    assert(m_entrySize);
    return m_header->startLba / getMaxEntriesPerPage();
}

bool ZoneMapSectionPage::validateHeader()
{
    assert(m_mapType);
    return (m_header->signature == kNandMapHeaderSignature
        && m_header->mapType == m_mapType
        && m_header->version == kNandMapSectionHeaderVersion);
}

RtStatus_t ZoneMapSectionPage::writeSection(
    uint32_t startingEntryNum,
    uint32_t remainingEntries,
    uint8_t * startingEntry,
    uint32_t * actualNumEntriesWritten)
{
    assert(startingEntry);
    assert(actualNumEntriesWritten);
    assert(m_pageBuffer.hasBuffer());
    assert(m_auxBuffer.hasBuffer());
    assert(m_metadataSignature);
    assert(m_mapType);
    
    RtStatus_t ret;
    uint32_t numWritten;
    
    // Calculate the entries per page dynamically using the size of each entry that was
    // passed in. We do this instead of using the globals in g_MapperDescriptor because this
    // function will be called for both zone and phy maps, which may have differing entry sizes.
    uint32_t entriesPerPage = getMaxEntriesPerPage();
    numWritten = std::min(entriesPerPage, remainingEntries);

    // Fill in the header.
    m_header->signature = kNandMapHeaderSignature;
    m_header->mapType = m_mapType;
    m_header->version = kNandMapSectionHeaderVersion;
    m_header->entrySize = m_entrySize;
    m_header->startLba = startingEntryNum;
    m_header->entryCount = numWritten;

    // Copy the entries into our sector buffer, after the header.
    memcpy(m_sectionData, startingEntry, numWritten * m_entrySize);

    // Initialize the redundant area. Set the Stmp code to the value passed in u32String.
    m_metadata.prepare(m_metadataSignature);

    // Write the page.
    ret = write();
    
    // Set the number of entries written based on whether the write succeeded.
    if (ret)
    {
        *actualNumEntriesWritten = 0;
    }
    else
    {
        *actualNumEntriesWritten = numWritten;
    }

    return ret;
}

////////////////////////////////////////////////////////////////////////////////
// End of file
////////////////////////////////////////////////////////////////////////////////
//! @}
