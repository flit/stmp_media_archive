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
//! \file
//! \brief Block allocation algorithm implementations.
////////////////////////////////////////////////////////////////////////////////
#if !defined(__BlockAllocator_h__)
#define __BlockAllocator_h__

#include "types.h"
#include "Taus88.h"

/////////////////////////////////////////////////////////////////////////////////
// Code
////////////////////////////////////////////////////////////////////////////////

namespace nand {

class PhyMap;

/*!
 * \brief Base class for free block allocators.
 *
 * \note The range \em must be set on a new instance before it can be used to
 *  allocate any blocks.
 */
class BlockAllocator
{
public:
    /*!
     * \brief Constraints for which blocks can be selected during block allocation.
     *
     * Each of the fields can either be set to a valid value or #kUnconstrained if that
     * field is to be ignored (not used as a constraint).
     *
     * If a die number is specified, then the chip number must also be specified. Dice always
     * belong to a given chip.
     */
    struct Constraints
    {
        //! \brief Constants for allocation constraints.
        enum _constraints_constants
        {
            //! Set a constraint to this constant to cause it to be ignored when allocating
            //! blocks.
            kUnconstrained = -1
        };
        
        int m_chip;     //!< Chip select.
        int m_die;      //!< Die number of the chip. #m_chip must also be set if this is used.
        int m_plane;    //!< Plane number within the die and/or chip.
        
        //! \brief Constructor that sets all fields to unconstrained.
        Constraints() : m_chip(kUnconstrained), m_die(kUnconstrained), m_plane(kUnconstrained) {}
    };

    //! \brief Constructor, optionally taking a pointer to the phy map.
    //!
    //! The constructor sets all constraints to unconstrained by default.
    BlockAllocator(PhyMap * map=NULL);
    
    //! \brief Specify the range of blocks that can be allocated.
    void setRange(uint32_t start, uint32_t end);
    
    //! \name Phy map
    //@{
    void setPhyMap(PhyMap * map) { m_phymap = map; }
    PhyMap * getPhyMap() { return m_phymap; }
    //@}
    
    //! \name Constraints
    //@{
    void setConstraints(const Constraints & newConstraints);
    void clearConstraints();
    const Constraints & getConstraints() const { return m_constraints; }
    //@}
    
    //! \brief Finds and returns a free block for use.
    //! \param[out] Upon successful exit this will contain the free block's absolute
    //!     address. The valid range is from 0 to the number of entries in the phy map. If
    //!     false is returned, then this value is undetermined.
    //! \retval true A free block was found and returned in \a newBlockAddress.
    //! \retval false The entire phy map is full. No more blocks are free.
    virtual bool allocateBlock(uint32_t & newBlockAddress) = 0;

protected:
    PhyMap * m_phymap;      //!< Phymap to use.
    uint32_t m_start;   //!< First available block.
    uint32_t m_end;     //!< Last available block.
    Constraints m_constraints;  //!< Allocation constraints.
    
    //! \brief Returns the begin and end of the range, limited to constraints.
    void getConstrainedRange(uint32_t & start, uint32_t & end);

    //! \brief Performs a looping search from a given position.
    bool splitSearch(uint32_t start, uint32_t end, uint32_t position, uint32_t * result);
};

/*!
 * \brief Allocator that starts from a random location each time.
 *
 * This allocator will allocate from a random location each time a block is allocated.
 * For each allocation, it starts by picking a random block number within the range specified
 * by the call to setRange(). Then the phy map is searched, scanning forward until a free
 * block is found. If no free block is found, then the search starts over at the beginning
 * of the specified range. Using this algorithm, block allocation has a fixed maximum time
 * in the case where all blocks are used (no free blocks).
 *
 * Upon object construction, the random number generator is seeded with the hardware entropy
 * register combined with the current microsecond counter (both in the DIGCTL block). This
 * method is used to ensure that the seed changes every time an instance of this class is
 * created, as the entropy register is only set once at system reset.
 */
class RandomBlockAllocator : public BlockAllocator
{
public:
    //! \brief Constructor. Seeds the PRNG.
    RandomBlockAllocator(PhyMap * map=NULL);
    
    //! \copydoc BlockAllocator::allocateBlock()
    virtual bool allocateBlock(uint32_t & newBlockAddress);

protected:
    Taus88 m_rng;   //!< The pseudo-random number generator object.
};

/*!
 * \brief Allocator that loops around the search range.
 */
class LinearBlockAllocator : public BlockAllocator
{
public:
    //! \brief Constructor.
    LinearBlockAllocator(PhyMap * map=NULL);
        
    //! \copydoc BlockAllocator::allocateBlock()
    virtual bool allocateBlock(uint32_t & newBlockAddress);
    
    //! \brief Set the next position to start searching from.
    void setCurrentPosition(uint32_t position);

protected:
    uint32_t m_currentPosition; //!< Position to start searching from for the next allocation.
};

} // namespace nand

#endif // __BlockAllocator_h__
////////////////////////////////////////////////////////////////////////////////
// End of file
////////////////////////////////////////////////////////////////////////////////
//! @}
