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
//! \file PersistentPhyMap.h
//! \brief Declaration of the persistent phy map class.
////////////////////////////////////////////////////////////////////////////////
#if !defined(__persistent_phy_map_h__)
#define __persistent_phy_map_h__

#include "PersistentMap.h"

///////////////////////////////////////////////////////////////////////////////
// Definitions
///////////////////////////////////////////////////////////////////////////////

namespace nand {

class Mapper;
class PhyMap;

/*!
 * \brief Handles storage of a PhyMap on the NAND.
 */
class PersistentPhyMap : public PersistentMap
{
public:

    //! \brief Constructor.
    PersistentPhyMap(Mapper & mapper);
    
    //! \brief Destructor.
    virtual ~PersistentPhyMap();
    
    //! \brief Initializer.
    void init();
    
    //! \brief Finds and loads the map.
    RtStatus_t load();
    
    //! \brief Saves the map into the current block, consolidating if necessary.
    RtStatus_t save();
    
    //! \brief Allocates a new block and writes the map to it.
    RtStatus_t saveNewCopy();
    
    PhyMap * getPhyMap();
    void setPhyMap(PhyMap * theMap);

protected:
    PhyMap * m_phymap;  //!< The map that is being persisted.
    bool m_isLoading;   //!< True if we're in the middle of loading the phymap.

    virtual RtStatus_t getSectionForConsolidate(
        uint32_t u32EntryNum,
        uint32_t thisSectionNumber,
        uint8_t *& bufferToWrite,
        uint32_t & bufferEntryCount,
        uint8_t * sectorBuffer);
};

} // namespace nand

#endif // __persistent_phy_map_h__
////////////////////////////////////////////////////////////////////////////////
// End of file
////////////////////////////////////////////////////////////////////////////////
//! @}
