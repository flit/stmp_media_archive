////////////////////////////////////////////////////////////////////////////////
//! \addtogroup os_resource
//! @{
//
// Copyright (c) 2004-2008 SigmaTel, Inc.
//
//! \file    os_resource_api.c
//! \brief   Contains functionality to open and load a resource.
//! \version 1.0 (base ver num, not incremented since)
//! \date    23-June 2005
////////////////////////////////////////////////////////////////////////////////

#include "os_resource_internal.h"
#include "drivers/media/cache/media_cache.h"

//
//! \brief Opens a resource and returns a File Handle to it.
//! 
//! \param[in] ResourceID    ID for the resource to open (from resource.h)
//! \param[out] ResourceSize Pointer to uint32_t that will be filled with the resource size
//! \param[out] ResourceValue  The value of a resource if the resource is of RESOURCE_TYPE_VALUE (can be NULL)
//! 
//! \retval Resource Handle if success
//! \retval -1 if error
int32_t os_resource_Open(uint32_t ResourceID,uint32_t* ResourceSize, uint16_t* ResourceValue){
    int32_t retVal;
    tx_mutex_get(&g_rsc_Globals.ResourceCacheMutex, TX_WAIT_FOREVER);
    retVal = os_resource_OpenInternal(ResourceID,ResourceSize,ResourceValue);
    tx_mutex_put(&g_rsc_Globals.ResourceCacheMutex);
    return retVal;
}

#ifndef FOR_INTERNAL_USE_ONLY
//
//! \brief Opens a resource and returns a File Handle to it.
//! 
//! \param[in] ResourceID    ID for the resource to open (from resource.h)
//! \param[out] ResourceSize Pointer to uint32_t that will be filled with the resource size
//! \param[out] ResourceValue  The value of a resource if the resource is of RESOURCE_TYPE_VALUE (can be NULL)
//! \note       This function is internal to the resource manager sub-system and should not be called directly
int32_t os_resource_OpenInternal(uint32_t ResourceID,uint32_t* ResourceSize, uint16_t* ResourceValue){
    os_resource_ResourceId_t Resource;
    int8_t i, loopCounter = 0;
    int32_t ResourceHandle;
    uint32_t Index, TempIndexUsage = 3; 
    int32_t IndexUsage;
    uint32_t IndexMask = TI1_INDEX_MASK;
    ResourceTableEntry_t Offsets[4];
    ResourceTableEntry_t TableEntry, ResourceOffset;
    uint32_t CurrentTablePos = RSRC_SECTOR_SIZE;	//there is a one sector padding at the beginning of the resource System Drive
    int32_t     RetVal;
    
    if(!g_rsc_Globals.g_bResourceFileOpen)
        return ERROR_OS_FILESYSTEM_FILESYSTEM_NOT_FOUND;
    
    Resource.I = ResourceID;
    IndexUsage = Resource.IndexUsage;


    for(i = MAX_OFFSETS_TO_CACHE - 1; i >= 0; i--){
        Offsets[i].U = 0; //clear out all of the offsets    
    }

	//Clean Out the Resource Handle Table
    if(g_rsc_Globals.g_bResourceFileOpen){
	    for(i = 0; i < MAX_RESOURCES_OPEN; i++){
	        if(g_rsc_Globals.ResourceHandleTable[i].Allocated == 0)
		        break;
	    }
	}
	//Have we opened too many resources at once?
	if(i >= MAX_RESOURCES_OPEN){
        return ERROR_OS_FILESYSTEM_NO_FREE_HANDLE;
	}    
    // i contains the resource Handle offset Number
    ResourceHandle = i;
    //see if we have this resource ID somewhere in the Cache (PRT,SRT,TRT, or QRT)
    ResourceOffset.U = FindCachedResource(Resource, &TempIndexUsage);
    
    if(ResourceOffset.U){
        if(TempIndexUsage == Resource.IndexUsage){  //we found the exact place for the resource
            //we do not need to update the caches at all
#ifdef _RSRC_CACHE_PROFILING            
            g_NumDirectHits++;
#endif
            if(ResourceOffset.ResourceType == RESOURCE_TYPE_VALUE){
                *ResourceSize = 2;  //values can only be two in size!
                if(ResourceValue){
                    *ResourceValue = ResourceOffset.FileOffset; //get the value
                }
                return ERROR_OS_FILESYSTEM_RESOURCE_INVALID_VALUE_PTR;
            }
            else if(ResourceOffset.ResourceType == RESOURCE_TYPE_NESTED){
                *ResourceSize = sizeof(TableEntry) * 256;
            }
            else{
                *ResourceSize = ReadResourceSize(ResourceOffset.FileOffset);
                if(*ResourceSize == 0){
                    return ERROR_OS_FILESYSTEM_RESOURCE_SIZE_READ;
                }
            }

            g_rsc_Globals.ResourceHandleTable[ResourceHandle].begPos = ResourceOffset.FileOffset;
            g_rsc_Globals.ResourceHandleTable[ResourceHandle].curPos = ResourceOffset.FileOffset;
            if(ResourceOffset.ResourceType != RESOURCE_TYPE_NESTED){
                g_rsc_Globals.ResourceHandleTable[ResourceHandle].begPos += RSRC_SIZE_TYPE_SIZE;
                g_rsc_Globals.ResourceHandleTable[ResourceHandle].curPos += RSRC_SIZE_TYPE_SIZE;
            }
            g_rsc_Globals.ResourceHandleTable[ResourceHandle].size = *ResourceSize;
	        g_rsc_Globals.ResourceHandleTable[ResourceHandle].Allocated = true;         
            
            return ResourceHandle + RSRC_FILE_NUM_OFFSET;
        }
#ifdef _RSRC_CACHE_PROFILING
        g_NumPartialHits++;
#endif
        CurrentTablePos = ResourceOffset.FileOffset;
        //this means that we found part of the path to the resource but not the actual resource
        IndexUsage -= TempIndexUsage;    //how much of the path did we resolve
        IndexUsage--;    //so the mask will be correct
        TempIndexUsage = IndexUsage;  
    }

    IndexMask <<= IndexUsage * 8;
    TableEntry.ResourceType = RESOURCE_TYPE_NESTED; //so the loop will be entered appropriately
    IndexUsage++;

    while((TableEntry.ResourceType == RESOURCE_TYPE_NESTED) && IndexUsage){
        Index = Resource.I & INDEX_MASK;
        if(loopCounter){
            IndexMask >>= 8;    //we need to shift it over    
        }
        Index = Index & IndexMask;    //mask everything out but the correct index
        Index >>= ((IndexUsage-1) * 8);
        RetVal = ReadTableEntry(&TableEntry, CurrentTablePos, Index);
        if(RetVal != SUCCESS){
            //there was an error reading the TableEntry
            return RetVal;
        }
     
        if(TableEntry.ResourceType == 0){
            if(ResourceValue){
                *ResourceValue = 0xFFFF;
            }            
            ResourceHandle = ERROR_OS_FILESYSTEM_RESOURCE_INVALID_HANDLE;    //invalidate the handle
        }

        Offsets[IndexUsage-1] = TableEntry;
        if(TableEntry.ResourceType == RESOURCE_TYPE_VALUE){
            *ResourceSize = 2;
            if(ResourceValue){
                *ResourceValue = TableEntry.FileOffset; //get the value
            }
            ResourceHandle = ERROR_OS_FILESYSTEM_RESOURCE_INVALID_HANDLE;    //invalidate the handle
        }
        else{
            CurrentTablePos = TableEntry.FileOffset;    //go ahead and seek no matter what
            if(TableEntry.ResourceType == RESOURCE_TYPE_NESTED){
                *ResourceSize = sizeof(TableEntry) * 256;
            }
            else{ //we are at the resource. Err case: CurrentTablePos was too large, causing error. todo: qualify param first.SDK-2020
                *ResourceSize = ReadResourceSize(CurrentTablePos);
                if(*ResourceSize == 0){
                    return ERROR_OS_FILESYSTEM_RESOURCE_SIZE_READ;
                }
            }
        }
        IndexUsage--;
        loopCounter++;
    }
    
    //consider changing the below condition to if ResourceHandle > 0
    if(ResourceHandle != ERROR_OS_FILESYSTEM_RESOURCE_INVALID_HANDLE){
        g_rsc_Globals.ResourceHandleTable[ResourceHandle].begPos = TableEntry.FileOffset;
        g_rsc_Globals.ResourceHandleTable[ResourceHandle].curPos = TableEntry.FileOffset;            
        if(TableEntry.ResourceType != RESOURCE_TYPE_NESTED){
            g_rsc_Globals.ResourceHandleTable[ResourceHandle].begPos += RSRC_SIZE_TYPE_SIZE;
            g_rsc_Globals.ResourceHandleTable[ResourceHandle].curPos += RSRC_SIZE_TYPE_SIZE;
        }

        g_rsc_Globals.ResourceHandleTable[ResourceHandle].size = *ResourceSize;
	    g_rsc_Globals.ResourceHandleTable[ResourceHandle].Allocated = true;
        CacheResource(Resource.I, Offsets, TempIndexUsage);
    }
    
    //this may also indicate that a value resource was read.  Value resources cannot be opened only read.
    if(ResourceHandle < 0){
        return ERROR_OS_FILESYSTEM_RESOURCE_INVALID_HANDLE;
    }
    return ResourceHandle + RSRC_FILE_NUM_OFFSET;
}
#endif /* FOR_INTERNAL_USE_ONLY */

//
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
RtStatus_t os_resource_LoadResource(uint32_t ResourceID, void* pDest, uint32_t size, uint8_t ResourceType)
{
    uint32_t uResourceHandle;
    uint32_t ResourceSize;
    uResourceHandle = os_resource_Open(ResourceID, &ResourceSize, NULL);
    if(uResourceHandle)
    {
        if(Fread(uResourceHandle,(uint8_t*)pDest,size) != size)
        {
            Fclose(uResourceHandle);
            return ERROR_OS_FILESYSTEM_RESOURCE_LOAD;
        }
        Fclose(uResourceHandle);

    }
    else
    {
        return ERROR_OS_FILESYSTEM_RESOURCE_LOAD;
    }

    return SUCCESS;
}

#ifndef FOR_INTERNAL_USE_ONLY
//
//! \brief Finds a resource in the cache
//! 
//! \param[in] ResourceID    Pointer to the AgeArray for the resource table cache
//! \param[out] indexFound  Returned value which tells at which level of the cache the resource was found
//!
//!
//! \internal
//! \note This function is called from within the resource manager system and is not meant 
//!       to be called from the outside.
//! \note Upon returning, the callee should check indexFound to see if it matches the IndexUsage
//!            field of the resource trying to be loaded.  An age of zero means the cache line is unused
//!
uint32_t FindCachedResource(os_resource_ResourceId_t ResourceID, uint32_t* indexFound){
    RtStatus_t retVal;
    uint32_t Offset = 0;    //must be initialized to zero
    int8_t IndexUsage = ResourceID.IndexUsage;    //save it for later
    ResourceID.I &= INDEX_MASK;    //get rid of the index usage bits so they don't mess us up

    while(IndexUsage >= 0){
        //if this is a success then the item should move to the top of the cache.
        retVal = util_lru_FindCachedItem(&g_rsc_Globals.Caches[IndexUsage], &ResourceID.I, &Offset);
        if(retVal == SUCCESS){  //did we find the item in the cache
            *indexFound = IndexUsage;
        }
        if(Offset){
            break;    //get out of the while loop
        }
        ResourceID.I >>= 8;
        IndexUsage--;
    }
    
    return Offset;
}

//
//! \brief Adds a line to the cache
//! 
//! \param[in] ResourceID    Pointer to the AgeArray for the resource table cache
//! \param[out] indexFound  Returned value which tells at which level of the cache the resource was found
//!
//!
//! \internal
//! \note This function is called from within the resource manager system and is not meant 
//!       to be called from the outside.
//! \note Upon returning, the callee should check indexFound to see if it matches the IndexUsage
//!            field of the resource trying to be loaded.  An age of zero means the cache line is unused
//!
void CacheResource(uint32_t ResourceID, ResourceTableEntry_t* OffsetArray, int8_t PathResolved){
    os_resource_ResourceId_t Resource;
    int8_t Index;
    uint32_t EjectedCacheItem;
    Resource.I = ResourceID;
    Index = Resource.IndexUsage;
    Resource.I &= INDEX_MASK;

    while((Index >= 0) && (PathResolved >= 0)){
        //check to see if the item is already in the Cache or not
        if(util_lru_FindCachedItem(&g_rsc_Globals.Caches[Index], &Resource.I, OffsetArray) != SUCCESS){
            //it is not in the cache so add it
            util_lru_AddItemToCache(&g_rsc_Globals.Caches[Index], &Resource.I, OffsetArray, &EjectedCacheItem);
        }
        OffsetArray++;
        Resource.I >>= 8;
        PathResolved--;
        Index--;
    }
}
#endif /* FOR_INTERNAL_USE_ONLY */

//
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
RtStatus_t os_resource_LoadResourceValue(uint32_t ResourceID, uint16_t* pResourceValue){
    int32_t uResourceHandle;
    uint32_t ResourceSize;
    //even though os_resource_Open returns an error, ResoureValue gets populated with the correct value
    uResourceHandle = os_resource_Open(ResourceID,&ResourceSize,pResourceValue);
    if(uResourceHandle >= RSRC_FILE_NUM_OFFSET){
        //this is an error because we should not be able to open a RESOURCE_TYPE_VALUE resource
        Fclose(uResourceHandle);    //reclaim the handle from the lead byte Nested Table
        return ERROR_OS_FILESYSTEM_RESOURCE_LOAD;
    }
    else if(*pResourceValue == 0xFFFF){
        return ERROR_OS_FILESYSTEM_RESOURCE_LOAD;
    }

    return SUCCESS;
}

#ifndef FOR_INTERNAL_USE_ONLY
//
//! \brief Closes an open resource.
//!
//! \retval RESOURCE_SUCCESS
//! \retval RESOURCE_ERROR
//!
//! \note   This function frees up one resoure handle so it can be used by
//!         another part of the system.
RtStatus_t os_resource_Close(int32_t fno){
    RtStatus_t retVal = ERROR_OS_FILESYSTEM_HANDLE_NOT_ACTIVE;
    if((fno < RSRC_FILE_NUM_OFFSET) || (fno > RSRC_LAST_FILE_NUM_OFFSET))
        return ERROR_OS_FILESYSTEM_HANDLE_NOT_ACTIVE;    
    fno -= RSRC_FILE_NUM_OFFSET;
    tx_mutex_get(&g_rsc_Globals.ResourceCacheMutex, TX_WAIT_FOREVER);
    if( g_rsc_Globals.ResourceHandleTable[fno].Allocated && g_rsc_Globals.g_bResourceFileOpen)
    {
        g_rsc_Globals.ResourceHandleTable[fno].Allocated = 0;
        retVal = SUCCESS;
    }
    tx_mutex_put(&g_rsc_Globals.ResourceCacheMutex);
    return retVal;
}


int32_t os_resource_Read(int32_t fno,uint8_t *buf, int32_t size){
    int32_t retVal;
    tx_mutex_get(&g_rsc_Globals.ResourceCacheMutex, TX_WAIT_FOREVER);
    retVal = os_resource_ReadInternal(fno,buf,size);
    tx_mutex_put(&g_rsc_Globals.ResourceCacheMutex);
    return retVal;
}


//
//! \brief Reads an open resource.
//!
//! \retval RESOURCE_SUCCESS
//! \retval RESOURCE_ERROR
//!
//! \note   This function reads data from an open resource.
int32_t os_resource_ReadInternal(int32_t fno, uint8_t *buf, int32_t size)
{
    ResourceHandle_t *Handle;

    if ((fno < RSRC_FILE_NUM_OFFSET) || (fno > RSRC_LAST_FILE_NUM_OFFSET))
    {
        return ERROR_OS_FILESYSTEM_HANDLE_NOT_ACTIVE;
    }
    fno -= RSRC_FILE_NUM_OFFSET;
    
    if (!g_rsc_Globals.ResourceHandleTable[fno].Allocated || !g_rsc_Globals.g_bResourceFileOpen)
    {
        //__gh_set_errno(EBADF);	//if we need it thread safe
	    return ERROR_OS_FILESYSTEM_HANDLE_NOT_ACTIVE;
    }
    
    Handle = &g_rsc_Globals.ResourceHandleTable[fno];
    size = (((size) < ((Handle->size + Handle->begPos - Handle->curPos))) ? (size) : ((Handle->size + Handle->begPos - Handle->curPos)));

    if (size > 0)
    {
        const int first_sector = (Handle->curPos >> g_rsc_Globals.bytesPerSectorShift);
        const int last_sector = ((Handle->curPos + size) >> g_rsc_Globals.bytesPerSectorShift);

        int remaining = size;
        int sector;
        MediaCacheParamBlock_t pb = {0};
        
        // Set the unchanging fields in the read paramblock.
        pb.drive = g_rsc_Globals.g_uResourceSystemDrive;
        pb.mode = WRITE_TYPE_RANDOM;
        pb.requestSectorCount = 1;

        for (sector = first_sector; sector <= last_sector; sector++)
        {
            const int offset = (Handle->curPos & g_rsc_Globals.bytesPerSectorMask);
            int bytes = (((remaining) < ((g_rsc_Globals.bytesPerSector - offset))) ? (remaining) : ((g_rsc_Globals.bytesPerSector - offset)));
            
            ddi_ldl_push_media_task("os_resource_ReadInternal");
 
            // Adds in the sector offset to the start of the resource section of the .sb file.
            // Finish filling in the pb and do the read.
            pb.sector = sector + g_rsc_Globals.resourceSectionSectorOffset;
            if (media_cache_read(&pb) != SUCCESS)
            {
                ddi_ldl_pop_media_task();
                return ERROR_GENERIC;
            }
            
            ddi_ldl_pop_media_task();

            if (pb.buffer != NULL)
            {
                memcpy( buf, pb.buffer + offset, bytes );
            }
            else
            {
                media_cache_release(pb.token);
                return ERROR_GENERIC;
            }
            
            media_cache_release(pb.token);
            
            remaining -= bytes;
            buf += bytes;
        
            Handle->curPos += bytes;

            // update globals
            g_rsc_Globals.currentResourceSector = sector;
            g_rsc_Globals.currentResourceByteOffset = offset;
        }
    }
        
    return size;
}

//
//! \brief Reads an open resource.
//!
//! \retval RESOURCE_SUCCESS
//! \retval RESOURCE_ERROR
//!
//! \note   This function reads data from an open resource.
//! \todo   need to add checking so that the resource will not load past it's own boundary
RtStatus_t os_resource_Seek(int32_t fno, int32_t offset, int32_t end){
    
    ResourceHandle_t* Handle;
    if((fno < RSRC_FILE_NUM_OFFSET) || (fno > RSRC_LAST_FILE_NUM_OFFSET))
        return ERROR_OS_FILESYSTEM_HANDLE_NOT_ACTIVE;
    fno -= RSRC_FILE_NUM_OFFSET;
    if( !g_rsc_Globals.ResourceHandleTable[fno].Allocated || 
        !g_rsc_Globals.g_bResourceFileOpen)
    {
	    //set errno correctly
	    return ERROR_OS_FILESYSTEM_HANDLE_NOT_ACTIVE;
    }
    Handle = &g_rsc_Globals.ResourceHandleTable[fno];
    //we do not actually need to change the position of the
    //resource file since these are "virtual" file pointers
    switch(end){
	case SEEK_SET:	//<! seek from the beggining
	    if(offset >= Handle->size){
		    offset = Handle->size - 1;	//this will put it at the end
	    }
	    else if(offset < 0){
		    offset = 0;
	    }
	    Handle->curPos = Handle->begPos + offset;
	    break;
	case SEEK_CUR:	//<! seek from the current position
	    if((offset + Handle->curPos) >= (Handle->begPos + Handle->size)){	//overflows to the end
		    Handle->curPos = Handle->begPos + Handle->size - 1;	//delta between end and current position
	    }
	    else if((offset + Handle->curPos) < Handle->begPos)	//underflow
	    {
		    Handle->curPos = Handle->begPos;
	    }
	    else{
		    Handle->curPos += offset;
	    }
	    break;
	case SEEK_END:	//<! seek from the end
	    if(offset < 0){	//they should not be able to seek forward
		    if((Handle->curPos + offset) < Handle->begPos){
		        Handle->curPos = Handle->begPos;
		    }
		    else{
		        Handle->curPos = Handle->begPos + Handle->size - 1 + offset;
		    }
	    }
	    break;
	default:
	    break;
    }
    return SUCCESS;
}

//
//! \brief Reads the size of a resource.
//!
//! \retval Size of the resource
//!
//! \param[in] uint32_t ResourcePosition - absolute position of resource in resource file
//!
//! \note   This function reads the first 4 bytes from a resource and returns the 32-bit value.
//! \note   This function is internal to the resource manager sub-system and should not be called by external code
uint32_t ReadResourceSize(uint32_t ResourcePosition)
{
    uint8_t *buffer;
    uint32_t token;
    uint32_t resourceSize = 0;  //return a size of zero if there is an error
    buffer = (uint8_t *)ResourceSeekToPos(ResourcePosition, &token);
    if (buffer != NULL)
    {
        memcpy(&resourceSize,buffer + g_rsc_Globals.currentResourceByteOffset,4);
        media_cache_release(token);
    }
    return resourceSize;
}

//
//! \brief Reads a table entry from the resource file.
//!
//! \retval SUCCESS
//!
//! \param[out] ResourceTableEntry_t *TableEntry, 
//! \param[in]  uint32_t TablePos - absolute position of the table in the resource file
//! \param[in] uint16_t TableEntryNumber - table entry index [0,255]
//!
//! \note   This function is internal to the resource manager sub-system and should not be called by external code
RtStatus_t ReadTableEntry(ResourceTableEntry_t *TableEntry, uint32_t TablePos, uint16_t TableEntryNumber)
{
    RtStatus_t RetValue = SUCCESS;
    uint8_t *buffer;
    uint32_t token;
    (*TableEntry).U = 0;
    buffer = (uint8_t *)ResourceSeekToPos(TablePos + TableEntryNumber * sizeof(ResourceTableEntry_t), &token);
    if (buffer != NULL)
    {
        memcpy(TableEntry,buffer + g_rsc_Globals.currentResourceByteOffset, sizeof(ResourceTableEntry_t));
        media_cache_release(token);
    }
    else
    {
        RetValue = ERROR_GENERIC;
    }
    return RetValue;
}

//! \brief Reads in a sector from the resource file.
//!
//! The sector offset from the start of the .sb file holding the resource file is
//! added to the seek position in sectors in order to get the actual sector to read.
//!
//! \retval SUCCESS
//!
//! \param[in] ResourcePosition Absolute position in bytes to seek in the resource file.
//!
//! \note   This function is internal to the resource manager sub-system and should not be called by external code
uint8_t * ResourceSeekToPos(uint32_t ResourcePosition, uint32_t * token)
{
    MediaCacheParamBlock_t pb = {0};
    
    g_rsc_Globals.currentResourceSector = ( ResourcePosition >> g_rsc_Globals.bytesPerSectorShift );
    g_rsc_Globals.currentResourceByteOffset = ( ResourcePosition & g_rsc_Globals.bytesPerSectorMask );
    
    ddi_ldl_push_media_task("ResourceSeekToPos");
    
    //cache in the sector
    pb.drive = g_rsc_Globals.g_uResourceSystemDrive;
    pb.sector = g_rsc_Globals.currentResourceSector + g_rsc_Globals.resourceSectionSectorOffset;
    pb.requestSectorCount = 1;
    pb.mode = WRITE_TYPE_RANDOM;
    
    if (media_cache_read(&pb) != SUCCESS)
    {
        ddi_ldl_pop_media_task();
        return NULL;
    }
    
    ddi_ldl_pop_media_task();
    
    // Return the token so the caller can release the cache entry.
    *token = pb.token;
    
    return pb.buffer;
}
#endif /* FOR_INTERNAL_USE_ONLY */

////////////////////////////////////////////////////////////////////////////////
// End of file
////////////////////////////////////////////////////////////////////////////////
//! @}
