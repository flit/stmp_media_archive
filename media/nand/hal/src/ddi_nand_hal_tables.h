////////////////////////////////////////////////////////////////////////////////
//! \addtogroup ddi_media_nand_hal_internals
//! @{
//
// Copyright(C) SigmaTel, Inc. 2007
//
//! \file   ddi_nand_hal_common.h
//! \brief  Declarations from the NAND HAL common directory.
////////////////////////////////////////////////////////////////////////////////
#if !defined(_ddi_nand_hal_tables_h_)
#define _ddi_nand_hal_tables_h_

#include "ddi_nand_hal_internal.h"

////////////////////////////////////////////////////////////////////////////////
// Types
////////////////////////////////////////////////////////////////////////////////

//! \brief Describes pages in a type of NAND.
//!
//! Factoring out this information into a separate structure enables all NAND
//! types with the same sector characteristics to share the same instance of
//! this structure.
typedef struct {

    //! \brief The total size of a page, including both data and the redundant area.
    uint32_t wTotalSize;

    //! \brief Size in bytes of the data area of the page.
    uint32_t wDataSize;
    
    //! \brief Size of the metadata or redundant area of the page.
    //!
    //! \note This is the size for raw NAND. For BCH encoded pages the actual
    //!       size is in the ECC Descriptor.
    uint32_t pageMetadataSize;

} NandPageDescriptor_t;

//! \brief Defines a SigmaTel NAND type.
//!
//! This structure is a collection of pointers to other structures, each of
//! which describe some aspect of a SigmaTel "NAND type." Devices of the
//! same NAND type have equivalent behaviors and share other details in
//! common, but they may vary in their structural and/or timing
//! characteristics.
//!
//! \sa NandInitDescriptor_t
//! \sa NandDeviceCodeMap_t
typedef struct {

    //! Describes sectors in this NAND type.
    const NandPageDescriptor_t * pSectorDescriptor;

    //! Number of pages contained in each block of the NAND.
    uint16_t pagesPerBlock;
    
    uint8_t columnAddressBytes:4;   //!< Number of column address bytes.
    uint8_t rowAddressBytes:4;      //!< Number of row address bytes.
    
    //! The number that identifies this NAND type.
    NandType_t NandType;
    
    //! Type of memory cell in this device family.
    NandCellType_t cellType;
    
    //! Number of planes per die.
    uint16_t planesPerDie;

} NandTypeDescriptor_t;

//! \brief Describes a NAND device and associates it with a specific
//! combination of device and manufacturer codes.
//!
//! SigmaTel NAND types (#NandTypeDescriptor_t) describe classes of
//! devices that have equivalent behaviors and share other details in common,
//! but they may vary in their structural and/or timing characteristics.
//! This data structure joins timing information with structural properties (a
//! block and die count, etc.) and a pointer to a #NandTypeDescriptor_t to define a
//! unique device description. An array of these structs forms a lookup table
//! based on the manufacturer/device ID pair (DeviceManufacturerCode). So within
//! each such table, the DeviceManufacturerCode values must be unique.
//!
//! Typically there is as single table entry for a NAND family whose parts differ
//! only in the number of chip enables in the package. The instance of this struct
//! contains the total blocks and total dice for a single chip enable. This assumes that
//! the number of blocks and dice per chip enable are the same for each device (which
//! must be the case).
typedef struct {

    //! \brief The packed device and manufacturer codes.
    //!
    //!     - <code>[15: 8]</code> Device Code
    //!     - <code>[ 7: 0]</code> Manufacturer Code
    uint16_t DeviceManufacturerCode;

    //! \brief The number of die per chip select.
    uint16_t totalInternalDice;

    //! \brief The number of blocks per chip select.
    uint32_t totalBlocks;

    //! \brief The structure that defines the NAND type.
    const NandTypeDescriptor_t *  pNandDescriptorSubStruct;

    //! The timing characteristics for this device type.
    NAND_Timing2_struct_t NandTimings;

    //! Describes the error correction used by this NAND.
    const NandEccDescriptor_t * pEccDescriptor;
    
    //! Device name table.
    NandDeviceNameTable::TablePointer_t deviceNames;

} NandDeviceCodeMap_t;

////////////////////////////////////////////////////////////////////////////////
// Externs
////////////////////////////////////////////////////////////////////////////////

extern const NandDeviceCodeMap_t Type2DescriptorIdList[];
extern const NandDeviceCodeMap_t LargeMLCDescriptorIdList[];
extern const NandDeviceCodeMap_t Type7DescriptorIdList[];
extern const NandDeviceCodeMap_t Type8DescriptorIdList[];
extern const NandDeviceCodeMap_t Type9DescriptorIdList[];
extern const NandDeviceCodeMap_t Type10DescriptorIdList[];
extern const NandDeviceCodeMap_t Type11DescriptorIdList[];
extern const NandDeviceCodeMap_t Type15DescriptorIdList[];
extern const NandDeviceCodeMap_t Type16DescriptorIdList[];
extern const NandDeviceCodeMap_t Type16DescriptorIdList_24nm[];
extern const NandDeviceCodeMap_t BchEcc12DescriptorIdList[];
extern const NandDeviceCodeMap_t BchEcc16DescriptorIdList[];

#endif // _ddi_nand_hal_tables_h_
//! @}
