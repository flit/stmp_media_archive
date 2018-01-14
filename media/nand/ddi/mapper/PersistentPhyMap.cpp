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
//! \addtogroup ddi_nand_mapper
//! @{
//! \file PersistentPhyMap.cpp
//! \brief Implementation of the persistent phy map class.
////////////////////////////////////////////////////////////////////////////////

#include <algorithm>
#include "PersistentPhyMap.h"
#include "Mapper.h"
#include "PhyMap.h"
#include "ZoneMapSectionPage.h"

using namespace nand;

///////////////////////////////////////////////////////////////////////////////
// Code
///////////////////////////////////////////////////////////////////////////////

PersistentPhyMap::PersistentPhyMap(Mapper & mapper)
:   PersistentMap(mapper, kNandPhysMapSignature, PHYS_STRING_PAGE1),
    m_phymap(NULL),
    m_isLoading(false)
{
}

PersistentPhyMap::~PersistentPhyMap()
{
}

void PersistentPhyMap::init()
{
    uint32_t count = PhyMap::getEntryCountForBlockCount(m_mapper.getMedia()->getTotalBlockCount());
    PersistentMap::init(PhyMap::kEntrySizeInBytes, count);
}

RtStatus_t PersistentPhyMap::load()
{
    assert(m_phymap);

    // Automatically clear the is-loading flag when we leave this scope.
    m_isLoading = true;
    AutoClearFlag clearLoading(m_isLoading);
    
    // Search the nand for the location of the phy map.
    uint32_t mapPhysicalBlock;
    RtStatus_t status = m_mapper.findMapBlock(kMapperPhyMap, &mapPhysicalBlock);
    if (status != SUCCESS)
    {
        return status;
    }
    
//    tss_logtext_Print(LOGTEXT_VERBOSITY_1 | LOGTEXT_EVENT_DDI_NAND_GROUP, "Loading phymap from block %u\n", mapPhysicalBlock);
    
    // Save the phy map location.
    m_block = mapPhysicalBlock;
    
    // Scan the block.
    status = buildSectionOffsetTable();
    if (status != SUCCESS)
    {
        return status;
    }
    
    // Get a temp buffer.
    SectorBuffer buffer;
    if ((status = buffer.acquire()) != SUCCESS)
    {
        return status;
    }
    
    // Prepare buffer pointers.
    NandMapSectionHeader_t * header = (NandMapSectionHeader_t *)buffer.getBuffer();
    uint32_t *pSectionPtr = (uint32_t *)(buffer.getBuffer() + SIZE_IN_WORDS(sizeof(*header)));
    uint8_t * pStartAddr = (uint8_t *)m_phymap->getAllEntries();
    uint32_t startEntryNumber = 0;
    
    // Read each of the map sections from the NAND.
    while (startEntryNumber < m_totalEntryCount)
    {
        // This call loads the entire section page into our buffer, including the header.
        // It also verifies the header, so we know the section is valid unless an error
        // is returned.
        status = retrieveSection(startEntryNumber, buffer, true);
        if (status != SUCCESS)
        {
            return status;
        }

        // Copy section data into the phymap.
        uint32_t u32NumBytes = header->entryCount * PhyMap::kEntrySizeInBytes;
        memcpy(pStartAddr, pSectionPtr, u32NumBytes);
        
        // Advance to the next section.
        startEntryNumber += header->entryCount;
        pStartAddr += u32NumBytes;
    }
    
    return SUCCESS;
}

RtStatus_t PersistentPhyMap::save()
{
    assert(m_phymap);
    assert(m_block.isValid());
    
    RtStatus_t status;
    uint32_t currentEntryNumber = 0;
    int remainingEntries = m_totalEntryCount;
    
//    tss_logtext_Print(~0, "Saving phymap to block %u\n", m_block.get());
    
    while (remainingEntries > 0)
    {
        // Write this section.
        status = addSection((uint8_t *)&(*m_phymap)[currentEntryNumber], currentEntryNumber, remainingEntries);
        if (status != SUCCESS)
        {
            return status;
        }
        
        // Advance to the next section.
        currentEntryNumber += m_maxEntriesPerPage;
        remainingEntries -= m_maxEntriesPerPage;
    }
    
//    tss_logtext_Print(~0, "New top page index of phymap is %u (block #%u)\n", m_topPageIndex, m_block.get());
    
    return SUCCESS;
}

RtStatus_t PersistentPhyMap::saveNewCopy()
{
    uint32_t physicalBlock;
    
    // Use the phymap to allocate a block from the block range reserved for maps.
    RtStatus_t ret = m_mapper.getBlock(&physicalBlock, kMapperBlockTypeMap, 0);
    if (ret != SUCCESS)
    {
        return ret;
    }

    m_block = physicalBlock;
    m_topPageIndex = 0;
    
    return save();
}

PhyMap * PersistentPhyMap::getPhyMap()
{
    return m_phymap;
}

void PersistentPhyMap::setPhyMap(PhyMap * theMap)
{
//    if (m_phymap)
//    {
//        delete m_phymap;
//    }

    m_phymap = theMap;
}

RtStatus_t PersistentPhyMap::getSectionForConsolidate(
    uint32_t u32EntryNum,
    uint32_t thisSectionNumber,
    uint8_t *& bufferToWrite,
    uint32_t & bufferEntryCount,
    uint8_t * sectorBuffer)
{
    // If we're loading the phymap, then we should read the requested section from the
    // NAND to get the latest copy.
    if (m_isLoading)
    {
        return PersistentMap::getSectionForConsolidate(u32EntryNum, thisSectionNumber, bufferToWrite, bufferEntryCount, sectorBuffer);
    }
    
    // Otherwise we can assume that the map in memory is the latest copy.
    assert(m_phymap);
    bufferToWrite = (uint8_t *)&(*m_phymap)[u32EntryNum];
    bufferEntryCount = std::min<uint32_t>(m_maxEntriesPerPage, m_totalEntryCount - u32EntryNum);
    
    return SUCCESS;
}

////////////////////////////////////////////////////////////////////////////////
// End of file
////////////////////////////////////////////////////////////////////////////////
//! @}
