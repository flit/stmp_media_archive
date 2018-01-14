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
//! \file   circular_array.h
//! \brief  Definition of the CircularArray class.
////////////////////////////////////////////////////////////////////////////////
#if !defined(__circular_array_h__)
#define __circular_array_h__

#include "types.h"
#include "os/dmi/os_dmi_api.h"

///////////////////////////////////////////////////////////////////////////////
// Definitions
///////////////////////////////////////////////////////////////////////////////

/*!
 * \brief Array of some type.
 */
template <typename E>
class CircularArray
{
public:
    RtStatus_t init(unsigned maxCount)
    {
        m_entries = NULL;
        m_count = 0;
        reset();
        
        // Allocate entry array. Use DMI directly because malloc() is not in .init.text.
        RtStatus_t status = os_dmi_MemAlloc((void **)&m_entries, maxCount * sizeof(E), false, DMI_MEM_SOURCE_SDRAM);
        if (status != SUCCESS)
        {
            return NULL;
        }
        if (!m_entries)
        {
            return ERROR_GENERIC;
        }
        
        // Clear all the entries.
        memset(m_entries, 0, maxCount * sizeof(E));
        
        m_count = maxCount;
        
        return SUCCESS;
    }

    void cleanup()
    {
        if (m_entries)
        {
            free(m_entries);
        }
    }

    void insert(const E & newEntry)
    {
        if (m_entries)
        {
            m_entries[m_head] = newEntry;
            
            if (++m_head >= m_count)
            {
                m_head = 0;
                m_wrapCount++;
            }
        }
    }

    void reset()
    {
        m_head = 0;
        m_wrapCount = 0;
    }
    
protected:
    unsigned m_count;   //!< Total number of slots in #m_entries.
    unsigned m_head;    //!< The index of where to insert the next entry.
    E * m_entries;  //!< Array of entries #m_count long, allocated with malloc().
    unsigned m_wrapCount;  //!< Number of times the head wrapped from the end to the beginning. Useful to get a picture of the total number of accesses.
};


#endif // __circular_array_h__
///////////////////////////////////////////////////////////////////////////////
// EOF
///////////////////////////////////////////////////////////////////////////////
