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
//! \brief  Definition of the NSSM manager.
////////////////////////////////////////////////////////////////////////////////
#if !defined(__nssm_manager_h__)
#define __nssm_manager_h__

#include "types.h"
#include "PageOrderMap.h"
#include "RedBlackTree.h"
#include "wlru.h"
#include "DeferredTask.h"
#include "ddi_nand_ddi.h"
#include "drivers/media/include/ddi_media_timers.h"

///////////////////////////////////////////////////////////////////////////////
// Definitions
///////////////////////////////////////////////////////////////////////////////

namespace nand {

#if defined(NO_SDRAM)
    //! Set the default number of maps for the data drive. Maps are allocated dynamically.
    #define NUM_OF_MAX_SIZE_NS_SECTORS_MAPS 9
#else
    //! Set the default number of maps for the data drive. Maps are allocated dynamically.
    #define NUM_OF_MAX_SIZE_NS_SECTORS_MAPS 64
#endif

class Mapper;
class NonsequentialSectorsMap;

/*!
 * \brief Manages the array of nonsequential sector maps.
 *
 * An array of nonsequential sector maps are shared by all data drive regions, to hold
 * a mapping of the order in which sectors have been written to open block splits.
 *
 * \ingroup ddi_nand_data_drive
 */
class NssmManager
{
public:

    /*!
     * \brief Statistics about map usage.
     */
    struct Statistics
    {
        //! \name Map builds
        //@{
        uint32_t buildCount;    //!< Number of times an NSSM had to be built by reading metadata from the NAND.
        uint32_t multiBuildCount;   //!< Number of multiplane builds.
        uint32_t orderedBuildCount; //!< Times that a full build was avoided because the logical order flag was set.
        uint32_t blockDepthSum; //!< Total number of pages found in all block builds. Used to compute average.
        uint32_t averageBlockDepth; //!< Average number of filled pages encountered when building maps.
        AverageTime averageBuildTime;   //!< Average time it takes to build a map by reading metadata. Does not include times for ordered builds.
        AverageTime averageMultiBuildTime;  //!< Average time for a multiplane build.
        //@}

        //! \name Merges
        //!
        //! Number of times two blocks were merged. There are three different algorithms possible,
        //! and each instance is counted separately.
        //@{
        uint32_t mergeCountCore;    //!< Normal merge between old and new blocks into a newly allocated third block.
        uint32_t mergeCountShortCircuit;    //!< Old block is simply discarded.
        uint32_t mergeCountQuick;   //!< Old block is merged into new block in-place, without allocating a third block.
        AverageTime averageCoreMergeTime;   //!< Average time it takes to perform a core merge.
        //@}
        
        uint32_t indexHits; //!< Times a requested map was found in the index.
        uint32_t indexMisses;   //!< Times a requested map wasn't in the index.
        
        uint32_t writeSetOrderedCount;  //!< Number of times a page write resulted in the logical order flag being set.
        uint32_t mergeSetOrderedCount;  //!< Number of times a merge resulted in a block in logical order.
        
        uint32_t relocateBlockCount;    //!< Times a virtual block was relocated using the relocate task.
    };
    
    //! \brief Constructor.
    NssmManager(Media * nandMedia);
    
    //! \brief Destructor.
    //!
    //! Frees memory allocated for the NSSM array.
    ~NssmManager();
    
    //! \brief Allocates or reallocates the array of NSSMs.
    RtStatus_t allocate(unsigned uMapsPerBaseNSSMs);
    
    //! \brief get new array for page order internal array. 
    //! \note this function is called from NSSM::init function as part of NssmManager::allocate procedure
    uint8_t *getPOBlock(void);
    
    //! \brief Returns the size of the NSSM array in terms of the base block size.
    unsigned getBaseNssmCount();
    
    //! \name Flush and invalidate
    //@{
    void flushAll();
    void invalidateAll();
    void invalidateDrive(LogicalDrive * pDriveDescriptor);
    //@}
    
    NonsequentialSectorsMap * getMapForIndex(unsigned index);
    RtStatus_t getMapForVirtualBlock(uint32_t blockNumber, NonsequentialSectorsMap ** map);
    
    //! \name Accessors
    //@{
    Statistics & getStatistics() { return m_statistics; }
    Media * getMedia() { return m_media; }
    Mapper * getMapper() { return m_mapper; }
    //@}

protected:
    
    Media * m_media;    //!< The NAND media object.
    Mapper * m_mapper;  //!< The virtual to logical mapper object.
    unsigned m_mapCount;    //!< Number of descriptors pointed to by the \a m_mapsArray field.
    NonsequentialSectorsMap * m_mapsArray;  //!< Pointer to the shared array of non-sequential sectors map objects. All data-type drives use this same array.
    RedBlackTree m_index;   //!< Index of the maps.
    WeightedLRUList m_lru;  //!< LRU for the maps.
    Statistics m_statistics;    //!< Statistics about map usage.

    // Allocator which allocates internal array of PageOrderMap.
    unsigned m_uPOBlockSize;
    unsigned m_uPOUseIndex;
    uint8_t *m_PODataArray;
    
    RtStatus_t buildMap(uint32_t u32LBABlkAddr, NonsequentialSectorsMap ** resultMap);
    
    friend class NonsequentialSectorsMap;
};

} // namespace nand

#endif // __nssm_manager_h__
///////////////////////////////////////////////////////////////////////////////
// EOF
///////////////////////////////////////////////////////////////////////////////
