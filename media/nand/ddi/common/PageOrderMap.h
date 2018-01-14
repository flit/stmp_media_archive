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
//! \addtogroup ddi_nand_media
//! @{
//! \file PageOrderMap.h
//! \brief 
///////////////////////////////////////////////////////////////////////////////
#if !defined(_PageOrderMap_h_)
#define _PageOrderMap_h_

#include "types.h"
#include "error.h"

///////////////////////////////////////////////////////////////////////////////
// Definitions
///////////////////////////////////////////////////////////////////////////////

namespace nand
{

/*!
 * \brief Map of logical page index to physical page index.
 *
 * In addition to being a simple logical to physical map, this class tracks
 * whether each entry has been set to a valid value, i.e. whether it is
 * occupied.
 *
 * This class uses a single malloc'd block to hold both the map and occupied
 * arrays. The occupied array is at the beginning of the block, following by
 * the map array. This is slightly more efficient than two separate
 * allocations.
 *
 * Note that the number of entries doesn't necessarily have to be equal to the
 * number of pages in a block. The physical offset associated with each entry
 * can be any number within the range specified in the call to init(). So if you have
 * fewer logical entries than the number of pages per block, you can still
 * track their location across the full block.
 */
class PageOrderMap
{
public:
    //! \name Init and cleanup
    //@{
        //! \brief Default constructor.
        PageOrderMap() : m_entryCount(0), m_map(0), m_occupied(0) {}
        
        //! \brief Destructor.
        ~PageOrderMap() { cleanup(); }
        
        //! \brief Assignment operator.
        PageOrderMap & operator = (const PageOrderMap & other);
        
        //! \brief Init method taking the number of entries.
        //!
        //! By default, the maximum value for any entry is the \a entryCount minus 1. So if you have
        //! 256 entries, the maximum value for any one of those would be 255. You can override this
        //! maximum, however, by passing a value for the \a maxEntryValue parameter.
        //!
        //! \param entryCount Total number of entries to manage.
        //! \param maxEntryValue Maximum value for each entry. See above for details about the
        //!     default value.
        //! \retval SUCCESS The map object was initialized successfully.
        //! \retval ERROR_OUT_OF_MEMORY Failed to allocate the map.
        RtStatus_t init(unsigned entryCount, unsigned maxEntryValue=0,bool bAllocLSITable = true);

        //! \breif Assign given pointer to m_map array pointer of PageOrderMap        
        void setMapArray(uint8_t *pArray);
        
        //! \brief Frees map memory.
        void cleanup();
    //@}
    
    //! \name Entries
    //@{
        //! \brief Returns the number of entries.
        unsigned getEntryCount() const { return m_entryCount; }
        
        //! \brief Returns the value associated with a logical index.
        unsigned getEntry(unsigned logicalIndex) const;
        
        //! \brief Sets the value for a given logical index and marks it occupied.
        void setEntry(unsigned logicalIndex, unsigned physicalIndex);
        
        //! \brief Get the occupied status of a logical index.
        bool isOccupied(unsigned logicalIndex) const;
        
        //! \brief Sets or clears the occupied flag for an entry.
        void setOccupied(unsigned logicalIndex, bool isOccupied=true);
        
        //! \brief Returns the value associated with a logical index.
        unsigned operator [] (unsigned logicalIndex) const;
        
        //! \brief Sets the map so all entries are unoccupied.
        void clear(bool bDontPreserveLSITable=true);
        
        //! \brief Return give size of single entry
        static unsigned getEntrySize(unsigned entryCount, unsigned maxEntryValue=0);
    //@}
    
    //! \name Sorted order
    //@{
        //! \brief Checks whether logical is equal to physical through a specified entry.
        //!
        //! This method scans \a entriesToCheck number of entries, starting at the first. It
        //! looks for whether each entry's associated value is equal to that entry's index. If
        //! this is true for all the examined entries, then the map is considered to be in
        //! sorted order and true is returned. If the value of any examined entry is something
        //! other than that entry's index then false is returned.
        //!
        //! \param entriesToCheck The number of entries to check for sorted order, starting at
        //!     the first entry.
        //! \return Whether entries are in sorted order through \a entriesToCheck.
        bool isInSortedOrder(unsigned entriesToCheck) const;
        
        //! \brief Set all entries to the sorted order.
        void setSortedOrder();
        
        //! \brief Set a range of entries to a sorted order.
        //!
        //! For \a count entries starting at entry number \a startEntry, assign each entry
        //! an incrementing value beginning with \a startValue. If you set \a startEntry to 0,
        //! \a count to the total number of entries, and \a startValue to 0, then the result
        //! is the same as if calling #setSortedOrder().
        //!
        //! \param startEntry Index of the first entry to assign values to.
        //! \param count Number of entries to assign values to. If this is 1, then only entry
        //!     \a startEntry will be modified.
        //! \param startValue The value to set \a startEntry to. Other entries will be assigned
        //!     a value equal to (\a startValue + (\em entryNumber - \a startEntry)).
        void setSortedOrder(unsigned startEntry, unsigned count, unsigned startValue);
    //@}
    
    //! \name Other
    //@{
        //! \brief Count number of distinct entries in the map.
        //!
        //! This function counts the number of actual entries contained in physical
        //! block which is represented by the map.  Duplicate entries overwrite each
        //! other.  So, it is sufficient to simply count number of entries which
        //! are occupied.
        //!
        //! \return Number of distinct entries.
        unsigned countDistinctEntries() const;
        
        //! \brief Counts entries that exist in this map but not another.
        //!
        //! Given another page order map, this method determines how many logical entries
        //! exist only in this map and not the other.
        //!
        //! If the two maps have different numbers of entries then 0 will be returned.
        //!
        //! \param other The page order map to compare against.
        //! \return Number of entries.
        unsigned countEntriesNotInOtherMap(const PageOrderMap & other) const;
    //@}

protected:
    unsigned m_entryCount;  //!< Number of entries.
    unsigned m_entrySize;   //!< Size of each entry in bytes. Determined by the maximum entry value.
    uint8_t * m_map;    //!< Array of map entries. Points just after \a m_occupied.
    uint32_t * m_occupied;  //!< Bitmap of occupied status for the entries. This is the real pointer to the malloc'd memory.
};

} // namespace nand

#endif // _PageOrderMap_h_

////////////////////////////////////////////////////////////////////////////////
// End of file
////////////////////////////////////////////////////////////////////////////////
//! @}
