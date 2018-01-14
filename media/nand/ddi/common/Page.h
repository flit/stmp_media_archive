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
//! \file Page.h
//! \brief Class to wrap a page of a NAND.
///////////////////////////////////////////////////////////////////////////////
#if !defined(_ddi_nand_page_h_)
#define _ddi_nand_page_h_

#include "drivers/media/nand/hal/ddi_nand_hal.h"
#include "drivers/media/buffer_manager/media_buffer.h"
#include "Metadata.h"

///////////////////////////////////////////////////////////////////////////////
// Definitions
///////////////////////////////////////////////////////////////////////////////

namespace nand {

/*!
 * \brief Representation of one page of a NAND.
 */
class Page : public PageAddress
{
public:
    //! \name Init and cleanup
    //@{
    //! \brief Default constructor, inits to absolute page 0.
    Page();
    
    //! \brief Constructor taking a page address.
    explicit Page(const PageAddress & addr);
    
    //! \brief Constructor taking a block address.
    explicit Page(const BlockAddress & addr);
    
    //! \brief Copy constructor.
    explicit Page(const Page & other);
    
    //! \brief Assignment operator.
    Page & operator = (const Page & other);
    
    //! \brief Destructor.
    virtual ~Page();
    //@}
    
    //! \name Addresses
    //@{
    //! \brief Assignment operator.
    Page & operator = (const PageAddress & addr);
    
    //! \brief Assignment operator.
    Page & operator = (const BlockAddress & addr);
    
    //! \brief Change the address.
    void set(const PageAddress & addr);
    
    //! \brief Prefix increment operator to advance the page address to the next page.
    Page & operator ++ ();
    
    //! \brief Prefix decrement operator.
    Page & operator -- ();
    
    //! \brief Increment operator.
    Page & operator += (uint32_t amount);
    
    //! \brief Decrement operator.
    Page & operator -= (uint32_t amount);
    //@}
    
    //! \name Page sizes
    //@{
    //! \brief Returns the page's data size in bytes.
    inline unsigned getDataSize() const { return getNand()->pNANDParams->pageDataSize; }
    
    //! \brief Returns the full page size in bytes.
    inline unsigned getPageSize() const { return getNand()->pNANDParams->pageTotalSize; }
    
    //! \brief Returns the size of the page's metadata in bytes.
    inline unsigned getMetadataSize() const { return getNand()->pNANDParams->pageMetadataSize; }
    //@}
    
    //! \name Buffers
    //@{
    //! \brief Specify the buffers to use for reading and writing.
    virtual void setBuffers(SECTOR_BUFFER * pageBuffer, SECTOR_BUFFER * auxBuffer);
    
    //! \brief
    RtStatus_t allocateBuffers(bool page=true, bool aux=true);
    
    //! \brief Force early release of any buffers that were allocated.
    void releaseBuffers();
    
    //! \brief Returns the metadata wrapper object for this page.
    inline Metadata & getMetadata() { return m_metadata; }
    
    //! \brief Returns the page buffer.
    inline SECTOR_BUFFER * getPageBuffer() { return m_pageBuffer; }
    
    //! \brief Returns the auxiliary buffer.
    inline SECTOR_BUFFER * getAuxBuffer() { return m_auxBuffer; }
    //@}
    
    //! \name Operations
    //@{
    //! \brief Read the page into the provided buffer.
    virtual RtStatus_t read(NandEccCorrectionInfo_t * eccInfo=0);

    //! \brief Read the page's metadata into the provided auxiliary buffer.
    virtual RtStatus_t readMetadata(NandEccCorrectionInfo_t * eccInfo=0);

    //! \brief Write the page contents.
    virtual RtStatus_t write();
    
    //! \brief Write the page and mark the block bad if the write fails.
    RtStatus_t writeAndMarkOnFailure();
    
    //! \brief Check if the page is erased.
    bool isErased(RtStatus_t * status=NULL);
    //@}

protected:
    NandPhysicalMedia * m_nand;
    MediaBuffer m_pageBuffer;
    MediaBuffer m_auxBuffer;
    Metadata m_metadata;
    
    //! \brief Method to let subclasses know that buffers were changed.
    virtual void buffersDidChange();
};

/*!
 * \brief Represents either a firmware or boot block page.
 *
 * The primary difference between this class and its Page superclass is that
 * this one uses a different HAL API to write pages. Instead of using the normal
 * #NandPhysicalMedia::writePage(), it uses #NandPhysicalMedia::writeFirmwarePage().
 * In addition, it can optionally write the page as raw data, as is required for
 * certain boot pages such as the NCB on the STMP3780.
 */
class BootPage : public Page
{
public:
    //! \brief Default constructor.
    BootPage();

    //! \brief Constructor taking a page address.
    BootPage(const PageAddress & addr);

    //! \brief Write the page contents.
    virtual RtStatus_t write();
    
    //! \brief
    void setRequiresRawWrite(bool doRawWrite) { m_doRawWrite = doRawWrite; }

protected:
    bool m_doRawWrite;  //!< Whether the page must be written raw.
};

} // namespace nand

#endif // _ddi_nand_page_h_

////////////////////////////////////////////////////////////////////////////////
// End of file
////////////////////////////////////////////////////////////////////////////////
//! @}
