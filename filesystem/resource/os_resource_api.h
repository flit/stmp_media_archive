////////////////////////////////////////////////////////////////////////////////
//! \addtogroup os_resource
//! @{
//
// Copyright (c) 2004-2005 SigmaTel, Inc.
//
//! \file os_resource_api.h
//! \brief Contains prototypes and defines for the Resource Manager subsystem.
//!
//! The resource manager subsystem is responsible for allowing access to the resource
//! file system.  This can be used to open and read resource stored in a hidden
//! section of the internal NAND Flash media.
////////////////////////////////////////////////////////////////////////////////
#ifndef __RSC_H
#define __RSC_H

////////////////////////////////////////////////////////////////////////////////
// Includes and external references
////////////////////////////////////////////////////////////////////////////////
#include "types.h"

////////////////////////////////////////////////////////////////////////////////
// Externs
////////////////////////////////////////////////////////////////////////////////


////////////////////////////////////////////////////////////////////////////////
// Definitions
////////////////////////////////////////////////////////////////////////////////

// Note, all 36xx system drives (including resources drives) will use 2K sector sizes
// so do not bother trying to tie resource sector size to NAND size.
#define RSRC_SECTOR_SIZE  2048        //!< Size of Resource sector memory.


//! \brief Controls how many resource handles are allocated to the Resource Manager.
//!
//! Decreasing this number lessens the number of resources that can be open at any 
//! given time, while increasing it increases that number.
#define MAX_RESOURCES_OPEN 10

// Resource Traversal Caches
// These sizes should be changed based on the resource file makeup 

//! \brief Controls the number of primary resource table entries that are cached 
//! by the Resource Manager. 
//!
//! Depending on the structure of a given resource file, 
//! this number should be decreased or increased depending on how many resources 
//! are at the depth of the primary resource table.
#define PRT_CACHE_SIZE 20
//! \brief Controls the number of secondary resource table entries that are cached by the 
//! Resource Manager.
//!
//! Depending on the structure of a given resource file, this number 
//! should be decreased or increased depending on how many resources are at the depth 
//! of the secondary resource table.
#define SRT_CACHE_SIZE 3
//! \brief Controls the number of tertiary resource table entries that will be cached 
//! by the Resource Manager.
//!
//! Depending on the structure of a given resource file, this 
//! number should be decreased or increased depending on how many resources are at the 
//! depth of the tertiary resource table.
#define TRT_CACHE_SIZE 30
//! \brief Controls the number of quaternary resource table entries that will be cached 
//! by the Resource Manager.
//!
//! Depending on the structure of a given resource file, this 
//! number should be decreased or increased depending on how many resources are at the 
//! depth of the quaternary resource table.
#define QRT_CACHE_SIZE 2

//! \brief Defines bitfields for resource IDs.
typedef union _os_resource_ResourceId_t{
    struct{
        //! \brief Table Index 1 used for traversing nested resource tables.
        uint32_t TI1 :8;
        //! \brief Table Index 2 used for traversing nested resource tables.
        uint32_t TI2 :8;
        //! \brief Table Index 3 used for traversing nested resource tables.
        uint32_t TI3 :8;
        //! \brief Table Index 4 used for traversing nested resource tables.
        uint32_t TI4 :6;
        //! \brief Value indicating which Table Indeces are used for this resource ID.  Determines level of nesting.
        uint32_t IndexUsage : 2;
    };
    //! \brief Accessor to the entire resource ID instead of the individual bit fields.
    uint32_t I;
} os_resource_ResourceId_t;


////////////////////////////////////////////////////////////////////////////////
// Prototypes
////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////
//! \brief Initializes the resource subsystem.
//!
//! \fntype Function
//!
//! This function clears out the resource handle table and 
//! tests to make sure the resource file exists/
//!
//! \param[in] wTag The section tag value within the firmware system drive.
//!
//! \return Success of failure of resource manager initialization.
//! \retval RESOURCE_SUCCESS
//! \retval RESOURCE_ERROR
//!
//! \internal
//! \see To view the function definition, see os_resource_init.c.
///////////////////////////////////////////////////////////////////////////////
int32_t os_resource_Init(uint32_t wTag);

///////////////////////////////////////////////////////////////////////////////
//
//! \brief Opens a resource and returns a File Handle to it.
//! 
//! \param[in]  ResourceID    ID for the resource to open (from resource.h)
//! \param[out] ResourceSize  Pointer to uint32_t that will be filled with the resource size
//! \param[out] ResourceValue Value of a resource if the resource is of RESOURCE_TYPE_VALUE (can be NULL)
//! 
//! \retval Resource Handle if success
//! \retval -1 if error
//!
//! \internal
//! \see To view the function definition, see os_resource_api.c..
///////////////////////////////////////////////////////////////////////////////
int32_t os_resource_Open(uint32_t ResourceID, uint32_t* ResourceSize, uint16_t* ResourceValue);

///////////////////////////////////////////////////////////////////////////////
//! \brief Loads a resource into a destination buffer
//! 
//! \param[in] ResourceID    ID for the resource to load (from resource.h)
//! \param[in] pDest        Destination buffer for the resource data
//! \param[in] size         Destination buffer size
//! \param[in] ResourceType The type of resource to be loaded
//!
//! \todo Return an error if the size of the resource is bigger than the destination
//!         buffer
//!
//! \note Currently the ResourceType is not used, but it could be used for erro checking
//!
//! \note If the resource is bigger than the destination buffer then it will not be loaded
//!            fully (we may return an error in the future)
//!
//! \internal
//! \see To view the function definition, see os_resource_api.c..
///////////////////////////////////////////////////////////////////////////////
RtStatus_t os_resource_LoadResource(uint32_t ResourceID, void* pDest, uint32_t size, uint8_t ResourceType);

///////////////////////////////////////////////////////////////////////////////
//! \brief Loads the value of a resource of type RESOURCE_TYPE_VALUE
//! 
//! \param[in] ResourceID    Resource ID for the RESOURCE_TYPE_VALUE resource
//! \param[out] pResourceValue The value of the Resource if successful
//!
//! \retval SUCCESS
//! \retval ERROR_OS_FILESYSTEM_RESOURCE_LOAD
//!
//! \note This function will return an error if a resource of any type other than
//!         RESOURCE_TYPE_VALUE is passed as ResourceID.
//!
//! \internal
//! \see To view the function definition, see os_resource_api.c.
///////////////////////////////////////////////////////////////////////////////
RtStatus_t os_resource_LoadResourceValue(uint32_t ResourceID, uint16_t* pResourceValue);


#endif  // __RSC_H
///////////////////////////////////////////////////////////////////////////////
// End of file
///////////////////////////////////////////////////////////////////////////////
//! @}
