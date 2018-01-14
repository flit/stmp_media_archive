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
//! \addtogroup ddi_nand_media
//! @{
//! \file DiscoveredBadBlockTable.h
//! \brief This file contains declaration of the DBBT class.
////////////////////////////////////////////////////////////////////////////////
#if !defined(__discoveredbadblocktable_h__)
#define __discoveredbadblocktable_h__

#include "types.h"
#include "drivers/media/buffer_manager/media_buffer.h"
#include "drivers/media/nand/rom_support/rom_nand_boot_blocks.h"
#include "ddi_nand_hal.h"
#include "DeferredTask.h"

////////////////////////////////////////////////////////////////////////////////
// Definitions
////////////////////////////////////////////////////////////////////////////////

namespace nand {

// Forward declaration
typedef union _BootBlockLocation BootBlockLocation_t;
class Media;

/*!
 * \brief Manages finding, reading, and writing the DBBT copies on the NAND.
 */
class DiscoveredBadBlockTable
{
public:

    //! \brief Types of tables found in the boot-block known as the "DBBT".
    //!
    //! The boot-block known as the DBBT can actually contain more than just
    //! locations of bad-blocks (i.e. the original "DBBT" information).
    //! This enumeration lists the contents.
    typedef enum _DbbtContent
    {
        kDBBT,  //!< \brief The discovered bad-blocks table.
        kBBRC   //!< \brief The bad-block region counts.
    } DbbtContent_t;
    

    //! \brief Default constructor.
    //!
    //! This constructor simply provides the object with the parent media instance, and inits
    //! all members to their default state.
    DiscoveredBadBlockTable(Media * nandMedia);
    
    //! \brief Destructor.
    ~DiscoveredBadBlockTable() {}
    
    //! \brief Tells the object to use buffers provided by the caller.
    void setBuffers(SECTOR_BUFFER * sectorBuffer, SECTOR_BUFFER * auxBuffer);

    //! \brief Look for a DBBT within a search area starting at the given location.
    RtStatus_t scan(uint32_t u32NAND, uint32_t *pu32DBBTPhysBlockAdd);

    //! \brief Erase all copies of the DBBT.
    RtStatus_t erase();

    //! \brief Write the DBBT with the current set of bad blocks.
    RtStatus_t save();

    //! \brief Returns the page offset within the DBBT block for the requested DBBT section.
    uint32_t getDbbtPageOffset(unsigned uChip, DbbtContent_t DbbtContent);

    //! \brief Returns a pointer to the bad block count word given a BBRC and region index.
    //!
    //! The caller passes in a pointer to a BBRC page of the DBBT and a region number. The BBRC
    //! must have already been read by the caller so that \a pBootBlockStruct points to valid
    //! data.
    //!
    //! \param pBootBlockStruct Pointer to a BBRC page that has already been read by the caller.
    //! \param uRegion The region number to get the count for.
    static uint32_t * getPointerToBbrcEntryForRegion(BootBlockStruct_t *pBootBlockStruct, unsigned uRegion);
    
    //! \brief Access to the layout structure.
    DiscoveredBadBlockStruct_t & getLayout() { return m_layout; }


protected:
    Media * m_media;    //!< The NAND logical media object.
    SectorBuffer m_sectorBuffer;    //!< Sector buffer object.
    AuxiliaryBuffer m_auxBuffer;    //!< Aux buffer object.
    DiscoveredBadBlockStruct_t m_layout;    //!< Page layout within the DBBT.

    //! \brief Write the entire DBBT page by page.
    RtStatus_t writeBadBlockTables();

    RtStatus_t writeOneBadBlockTable(BootBlockLocation_t & tableLocation);

    //! \brief Write the DBBT pages containing bad blocks.
    RtStatus_t writeChipsBBTable(BlockAddress & tableAddress);
    
    //! \brief Write the DBBT page with the bad block region counts.
    RtStatus_t writeBbrc(BlockAddress & tableAddress);
    
    //! \brief
    void fillInLayout();
    
    //! \brief Formats the sector buffer with the DBBT page for the given chip select.
    void fillDbbtPageForChip(uint32_t iChip);

    //! \brief Write a range of empty pages.
    RtStatus_t writeEmptyPages(BlockAddress & tableAddress, int startOffset, int endOffset);
    
    //! \brief Makes sure there are valid buffers available, allocating if necessary.
    RtStatus_t allocateBuffers();

};

/*!
 * \brief Task to write the DBBT to NAND.
 */
class SaveDbbtTask : public DeferredTask
{
public:
    
    //! \brief Constants for the block update task.
    enum _task_constants
    {
        //! \brief Unique ID for the type of this task.
        kTaskTypeID = 'dbbt',
        
        //! \brief Priority for this task type.
        kTaskPriority = 12
    };

    //! \brief Constructor.
    SaveDbbtTask();
    
    //! \brief Return a unique ID for this task type.
    virtual uint32_t getTaskTypeID() const;
    
    //! \brief Check for preexisting duplicate tasks in the queue.
    virtual bool examineOne(DeferredTask * task);
    

protected:

    //! \brief The task implementation.
    virtual void task();
};

} // namespace nand

#endif // __discoveredbadblocktable_h__
////////////////////////////////////////////////////////////////////////////////
// End of file
////////////////////////////////////////////////////////////////////////////////
//! @}
