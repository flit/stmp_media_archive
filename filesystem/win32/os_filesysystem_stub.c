//#ifdef WIN32
//#ifndef WINVER				// Allow use of features specific to Windows 95 and Windows NT 4 or later.
//#define WINVER 0x0501		// Change this to the appropriate value to target Windows 98 and Windows 2000 or later.
//#endif
//#include <afxwin.h>

#include "windows.h"
#define WORD SDK_WORD
#define DWORD SDK_DWORD
#define BOOL SDK_BOOL
#define UINT64 SDK_UINT64
#define WCHAR SDK_WCHAR
#define FileSystemType SDKFileSystemType

#include <types.h>

#include <map>
#include <stdio.h>
#include <memory.h>
#include <direct.h>
#include "fcntl.h"
//extern int _fmode;
#include <errno.h>
#include <io.h>
#include <string.h>
#include <algorithm>
#ifndef CMI_PROJ
#include "..\os\filesystem\resource\src\os_resource_internal.h"
#endif
#include "..\drivers\media\cache\media_cache.h"
#include "..\components\sb_info\cmp_sb_info.h"
#include "..\components\sb_info\src\sb_format.h"

// 

//unsigned __int64 getDriveFreeSpace(unsigned int dirveNum);
//unsigned __int64 getDriveTotalSpace(unsigned int driveNum);
//do not use the handle returned by this function!!!
unsigned __int64 getDriveFreeSpace(unsigned int driveNum){
    unsigned uErr, uDrive  = 0;
    struct _diskfree_t df = {0};
    __int64 availSizeInBytes = 0;
    uErr = _getdiskfree(driveNum, &df);
    if(uErr == 0){
        //success
        availSizeInBytes = df.avail_clusters;
        availSizeInBytes *= df.sectors_per_cluster;
        availSizeInBytes *= df.bytes_per_sector;
        return availSizeInBytes;
    }
    else{
        return 0;
    }
}

unsigned __int64 getDriveTotalSpace(unsigned int driveNum){
    unsigned uErr, uDrive  = 0;
    struct _diskfree_t df = {0};
    __int64 totalSizeInBytes = 0;
    uErr = _getdiskfree(driveNum, &df);
    if(uErr == 0){
        //success
        totalSizeInBytes = df.total_clusters;
        totalSizeInBytes *= df.sectors_per_cluster;
        totalSizeInBytes *= df.bytes_per_sector;
        return totalSizeInBytes;
    }
    else{
        return 0;
    }
}


/*
RtStatus_t FastOpen(int64_t Key, uint8_t *mode)
{
    int32_t i32Retval;
    memcpy(&i32Retval, &Key, sizeof(int));
    return i32Retval;
}

int64_t FgetFastHandle(int32_t HandleNumber)
{
    int64_t i32Retval;

    i32Retval = (int64_t)HandleNumber;
    return i32Retval;
}
*/

REENTRANT RtStatus_t Fclose_FAT(int32_t HandleNumber){
    return ERROR_OS_FILESYSTEM_FILESYSTEM_NOT_FOUND;
}

REENTRANT int32_t Fread_FAT(int32_t HandleNumber, uint8_t *Buffer, int32_t NumBytesToRead){
    return ERROR_OS_FILESYSTEM_FILESYSTEM_NOT_FOUND;
}

REENTRANT int32_t Fwrite_FAT(int32_t HandleNumber, uint8_t *Buffer, int32_t NumBytesToWrite){
    return ERROR_OS_FILESYSTEM_FILESYSTEM_NOT_FOUND;    
}

REENTRANT RtStatus_t Fseek_FAT(int32_t HandleNumber, int32_t NumBytesToSeek, int32_t SeekPosition){
    return ERROR_OS_FILESYSTEM_FILESYSTEM_NOT_FOUND;
}

RtStatus_t Fremove(const uint8_t *filepath)
{
    if(remove((const char*)filepath)==-1)
        return -1;
    else
        return SUCCESS;
}

// 
int32_t Fread(int32_t HandleNumber, uint8_t *Buffer, int32_t NumBytesToRead)
{
    FILE *fp;
#ifndef CMI_PROJ
    if(HandleNumber >= RSRC_FILE_NUM_OFFSET && HandleNumber <= RSRC_LAST_FILE_NUM_OFFSET)
    {
        return os_resource_Read(HandleNumber, Buffer, NumBytesToRead);
    }
    else
#endif
    {
        fp = (FILE *)HandleNumber;
        return fread((void *)Buffer, sizeof( char ), NumBytesToRead, fp);
    }
}

int32_t Fwrite(int32_t HandleNumber, uint8_t *Buffer, int32_t NumBytesToWrite)
{
    FILE *fp;
    fp = (FILE *)HandleNumber;
    return fwrite((uint8_t *)Buffer, sizeof( char ), NumBytesToWrite, fp);
}

RtStatus_t Fseek(int32_t HandleNumber, int32_t NumBytesToSeek, int32_t SeekPosition)
{
    FILE *fp;
    RtStatus_t retval;
    
#ifndef CMI_PROJ
    if(HandleNumber >= RSRC_FILE_NUM_OFFSET && HandleNumber <= RSRC_LAST_FILE_NUM_OFFSET)
    {
        return os_resource_Seek(HandleNumber, NumBytesToSeek, SeekPosition);
    }
    else
#endif    
    
    
    fp= (FILE *)HandleNumber;

    retval = fseek( fp, NumBytesToSeek, SeekPosition);

    if(retval != 0) 
        retval = -1;

    return retval;
}

// 
int32_t Ftell(int32_t HandleNumber)
{
    FILE *fp;
    fp = (FILE *)HandleNumber;
    return ftell(fp);
}

int32_t GetFileSize(int32_t HandleNumber)
{
    FILE *fp;
    int32_t i32CurrPos, i32Size = 0, i32Count = 0;

    fp = (FILE *)HandleNumber;
    i32CurrPos = ftell(fp);

    fseek(fp,0, SEEK_END);
    i32Size = ftell(fp);
#if 0
    fseek(fp,0, SEEK_SET);
    while(!feof(fp))
    {
      // Attempt to read in 100 bytes
      i32Count = fread(cBuffer, sizeof(char), 100, fp);

      if(ferror(fp))
         break;

      // Total up actual bytes read 
      i32Size += i32Count;
   }
#endif

   fseek(fp, i32CurrPos, SEEK_SET);
   return i32Size;
}

// 
RtStatus_t Mkdir(uint8_t *filepath)
{
    RtStatus_t retVal;

    if((retVal =_mkdir((const char*)filepath))==SUCCESS)
    {
        retVal = SUCCESS; 
    }
    else if(errno == EEXIST )
    {   // Directoty already exists, check this 
        retVal = ERROR_OS_FILESYSTEM_DIRECTORY_IS_NOT_WRITABLE;

        // Need to handle all other errors....
    }
    
    else if(errno == ENOENT )
	{   // Path was not found
        retVal = ERROR_OS_FILESYSTEM_INVALID_DIR_PATH;     
    }
    return retVal;
}

RtStatus_t Rmdir(uint8_t *filepath)
{
    RtStatus_t retVal;

    if((retVal =_rmdir((const char*)filepath))==SUCCESS)
    {
        retVal = SUCCESS; 
    }
    else if(retVal == ENOTEMPTY )
    {   // Directoty not empty/directory current working directory/not a directory path 
        retVal = ERROR_OS_FILESYSTEM_DIR_NOT_EMPTY;
    }
    else if(retVal == ENOENT )
    {   // Path was not found
        retVal = ERROR_OS_FILESYSTEM_INVALID_DIR_PATH;     
    }
    else if(retVal == EACCES)
    {   // There is an open handle to this directory 
        retVal = ERROR_OS_FILESYSTEM_DIR_NOT_REMOVABLE;
    }
    return retVal;
}

RtStatus_t Chdir(uint8_t *filepath)
{
    if(_chdir((const char*)filepath)==SUCCESS)
        return SUCCESS;
    else 
        return ERROR_GENERIC;
}

RtStatus_t Chdirw(uint8_t *filepath)
{
	// this is wrong wrong wrong.
	if(_wchdir((const wchar_t*)filepath)==SUCCESS)
        return SUCCESS;
    else 
        return ERROR_GENERIC;
}

int32_t filegetdate(int32_t HandleNumber, int32_t crt_mod_date_time_para, DIR_DATE *dirdate, DIR_TIME *dirtime)
{
    return SUCCESS;
}

RtStatus_t Fflush(int32_t HandleNumber){
    FILE *fp = (FILE *) HandleNumber;
    return fflush(fp);
}

int32_t FSSize(int32_t DeviceNum, int32_t TYPE){
    unsigned __int64 sizeInBytes = getDriveTotalSpace(3);
    return ( (int32_t) sizeInBytes >> 20);
}
//returns the media free space in bytes
int64_t FSFreeSpace(int32_t Device)
{
    return getDriveFreeSpace(3);	//1 MB
}


RtStatus_t FlushCache(void)
{
    return SUCCESS;
}

//#endif

RtStatus_t FindClose(int32_t HandleNumber)
{
    _findclose(HandleNumber);
    return SUCCESS;
}

//*********************************************************************************
//*********************************************************************************
// from stub\ddildl-stub.c
//*********************************************************************************
//*********************************************************************************
#include "application\framework\sdk_os_media\app_sb_section_defs.h"
bool g_bFrameworkExternalDriveOrFsInit;
// Note, all 36xx system drives (including resources drives) will use 2K sector sizes
// so do not bother trying to tie resource sector size to NAND size.
#define RESOURCE_SECTOR_SIZE  RSRC_SECTOR_SIZE

int32_t g_pResourceFile;
uint64_t resourceFileSize;
char g_resourceFilename[256];

RtStatus_t FindResourceSystemDrive(uint32_t wLogDriveNumber);
RtStatus_t CloseResourceSystemDrive(void);

uint32_t g_uSectorsRead = 0;
RtStatus_t DriveReadSector(uint32_t wLogDriveNumber, uint32_t dwSectorNumber, uint8_t *pSectorData)
{
    g_uSectorsRead++;
    if(wLogDriveNumber == DRIVE_TAG_BOOTMANAGER_S){ //use 2 for now since we are stubbing...this will tell if it is the resource system drive
        Fseek(g_pResourceFile,0,SEEK_SET);
        if(resourceFileSize < (dwSectorNumber * RESOURCE_SECTOR_SIZE)){
            return ERROR_GENERIC;
        }
        else{
            Fseek(g_pResourceFile,dwSectorNumber * RESOURCE_SECTOR_SIZE,SEEK_SET);
            Fread(g_pResourceFile,pSectorData,RESOURCE_SECTOR_SIZE);
            return SUCCESS;
        }
    }    
    
    return SUCCESS;
}

RtStatus_t MediaFindDriveWithTag(uint32_t wTagForDrive)
{
	if(wTagForDrive == DRIVE_TAG_BOOTMANAGER_S){
		g_pResourceFile = Fopen((uint8_t*)g_resourceFilename, (uint8_t*)"r");
	}

	if(g_pResourceFile <= 0){
        resourceFileSize = 0;
        return -1;
    }
    Fseek(g_pResourceFile,0,SEEK_SET);
    resourceFileSize = Ftell(g_pResourceFile);
    Fseek(g_pResourceFile,0,SEEK_END);
    resourceFileSize = Ftell(g_pResourceFile) - resourceFileSize;   //get the size of the resource file
    //we may not need the next line either
    Fseek(g_pResourceFile,0,SEEK_SET);
    return wTagForDrive;
}

RtStatus_t DriveInit(uint32_t u32LogDriveNumber){
    return SUCCESS;
}


RtStatus_t CloseResourceSystemDrive(void){
    if(g_pResourceFile){
        Fclose(g_pResourceFile);
    }
    return SUCCESS;
}

RtStatus_t DriveGetInfo(DriveTag_t u32LogDriveNumber, uint32_t Type, void * pInfo)
{
	if( u32LogDriveNumber == DRIVE_TAG_BOOTMANAGER_S )
    {
		if( Type == kDriveInfoSectorSizeInBytes )
        {
            *(uint32_t *)pInfo = RSRC_SECTOR_SIZE;
        }
    }
    return SUCCESS;
}
uint8_t g_os_resource_Sector[RESOURCE_SECTOR_SIZE + 64];

RtStatus_t cmp_sb_info_GetSectionInfo(DriveTag_t driveTag, uint32_t u32SectionTag, sb_section_info_t *psSectionInfo)
{
    return SUCCESS;
}

int32_t * ReadSector(int32_t deviceNumber, int32_t sectorNumber,int32_t WriteType)
{
    g_uSectorsRead++;
    if(deviceNumber == DRIVE_TAG_BOOTMANAGER_S){ //use 2 for now since we are stubbing...this will tell if it is the resource system drive
        Fseek(g_pResourceFile,0,SEEK_SET);
        if(resourceFileSize < (sectorNumber * RESOURCE_SECTOR_SIZE)){
            return (int32_t *) NULL;
        }
        else{
            Fseek(g_pResourceFile,sectorNumber * RESOURCE_SECTOR_SIZE,SEEK_SET);
			return Fread(g_pResourceFile,g_os_resource_Sector,RESOURCE_SECTOR_SIZE) >= 0 ? (int32_t*)g_os_resource_Sector : (int32_t*) NULL ;
        }
    }    

	return (int32_t *) NULL;
}

RtStatus_t media_cache_read(MediaCacheParamBlock_t * pb)
{
    pb->buffer = (uint8_t *)ReadSector(pb->drive, pb->sector, pb->mode);
    return SUCCESS;
}

RtStatus_t media_cache_release(uint32_t token)
{
    return SUCCESS;
}

void EnterNonReentrantSection(void)
{
	return ;	// void
}
void LeaveNonReentrantSection(void)
{
	return ;	// void
}


//*********************************************************************************
//*********************************************************************************
// from stub\fsapi.c
//*********************************************************************************
//*********************************************************************************

typedef struct FastHandleMapEntry{
    std::string fullPath;
    int32_t fileHandle;
} FastHandleMapEntry_t;


//extern "C" database_RecordDescriptor_t Database[STOR_MAX_NUM_STORES][MAX_DATABASE_RECORDS];
std::map<int64_t, FastHandleMapEntry_t> FastHandleMap;

RtStatus_t Fclose(int32_t HandleNumber)
{
    FILE *fp;
    RtStatus_t retval;
#ifndef CMI_PROJ  
    if(HandleNumber >= RSRC_FILE_NUM_OFFSET && HandleNumber <= RSRC_LAST_FILE_NUM_OFFSET){
        retval = os_resource_Close(HandleNumber);
    }
    else
#endif
    {
        fp = (FILE*)HandleNumber;
        //remove mapEntries with Handle Number matching fp
        std::map<int64_t, FastHandleMapEntry_t>::iterator i;
        for( i = FastHandleMap.begin(); i != FastHandleMap.end(); i++){
            if(HandleNumber == i->second.fileHandle){
                i->second.fileHandle = 0;
                //FastHandleMap.erase(i);
                break;
            }
        }
        
        
        retval = (RtStatus_t) fclose(fp);
    }
    return retval;

}

RtStatus_t Fopen(uint8_t *filepath, uint8_t *mode)
{
	FILE *fp = NULL;
    int32_t i32Retval = -1;

	if (!strcmp((const char *) "r", (const char *) mode) ||
		!strcmp((const char *) "w", (const char *) mode) ||
		!strcmp((const char *) "a", (const char *) mode) ||
		!strcmp((const char *) "r+", (const char *) mode) ||
		!strcmp((const char *) "w+", (const char *) mode) ||
		!strcmp((const char *) "a+", (const char *) mode))   
	{

        
		_set_fmode(_O_BINARY);

        if ( fopen_s(&fp, (const char *) filepath, (const char *) mode) )
		{
			char buffer[80];
			_strerror_s(buffer, 80, NULL);
			i32Retval = -1;
		}
        else
		{
    		memcpy(&i32Retval, &fp, sizeof(int));
            
			//add file information to our fast open map
            FastHandleMapEntry_t mapEntry;
            char* tempPtr = NULL;
            char fullPath[1024];
            char longPath[1024];
            if(GetFullPathNameA((LPSTR)filepath, 1024, fullPath, &tempPtr)){
                GetLongPathNameA(fullPath, longPath, 1024);
                mapEntry.fileHandle = i32Retval;
                mapEntry.fullPath = longPath;
                int64_t nextKey = FastHandleMap.size();
                FastHandleMap[nextKey] = mapEntry;
            }
        }
	}
    return (RtStatus_t) i32Retval;
}

//extern "C" 
RtStatus_t FastOpen(int64_t Key, uint8_t *mode)
{
    std::map<int64_t, FastHandleMapEntry_t>::iterator i;
    i = FastHandleMap.find(Key);
    if(i == FastHandleMap.end()){
        return ERROR_GENERIC;
    }
    else{
        return Fopen((uint8_t *) i->second.fullPath.c_str(),mode);
    }
}

const char* GetKeyFullPath(int64_t Key)
{
    std::map<int64_t, FastHandleMapEntry_t>::iterator i;
    i = FastHandleMap.find(Key);
    
    return i->second.fullPath.c_str();
}

//returns the fast handle for an open file if it is in the Fast Map.
//extern "C" 
int64_t FgetFastHandle(int32_t HandleNumber)
{
    int64_t Key = 0;
    std::map<int64_t, FastHandleMapEntry_t>::iterator i;
    for( i = FastHandleMap.begin(); i != FastHandleMap.end(); i++){
        if(HandleNumber == i->second.fileHandle){
            Key = i->first;
            break;
        }
    }
    return Key;
}

//extern "C" 
RtStatus_t FindFirst(FindData_t *_finddata, uint8_t *FileName)
{
	long hFile;
    struct _finddata_t FindData;
    
    // Find first folder\file in current directory
	if((hFile = _findfirst((const char *)FileName, &FindData))==-1L)
    {   // No files\folders in this directory
        return ERROR_GENERIC;
    }
    else
    {   
        char fullPath[1024];
        char fullShortPath[1024];
        char* tempPtr = NULL;
        int64_t nextKey = FastHandleMap.size();
        //add the path to the FastHandleMap
        if(GetFullPathNameA(FindData.name, 1024, fullPath, &tempPtr)){
            //check if the path is already in the map and don't add it again if it is
            std::map<int64_t, FastHandleMapEntry_t>::iterator i;
            for( i = FastHandleMap.begin(); i != FastHandleMap.end(); i++){
                if(0 == strcmp(fullPath, i->second.fullPath.c_str())){
                    break;
                }
            }
            if(GetShortPathNameA(FindData.name, fullShortPath, 1024)){
                if(i == FastHandleMap.end()){
                    FastHandleMapEntry_t mapEntry;
                    mapEntry.fileHandle = 0;
                    mapEntry.fullPath = fullPath;
                    FastHandleMap[nextKey] = mapEntry;
                    _finddata->Key = nextKey;
                }
                else{
                    _finddata->Key = i->first;
                }
                
                strcpy_s((char *)_finddata->name, MAX_FILESNAME, fullShortPath);
                _finddata->attrib = FindData.attrib;
                _finddata->FileSize = FindData.size;
            }
        }
        return (RtStatus_t) hFile;
    }       
}
//extern "C" 
RtStatus_t FindFirstLFN(FindData_t *_finddata, uint8_t *Filename, void*pLFN)
{
    wchar_t wShortFile[13];
    wchar_t wPathName[1024];
    RtStatus_t Rtn;       

    Rtn = FindFirst(_finddata,Filename);
    for(int i=0;i<13;i++)
    {
        wShortFile[i] = _finddata->name[i];
    }
    if(SUCCESS==Rtn)
    {
        int size = GetLongPathNameW((LPCWSTR)wShortFile,wPathName,1024);
        memcpy(pLFN, wPathName,(size+1)*2);
    }
    return Rtn;
}

//extern "C" 
RtStatus_t FindNext(int32_t HandleNumber, FindData_t *_finddata)
{
	struct _finddata_t FindData;
    if(_findnext(HandleNumber, &FindData)==0)
    {  
        char fullPath[1024];
        char fullShortPath[1024];
        char* tempPtr = NULL;
        int64_t nextKey = FastHandleMap.size();
        //add the path to the FastHandleMap
        if(GetFullPathNameA(FindData.name, 1024, fullPath, &tempPtr)){
            //check if the path is already in the map and don't add it again if it is
            std::map<int64_t, FastHandleMapEntry_t>::iterator i;
            for( i = FastHandleMap.begin(); i != FastHandleMap.end(); i++){
                if(0 == strcmp(fullPath,i->second.fullPath.c_str())){
                    break;
                }
            }
            if(GetShortPathNameA(FindData.name, fullShortPath, 1024)){
                if(i == FastHandleMap.end()){
                    FastHandleMapEntry_t mapEntry;
                    mapEntry.fileHandle = 0;
                    mapEntry.fullPath = fullPath;
                    FastHandleMap[nextKey] = mapEntry;
                    _finddata->Key = nextKey;
                }
                else{
                    _finddata->Key = i->first;
                }
                
                strcpy_s((char *)_finddata->name, MAX_FILESNAME, fullShortPath);
                _finddata->attrib = FindData.attrib;
                _finddata->FileSize = FindData.size;
            }
        }        
        return SUCCESS;
    }
    else 
    {   // No more matching files could be found
        return ERROR_GENERIC;
    }
}

//extern "C" 
RtStatus_t FindNextLFN(int32_t HandleNumber, FindData_t *_finddata, void*pLFN)
{
    wchar_t wShortFile[13];
    wchar_t wPathName[1024];
    RtStatus_t Rtn;       
    Rtn = FindNext(HandleNumber,_finddata);
    for(int i=0;i<13;i++)
    {
        wShortFile[i] = _finddata->name[i];
    }
    if(SUCCESS==Rtn)
    {
        int size = GetLongPathNameW((LPCWSTR)wShortFile,wPathName,1024);
        memcpy(pLFN, wPathName,(size+1)*2);
    }
    return Rtn;
}


//extern "C" 
RtStatus_t GetShortfilename(int64_t Key,uint8_t *Buffer)
{
	char shortpath[1024];
    std::map<int64_t, FastHandleMapEntry_t>::iterator i;
    i = FastHandleMap.find(Key);

    if(i==FastHandleMap.end())
        return ERROR_GENERIC;
    transform(i->second.fullPath.begin(),i->second.fullPath.end(),i->second.fullPath.begin(),toupper);
    GetShortPathNameA(i->second.fullPath.c_str(), shortpath, 1024);

	char* lastBackslash = strrchr(shortpath, '\\');
    strcpy_s((char*)Buffer, MAX_FILESNAME, &shortpath[(lastBackslash+1)-shortpath]);
    
    return SUCCESS;

}

////////////////////////////////////////////////////////////////////////////////
//!
//!     \brief       Detect the present of an MMC/SD card.
//!
//!     Cancel 
//!
//!     \fntype      non-reentrant function
//!
//!     \retval      MEDIA_MMC_VALID if fsfattype succeeds and card present
//!                  or MEDIA_MMC_INVALID if present but failed
//!                  or MEDIA_MMC_NOT_PRESENT 
//!
//!     \see 
//
//      Note:        global checked indicates that an external media is 
//                   present with Drive ready or maybe FS ready too. ddi_media.h has the extern.
////////////////////////////////////////////////////////////////////////////////
media_mmc_status_t ExternalMMCMediaPresent(void)
{
    media_mmc_status_t Status = MEDIA_MMC_NOT_PRESENT;

    if( g_bFrameworkExternalDriveOrFsInit )
    {
        Status = MEDIA_MMC_VALID;
    }

	return(Status);
}
