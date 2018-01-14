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
//! \file ZoneMapSectionPage.h
//! \brief Class to wrap a section of the zone map.
///////////////////////////////////////////////////////////////////////////////
#if !defined(_ZoneMapSectionPage_h_)
#define _ZoneMapSectionPage_h_

#include "Page.h"
#include "Mapper.h"

///////////////////////////////////////////////////////////////////////////////
// Definitions
///////////////////////////////////////////////////////////////////////////////

namespace nand
{

//! \name STMP codes
//@{
    //! \brief Metadata STMP code value for zone map pages.
    #define LBA_STRING_PAGE1            (('L'<<24)|('B'<<16)|('A'<<8)|'M')

    //! \brief Metadata STMP code value for phys map pages.
    #define PHYS_STRING_PAGE1          (('E'<<24)|('X'<<16)|('M'<<8)|'A')
//@}

//! \name Map section header constants
//@{
    //! \brief Signature shared by all map types, used to identify a valid map header.
    const uint32_t kNandMapHeaderSignature = 'xmap';

    //! \brief Unique signature used for the zone map.
    const uint32_t kNandZoneMapSignature = 'zone';

    //! \brief Unique signature used for the phy map.
    const uint32_t kNandPhysMapSignature = 'phys';

    //! \brief Current version of the map header.
    //!
    //! The low byte is the minor version, all higher bytes form the major version.
    //!
    //! Version history:
    //! - Version 1.0 was the original map section format that had a very basic two-word "header"
    //! with no signature.
    //! - Version 2.0 is the first version with a real header.
    const uint32_t kNandMapSectionHeaderVersion = 0x00000200;
//@}

/*!
 * \brief Header for zone and phy maps when stored on the NAND.
 */
struct NandMapSectionHeader
{
    uint32_t signature;  //!< Common signature for all map types.
    uint32_t mapType;  //!< 'zone' or 'phys'
    uint32_t version;    //!< Version of this header structure, see #kNandMapSectionHeaderVersion.
    uint32_t entrySize;  //!< Size in bytes of each entry.
    uint32_t entryCount; //!< Total number of entries in this section.
    uint32_t startLba;   //!< LBA for the first entry in this section.
};

//! Type for the map section header.
typedef struct NandMapSectionHeader NandMapSectionHeader_t;

/*!
 * \brief Represents one section of a zone map.
 *
 * This class works for both the virtual to physical map (zone map) as well as the physical
 * allocation map (phy map). Be sure to set the map type with the #setMapType() method after
 * you create and instance of this class.
 *
 * You can use a ZoneMapSectionPage to either read or write pages from a map block. When reading,
 * this class is useful to help parse and validate the section page header. For writing,
 * the writeSection() method can fill in the header and help compute sizes and offsets. Before you
 * can do any reading or writing, remember to specify the buffers to use either explicitly or
 * by calling #allocateBuffers().
 *
 * To use the class for reading a section page, you only need to set the map type with a call
 * to setMapType(). If you wish to use getSectionNumber(), then you will also need to set
 * the map entry size in bytes by calling setEntrySize(). Once the object is configured, use
 * the superclass's #read() method to actually read the page. After the read completes, you can
 * access the section header with the #getHeader() and related getX methods. You should call
 * #validateHeader() to ensure that the section that was just read is valid.
 *
 * To write a section page by way of the writeSection() method, call setEntrySize(),
 * setMetadataSignature(), and setMapType() after instatiating the object. writeSection() is
 * intended to be used in a loop, though it can just as easily be used for a single write.
 * 
 */
class ZoneMapSectionPage : public Page
{
public:
    //! \name Construction and destruction
    //@{
        //! \brief Default constructor.
        ZoneMapSectionPage();

        //! \brief Constructor taking a page address.
        ZoneMapSectionPage(const PageAddress & addr);
        
        //! \brief Assignment operator.
        ZoneMapSectionPage & operator = (const PageAddress & addr);
    //@}
    
    //! \name Configuration
    //@{
        //! \brief Specify the entry  size in bytes.
        void setEntrySize(unsigned entrySize) { m_entrySize = entrySize; }

        //! \brief Set the signature value for the section page metadata.
        void setMetadataSignature(uint32_t sig) { m_metadataSignature = sig; }

        //! \brief Set the map type signature.
        void setMapType(uint32_t theType) { m_mapType = theType; }

        //! \brief Computes the number of entries that will fit in a section page.
        unsigned getMaxEntriesPerPage() { return (getDataSize() - sizeof(NandMapSectionHeader_t)) / m_entrySize; }
    //@}
    
    //! \name Section information
    //@{
        //! \brief Get the header structure for the page.
        NandMapSectionHeader_t * getHeader() { return m_header; }
        
        //! \brief Get a pointer to the section entry data.
        uint8_t * getEntries() { return m_sectionData; }
        
        //! \brief Returns the section number for this section.
        uint32_t getSectionNumber();
        
        //! \brief Returns the starting LBA.
        uint32_t getStartLba() { return m_header->startLba; }
        
        //! \brief Returns the entry count.
        uint32_t getEntryCount() { return m_header->entryCount; }
        
        //! \brief Validates the header contents.
        bool validateHeader();
    //@}
    
    //! \brief Write one page of the map block.
    //!
    //! This method intended to be used in a loop, though it can just as easily be used for a
    //! single write of a map section.
    //!
    //! \param startingEntryNum
    //! \param remainingEntries
    //! \param startingEntry
    //! \param[out] actualNumEntriesWritten
    //! \return Either #SUCCESS or an error code.
    RtStatus_t writeSection(
        uint32_t startingEntryNum,
        uint32_t remainingEntries,
        uint8_t * startingEntry,
        uint32_t * actualNumEntriesWritten);


protected:
    NandMapSectionHeader_t * m_header;  //!< The header of the section.
    uint8_t * m_sectionData;    //!< Pointer to the start of the section data in the page buffer.
    unsigned m_entrySize;   //!< Size in bytes of each entry.
    uint32_t m_metadataSignature;   //!< The signature set in the metadata of section pages.
    uint32_t m_mapType;     //!< Map type signature as used in the section page header.

    //! \brief Specify the buffers to use for reading and writing.
    virtual void buffersDidChange();
};

} // namespace nand

#endif // _ZoneMapSectionPage_h_

////////////////////////////////////////////////////////////////////////////////
// End of file
////////////////////////////////////////////////////////////////////////////////
//! @}
