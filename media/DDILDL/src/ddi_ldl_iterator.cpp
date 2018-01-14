///////////////////////////////////////////////////////////////////////////////
// Copyright (c) Freescale Semiconductor. All rights reserved.
// 
// Freescale Semiconductor
// Proprietary & Confidential
// 
// This source code and the algorithms implemented therein constitute confidential
// information and may comprise trade secrets of Freescale Semiconductor or its
// associates, and any use thereof is subject to the terms and conditions of the
// Confidential Disclosure Agreement pursual to which this source code was
// originally received.
////////////////////////////////////////////////////////////////////////////////
//! \addtogroup ddi_media
//! @{
//! \file ddi_ldl_util.c
//! \brief Utilities used by the logical drive layer.
////////////////////////////////////////////////////////////////////////////////

#include "types.h"
#include "ddi_media_internal.h"
#include "drivers/media/ddi_media.h"
#include "drivers/media/ddi_media_errordefs.h"
#include <stdlib.h>
#include "hw/core/vmemory.h"

////////////////////////////////////////////////////////////////////////////////
// Types
////////////////////////////////////////////////////////////////////////////////

/*!
 * \brief Internal drive iterator structure.
 */
struct OpaqueDriveIterator
{
    //! Index of the next drive to return the tag of.
    unsigned nextIndex;
};

////////////////////////////////////////////////////////////////////////////////
// Code
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
// See documentation in ddi_media.h
////////////////////////////////////////////////////////////////////////////////
__INIT_TEXT RtStatus_t DriveCreateIterator(DriveIterator_t * iter)
{
    // Allocate an iterator.
    DriveIterator_t newIter = (DriveIterator_t)malloc(sizeof(OpaqueDriveIterator));
    if (newIter == NULL)
    {
        return ERROR_GENERIC;
    }
    
    // Set up iterator.
    newIter->nextIndex = 0;
    
    // Return the new iterator to the caller.
    *iter = newIter;
    return SUCCESS;
}

////////////////////////////////////////////////////////////////////////////////
// See documentation in ddi_media.h
////////////////////////////////////////////////////////////////////////////////
__INIT_TEXT RtStatus_t DriveIteratorNext(DriveIterator_t iter, DriveTag_t * tag)
{
    assert(iter);
    
    // Return the tag for this index to the caller and increment the iterator's index
    // for the next time through.
    while (iter->nextIndex < MAX_LOGICAL_DRIVES)
    {
        LogicalDrive * drive = g_ldlInfo.m_drives[iter->nextIndex++];
        
        // Skip drives that do not have a valid API table set.
        if (drive)
        {
            *tag = drive->getTag();
            break;
        }
    }
    
    // Check if all drives have been returned through this iterator.
    if (iter->nextIndex >= MAX_LOGICAL_DRIVES)
    {
        return ERROR_DDI_LDL_ITERATOR_DONE;
    }
    
    return SUCCESS;
}

////////////////////////////////////////////////////////////////////////////////
// See documentation in ddi_media.h
////////////////////////////////////////////////////////////////////////////////
__INIT_TEXT void DriveIteratorDispose(DriveIterator_t iter)
{
    if (iter)
    {
        free(iter);
    }
}

//! @}


