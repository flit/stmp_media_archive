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
//! \defgroup ddi_media_nand_hal_internals
//! @{
//! \file   device_name_table.h
//! \brief  Definition of the NAND device name table support.
////////////////////////////////////////////////////////////////////////////////
#if !defined(__device_name_table_h__)
#define __device_name_table_h__

#include "types.h"

///////////////////////////////////////////////////////////////////////////////
// Definitions
///////////////////////////////////////////////////////////////////////////////

/*!
 * \brief Wraps a name table to provide parsing functionality.
 * 
 * A device name table is a sequence of opcodes and parameters that provides a flexible and
 * extensible mechanism for providing product names for NANDs. It is designed to take as
 * little memory space as possible. The table contains one or more values built up from the
 * #NandDeviceNameTable::_opcodes enumeration constants. Each entry must start with a command
 * opcode that may optionally have flags set on it. The command opcode type determines whether
 * there are any parameter words that follow it. Currently all commands have a single 
 * parameter word. If the #kNandDeviceNameOpcode_EndFlag flag is set on an opcode word, it
 * means that there are no further commands, though there may be following parameter words for
 * the final command. 
 * 
 * Name table opcodes are broken into three pieces within the 32-bit word. The top 16 bits 
 * contain a signature value (0xaa55), the least significant byte contains the command, 
 * and the second byte contains any flags. 
 * 
 * Opcode word fields:
 * <pre> 
 *    ffff0000 <- signature 
 *    0000ff00 <- flags 
 *    000000ff <- command 
 * </pre>
 *
 * The actual name table is itself declared as just an array of uint32_t values, where each
 * array entry is either an opcode or a parameter. The type #NandDeviceNameTable::TablePointer_t
 * should be used when referring to a name table. This class is intended as a wrapper class
 * that can perform operations on such a table. It also serves as a convenient namespace for
 * constants related to the name tables.
 * 
 * Use the DEVNAME_x preprocessor macros to help construct the table so that is easy to read.
 * The macros with "_END" on the name produce an entry with the end flag set in the opcode.
 * 
 * Here is an example device name table: 
 * \code 
 *     const uint32_t device_names[] = { 
 *             DEVNAME_1CE("MT29F32G08CBABA"), 
 *             DEVNAME_2CE_END("MT29F64G08CFABA") 
 *         }; 
 * \endcode 
 *
 * The above example will create a name table that can provide two different product names,
 * one for a single chip enable and one for two chip enables. The chip enable count here is
 * the \em total number of active NAND chip enables in the system, not the number of chip
 * enables per physical chip (which is not possible to discover, anyway).
 */
class NandDeviceNameTable
{
public:

    //! \brief Type for an entry in a name table.
    typedef const uint32_t TableEntry_t;

    //! \brief Type for a pointer to a name table.
    typedef TableEntry_t * TablePointer_t;
    
    //! \brief Function definition for custom device name function.
    //!
    //! The function must return a string allocated with malloc(), or it may return NULL if
    //! no name is available or there was an error.
    //!
    //! Upon entering the function, The \a table parameter points at the word in the device name
    //! table after the custom function address . This allows the function to read parameters
    //! from the table. If parameters are read, then the value pointed to by \a table must be
    //! updated appropriately.
    typedef char * (*CustomNameFunction_t)(TablePointer_t * table);

    //! \brief Device name table opcodes.
    enum _opcodes
    {
        //! Indicates that the next word is a custom name function matching the type
        //! #NandCustomDeviceNameFunction_t. If the function returns a valid string, then
        //! the name table will not be processed any further. Otherwise, if it returns
        //! NULL, then the table will continue to be examined for matching names.
        kOpcode_CustomFunction = 1L,
        
        //! The next word contains the device name for a 1 chip enable configuration.
        kOpcode_1CE = 2L,
        
        //! The next word contains the device name for a 2 chip enable configuration.
        kOpcode_2CE = 3L,
        
        //! The next word contains the device name for a 3 chip enable configuration.
        kOpcode_3CE = 4L,
        
        //! The next word contains the device name for a 4 chip enable configuration.
        kOpcode_4CE = 5L,
        
        //! Set this flag on another opcode to mark the last opcode in the table. Note that there
        //! may be additional words in the table if the final opcode has parameters.
        kOpcode_EndFlag = 0x00008000L,
        
        //! This constant masks the bits in the opcode word that contain the command type.
        kOpcode_CommandMask = 0x000000ffL,
        
        //! The top halfword of each opcode has a signature value that helps to distinguish a
        //! valid opcode.
        kOpcode_Signature = 0xaa550000L,
        
        //! Mask for the signature.
        kOpcode_SignatureMask = 0xffff0000L
    };

    //! \brief Constructor.
    inline NandDeviceNameTable(TablePointer_t table) : m_table(table) {}
    
    //! \brief Returns the device name for a given number of chip selects.
    char * getNameForChipSelectCount(unsigned chipSelectCount);

protected:
    TablePointer_t m_table; //!< The name table pointer.
};

//! \name Device name table helper macros
//@{
#define DEVNAME_OP(code) (NandDeviceNameTable::kOpcode_Signature | NandDeviceNameTable::kOpcode_##code)
#define DEVNAME_OP_END(code) (DEVNAME_OP(code) | NandDeviceNameTable::kOpcode_EndFlag)

#define DEVNAME_CUSTOM(fn) DEVNAME_OP(CustomFunction), (uint32_t)(fn)
#define DEVNAME_CUSTOM_END(fn) DEVNAME_OP_END(CustomFunction), (uint32_t)(fn)

#define DEVNAME_1CE(name) DEVNAME_OP(1CE), (uint32_t)(name)
#define DEVNAME_1CE_END(name) DEVNAME_OP_END(1CE), (uint32_t)(name)

#define DEVNAME_2CE(name) DEVNAME_OP(2CE), (uint32_t)(name)
#define DEVNAME_2CE_END(name) DEVNAME_OP_END(2CE), (uint32_t)(name)

#define DEVNAME_4CE(name) DEVNAME_OP(4CE), (uint32_t)(name)
#define DEVNAME_4CE_END(name) DEVNAME_OP_END(4CE), (uint32_t)(name)
//@}

#endif // __device_name_table_h__
//! @}
///////////////////////////////////////////////////////////////////////////////
// EOF
///////////////////////////////////////////////////////////////////////////////
