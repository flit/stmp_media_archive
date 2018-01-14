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
//! \addtogroup ddi_media_nand_hal
//! @{
//! \file PersistentMap.h
//! \brief Definition of the nand::PersistentMap class.
////////////////////////////////////////////////////////////////////////////////
#if !defined(__persistent_map_h__)
#define __persistent_map_h__

#include "types.h"
#include "errordefs.h"
#include "Block.h"
#include "PageOrderMap.h"

namespace nand
{

// Forward declarations
class Mapper;
class ZoneMapSectionPage;

/*!
 * \brief Base class for a map that is stored on the NAND.
 *
 * This class implements a map composed of integer entries that is broken into one or
 * more sections, each the size of a NAND page. The map is stored on the NAND in an
 * efficient manner, by writing sections sequentially to pages within a block.
 *
 * As a new version of a section becomes available, it is written to the next page in the
 * block. The sections can be in any order in the block, and there can be multiple copies
 * of any given section, but only the most recent copy of a section will be recognized.
 * Only when the block is completely full, with no free pages, will the map be copied
 * (consolidated) to a new block.
 *
 * The content for sections of the map is not handled by this class. It is the
 * responsibility of subclasses or users of the class to provide that content.
 *
 * Right now, this class only supports storing the map within a single block. But it is
 * possible that in the future this restriction may be relaxed, in order to store maps
 * that are larger than will fit within one block.
 */
class PersistentMap
{
public:

    //! \brief Default constructor.
    PersistentMap(Mapper & mapper, uint32_t mapType, uint32_t metadataSignature);
    
    //! \brief Destructor.
    virtual ~PersistentMap();
    
    //! \brief Initializer.
    void init(int entrySize, int entryCount);
    
    //! \brief Does the given block belong to this map?
    bool isMapBlock(const BlockAddress & address) { return m_block == address; }

    //! \brief Rebuild the map into a new block.
    virtual RtStatus_t consolidate(
        bool hasValidSectionData=false,
        uint32_t sectionNumber=0,
        uint8_t * sectionData=NULL,
        uint32_t sectionDataEntryCount=0);
    
    //! \brief Write an updated section of the map.
    RtStatus_t addSection(
        uint8_t *pMap,
        uint32_t u32StartingEntryNum,
        uint32_t u32NumEntriesToWrite);

    //! \brief Load section of the map.
    RtStatus_t retrieveSection(
        uint32_t u32EntryNum,
        uint8_t *pMap,
        bool shouldConsolidateOnRewriteSectorError);

    //! \brief Returns the address of the block currently holding this map on the media.
    const BlockAddress & getAddress() const { return m_block; }
    
protected:

    Mapper & m_mapper;      //!< Our parent mapper instance.
    BlockAddress m_block;   //!< The block containing this map.
    int m_entrySize;        //!< Size of each map entry in bytes.
    int m_maxEntriesPerPage;    //!< Number of entries that fit in one NAND page.
    uint32_t m_signature;   //!< The map type signature.
    uint32_t m_metadataSignature;   //!< A signature written into the metadata of each map section page.
    int m_topPageIndex;     //!< Number of sections currently in the map's block.
    int m_totalEntryCount;  //!< Total number of entries in the entire map.
    int m_totalSectionCount;    //!< Total number of sections in the entire map.
    PageOrderMap m_sectionPageOffsets;   //!< Map from zone map section number to page offset within the zone map block.
    bool m_didConsolidateDuringAddSection;  //!< Set to true if addSection() does a consolidate.
    int m_buildReadCount;

    //! \brief Scan the map's block an build the section offset table.
    RtStatus_t buildSectionOffsetTable();
    
    //! \brief Do a binary search to find the first empty page.
    RtStatus_t findTopPageIndex(ZoneMapSectionPage & mapPage, bool & needsRewrite);
    
    //! \brief Scan to find the most recent copies of each section.
    RtStatus_t fillUnknownSectionOffsets(ZoneMapSectionPage & mapPage, bool & needsRewrite);

    //! \brief Read a section during consolidation.
    //!
    //! The default implementation simply uses retrieveSection() to load the data.
    //! Having this function virtual makes it possible for subclasses to override and provide
    //! additional methods for obtaining the section data, for instance from a cache.
    virtual RtStatus_t getSectionForConsolidate(
        uint32_t u32EntryNum,
        uint32_t thisSectionNumber,
        uint8_t *& bufferToWrite,
        uint32_t & bufferEntryCount,
        uint8_t * sectorBuffer);
    
};

} // namespace nand

#endif // __persistent_map_h__
////////////////////////////////////////////////////////////////////////////////
// EOF
////////////////////////////////////////////////////////////////////////////////
