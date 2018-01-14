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
//! \brief Declaration of virtual block class.
////////////////////////////////////////////////////////////////////////////////
#if !defined(__virtual_block_h__)
#define __virtual_block_h__

#include "types.h"
#include "ddi_nand_hal.h"

/////////////////////////////////////////////////////////////////////////////////
// Declarations
////////////////////////////////////////////////////////////////////////////////

namespace nand {

class DataRegion;
class Mapper;

/*!
 * \brief Translates between logical, virtual, and physical addresses.
 *
 * This class encapsulates all knowledge of how multiple planes are arranged and addressed
 * for data drives. It also keeps track of the physical blocks associated with each plane
 * through the mapper.
 *
 * The virtual block has a virtual number of pages per block (<i>q<sub>v</sub></i>, below). Virtual
 * block addresses are spaced between each other by, and divisible by, the number of physical
 * blocks that fit into the virtual number of pages per block. For a two-plane configuration, two
 * physical blocks fit into one virtual block. So for this case, virtual block addresses are
 * divisible by two. Virtual block numbers range from 0 to the total number of blocks in all NANDs,
 * and there is one virtual block number for all planes.
 *
 * The mapper key block is based on the virtual block and used as a key into the mapper to look up
 * associated physical blocks for each plane. Here, the key block addresses have the plane number
 * added to the base virtual block address before being passed into the mapper. There is one key
 * block number for each plane.
 *
 * The number of planes and the number of pages in a virtual block are accessible through the static
 * methods getPlaneCount() and getVirtualPagesPerBlock(), respectively. You must be sure to call
 * determinePlanesToUse() at init time, before either of these values are used. If a VirtualBlock
 * instance is created before these values are initialized, it will assert.
 *
 * <b>Plane count</b> (<i>N</i>) - Number of supported planes and/or chip selects.<br/>
 * <b>Pages per block</b> (<i>q</i>) - Physical pages per block for the NAND.<br/>
 * <b>Virtual pages per block</b> (<i>q<sub>v</sub></i>) - NAND pages per block multiplied by
 *      <i>P</i>.<br/>
 * <b>Region start block</b> (<i>B<sub>r</sub></i>) - Absolute virtual address for the first block
 *      of the region.<br/>
 * <b>Logical sector</b> - Sector within the drive that is being read or written.<br/>
 * <b>Logical sector within region</b> (<i>s</i>) - Logical sector relative to the beginning of the
 *      region in which it falls.<br/>
 * <b>Logical block</b> (<i>B<sub>l</sub></i>) - Block within the region containing <i>s</i>. Has
 *      <i>q<sub>v</sub></i> pages per block.<br/>
 * <b>Logical offset</b> (<i>O<sub>l</sub></i>) - Page offset within <i>B<sub>l</sub></i> for
 *      <i>s</i>. Ranges from 0 through <i>q<sub>v</sub></i> - 1.<br/>
 * <b>Virtual offset</b> (<i>O<sub>v</sub></i>) - Page offset associated with <i>O<sub>l</sub></i>
 *      in the NSSM. Ranges from 0 through <i>q<sub>v</sub></i> - 1. For a single plane
 *      configuration, this is equivalent to physical offset.<br/>
 * <b>Virtual plane</b> (<i>p</i>) - Index of the plane for the virtual offset.<br/>
 * <b>Virtual block</b> (<i>B<sub>v</sub></i>) - Block with a virtual number of pages per block
 *      (<i>q<sub>v</sub></i>). Used as the primary key for the NSSM index.<br/>
 * <b>Mapper key block</b> (<i>B<sub>k</sub></i>) - This is the block number passed to the mapper
 *      as the key to find a physical block address. It is simply the virtual block address plus
 *      the plane number, or <i>B<sub>v</sub></i> + <i>p</i>. There will be one key block per plane,
 *      each associated with one physical block. Has <i>q</i> pages per block.<br/>
 * <b>Physical block</b> (<i>B<sub>p</sub></i>) - Address of actual block being written to or read
 *      from.<br/>
 * <b>Physical offset</b> (<i>O<sub>p</sub></i>) - Page offset being accessed within physical
 *      block.<br/>
 *
 * \par Equations:
 * <ol>
 * <li> <i>B<sub>l</sub></i> = <i>s</i> / (<i>N</i> * <i>q</i>) </li>
 * <li> <i>O<sub>l</sub></i> = <i>s</i> % (<i>N</i> * <i>q</i>) </li>
 * <li> <i>O<sub>v</sub></i> = NSSM(<i>O<sub>l</sub></i>) </li>
 * <li> <i>p</i> = <i>O<sub>v</sub></i> % <i>N</i> </li>
 * <li> <i>B<sub>v</sub></i> = <i>N</i> * <i>B<sub>l</sub></i> + <i>B<sub>r</sub></i> </li>
 * <li> <i>B<sub>k</sub></i> = <i>B<sub>v</sub></i> + <i>p</i> </li>
 * <li> <i>B<sub>p</sub></i> = Mapper(<i>B<sub>k</sub></i>) </li>
 * <li> <i>O<sub>p</sub></i> = (<i>O<sub>v</sub></i> - <i>p</i>) / <i>N</i> </li>
 * </ol>
 *
 * \ingroup ddi_nand_data_drive
 */
class VirtualBlock : public BlockAddress
{
public:

    //! \brief Maximum number of planes supported by the data drive.
    static const unsigned kMaxPlanes = 2;
    
    //! \brief Constant for the first plane.
    static const unsigned kFirstPlane = 0;

    //! \brief Default constructor.
    VirtualBlock();

    //! \brief Constructor taking the mapper instance.
    VirtualBlock(Mapper * theMapper);
    
    //! \brief Copy constructor.
    VirtualBlock(const VirtualBlock & other);
    
    //! \brief Destructor.
    ~VirtualBlock();
    
    //! \brief Set mapper instance apart from constructor.
    void setMapper(Mapper * theMapper);
    
    //! \name Virtual address
    //@{
        //! \brief Set virtual block address explicitly.
        //!
        //! The cached physical block information is cleared, so it will be read from the
        //! mapper when next accessed.
        void set(const BlockAddress & address);
        
        //! \brief Assignment operator.
        //!
        //! Only the virtual block address is copied from \a other. The physical block information
        //! is cleared, so it will be read from the mapper when next accessed.
        VirtualBlock & operator = (const BlockAddress & other)
        {
            set(other);
            return *this;
        }
        
        //! \brief Assignment operator. Copies physical block address information.
        VirtualBlock & operator = (const VirtualBlock & other);
        
        //! \brief Set virtual block address from a region and logical sector within that region.
        //! \return The logical page offset into the block is returned.
        unsigned set(DataRegion * region, uint32_t logicalSectorInRegion);
    //@}
    
    //! \name Statistics
    //@{
        //! \brief Decide on how many planes to use based on NAND parameters.
        static void determinePlanesToUse();
        
        //! \brief Returns the number of planes in use.
        static unsigned getPlaneCount() { assert(s_planes); return s_planes; }
        
        //! \brief Return the pages in this virtual block.
        static unsigned getVirtualPagesPerBlock() { assert(s_virtualPagesPerBlock); return s_virtualPagesPerBlock; }
    //@}
    
    //! \name Allocated status
    //@{
        //! \brief Returns true if no planes have a physical block associated with them.
        bool isFullyUnallocated();
        
        //! \brief Returns true if all planes have an associated physical block.
        bool isFullyAllocated();
        
        //! \brief Test if a plane has a physical block allocated for it.
        bool isPlaneAllocated(unsigned thePlane);
        
        //! \brief Explicitly set the allocated state for a plane's physical block.
        void setPlaneAllocated(unsigned thePlane, bool isAllocated);
        
        //! \brief Returns true if all physical blocks are allocated and reside on a single NAND.
        bool isFullyAllocatedOnOneNand();
    //@}
    
    //! \name Physical blocks
    //@{
        //! \brief Get the physical block for a plane of this virtual block from the mapper.
        //! \retval SUCCESS The block address is a valid absolute physical address.
        //! \retval ERROR_DDI_NAND_MAPPER_INVALID_PHYADDR No physical block is yet associated
        //!     with the plane.
        RtStatus_t getPhysicalBlockForPlane(unsigned thePlane, BlockAddress & address);
        
        //! \brief Explicitly set the physical block address for a plane.
        void setPhysicalBlockForPlane(unsigned thePlane, const BlockAddress & address);
        
        //! \brief Allocate a new physical block for one plane of the virtual block.
        RtStatus_t allocateBlockForPlane(unsigned thePlane, BlockAddress & newPhysicalBlock);
        
        //! \brief Allocate physical blocks for every plane.
        //!
        //! This method will allocate a physical block for every plane of the virtual block. If a
        //! plane already has a physical block associated with it, a new block will be allocated
        //! but the original won't be deallocated. Thus, it is possible to cause conflicts if the
        //! NAND were left in such a state. However, this behaviour is also necessary for NSSMs
        //! to be able to have backup blocks.
        RtStatus_t allocateAllPlanes();
        
        //! \brief Erase and free the physical blocks for every plane.
        //!
        //! This does not actually disassociate the physical blocks from the virtual blocks. It
        //! just marks the physical blocks free in the phy map and erases them. The intended use
        //! is to free backup physical blocks.
        RtStatus_t freeAndEraseAllPlanes();
        
        //! \brief Dispose of cached physical addresses.
        void clearCachedPhysicalAddresses();
    //@}
    
    //! \name Virtual offset conversion
    //@{
        //! \brief Computes the plane index for a page offset into the virtual block.
        unsigned getPlaneForVirtualOffset(unsigned offset) const;
        
        //! \brief Convert a page offset into the virtual block into a real physical page address.
        //! \retval SUCCESS The page address is valid.
        //! \retval ERROR_DDI_NAND_MAPPER_INVALID_PHYADDR No physical block is yet associated
        //!     with the plane of the virtual offset.
        RtStatus_t getPhysicalPageForVirtualOffset(uint32_t virtualOffset, PageAddress & address);
        
        //! \brief Get the mapper key block number for a virtual offset.
        uint32_t getMapperKeyFromVirtualOffset(unsigned offset);
        
        //! \brief Convert a mapper key block back to a virtual block number.
        uint32_t getVirtualBlockFromMapperKey(uint32_t mapperKey);
    //@}

protected:

    static unsigned s_planes;              //!< Number of planes to actually use.
    static unsigned s_virtualPagesPerBlock;    //!< Pages per block for the virtual block.
    static unsigned s_planeMask;        //!< Mask to extract plane from virtual offset
    static unsigned s_planeShift;       //!< Shift to quickly divide page by plane
    static unsigned s_virtualPagesPerBlockShift; //!< Shift to convert page to virtual block
    static unsigned s_virtualPagesPerBlockMask;   //!< To compute LSI for given page offset
    
    Mapper * m_mapper;              //!< The mapper instance.

    /*!
     * \brief Information about an associated physical block for one plane.
     */
    struct PhysicalAddressInfo
    {
        BlockAddress m_address; //!< The physical block address.
        bool m_isCached;        //!< Whether the physical address has been set.
        bool m_isUnallocated;   //!< True if there is no physical block associated with the plane.
    };
    
    PhysicalAddressInfo m_physicalAddresses[kMaxPlanes];    //!< Cached physical addresses for each plane.
    
};

} // namespace nand

#endif // __virtual_block_h__
////////////////////////////////////////////////////////////////////////////////
// End of file
////////////////////////////////////////////////////////////////////////////////
