#include "types.h"
#include "error.h"

#include "drivers\ddi_media.h"
#include <stdio.h>
#include "objs\resource_file.h"
#include "..\..\..\drivers\media\include\ddi_media_internal.h"
#include "..\..\..\os\filesystem\resource\src\os_resource_internal.h"

#define RESOURCE_SECTOR_SIZE 2048

unsigned char *g_pResourceFile = g_ResourceFile;
uint64_t resourceFileSize;

RtStatus_t FindResourceSystemDrive(uint32_t wLogDriveNumber);
RtStatus_t CloseResourceSystemDrive(void);

uint32_t g_uSectorsRead = 0;
RtStatus_t DriveReadSectorA(uint32_t wLogDriveNumber, uint32_t dwSectorNumber,
    uint8_t *pSectorData)
{
	unsigned char *addrToRead;
    g_uSectorsRead++;
    if(wLogDriveNumber == 2 || wLogDriveNumber == 3){ //use 2 for now since we are stubbing...this will tell if it is the resource system drive
        addrToRead = (dwSectorNumber * RESOURCE_SECTOR_SIZE) + g_pResourceFile;
        memcpy(pSectorData, addrToRead, RESOURCE_SECTOR_SIZE);
        return SUCCESS;
    }    
    return ERROR_GENERIC;
}

int32_t FindDriveWithTag(uint32_t wTagForDrive)
{
    int32_t iDrive = -1;//default to error
	//cycle through all of the Drives until we find the correct TAG
    int32_t i;

    for(i=0;i<g_wNumDrives;i++)
    {
        if(g_Drive[i].u32Tag == wTagForDrive)
        {
            iDrive = i;
            break;
        }
    }

    return iDrive;
}

RtStatus_t CloseResourceSystemDrive(void){
    return SUCCESS;
}

bool IsDecoderStop(void)
{
    return(TRUE);
}
