///////////////////////////////////////////////////////////////////////////////
//!
//! \file formatter.c
//!
//! \brief This module contains formatter code.
//
// Copyright (c) SigmaTel, Inc. 2005,2006
//
// SigmaTel, Inc.
// Proprietary  Confidential
//
// This source code and the algorithms implemented therein constitute
// confidential information and may comprise trade secrets of SigmaTel, Inc.
// or its associates, and any use thereof is subject to the terms and
// conditions of the Confidential Disclosure Agreement pursuant to which this
// source code was originally received.
//
///////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
// Include files
////////////////////////////////////////////////////////////////////////////////

#include <types.h>
#include <error.h>
#include <os/filesystem/filesystem.h>
#include <os/filesystem/fat/include/fstypes.h> //! \todo malinclusion
#include <os/filesystem/fat/include/platform.h> //! \todo malinclusion
#include <os/filesystem/fat/include/diroffset.h> //! \todo malinclusion
#include <os/filesystem/fsapi.h> //! \todo malinclusion
#include <os/filesystem/fat/include/handletable.h> //! \todo malinclusion //! \todo malinclusion
#include <os/filesystem/fat/include/devicetable.h> //! \todo malinclusion //! \todo malinclusion
#include "fat_internal.h"
#include <drivers/rtc/ddi_rtc.h>
#include <string.h>
#include <os/filesystem/include/fs_steering.h> //! \todo malinclusion
#include <os/dmi/os_dmi_api.h>
#include <stdlib.h>
#include "components/telemetry/tss_logtext.h"
#include <time.h>       // see time(), localtime()
#include "components/user_clock/datetime.h"

////////////////////////////////////////////////////////////////////////////////
// Defines
////////////////////////////////////////////////////////////////////////////////
#define MIN_FAT_YEAR  1980   // minimum year for FAT entries

////////////////////////////////////////////////////////////////////////////////
// Externs
////////////////////////////////////////////////////////////////////////////////

extern FileSystemMediaTable_t *MediaTable; 
extern HandleTable_t *Handle;

/*
extern int SetCWDHandle(int DeviceNo);
extern int GetCWDHandle(void);
extern long long FATsectorno(int DeviceNum,LONG clusterno,int *FATNtryoffset);
extern int WriteSector(int deviceNumber, LONG sectorNumber, int Offset, int *Sourcebuffer,int SourceOffset,int size,int WriteType);
extern int *ReadSector(int DeviceNum, LONG sectorNum,int WriteType);
extern long ReadDirectoryRecord(int HandleNumber,int RecordNumber,int *Buffer);

extern RtStatus_t Freehandle(int32_t HandleNumber);
extern void Uppercase(uint8_t *file);
*/
////////////////////////////////////////////////////////////////////////////////
// Prototypes
////////////////////////////////////////////////////////////////////////////////

static RtStatus_t AllocateFormatterMemory(int32_t DeviceNumber);
static void DeallocateFormatterMemory(void);

////////////////////////////////////////////////////////////////////////////////
// Variables
////////////////////////////////////////////////////////////////////////////////

typedef struct runLength
{
  int value;
  int run;
} runLengthType;

#define FAT_TABLE_SIZE 3100
#define SECTOR_SIZE_IN_WORDS 512

static runLengthType * stc_FatTableEntries = NULL;
static uint32_t * stc_TmpBuffer = NULL;
static int stc_u32numSaveEntries = 0;

////////////////////////////////////////////////////////////////////////////////
// Code
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
//! \brief This function is a "memmove" substitute.
//!
//! DSP C library does not have a "memmove" and _memcpy does not do the
//! right thing when source and destination overlap.
//!
//! This function is only for the case where dest points
//! further down in memory than source.
//!
//! \param[in] dest
//! \param[in] source
//! \param[in] numEntries
////////////////////////////////////////////////////////////////////////////////
void makeroom(runLengthType *dest, runLengthType *source, int numEntries)
{
  int i;
  
  for(i=(numEntries-1);i>=0;i--)
  {
  
    dest[i] = source[i];
    
  } /* for */  
  
} /* makeroom */

////////////////////////////////////////////////////////////////////////////////
//! \brief This function is another "memmove" substitute.
//!
//! DSP C library does not contain a "memmove".
//!
//! This function is only for the case where source 
//! points further down in memory than dest.
//!
//! \param[in] dest
//! \param[in] source
//! \param[in] numEntries        
//!
////////////////////////////////////////////////////////////////////////////////
void slideUp(runLengthType *dest, runLengthType *source, int numEntries)
{
  int i;

  for(i=0;i<numEntries;i++)
  {
    
    dest[i] = source[i];

  }

} /* slideUp */

////////////////////////////////////////////////////////////////////////////////
//! \brief This routine inserts "newElem" into run-length encoded array.
//!
//! Array has to be either empty or sorted.
//!
//! New element will be inserted in sorted order and if 
//! new element can extend existing run-length it will be
//! incorporated into existing run-length.  If inserting
//! a new element causes adjacent run-lengths to become
//! contiguous, the adjacent run-lengths will be merged.
//!
//! \param[in] newElem
//! \param[in] rlArray
//! \param[in] numEntries      
//!
//! \retval 0 If success
////////////////////////////////////////////////////////////////////////////////
int insertion(int newElem, runLengthType *rlArray, int *numEntries)
{
  int numEntriesToMove;
  int i;
  runLengthType insertElem;
  BOOL attemptMerge;
  int mergePrev=0;
  int mergeNext=0;
  int previousVal=0;
  int previousRun=0;
  int localNumEntries;
  
  localNumEntries = *numEntries;
  
  if (0==localNumEntries)
  {

    insertElem.value = newElem;
    insertElem.run   = 1;

    rlArray[localNumEntries++] = insertElem;

  }
  else
  {
    /* First search for rightful place
     * It is correct to assume that list is
     * already sorted.
     */

    for(i=0;i<localNumEntries;i++)
    {

      if ((rlArray[i].value)>=newElem)
      {

        break;

      }

    }

    if (i==(localNumEntries))
    {

      /* See if new element is contained by last 
       * entry.
       */
      if (((rlArray[localNumEntries-1].value) + (rlArray[localNumEntries-1].run))==newElem)
      {

        (rlArray[localNumEntries-1].run)++;

      }
      else if (((rlArray[localNumEntries-1].value) + (rlArray[localNumEntries-1].run))<newElem)
      {

        insertElem.value = newElem;
        insertElem.run   = 1;

        rlArray[localNumEntries++] = insertElem;

      } 

      /* else, new element is contained within last existing entry.  
       * Nothing else needs to be done.
       */
      
    }
    else
    {

      attemptMerge = FALSE;

      if (i)
      {
        previousVal = (rlArray[i-1].value);
        previousRun = (rlArray[i-1].run);
      }
      
      /* See if new element can be combined with next entry
       */
      if (newElem==((rlArray[i].value)-1))
      {

        if (i)
        {
          attemptMerge = TRUE;
          mergePrev = i-1;
          mergeNext = i;
        }

        (rlArray[i].value) = newElem;
        (rlArray[i].run)++;

      }
      /* See if new element can be combined with previous entry.
       */
      else if (((previousVal+previousRun)==newElem) && (i))
      {

        attemptMerge = TRUE;
        mergePrev = i-1;
        mergeNext = i;

        (rlArray[i-1].run)++;

      }      
      /* The element cannot be combined.  If element is not already
       * contained in next entry or previous entry, new entry has to be created.
       */
      else if ((newElem < (rlArray[i].value)) && 
               ((((previousVal+previousRun) < newElem) && (i)) || (0==i)))
      {
        
        insertElem.value = newElem;
        insertElem.run   = 1;

        numEntriesToMove = (localNumEntries-i);            
            
        makeroom(&(rlArray[i+1]),&(rlArray[i]),numEntriesToMove);

        rlArray[i] = insertElem;

        localNumEntries++;
      }

      if (attemptMerge)
      {

        if (((rlArray[mergePrev].value)+(rlArray[mergePrev].run))>=(rlArray[mergeNext].value))
        {

          /* Merge the two entries and then slide all elements after mergePrev up by one.
           */
          rlArray[mergePrev].run = (rlArray[mergeNext].value + rlArray[mergeNext].run - rlArray[mergePrev].value);

          numEntriesToMove = (localNumEntries-mergeNext-1);            
            
          slideUp(&(rlArray[mergeNext]),&(rlArray[mergeNext+1]),numEntriesToMove);

          localNumEntries--;

        } /* if */

      } /* if */

    } /* else */

  } /* else */

  *numEntries = localNumEntries;

  return 0;
}

////////////////////////////////////////////////////////////////////////////////
//! \brief This routine makes a record of all the clusters
//!     which are linked to starting cluster.
//!
//! \param[in] Device
//! \param[in] startCluster
//! \param[in] Fat32 Whether the filesystem is FAT32 or not. True means it is
//!     FAT32.
//! \param[out] numClusters    
//!
//! \retval 0 If no error
////////////////////////////////////////////////////////////////////////////////
int FollowFatChain(int Device, int startCluster, BOOL Fat32, int *numClusters)
{
   int nextcluster;
   int exitCondition;
   int ret;
   int localNumClusters = 0;
   
   /* Nothing to do.  File is empty.  If directory
    * is system directory, file will be saved by
    * saving directory.
    */
   if (0==startCluster)
   {
   
     return 0;
     
   }
   
   ret = insertion(startCluster,stc_FatTableEntries,&stc_u32numSaveEntries);
   	   
   localNumClusters = 1;
   
   if (ret)
   {
   
     return ret;
     
   }
   
   while (1)
   {
   
     nextcluster = Findnextcluster(Device,startCluster);

     if (Fat32)
     {
       exitCondition = ((((unsigned int)nextcluster) > ((unsigned int)0xFFFFF0)) || (ERROR_OS_FILESYSTEM_EOF==nextcluster) || (0==nextcluster));
     }
     else
     {
       exitCondition = ((((unsigned int)nextcluster) > ((unsigned int)0xFFF0)) || (ERROR_OS_FILESYSTEM_EOF==nextcluster) || (0==nextcluster));
     }
     
     if (exitCondition)
     {
     
       break;
       
     } /* if */
     
     startCluster = nextcluster;
     
     ret = insertion(startCluster,stc_FatTableEntries,&stc_u32numSaveEntries);
     
     localNumClusters++;
     
     if (ret)
     {
     
       return ret;
       
     }
     
   } /* while */

   *numClusters = localNumClusters;
   
   return 0;
   
} /* FollowFatChain */

////////////////////////////////////////////////////////////////////////////////
//! \brief This routine marks first character of directory record.  
//!
//! stc_TmpBuffer has to contain data from
//! directory record, and handleNo has to point to
//! location just past end of directory record.
//!
//! \param[in] handleNo
//! \param[in] character
//!
//! \retval 0 If no error.
////////////////////////////////////////////////////////////////////////////////
int markFirstCharacter(int handleNo, int character)
{
  int ret;

  ((uint8_t *)stc_TmpBuffer)[0] = character;

  (Handle[handleNo].Mode) |= WRITE_MODE;
        
  /* Writing Root Directory with Fwrite will not write root directory.
   * It will write to PBS.
   */
  if (Handle[handleNo].StartingCluster == 0)
  {
    
    if((ret= FSWriteSector(Handle[handleNo].Device,Handle[handleNo].CurrentSector,Handle[handleNo].BytePosInSector,
	    (uint8_t *)stc_TmpBuffer,0,DIRRECORDSIZE,WRITE_TYPE_RANDOM)) <0) 
    {
      
      return ret;
        
    }

  }
  else
  {
  
    /* Need to point to beginning of directory record again.
     */
    if ((ret = Fseek(handleNo,-DIRRECORDSIZE,SEEK_CUR))!=0)
    {
  
      return ret;
    
    }
  
    if ((ret = Fwrite(handleNo,(uint8_t *)stc_TmpBuffer,DIRRECORDSIZE)) <=0) 
    {
  
      return ret;
     
    }    

    ret = Fflush(handleNo);
  
    if (ret)
    {
  
      return ret;
    
    }

  }
  
  return 0;

} /* markAsDeleted */

////////////////////////////////////////////////////////////////////////////////
//! \brief Match filepath with file-name portion of Buffer.
//!
//! \param[in] stc_filepath
//! \param[in] Buffer
//! \param[in] length
//!
//! \retval FILE_FOUND If filepath matches file-name portion of Buffer.
////////////////////////////////////////////////////////////////////////////////
int dirnamematch(char *filepath, char *Buffer, int length)
{
  int Byteno = 0;
  int byte;
  int filenamebyte;
  uint8_t Shortname[5];

  memcpy(Shortname,filepath,5);
   
  Uppercase(Shortname);
    
  while ((byte = Shortname[Byteno]) != '\0')
  {

    filenamebyte = Buffer[Byteno];

    if (byte != filenamebyte)
    {

      return (ERROR_OS_FILESYSTEM_FILE_NOT_FOUND);
      
    }
 
    Byteno++;
    
    if (length==Byteno)
    {
    
      break;
      
    }
    
  } /* while */

  /* File name section of directory entry is terminated
   * with spaces.
   */
  filenamebyte = Buffer[Byteno];

  while(' '==filenamebyte)
  {

    filenamebyte = Buffer[++Byteno];

  }

  if(Byteno!=DIR_ATTRIBUTEOFFSET)
  {
  
    return  ERROR_OS_FILESYSTEM_FILE_NOT_FOUND;                   
    
  } 
      	
  return (ERROR_OS_FILESYSTEM_FILE_FOUND);
}

////////////////////////////////////////////////////////////////////////////////
//! \brief Local version of general utility function
//! \todo get rid of PutLittleEndianINT32IntoByteBufferInY
////////////////////////////////////////////////////////////////////////////////
static void PutLittleEndianINT32IntoByteBufferInY(void *pBuffer, int iStartIndex, long lValue)
{
    int i;
    uint8_t *pu8Buf = (uint8_t *)pBuffer;
    
    for(i=0;i<4;i++)
    {
        pu8Buf[iStartIndex + i] = lValue & 0xff;
        lValue=lValue>>8;
    }
}

////////////////////////////////////////////////////////////////////////////////
//! \brief Local version of general utility function
//! \todo get rid of PutLittleEndianINT16IntoByteBufferInY
////////////////////////////////////////////////////////////////////////////////
static void PutLittleEndianINT16IntoByteBufferInY(void *pBuffer, int iStartIndex, long lValue)
{
    int i;
    uint8_t *pu8Buf = (uint8_t *)pBuffer;
    for(i=0;i<2;i++)
    {
        pu8Buf[iStartIndex + i] = lValue & 0xff;
        lValue=lValue>>8;
    }
}
  
////////////////////////////////////////////////////////////////////////////////
//! \brief Deletes all entries of FAT (both FAT1 and FAT2)
//!     which do not belong to system files as indicated in stc_FatTableEntries.
//!
//! \param[in] Device
//! \param[in] stc_FatTableEntries
//! \param[in] stc_u32numSaveEntries
//!
//! \retval 0 If no error
////////////////////////////////////////////////////////////////////////////////
int purgeFAT(int Device)
{
  int FATNtryoffset;
  int FATEntry;
  int i;
  int j;
  BOOL Fat32;
  int NumFatEntriesPerSector;
  int FATCounter;
  int *buf;
  BOOL pastEnd;
  int BytesPerSector;
  int FATSize;
  int FirstFATSector;
  int firstValueInRange;
  int lastValueInRange;
  int ret;
    
  Fat32 = (FAT32==(MediaTable[Device].FATType));
  
  BytesPerSector = MediaTable[Device].BytesPerSector;
  
  FATSize = MediaTable[Device].FATSize;
  
  if (Fat32)
  {	   
  
    NumFatEntriesPerSector = BytesPerSector/4;
    
  }
  else
  {

    NumFatEntriesPerSector = BytesPerSector/2;
  
  }
  
  FirstFATSector = FATsectorno(Device,0,&FATNtryoffset);
    
  FATEntry = 0;  
  FATCounter = 0;
  
  if (0==stc_u32numSaveEntries)
  {
    pastEnd = TRUE;
  }
  else
  {
    pastEnd = FALSE;
  }
      
  for(i=0;i<FATSize;i++)
  {
      uint32_t cacheToken;
    EnterNonReentrantSection();
    if ((buf = FSReadSector(Device,i+FirstFATSector,WRITE_TYPE_RANDOM, &cacheToken))==(int *)0)
    {
      LeaveNonReentrantSection();
      return ERROR_OS_FILESYSTEM_READSECTOR_FAIL;
        
    } /* if */

    /* WriteSector needs buffer in Y.
     */
    for(j=0;j<BytesPerSector/sizeof(uint32_t);j++)
    {
    
      stc_TmpBuffer[j] = buf[j];
      
    }
    FSReleaseSector(cacheToken);
    LeaveNonReentrantSection();
                
    firstValueInRange = stc_FatTableEntries[FATEntry].value;
    lastValueInRange = stc_FatTableEntries[FATEntry].value + stc_FatTableEntries[FATEntry].run - 1;
    
    for(j=0;j<NumFatEntriesPerSector;j++)
    {
    
      /* If current FAT entry is not part of system file, system directory
       * or home directory, mark as free.
       */
      if ((FATCounter<firstValueInRange) || (pastEnd))
      {
                  
        if (Fat32)
        {
        
           PutLittleEndianINT32IntoByteBufferInY(stc_TmpBuffer, j*4, 0);
        
        } /* if */
        else
        {
       
           PutLittleEndianINT16IntoByteBufferInY(stc_TmpBuffer, j*2, 0);
        
        } /* else */
        
        //ret = insertion(FATCounter,FreeSpaceTableEntries,&stc_u32numFreeSpaceEntries);
   	   
      } /* if */
            
      /* stc_FatTableEntries is a sorted list of run-lengths.  If current
       * FAT entry under consideration is equal to last element covered
       * by current run-length, advance to next run-length.
       */
      if ((lastValueInRange<=FATCounter) && (FALSE==pastEnd))
      {
    	 
        while (((stc_FatTableEntries[FATEntry].value)+(stc_FatTableEntries[FATEntry].run)-1)<=FATCounter)
        {
      
          FATEntry++;
          
          if (FATEntry==stc_u32numSaveEntries)
          {
          
            pastEnd = TRUE;
            
            break;
            
          } /* if */              
          
        } /* while */
          
        if (FALSE==pastEnd)
        {

          firstValueInRange = stc_FatTableEntries[FATEntry].value;
          lastValueInRange = stc_FatTableEntries[FATEntry].value + stc_FatTableEntries[FATEntry].run - 1;
          
        }
          
      } /* if */
        
      FATCounter++;

    } /* for */

    /* Write FAT
     */
    if((ret = FSWriteSector(Device,i+FirstFATSector,0,(uint8_t *)stc_TmpBuffer,0,BytesPerSector,WRITE_TYPE_RANDOM)) <0)
    {
      
      return ret;
	
    }

	#ifdef ENABLE_WRITE_FAT2
    /* Write FAT2
     */
    if((ret = FSWriteSector(Device,i+FirstFATSector+FATSize,0,(int *)stc_TmpBuffer,0,BytesPerSector,WRITE_TYPE_RANDOM)) <0)
    {
      
      return ret;
	
    }
	#endif
   
  } /* for */
       
  return 0;
   
} /* PurgeFAT */

////////////////////////////////////////////////////////////////////////////////
//! \brief Delete non-system files and record cluster numbers
//!     of system files in stc_FatTableEntries
//!
//! \param[in] DeviceNumber
//! \param[in] handleNo Needs to point to system directory.
//! \param[in] Fat32
//!
//! \retval 0 If no error.
////////////////////////////////////////////////////////////////////////////////
int32_t SaveSystemFiles(int32_t DeviceNumber, int32_t handleNo, bool Fat32, bool saveHDSFiles)
{
  int32_t recordNo;
  int32_t dirattribute;
  int32_t StartingCluster;
  int32_t ret; 
  int32_t FirstByte;
  int32_t numClusters;
  
  recordNo = 0;
  
  while((ret = ReadDirectoryRecord(handleNo,recordNo,(uint8_t *)stc_TmpBuffer))>0)
  {    
        	 
    /* If first byte of directory record is zero, we've reached
     * end of directory.
     */
    if ((((uint8_t *)stc_TmpBuffer)[0])==0)
    {
    
      break;
      
    }

    dirattribute = ((uint8_t *)stc_TmpBuffer)[DIR_ATTRIBUTEOFFSET];      
   
    /* Save System files.
     */
    if ((dirattribute & ATTR_SYSTEM)         && 
        (0==(dirattribute & ATTR_VOLUME_ID)) &&     
        (saveHDSFiles))
    {
      /* Get number of first cluster of file.
       * Then follow FAT chain to discover all
       * clusters which belong to given file.
       */
      StartingCluster = ((uint16_t *)stc_TmpBuffer)[DIR_FSTCLUSHIOFFSET>>1];
      
      StartingCluster <<= 16;
          
      StartingCluster |= (((uint16_t *)stc_TmpBuffer)[DIR_FSTCLUSLOOFFSET>>1]);
      
      /* Record cluster numbers of system file
       */
      ret = FollowFatChain(DeviceNumber,StartingCluster,Fat32,&numClusters);
           
      //SystemFileNumClusters[stc_u32numSystemFiles++] = numClusters;
      
      if (ret)
      {
      
        return ret;
        
      }       
            
    } /* if */
    else if (dirattribute & ATTR_DIRECTORY)
    {

      FirstByte = ((uint8_t *)stc_TmpBuffer)[0];

      /* If entry is a directory and directory is not . or ..
       */
      if (FirstByte != 0x2E)
      {
           
        /* Mark as deleted.
         */
        ret = markFirstCharacter(handleNo,0xE5);
      
        if (ret)
        {
      
          return ret;
        
        } /* if */

      } /* if */

    }
    else if ((0==(dirattribute & ATTR_VOLUME_ID)) ||
             (0xF==(dirattribute & 0xF))) 
    {
    
      /* Mark as deleted.
       */
      ret = markFirstCharacter(handleNo,0xE5);
      
      if (ret)
      {
      
        return ret;
        
      }
      
    } /* else */
        
    recordNo++;
    
  } /* while */	  
  
  return 0;
  
} /* SaveSystemFiles */

////////////////////////////////////////////////////////////////////////////////
//! \brief Allocates memory used by the formatter using DMI.
//!
//! We don't wait for the allocation to succeed. If the memory is not
//! immediately available, an error will be returned.
//!
//! \param[in] DeviceNumber Index of the device that is being formatted. Used
//!     to get the sector size of the device.
//!
//! \retval SUCCESS Memory was successfully allocated.
//! \retval ERROR_OS_MEMORY_MANAGER_SYSTEM   If a system error occurs.
//! \retval ERROR_OS_MEMORY_MANAGER_NOMEMORY If the memory manager is out of memory.
////////////////////////////////////////////////////////////////////////////////
RtStatus_t AllocateFormatterMemory(int32_t DeviceNumber)
{
    RtStatus_t status;
    uint32_t count;
    
    count = FAT_TABLE_SIZE * sizeof(runLengthType);
    status = os_dmi_MemAlloc((void **)&stc_FatTableEntries, count, FALSE, DMI_MEM_SOURCE_DONTCARE);
    
    if (status == SUCCESS)
    {
        count = MediaTable[DeviceNumber].BytesPerSector;
        status = os_dmi_MemAlloc((void **)&stc_TmpBuffer, count, FALSE, DMI_MEM_SOURCE_DONTCARE);
    }
    
    return status;
}

////////////////////////////////////////////////////////////////////////////////
//! \brief Deallocates memory used by the formatter.
////////////////////////////////////////////////////////////////////////////////
void DeallocateFormatterMemory(void)
{
    if (stc_FatTableEntries)
    {
        os_dmi_MemFree(stc_FatTableEntries);
        stc_FatTableEntries = NULL;
    }
    
    if (stc_TmpBuffer)
    {
        os_dmi_MemFree(stc_TmpBuffer);
        stc_TmpBuffer = NULL;
    }
}

void GetDateTime(int *date,int *xtime)
{
    time_t     rtcTime;
	struct tm *locTime;
    DIR_DATE   DirDate;
    DIR_TIME   DirTime;
	int        year,month,hour,minute,second;
 	
	// Get RTC timestamp (the number of seconds since Epoch: Jan 1, 1970)
	 rtcTime = time( NULL );
	
	 // Convert the current time to local time representation
	 locTime = localtime( &rtcTime );
	
	 // Use current RTC date/time as file timestamp
#if 0
	 DirDate.Year	= locTime->tm_year + DATETIME_YEAR_OFFSET;
#else
	 DirDate.Year	= locTime->tm_year + 1900;	// ex. 107+1900 = 2007
#endif
	 DirDate.Month	= locTime->tm_mon + 1;		// [0,11]+1,  ex. 2+1 = 3 (March)
	 DirDate.Day	= locTime->tm_mday; 		// [1,31]
	 DirTime.Hour	= locTime->tm_hour; 		// [0,23]
	 DirTime.Minute = locTime->tm_min;			// [0,59]
	 DirTime.Second = locTime->tm_sec;			// [0,59]
	
	 // Convert current time to FAT date/time format
	 if( DirDate.Year < MIN_FAT_YEAR )
		 DirDate.Year = MIN_FAT_YEAR;			// clamp to minimum FAT year
	
	 // Local vars must have room to left shift the 8-bit DirDate fields
	 month = ((DirDate.Month << 5 ) & 0x01E0);
	 year  = ((DirDate.Year - MIN_FAT_YEAR) << 9 );
	 *date = (DirDate.Day | month | year);

	 // Local vars must have room to left shift the 8-bit DirTime fields
	 second = DirTime.Second >> 1;
	 minute = ((DirTime.Minute	<< 5) & 0x07E0);
	 hour = ((DirTime.Hour	<< 11 ) & 0xF800);
	 *xtime = (second | minute | hour);
}

////////////////////////////////////////////////////////////////////////////////
//! \brief Create the Volume label record with the label
////////////////////////////////////////////////////////////////////////////////
int32_t CreateVolumeLabelRecord (uint8_t buf[], const uint8_t *pLabel)
{
    int16_t ii;

    int date,time;
    
    for(ii=0; ii<11 && pLabel[ii]; ii++)
    {
        buf[ii] = pLabel[ii];
    }

    for(; ii<11; ii++)
    {
        buf[ii] = 0x20;
    }
                
    for(; ii<32; ii++)
    {
        buf[ii] = 0;
    }
    
    buf[DIR_ATTRIBUTEOFFSET] = ATTR_VOLUME_ID;
    GetDateTime(&date,&time);
    PutWord(buf, date, DIR_WRTDATEOFFSET); // Set modification date
    PutWord(buf, time, DIR_WRTTIMEOFFSET); // Set modification time

    return SUCCESS;
}

////////////////////////////////////////////////////////////////////////////////
//! \brief Seek to the position of the specific directory record
////////////////////////////////////////////////////////////////////////////////
int32_t SeekDirRecord (int32_t handleNo, int32_t recordNo)
{
    // With FAT16, StartingCluster==0 means the root dir and the clusters are contiguous.
    // SetcurrentPos() works for cluster 0, and SetcurrentPos() multiplies 
    // recordNo*DIRRECORDSIZE internally. 

    if (Handle[handleNo].StartingCluster == 0)
    {
        return SetcurrentPos(handleNo, recordNo);
    }
    // FAT32 has no root dir in StartingCluster 0, and Fseek() works for clusters that are 
    // nonzero in a cluster chain, and we have to multiply by DIRRECORDSIZE externally by Fssek()
    return Fseek(handleNo,recordNo*DIRRECORDSIZE,SEEK_SET);
}


////////////////////////////////////////////////////////////////////////////////
//! \brief Write the Volume label record
////////////////////////////////////////////////////////////////////////////////
int32_t WriteDirRecord (int handleNo, uint8_t buf[])
{
    int32_t ret = 0;
    
    if (Handle[handleNo].StartingCluster == 0)
    {
      if((ret= FSWriteSector(Handle[handleNo].Device,Handle[handleNo].CurrentSector,Handle[handleNo].BytePosInSector,
                 	        buf,0,DIRRECORDSIZE,WRITE_TYPE_RANDOM)) <0) 
      {
        return ret;
      }       
    }
    else
    {
      if ((ret = Fwrite(handleNo,buf,DIRRECORDSIZE)) <=0) 
      {
        return ret;
      }    
    
      ret = Fflush(handleNo);
      if(ret)
      {
        return ret;
      }            
    }

    return ret;
}

////////////////////////////////////////////////////////////////////////////////
//! \brief set volume label
////////////////////////////////////////////////////////////////////////////////
int32_t SetLabel(int32_t DeviceNumber, int32_t handleNo, bool Fat32, const uint8_t *pVolumeLabel)
{
  int32_t recordNo;
  int32_t dirattribute;
  int32_t ret; 
  int32_t FirstByte;
  uint32_t buf[8];
  int32_t i32FirstFreeRecord = -1;
  int32_t i32VolumeLabelRecord = -1;

  recordNo = 0;

  // Look for existing volume label record

  // ReadDirectoryRecord() returns a positive 64bit fast key or a negative error code on error. 
  // Unfortunately, when the root directory is full in Fat12/Fat16, the function will always return 
  // a negative error value after the last directory record is read. Thus, we ignore any error here
  while(ReadDirectoryRecord(handleNo,recordNo,(uint8_t *)buf)>=0)
  {    
        	 
    // If first byte of directory record is zero, we've reached end of directory.
    if ((((uint8_t *)buf)[0])==0)
    {
    
      break;
      
    }
    else if( (((uint8_t *)buf)[0])==0xE5)
    {
        if(i32FirstFreeRecord < 0)
        {
            i32FirstFreeRecord = recordNo;
        }
    }

    dirattribute = ((uint8_t *)buf)[DIR_ATTRIBUTEOFFSET];      
   
    if( dirattribute == ATTR_VOLUME_ID)
    {
        // always reuse the volume label record if it is found
        i32VolumeLabelRecord = recordNo;

        FirstByte = ((uint8_t *)buf)[0];
        if(FirstByte != 0xE5)
        {
            // a valid volume label record is found, no need to continue the search
            break;
        }            
    }
        
    recordNo++;
    
  } /* while */	  

  ret = CreateVolumeLabelRecord((uint8_t *)buf, pVolumeLabel);
  if(ret)
  {
      return ret;
  }
  
  if(i32VolumeLabelRecord >= 0)
  {
     // reuse this directory record for volume label
     ret = SeekDirRecord(handleNo, i32VolumeLabelRecord);
     if(ret)
     {
         return ret;
     }

     ret = WriteDirRecord(handleNo, (uint8_t *)buf);
     if(ret)
     {
         return ret;
     }
  }
  else if(!Fat32 && recordNo>=MediaTable[DeviceNumber].MaxRootDirEntries )
  {
     // FAT12/16 and all diretory record are used up, look for deleted record for reuse
     if(i32FirstFreeRecord >= 0)
     {
         // reuse this directory record for volume label
         ret = SeekDirRecord(handleNo, i32FirstFreeRecord);
         if(ret)
         {
             return ret;
         }
    
         ret = WriteDirRecord(handleNo, (uint8_t *)buf);
         if(ret)
         {
             return ret;
         }        
     }
     else     
     {
         //all records are used up, don't apply the new label
         return ERROR_OS_FILESYSTEM_NOSPACE_IN_ROOTDIRECTORY;
     }
  }
  else
  {  
     // create a directory record at the end of root directory for volume label
     ret = SeekDirRecord(handleNo, recordNo);
     if(ret)
     {
         return ret;
     }

     ret = WriteDirRecord(handleNo, (uint8_t *)buf);
     if(ret)
     {
         return ret;
     }
     
     if(Fat32)
     {   // append the null record
         ret = SeekDirRecord(handleNo, recordNo+1);
         if(ret)
         {
             return ret;
         }

         memset(buf, 0, DIRRECORDSIZE);
         ret = WriteDirRecord(handleNo, (uint8_t *)buf);
         if(ret)
         {
             return ret;
         }            
     }        
  }

  return 0; 
}



////////////////////////////////////////////////////////////////////////////////
//! \brief Formats Device indicated by DeviceNumber.  All files
//!     other than system files will be deleted.  As a 
//!     necessity, root directory and sytem directory are
//!     spared as well.
//!
//! \param[in] DeviceNumber
//! \param[in] saveHDSFiles
//! \param[in] pointer to volume label, NULL for no label
//!
//! \retval 0 No error.
////////////////////////////////////////////////////////////////////////////////
#define VERBOSE_FORMATTER 0  // define as 1 or 0
int32_t FormatAndLabel(int32_t DeviceNumber, bool saveHDSFiles, const uint8_t *pVolumeLabel)
{
    int32_t ret=0;
    int32_t handleNo; 
    int32_t clusterno;
    BOOL Fat32;
    int32_t clusterCount;
  
    if (AllocateFormatterMemory(DeviceNumber) != SUCCESS)
    {
        ret = 1;
        goto _FormatExit2;
    }
  
    EnterNonReentrantSection();
  
    ret = SetCWDHandle(DeviceNumber);
    if (ret)
    {
        goto _FormatExit;    
    }
  
    /* Root diectory has to be saved also
     */
    handleNo = GetCWDHandle();
  
    if (ERROR_OS_FILESYSTEM_NO_FREE_HANDLE==handleNo)
    { 
        ret = handleNo;    
        goto _FormatExit;
    }
  
    stc_u32numSaveEntries = 0;
    
    Fat32 = (FAT32==MediaTable[DeviceNumber].FATType);
  
    clusterno = Handle[handleNo].StartingCluster;

    /* Save cluster numbers of root directory.
     */
    ret = FollowFatChain(DeviceNumber,clusterno,Fat32,&clusterCount);
    if (ret)
    { 
        Freehandle(handleNo);
        goto _FormatExit;       
    }
      
    ret = insertion(0,stc_FatTableEntries,&stc_u32numSaveEntries);       
    if (ret)
    {     
        Freehandle(handleNo);    
        goto _FormatExit;       
    }

    ret = insertion(1,stc_FatTableEntries,&stc_u32numSaveEntries);       
    if (ret)
    {     
        Freehandle(handleNo);     
        goto _FormatExit;       
    }
  
    if(Fat32)
    {    
        ret = insertion(2,stc_FatTableEntries,&stc_u32numSaveEntries);
       
        if (ret)
        {     
            Freehandle(handleNo);
            goto _FormatExit;       
        }
    }
      
    ret = SaveSystemFiles(DeviceNumber,handleNo,Fat32,saveHDSFiles); 
    if (ret)
    {      
        Freehandle(handleNo);
        goto _FormatExit;    
    }     
                
   /* Purging FAT of all records other than
    * those belonging to Root directory,
    * System directory or to system files
    * completes the formatting process.
    */
    ret = purgeFAT(DeviceNumber);  
    if (ret)
    {
        Freehandle(handleNo);
        goto _FormatExit;
    }

    // apply volume label
    if( pVolumeLabel != NULL)
    {
        ret = SetLabel(DeviceNumber, handleNo, Fat32, pVolumeLabel);
    }

    Freehandle(handleNo);

_FormatExit: 
    LeaveNonReentrantSection();
_FormatExit2:
    DeallocateFormatterMemory();

    #if VERBOSE_FORMATTER
    if(ret)
    {   tss_logtext_Print(LOGTEXT_VERBOSITY_2|LOGTEXT_EVENT_UIM_GROUP,
                          "Fmt(%d)err:0x%x\r\n", DeviceNumber, ret);
    }                         
    #endif
        
    return ret;
} /* Format */


////////////////////////////////////////////////////////////////////////////////
//! \brief Formats Device indicated by DeviceNumber.  All files
//!     other than system files will be deleted.  As a 
//!     necessity, root directory and sytem directory are
//!     spared as well.
//!
//! \param[in] DeviceNumber
//! \param[in] saveHDSFiles
//!
//! \retval 0 No error.
////////////////////////////////////////////////////////////////////////////////
int32_t Format(int32_t DeviceNumber, bool saveHDSFiles)
{
    return FormatAndLabel(DeviceNumber, saveHDSFiles, NULL);
}

