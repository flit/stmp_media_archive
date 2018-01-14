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
//! \file   device_name_table.cpp
//! \brief  Definition of the NAND device name table support.
////////////////////////////////////////////////////////////////////////////////

#include "device_name_table.h"
#include <stdlib.h>
#include <string.h>

///////////////////////////////////////////////////////////////////////////////
// Code
///////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
//! Parses a device name table and returns the correct name.
//!
//! \param chipSelectCount Number of chip selects. Must be greater than zero.
////////////////////////////////////////////////////////////////////////////////
char * NandDeviceNameTable::getNameForChipSelectCount(unsigned chipSelectCount)
{
    assert(m_table);
    assert(chipSelectCount > 0);
    
    TablePointer_t table = m_table;
    const char * tempName = NULL;
    char * resultName;
    bool done = false;
    while (!done)
    {
        uint32_t opcode = *table++;
        
        // Check the opcode's signature to make sure this is a valid opcode.
        if ((opcode & kOpcode_SignatureMask) != kOpcode_Signature)
        {
            return NULL;
        }
        
        uint32_t command = opcode & kOpcode_CommandMask;
        switch (command)
        {
            case kOpcode_CustomFunction:
            {
                CustomNameFunction_t fn = reinterpret_cast<CustomNameFunction_t>(*table++);
                resultName = fn(&table);
                
                // If the function returns a string then return it directly to our caller.
                // Otherwise keep processing the table.
                if (resultName)
                {
                    return resultName;
                }
                break;
            }
                
            case kOpcode_1CE:
            case kOpcode_2CE:
            case kOpcode_3CE:
            case kOpcode_4CE:
                tempName = reinterpret_cast<const char *>(*table++);
                done = (chipSelectCount == (command - kOpcode_1CE + 1));
                break;
            
            default:
                // Unknown command! Abort!
                return NULL;
        }
        
        // Exit loop if this was the last command.
        done |= (opcode & kOpcode_EndFlag);
    }
    
    // Copy the name from the table into a newly allocated string.
    if (tempName)
    {
        unsigned nameLength = strlen(tempName) + 1; // Includes the null byte.
        resultName = reinterpret_cast<char *>(malloc(nameLength));
        memcpy(resultName, tempName, nameLength);
        return resultName;
    }
    
    return NULL;
}

///////////////////////////////////////////////////////////////////////////////
// EOF
///////////////////////////////////////////////////////////////////////////////
