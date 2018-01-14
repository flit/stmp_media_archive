#include <stdio.h>
#include "..\..\src\os_resource_internal.h"
#include "player_resources.h"
#include "os\tx_api.h"
#include "components\lru.h"
#include "os\fsapi.h"
#include "drivers\ddi_media.h"

extern WORD g_wNumDrives;

uint32_t g_uResourceDriveTag = DRIVE_TAG_RESOURCE_BIN;

int32_t TestIdCaching(void);
void CleanCache(void);



void decrypt_data()
{

}

void utf_TestThread_0( ULONG uLParm ){
    int32_t retVal, i;
    uint32_t resourceSize = 0;
    uint16_t resourceValue = 0;
    int32_t ResourceHandle = 0;
    //test resource manager initialization 
    retVal = MediaInit(0);
    if(retVal != 0){
	     printf("Media Initialization Failed");
	     return;
    }
    retVal = MediaDiscoverAllocation(0);
    if(retVal != 0){
        printf("Discover Allocation Error");
        return;
    }
    DriveInitAll();

	retVal = os_resource_Init(DRIVE_TAG_RESOURCE_BIN);
    if(retVal != SUCCESS){
	    printf("Initialization of resource manager failed with error code: %d\n", retVal);
	    return;
    }
    //at this point we know the resource file is open
    ResourceHandle = os_resource_Open(RSRC_ICON_VOL_00_BMP ,&resourceSize, &resourceValue);
    if(ResourceHandle < 0){
	    printf("Error opening resource: RSRC_ICON_VOL_00_BMP\n");
	    return;
    }
    Fclose(ResourceHandle);
    ResourceHandle = os_resource_Open(RSRC_ICON_VOL_01_BMP ,&resourceSize, &resourceValue);
    Fclose(ResourceHandle);
    //open a resource that does not exist
    ResourceHandle = os_resource_Open(12,&resourceSize, &resourceValue);
    if(ResourceHandle >= 0){
		printf("Error.  False resource ID opened.\n");
    }

    //Test resource ID Caching
    retVal = TestIdCaching();
	if(retVal != 0){
        printf("Resource ID Caching failed.\n");
        return;
	}

    printf("Resource Manager Unit Test Passed.\n");
    while(1);
    return;
}

int32_t TestIdCaching(void){
    uint32_t resourceSize = 0;
    uint16_t resourceValue = 0;    
    uint32_t ResourceHandle = 0;
    int8_t i;
    CleanCache();
    //open up two resource 6 times.  This should only allocate two places in the cache.
    for(i=0;i<6;i++){
		ResourceHandle = os_resource_Open(RSRC_ICON_VOL_00_BMP + i%2 ,&resourceSize, &resourceValue);
		Fclose(ResourceHandle);
    }    
	if(g_rsc_Globals.Caches[0].u16CacheActiveEntries != 2){
		return -1;
	}

    return 0;
}

void CleanCache(void){
    int8_t i;
    //clear out all of the ages and cache information
    g_rsc_Globals.Caches[0].u16CacheActiveEntries = 0;
    for(i=0;i<PRT_CACHE_SIZE;i++){
        g_rsc_Globals.g_PRTCache[i] = 0;
        g_rsc_Globals.g_PRTCacheOffsets[i].U = 0;
    }
    g_rsc_Globals.Caches[1].u16CacheActiveEntries = 0;
    for(i=0;i<SRT_CACHE_SIZE;i++){
        g_rsc_Globals.g_SRTCache[i] = 0;
        g_rsc_Globals.g_SRTCacheOffsets[i].U = 0;
    }
    g_rsc_Globals.Caches[2].u16CacheActiveEntries = 0;
    for(i=0;i<TRT_CACHE_SIZE;i++){
        g_rsc_Globals.g_TRTCache[i] = 0;
        g_rsc_Globals.g_TRTCacheOffsets[i].U = 0;
    }
    g_rsc_Globals.Caches[3].u16CacheActiveEntries = 0;
    for(i=0;i<QRT_CACHE_SIZE;i++){
        g_rsc_Globals.g_QRTCache[i] = 0;
        g_rsc_Globals.g_QRTCacheOffsets[i].U = 0;
    }
}


UINT    _txe_mutex_create(TX_MUTEX *mutex_ptr, CHAR *name_ptr, UINT inherit){
    return 0;
}

UINT    _txe_mutex_put(TX_MUTEX *mutex_ptr){
    return 0;
}

UINT    _txe_mutex_get(TX_MUTEX *mutex_ptr, ULONG wait_option){
    return 0;
}

RtStatus_t Fclose_FAT(int32_t HandleNumber){
    return ERROR_OS_FILESYSTEM_FILESYSTEM_NOT_FOUND;
}

int32_t Fread_FAT(int32_t HandleNumber, uint8_t *Buffer, int32_t NumBytesToRead){
    return ERROR_OS_FILESYSTEM_FILESYSTEM_NOT_FOUND;
}

int32_t Fwrite_FAT(int32_t HandleNumber, uint8_t *Buffer, int32_t NumBytesToWrite){
    return ERROR_OS_FILESYSTEM_FILESYSTEM_NOT_FOUND;    
}

RtStatus_t Fseek_FAT(int32_t HandleNumber, int32_t NumBytesToSeek, int32_t SeekPosition){
    return ERROR_OS_FILESYSTEM_FILESYSTEM_NOT_FOUND;
}
