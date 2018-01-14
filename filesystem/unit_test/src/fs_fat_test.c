////////////////////////////////////////////////////////////////////////////////
//! \addtogroup os_file
//! @{
//!
//  Copyright (c) 2005 SigmaTel, Inc.
//!
//! \file    fs_fat_test.c
//! \brief   Unit test for the FAT file system.
//! \version 0.1
//! \date    03/2005
//!
//! This file implements the unit test for the FAT file system.
//!
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
//  Includes
////////////////////////////////////////////////////////////////////////////////
#include <stdio.h>
#include <string.h>
#include <os\fsapi.h>
#include <drivers\ddi_media.h>
#include "fstypes.h"
#include "platform.h"
#include "fs_steering.h"

////////////////////////////////////////////////////////////////////////////////
//  External References
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
//  Equates
////////////////////////////////////////////////////////////////////////////////

#define  NEWADDED
#define FINDNEXT_TEST
//#define CHDIRW_TEST
//#define MKDIRW_TEST

// #define MAXDIRECTORYTEST
// #define MAXFILEOPENTEST
// #define DELETREETEST

#define DISPLAY_LENGTH  96
#define BAR_CHAR        6
#define NOERROR         0
#define TESTFAIL        1
#define TESTSUCCESS     0
#define ERROR_GENERIC           -1
#define NUM_WRITE_charS  1000
#define NUM_WRITE_BYTES  1000
#define MAX_COPY_WORDS   500
#define NUM_COMPARE_READ_WORDS      250
#define DRIVE_TAG_RESOURCE_BIN      0x02

////////////////////////////////////////////////////////////////////////////////
//  Variables
////////////////////////////////////////////////////////////////////////////////
//uint8_t GetBuffer[300];
//modify: define here
FindData_t FindData;

int8_t GetBuffer[2048];
int32_t DirectoryCount=0;
uint8_t BitBuffer[2048];
uint8_t ReadBuffer[2048*3];
uint8_t FileNames[80];
int32_t count=0;
int32_t count1=0;

uint8_t pwFileBuffer[(NUM_WRITE_BYTES+1)];
uint8_t pwCompareBuffer[(NUM_WRITE_BYTES+1)];
uint8_t Buffer[(NUM_WRITE_BYTES+1)];

uint8_t longfilebuf[100*2];
uint8_t longfile1buf[100*2];
uint8_t longfile2buf[50*2];


#ifdef MKDIRW_TEST
//Test MkdirW
uint8_t longdirbuf[100*2];
uint8_t longdir1buf[20*2];
uint8_t longdir2buf[20*2];
uint8_t longdir3buf[20*2];
uint8_t longdir4buf[30*2];
uint8_t longdir5buf[30*2];
#endif


#ifdef CHDIRW_TEST
//Test ChdirW
uint8_t longchdirbuf[50*2];
uint8_t longchrootbuf[20*2];
uint8_t longchlvel1buf[20*2];
uint8_t longchlevel2buf[20*2];
uint8_t longchlevel4buf[20*2];
#endif



#ifdef NEWADDED
uint8_t longfile3buf[100*2];
uint8_t longfile3[100]="a:/abcdefghijkl.txt";
#endif

int TestResult=TESTSUCCESS;
int i;
int RetValue=NOERROR;
int32_t fin,fout,fout1,fout2,fout3,fin1,foutw;

uint8_t testget[]="a:/file2.txt";
uint8_t bigfile[]="a:/test.wav";
uint8_t writebig[]="a:/copy.wav";
uint8_t testread[]="a:/file1.txt";
uint8_t testfile[]="a:/TEST.h";
uint8_t testfile1[]="a:/C/C1/test.txt";
uint8_t Nofile[]="a:/module/sbrdecoder/applysbr/c/src/test.c";
uint8_t handletest[]="a:/Handletable.h";
uint8_t putfile[]="a:/putfile.h";                 
uint8_t testfile2[]="a:/MYDIR4/test.h";
uint8_t writefile[]="a:/testgetfile.h";
uint8_t writefile1[]="a:/writefile.h";
uint8_t writefile2[]="a:/testp.h";
uint8_t testfile3[]="a:/MYDIR1/EOF.asm";
uint8_t testfile4[]="a:/C/C2/C3/test.h";
uint8_t testfile5[]="a:/C/C2";
uint8_t testfile6[]="a:/MYDIR1/EOF.asm";
uint8_t getfile[]="a:/getfile1.h";
uint8_t SeekFile[]="a:/seek.wav";
uint8_t AttrFile[]="a:/Handletable.h";
uint8_t chfile[]="a:/MyDir/SubDir/SubDir1/SubDir2/SubDir3/SubDir4/test.asm";
uint8_t chfile1[]="a:/inp.hex";
uint8_t chfile2[]="./temp.hex";
uint8_t chfile3[]="temp.hex";
uint8_t bFileDest[] ="a:/test.asm";
uint8_t bFileSource[] ="a:/test.asm";
uint8_t readfile[]="a:/c/test.c";
uint8_t removefile[]="a:/removefile.txt";

uint8_t FindBuffer[] = "*.mp3";
uint8_t FindFile[]="..";
uint8_t WorkingDir[]="a:/Songs1/Songs2/Songs3/Songs4";

uint8_t ReadFILE[] = "Test.wav";
uint8_t rootdir[]="a:/testdir1";
uint8_t rootdir1[]="a:/C/C2/C3/testdir1";
uint8_t level1dir[]="a:/MYDIR3/testdir1";
uint8_t level2dir[]="a:/MYDIR2/SubDir/testdir1";
uint8_t level3dir[]="a:/MyDir/testdir1";
uint8_t level5dir[]="a:/MyDir/SubDir/SubDir1/SubDir6/testdir1";
uint8_t level6dir[]="a:/MyDir/SubDir/SubDir1/SubDir2/SubDir3/SubDir4/";
uint8_t chdirb[]="b:/MyDir/SubDir/SubDir1/SubDir2/SubDir3/SubDir4";
uint8_t chfileb[]="test.hex";
uint8_t chdirl[]="a:/XXXXXX~1";
uint8_t chfile4[]="test.hex";
uint8_t chdir[]="a:/C/C2/C3";
uint8_t chdir1[]="a:/MYDIR2/SubDir";

uint8_t bTextFail[] = "Fail";

uint8_t Attrdir[]="a:/MyDir";

uint8_t longfile[100] = "a:/longfiletest.asm";
uint8_t longfile1[100]= "a:/FSLSubDir/FSLSubDir1/FSLSubDir2/FSLSubDir5/FileSystem_input.inc";
uint8_t longfile2[50]= "a:/longfilenametest.asm";

#ifdef MKDIRW_TEST
uint8_t longdir[100]= "a:/FSLSubDir/FSLSubDir1/FSLSubDir2/FSLSubDir5/FSLSubdir4";
uint8_t longdir1[20]= "a:/..";
uint8_t longdir2[20]= "a:/..1..abc123def";
uint8_t longdir3[20]= "a:/abc123def/";
uint8_t longdir4[30]= "a:/abc123def/abcdefghijk";
uint8_t longdir5[30]= "a:/abc123def/ab?cde<fgh>ijk";
#endif




#ifdef CHDIRW_TEST
uint8_t longdirFopen[20]= "temp.txt";
uint8_t longchdir[50]= "a:/FSLSubDir/FSLSubDir1/FSLSubDir2/FSLSubDir5";
uint8_t longchroot[20]="/";
uint8_t longchlvel1[10]="..";
uint8_t longchlevel2[10]="../../";
uint8_t longchlevel4[20]="../../../../";
#endif    

uint8_t DeleteDirectory[]="a:/c";
uint8_t chroot[]="/";
uint8_t chlvel1[]="..";
uint8_t chlevel2[]="../../";
uint8_t chlevel3[]="../../../..";
uint8_t chlevel4[]="../../../../..";
uint8_t chlevel5[]="../../../../../..";

extern uint32_t g_wNumDrives;

////////////////////////////////////////////////////////////////////////////////
// Prototypes
////////////////////////////////////////////////////////////////////////////////
void GetUnicodeString(uint8_t *filepath,uint8_t *buf,int32_t Strlen);
int TestHandle(void);
int TestReadMode(void);
int TestWriteMode(void);
int TestAppendMode(void);
int TestReadPlusMode(void);
int TestWritePlusMode(void);
int TestAppendPlusMode(void);
int TestFeof(void);
int TestFileread(void);
int TestWriteFile(void);
int TestFremove(void);
int TestFseek(void);
int TestFtell(void);
int TestMkdir(void);
int TestMkdirMax(int32_t DeviceNum);
int TestRmdir(void);
int TestFgetc(void);
int TestFgets(void);
int TestFputc(void);
int TestFputs(void);
int TestFclose(void);
int Testfilegetattrib(void);
int Testfilesetdate(void);
int TestChdir(void);
int TestFopenwRead(void);
int TestFopenwWrite(void);
int TestFopenwAppend(void);
int TestFopenwReadPlus(void);
int TestFopenwWritePlus(void);
int TestFopenwAppendPlus(void);
int TestFwritePMemory(void);
int TestFwriteXMemory(void);
int TestDeletTree(void);
int TestFwrite(void);
int TestFopen(void);
#ifdef FINDNEXT_TEST
int TestFindNext(void);
#endif

#ifdef MKDIRW_TEST
int TestMkdirW(void);
#endif

#ifdef CHDIRW_TEST
int TestChdirW(void);
#endif
#ifdef NEWADDED
int TestFopenw(void);
int TestAllModes(void);
#endif
int TestResourceFread(void);
int TestResourceFwrite(void);
int TestResourceFseek(void);
int TestResourceFclose(void);



int g_u32protectedMode;

void encrypt_data()
{

}

void decrypt_data()
{

}

////////////////////////////////////////////////////////////////////////////////
//  Code
////////////////////////////////////////////////////////////////////////////////
#pragma ghs nowarning 550   // ignore "set but never used" warning
////////////////////////////////////////////////////////////////////////////////
//
//>  Name:           Main
//
//   Type:           Function
//
//   Description:    Entry point for Fattest
//                   
//   Inputs:         none 
//
//   Outputs:        none
//                   
//   Notes:          none
//<
////////////////////////////////////////////////////////////////////////////////
void main(void)
{
    int32_t Strlen;
    int32_t i;

	printf("MediaInit(0)			");
    if(MediaInit(0) != SUCCESS)
	{
		TestResult=TESTFAIL;
		printf("FAIL\n");
	}
	else
	{
		printf("PASS\n");
	}
   
#ifdef LDL_STEERING
	printf("MediaDiscoverAllocation(0)			");
    if(MediaDiscoverAllocation(0) != SUCCESS)
	{
		TestResult=TESTFAIL;
		printf("FAIL\n");
	}
	else
	{
		printf("PASS\n");
	}
#endif  // #ifdef LDL_STEERING
   
#ifdef EXTERNAL_MEDIA_SDMMC
	printf("MediaInit(1)			");
    if(MediaInit(1) != SUCCESS)
	{
		TestResult=TESTFAIL;
		printf("FAIL\n");
	}
	else
	{
		printf("PASS\n");
	}

#ifdef LDL_STEERING
	printf("MediaDiscoverAllocation(1)			");
    if(MediaDiscoverAllocation(1) != SUCCESS)
	{
		TestResult=TESTFAIL;
		printf("FAIL\n");
	}
	else
	{
		printf("PASS\n");
	}
#endif  // #ifdef LDL_STEERING
#endif  // #ifdef EXTERNAL_MEDIA_SDMMC
   

//
#ifdef EXTERNAL_MEDIA_SDMMC
    MediaInit(1);
#endif

     DriveInitAll();

	printf("FSInit				");
    if(FSInit(bufx, bufy, maxdevices, maxhandles, maxcaches) != SUCCESS)
	{
		TestResult=TESTFAIL;
		printf("FAIL\n");
	}
	else
	{
		printf("PASS\n");
	}

    Strlen = Strlength(longfile); 
    GetUnicodeString(longfile,longfilebuf,Strlen);
    
    Strlen = Strlength(longfile1); 
    GetUnicodeString(longfile1,longfile1buf,Strlen);

    Strlen = Strlength(longfile1); 
    GetUnicodeString(longfile2,longfile2buf,Strlen);
#ifdef MKDIRW_TEST
//test for Mkdirw    
    Strlen = Strlength(longdir); 
    GetUnicodeString(longdir,longdirbuf,Strlen);
    Strlen = Strlength(longdir1); 
    GetUnicodeString(longdir1,longdir1buf,Strlen);
    Strlen = Strlength(longdir2); 
    GetUnicodeString(longdir2,longdir2buf,Strlen);
    Strlen = Strlength(longdir3); 
    GetUnicodeString(longdir3,longdir3buf,Strlen);
    Strlen = Strlength(longdir4); 
    GetUnicodeString(longdir4,longdir4buf,Strlen);
    Strlen = Strlength(longdir5); 
    GetUnicodeString(longdir5,longdir5buf,Strlen);
#endif    
#ifdef CHDIRW_TEST    
    Strlen = Strlength(longchdir); 
    GetUnicodeString(longchdir,longchdirbuf,Strlen);
    Strlen = Strlength(longchroot); 
    GetUnicodeString(longchroot,longchrootbuf,Strlen);
    Strlen = Strlength(longchlvel1); 
    GetUnicodeString(longchlvel1,longchlvel1buf,Strlen);
    Strlen = Strlength(longchlevel2); 
    GetUnicodeString(longchlevel2,longchlevel2buf,Strlen);
    Strlen = Strlength(longchlevel4); 
    GetUnicodeString(longchlevel4,longchlevel4buf,Strlen);
#endif    

#ifdef NEWADDED
    Strlen = Strlength(longfile3);
    GetUnicodeString(longfile3,longfile3buf,Strlen);
#endif
    for (i=0; i<maxdevices; i++)
    {
        Computefreecluster(i);
    }          

    /*fill the buffer for test*/
    for (i=0; i<512; i++)
    {
        BitBuffer[i]=100+i;
    }

		/*Test the Handle */
		printf("TestHandle			");
		if(TestHandle()==ERROR_GENERIC)
		{
			TestResult=TESTFAIL;
			printf("FAIL\n");
		}
		else
		{
			printf("PASS\n");
		}

		/* Test Fopen in read("r") mode*/
		printf("TestReadMode			");
		if(TestReadMode()==ERROR_GENERIC)
		{
			TestResult=TESTFAIL;
			printf("FAIL\n");
		}
		else
		{
			printf("PASS\n");
		}

		/* Test Fopen in write("w") mode*/
		printf("TestWriteMode			");
		if(TestWriteMode()==ERROR_GENERIC)
		{
			TestResult=TESTFAIL;
			printf("FAIL\n");
		}
		else
		{
			printf("PASS\n");
		}

		/* Test Fopen in append ("a") mode*/
		printf("TestAppendMode			");
		if(TestAppendMode()==ERROR_GENERIC)
		{
			TestResult=TESTFAIL;
			printf("FAIL\n");
		}
		else
		{
			printf("PASS\n");
		}

		/* Test Fopen in readplus ("r+") mode*/
		printf("TestReadPlusMode			");
		if(TestReadPlusMode()==ERROR_GENERIC)
		{
			TestResult=TESTFAIL;
			printf("FAIL\n");
		}
		else
		{
			printf("PASS\n");
		}

		/* Test Fopen in writeplus ("w+") mode*/
		printf("TestWritePlusMode			");
		if(TestWritePlusMode()==ERROR_GENERIC)   
		{
			TestResult=TESTFAIL;
			printf("FAIL\n");
		}
		else
		{
			printf("PASS\n");
		}

		/* Test Fopen in appendplus("a+") mode*/
		printf("TestAppendPlusMode			");
		if(TestAppendPlusMode()==ERROR_GENERIC)
		{
			TestResult=TESTFAIL;
			printf("FAIL\n");
		}
		else
		{
			printf("PASS\n");
		}

		/*Test the end of file*/    
		printf("TestFeof			");
		if(TestFeof()==ERROR_GENERIC)
		{
			TestResult=TESTFAIL;
			printf("FAIL\n");
		}
		else
		{
			printf("PASS\n");
		}

		/*Test File Read*/    
		printf("TestFileread			");
		if(TestFileread()==ERROR_GENERIC) 
		{
			TestResult=TESTFAIL;
			printf("FAIL\n");
		}
		else
		{
			printf("PASS\n");
		}

		/*Test File Write*/    
		printf("TestWriteFile			");
		if(TestWriteFile()==ERROR_GENERIC)   
		{
			TestResult=TESTFAIL;
			printf("FAIL\n");
		}
		else
		{
			printf("PASS\n");
		}

		/*Test the File remove*/
		printf("TestFremove			");
		if(TestFremove()==ERROR_GENERIC)
		{
			TestResult=TESTFAIL;
			printf("FAIL\n");
		}
		else
		{
			printf("PASS\n");
		}

		/*Test Ftell*/
		printf("TestFtell			");
		if(TestFtell()==ERROR_GENERIC)
		{
			TestResult=TESTFAIL;
			printf("FAIL\n");
		}
		else
		{
			printf("PASS\n");
		}

		/*Test the Make Directories*/
		printf("TestMkdir			");
		if(TestMkdir()==ERROR_GENERIC)   
		{
			TestResult=TESTFAIL;
			printf("FAIL\n");
		}
		else
		{
			printf("PASS\n");
		}

		/* Test Remove Directory*/ 
		printf("TestRmdir			");
		if(TestRmdir()==ERROR_GENERIC)   
		{
			TestResult=TESTFAIL;
			printf("FAIL\n");
		}
		else
		{
			printf("PASS\n");
		}
#ifdef MKDIRW_TEST
		printf("TestMkdirW			");
		if(TestMkdirW()==ERROR_GENERIC)   
		{
			TestResult=TESTFAIL;
			printf("FAIL\n");
		}
		else
		{
			printf("PASS\n");
		}
#endif		/*Test Fgetc (Get one character from file)*/ 
		printf("TestFgetc			");
		if(TestFgetc()==ERROR_GENERIC)   
		{
			TestResult=TESTFAIL;
			printf("FAIL\n");
		}
		else
		{
			printf("PASS\n");
		}

		/*Test Fgets (Get one string from file)*/ 
		printf("TestFgets			");
		if(TestFgets()==ERROR_GENERIC)   
		{
			TestResult=TESTFAIL;
			printf("FAIL\n");
		}
		else
		{
			printf("PASS\n");
		}

		/*Test Fputc (Put one character to the file)*/ 
		printf("TestFputc			");
		if(TestFputc()==ERROR_GENERIC)   
		{
			TestResult=TESTFAIL;
			printf("FAIL\n");
		}
		else
		{
			printf("PASS\n");
		}

		/*Test Fputs (Put one string to the file)*/ 
		printf("TestFputs			");
		if(TestFputs()==ERROR_GENERIC)   
		{
			TestResult=TESTFAIL;
			printf("FAIL\n");
		}
		else
		{
			printf("PASS\n");
		}

		/*Test Fileclose*/
		printf("TestFclose			");
		if(TestFclose()==ERROR_GENERIC)   
		{
			TestResult=TESTFAIL;
			printf("FAIL\n");
		}
		else
		{
			printf("PASS\n");
		}

		/*Test File seek*/
		printf("TestFseek			");
		if(TestFseek()==ERROR_GENERIC)   
		{
			TestResult=TESTFAIL;
			printf("FAIL\n");
		}
		else
		{
			printf("PASS\n");
		}

		/*Testfilegetattrib*/
		printf("Testfilegetattrib			");
		if( Testfilegetattrib()==ERROR_GENERIC)
		{
			TestResult=TESTFAIL;
			printf("FAIL\n");
		}
		else
		{
			printf("PASS\n");
		}

		/*Testfilesetdate*/
		printf("Testfilesetdate			");
		if( Testfilesetdate()==ERROR_GENERIC)   
		{
			TestResult=TESTFAIL;
			printf("FAIL\n");
		}
		else
		{
			printf("PASS\n");
		}

		/*Test Change Directories*/
		printf("TestChdir			");
		if(TestChdir()==ERROR_GENERIC)   
		{
			TestResult=TESTFAIL;
			printf("FAIL\n");
		}
		else
		{
			printf("PASS\n");
		}
#ifdef CHDIRW_TEST
		printf("TestChdirW			");
		if(TestChdirW()==ERROR_GENERIC)   
		{
			TestResult=TESTFAIL;
			printf("FAIL\n");
		}
		else
		{
			printf("PASS\n");
		}
#endif        

		/*Test Fopenw for Read(r) mode*/
		printf("TestFopenwRead			");
		if(TestFopenwRead()==ERROR_GENERIC)   
		{
			TestResult=TESTFAIL;
			printf("FAIL\n");
		}
		else
		{
			printf("PASS\n");
		}

		/*Test Fopenw for Write(w) mode*/
		printf("TestFopenwWrite			");
		if(TestFopenwWrite()==ERROR_GENERIC)   
		{
			TestResult=TESTFAIL;
			printf("FAIL\n");
		}
		else
		{
			printf("PASS\n");
		}

		/*Test Fopenw for Append(a) mode*/
		printf("TestFopenwAppend			");
		if(TestFopenwAppend()==ERROR_GENERIC)   
		{
			TestResult=TESTFAIL;
			printf("FAIL\n");
		}
		else
		{
			printf("PASS\n");
		}

		/*Test Fopenw for Rplus mode*/
		printf("TestFopenwReadPlus			");
		if(TestFopenwReadPlus()==ERROR_GENERIC)   
		{
			TestResult=TESTFAIL;
			printf("FAIL\n");
		}
		else
		{
			printf("PASS\n");
		}

		/*Test Fopenw for Wplus mode*/
		printf("TestFopenwWritePlus			");
		if(TestFopenwWritePlus()==ERROR_GENERIC)   
		{
			TestResult=TESTFAIL;
			printf("FAIL\n");
		}
		else
		{
			printf("PASS\n");
		}

		/*Test Fopenw for Aplus mode*/
		printf("TestFopenwAppendPlus			");
		if(TestFopenwAppendPlus()==ERROR_GENERIC)   
		{
			TestResult=TESTFAIL;
			printf("FAIL\n");
		}
		else
		{
			printf("PASS\n");
		}

	
	#ifdef DELETREETEST
		if(TestDeletTree()==ERROR_GENERIC)
		TestResult=TESTFAIL;

	#endif
	
	#ifdef FINDNEXT_TEST
		printf("TestFindNext			");
	    if((RetValue=Chdir(WorkingDir)) <0)
		TestResult=TESTFAIL;

		if(TestFindNext()==ERROR_GENERIC)    
		{
		 TestResult=TESTFAIL;
		 printf("FAIL\n");
		}
		else
		{
		  printf("PASS\n");
		}


	#endif
	#ifdef NEWADDED

		printf("TestFopenw                     ");
		if(TestFopenw()==ERROR_GENERIC)
		{
			TestResult=TESTFAIL;
			printf("FAIL\n");
		}
		else
		{
			printf("PASS\n");
		}

	#endif
	/* if want to create maximum number of directory define MAXDIRECTORY 
	   maximum number of directories entries: FAT12 : 256, Fat16:512 , FAT32 : NOLIMIT*/

	#ifdef MAXDIRECTORYTEST   
	    if(TestMkdirMax(0)==ERROR_GENERIC)   
		TestResult=TESTFAIL;

	#endif

	#ifdef MAXFILEOPENTEST   
		if(TestFopen()==ERROR_GENERIC)
		TestResult=TESTFAIL;

	#endif

    /*Test Resource File Read*/    
    printf("TestResourceFread       ");
    if(TestResourceFread()==ERROR_GENERIC) 
    {
        TestResult=TESTFAIL;
        printf("FAIL\n");
    }
    else
    {
        printf("PASS\n");
    }

    /*Test Resource File Write*/    
    printf("TestResourceFwrite      ");
    if(TestResourceFwrite()==ERROR_GENERIC) 
    {
        TestResult=TESTFAIL;
        printf("FAIL\n");
    }
    else
    {
        printf("PASS\n");
    }

    /*Test Resource File seek*/
    printf("TestResourceFseek    ");
    if(TestResourceFseek()==ERROR_GENERIC)   
    {
        TestResult=TESTFAIL;
        printf("FAIL\n");
    }
    else
    {
        printf("PASS\n");
    }

    /*Test Resource Fileclose*/
    printf("TestResourceFclose   ");
    if(TestResourceFclose()==ERROR_GENERIC)   
    {
        TestResult=TESTFAIL;
        printf("FAIL\n");
    }
    else
    {
        printf("PASS\n");
    }

	printf("Test complete\n");

}

#ifdef DELETREETEST
int32_t TestDeletTree(void)
{
	int32_t RetValue =0;

	if((RetValue =DeleteTree(DeleteDirectory)) <0)
	{
		return ERROR_GENERIC;
	}
	FlushCache();
	return NOERROR;
}
#endif

////////////////////////////////////////////////////////////////////////////////
//
//>  Name:           TestHandle
//
//   Type:           Function
//
//   Description:    Tests the handle                  
//
//   Inputs:         none 
//
//   Outputs:        none
//                   
//   Notes:          The test verifies that file handles up to maximum number can be allocated.
//<
////////////////////////////////////////////////////////////////////////////////

int  TestHandle()
{
    /*open the maximum number of file*/
    for(i=FIRST_VALID_HANDLE;i<maxhandles;i++)
    {
    if ((fout=Fopen(handletest,(uint8_t *)"r"))<0)
        return  ERROR_GENERIC;
    }
    /*open one more file  this fopen will return error*/
    if ((fout1=Fopen(handletest,(uint8_t *)"r"))>0)
        return  ERROR_GENERIC;
    for(fout=FIRST_VALID_HANDLE;fout<maxhandles;fout++)
    {
        Fclose(fout);
    }
    return NOERROR;
}
////////////////////////////////////////////////////////////////////////////////
//
//>  Name:           TestReadMode
//
//   Type:           Function
//
//   Description:    Tests the read mode for function Fopen                   
//
//   Inputs:         none 
//
//   Outputs:        none
//                   
//   Notes:          Verify Read Mode ("r"). If file does not present, function should return error.
//					 we can only do read operation from file in this mode.
//<
////////////////////////////////////////////////////////////////////////////////
int TestReadMode()
{    
    /* Open file in  read mode (Function fopen will fail, if file  does not exist) */
    if ((fout=Fopen(testfile,(uint8_t *)"r"))<0)
        return  ERROR_GENERIC;
    /*read some bytes*/
    if(Fread(fout,ReadBuffer,56) < 0)
    {
        Fclose(fout);
        return ERROR_GENERIC;
    }              
    /*Attempt to write to a file opened in the read mode, It should fail */   
    if(Fwrite(fout,BitBuffer,25) > 0)
    {
        Fclose(fout);
        return ERROR_GENERIC;
    }
    Fclose(fout);
    /* Attempt to open a non-existent file */
    if ((fout = Fopen(Nofile,(uint8_t *)"r"))>0)
        return  ERROR_GENERIC;
    FlushCache();
    return NOERROR;
}   
////////////////////////////////////////////////////////////////////////////////
//
//>  Name:           TestwriteMode
//
//   Type:           Function
//
//   Description:    Tests the write mode for function Fopen                   
//
//   Inputs:         none 
//
//   Outputs:        none
//                   
//   Notes:          Open the file in write mode and try to read and write using different handles.
//                   If the file is not present, it will create the file.                    
//<
////////////////////////////////////////////////////////////////////////////////
int   TestWriteMode()
{    
    int32_t fin,BytesToRead;
    int32_t Filesize,NumBytesToRead,NumByteToWrite=0;
    int BytesToWrite=512;
    /* Open file in  write mode (if the file is not present, it will create the file) */
    if ((fout=Fopen(writefile1,(uint8_t *)"w"))<0)
        return ERROR_GENERIC;
    /* Open for Read mode for filling Readbuffer*/  
    if ((fin=Fopen(readfile,(uint8_t *)"r"))<0)
        return ERROR_GENERIC;
    /*Seek to the End of file to get file size*/
    if((RetValue=Fseek(fin,0,SEEK_END))<0)
        return ERROR_GENERIC;
    Filesize=Ftell(fin);
    if((RetValue=Fseek(fin,0,SEEK_SET))<0)
        return ERROR_GENERIC;

    /* try to write  the whole file which is open in write mode*/   
    NumBytesToRead =Filesize;
    BytesToRead =0;
    while(NumByteToWrite < Filesize)
    {
        if((NumBytesToRead)>0)
        {
            if(NumBytesToRead> BytesToRead )
                BytesToRead=BytesToWrite;
            else
                BytesToRead = NumBytesToRead;
       
            if((RetValue = Fread(fin,ReadBuffer,BytesToRead))<0)
                return ERROR_GENERIC;
            if((RetValue = Fwrite(fout,ReadBuffer,BytesToRead))<0)
                return ERROR_GENERIC;
        NumByteToWrite+=RetValue;
        }
        NumBytesToRead-=BytesToWrite ;
        if((RetValue==0) ||(NumByteToWrite==Filesize))
        {
            NumByteToWrite = Filesize+1;
        }     
    }
   /*close open file */
    Fclose(fin);
    Fclose(fout);           
    FlushCache();
    return  NOERROR;
}
////////////////////////////////////////////////////////////////////////////////
//
//>  Name:           TestAppendMode
//
//   Type:           Function
//
//   Description:    Tests the append mode for function Fopen  
//                 
//   Inputs:         none 
//
//   Outputs:        none
//                   
//   Notes:          Open the file in append mode. If write operation is done, it will append the data
//                   at the end  of the file. If file is not present,it will create the file.
//<
////////////////////////////////////////////////////////////////////////////////
int   TestAppendMode()
{
    int32_t fin,BytesToRead;
    LONG Filesize,NumBytesToRead,NumByteToWrite=0;
    int BytesToWrite=512;
    
    /* Open for append mode (if file is not present,it will create the file) */
    if ((fout1=Fopen(writefile1,(uint8_t *)"a"))<0)
        return ERROR_GENERIC;
	/*Open the file in read mode*/
    if ((fin=Fopen(testfile3,(uint8_t *)"r"))<0)
        return ERROR_GENERIC;
    /*Seek to the End of file to get file size*/
    if((RetValue=Fseek(fin,0,SEEK_END))<0)
        return ERROR_GENERIC;
    Filesize=Ftell(fin);
    if((RetValue=Fseek(fin,0,SEEK_SET))<0)
        return ERROR_GENERIC;
    /* Try to write  the whole file which is open in append mode*/   
    NumBytesToRead =Filesize;
    BytesToRead =0;
    while(NumByteToWrite < Filesize)
    {
        if((NumBytesToRead)>0)
        {
            if(NumBytesToRead> BytesToRead )
                BytesToRead=BytesToWrite;
            else
                BytesToRead =NumBytesToRead;
       
            if((RetValue = Fread(fin,ReadBuffer,BytesToRead))<0)
                return ERROR_GENERIC;
            if((RetValue =Fwrite(fout1,ReadBuffer,BytesToRead))<0)
                return ERROR_GENERIC;
        NumByteToWrite+=RetValue;
        }
        NumBytesToRead-=BytesToWrite ;
        if((RetValue==0) ||(NumByteToWrite==Filesize))
        {
            NumByteToWrite = Filesize+1;
        }     
    }
   /*close open file */
    Fclose(fout1);
    Fclose(fin);
    FlushCache();
    return NOERROR;
}
////////////////////////////////////////////////////////////////////////////////
//
//>  Name:           TestReadplusMode
//
//   Type:           Function
//
//   Description:    Tests the Readplus mode for function Fopen                   
//
//   Inputs:         none 
//
//   Outputs:        none
//                   
//   Notes:          In this mode we can read as well as write in the file.
//                   If file is not present it will return error.
//<
////////////////////////////////////////////////////////////////////////////////

int   TestReadPlusMode()
{
    int32_t fin,BytesToRead;
    LONG Filesize,NumBytesToRead,NumByteToWrite=0;
    int BytesToWrite=512;

    /*Attempt to open file which is not exist,it should return error*/
    if ((fout = Fopen(Nofile,(uint8_t *)"r+"))>0)
        return  ERROR_GENERIC;
    /*Open file in  read plus mode(Fopen fails if file  does not exist)*/
    if ((fout = Fopen(testfile,(uint8_t *)"r+"))<0)
        return  ERROR_GENERIC;
        
    if ((fin=Fopen(testfile3,(uint8_t *)"r"))<0)
        return ERROR_GENERIC;
    /*Seek to the End of file to get file size*/
    if((RetValue=Fseek(fin,0,SEEK_END))<0)
        return ERROR_GENERIC;
    Filesize=Ftell(fin);
    
    if((RetValue=Fseek(fin,0,SEEK_SET))<0)
        return ERROR_GENERIC;
        
    /* Try to write  the whole file which is open in ("r+") mode*/   
    NumBytesToRead =Filesize;
    BytesToRead =0;
    while(NumByteToWrite < Filesize)
    {
        if((NumBytesToRead)>0)
        {
            if(NumBytesToRead> BytesToRead )
                BytesToRead=BytesToWrite;
            else
                BytesToRead =NumBytesToRead;
       
            if((RetValue = Fread(fin,ReadBuffer,BytesToRead))<=0)
                return ERROR_GENERIC;
            if((RetValue =Fwrite(fout,ReadBuffer,BytesToRead))<=0)
                return ERROR_GENERIC;
        NumByteToWrite+=RetValue;
        }
        NumBytesToRead-=BytesToWrite ;
        if((RetValue==0) ||(NumByteToWrite==Filesize))
        {
            NumByteToWrite = Filesize+1;
        }     
    }
   /*close open file */
     Fclose(fout);
     Fclose(fin);
     FlushCache();
     return NOERROR;
}
////////////////////////////////////////////////////////////////////////////////
//
//>  Name:           TestWriteplusMode
//
//   Type:           Function
//
//   Description:    Tests the writeplus mode for function Fopen                   
//
//   Inputs:         none 
//
//   Outputs:        none
//                   
//   Notes:          In this mode we can read as well write in the file.
//                   If the file is not present, function Fopen will create the file.
//<
////////////////////////////////////////////////////////////////////////////////
int   TestWritePlusMode()
{
     /*open the file in the writeplus(w+) mode*/
    if ((fout = Fopen(testfile4,(uint8_t *)"w+"))<0)
        return  ERROR_GENERIC;
    /*write some byte to file*/    
    if((RetValue =Fwrite(fout,BitBuffer,100))<0)
    {
        Fclose(fout);
        return ERROR_GENERIC;
    }
	/*Seek back the byte written*/
    if((RetValue=Fseek(fout,-100,SEEK_CUR))<0)
    {
        Fclose(fout);
        return ERROR_GENERIC;
    }
    /*Read back data that we had written*/
    if((RetValue = Fread(fout,ReadBuffer,100))<0)
    {
        Fclose(fout);
        return ERROR_GENERIC;
    } 
   /*Check we had  done correctly write and read in this mode*/    
    if(BitBuffer[0]!= ReadBuffer[0])
    {
        Fclose(fout);
        return ERROR_GENERIC;
    }
   /*close open file */
    Fclose(fout);
    FlushCache();
    return NOERROR;
}
////////////////////////////////////////////////////////////////////////////////
//
//>  Name:           TestAppendplusMode
//
//   Type:           Function
//
//   Description:    Tests the Appendplus ("a+") mode for function Fopen                   
//
//   Inputs:         none 
//
//   Outputs:        none
//                   
//   Notes:          In this mode we can read as well write in the file.
//                   If the file is not present, function Fopen will create the file in this mode.
//<
////////////////////////////////////////////////////////////////////////////////
int   TestAppendPlusMode()
{
    /*open file in appendplus mode (if the file is not present,function Fopen will create the file)*/
    if ((fout = Fopen(writefile,(uint8_t *)"a+"))<0)
        return  ERROR_GENERIC;
    /*Write to the file it should write at the end of the file*/        
    if((RetValue =Fwrite(fout,BitBuffer,50))<0)
    {
        Fclose(fout);
        return ERROR_GENERIC;
    }
    /* seek back the bytes write*/
    if((RetValue =Fseek(fout,-50,SEEK_CUR))<0)
    {
        Fclose(fout);
        return ERROR_GENERIC;
    }
    /*Try to read bytes which we had written*/
    if((RetValue = Fread(fout,ReadBuffer,50))<0)
    {
        Fclose(fout);
        return ERROR_GENERIC;
    }
   /*Check we had  done correctly write and read in this mode*/    
    if(BitBuffer[0]!= ReadBuffer[0])
    {
        Fclose(fout);
        return ERROR_GENERIC;
    }
   /*close open file */
    Fclose(fout);
    FlushCache();   
    return NOERROR;
}
////////////////////////////////////////////////////////////////////////////////
//
//>  Name:           TestFeof
//
//   Type:           Function
//
//   Description:    Tests the EOF (End of file) condition for file  
//                
//   Inputs:         none 
//
//   Outputs:        none
//                   
//   Notes:          It will test the End of file test.

////////////////////////////////////////////////////////////////////////////////

int    TestFeof()
{
    /*Open file in the append mode*/
    if ((fout2 = Fopen(testfile,(uint8_t *)"a+"))<0)
        return  ERROR_GENERIC;
   /*it should return the EOF*/     
    if(Feof(fout2)>0)
    {
        Fclose(fout2);
        return  ERROR_GENERIC;
    }
    /*Write some bytes to the file*/    
    if((RetValue=Fwrite(fout2,BitBuffer,25))<0)
    {
        Fclose(fout2);
        return ERROR_GENERIC;
    }
	/*Seek back number of bytes written*/
    if((RetValue=Fseek(fout2,-25,SEEK_CUR))<0)
        return  ERROR_GENERIC;         
    /* It should not Return EOF,it should return remain bytes from end of file*/
    if((RetValue=Feof(fout2))<0)
    {
        Fclose(fout2);
        return  ERROR_GENERIC;
    } 
   /*close open file */
    Fclose(fout2);
    return NOERROR;
}
////////////////////////////////////////////////////////////////////////////////
//
//>  Name:           TestFremove
//
//   Type:           Function
//
//   Description:    Tests the file is Remove or not
//
//   Inputs:         none 
//
//   Outputs:        none
//                   
//   Notes:          If the file is in read only mode it should not be deleted.
//<
////////////////////////////////////////////////////////////////////////////////
int   TestFremove()
{
    /*Remove the file*/ 
    if(Fremove(writefile1)<0)
       return  ERROR_GENERIC; 
    FlushCache();
    /*Try to open removed file ,it should give Error*/
    if ((fout = Fopen(removefile,(uint8_t *)"r"))>0)
        return  ERROR_GENERIC;
   /*Open the file*/        
    if ((fout2 = Fopen(testfile3,(uint8_t *)"r"))<0)
        return  ERROR_GENERIC;
    /* Try to Delete the Opened file it Fails*/
    if(Fremove(testfile3)>0)
        return  ERROR_GENERIC;
    /*Try to deleted read-only file,it should return error*/    
    if(Fremove(testfile6)>0)
        return  ERROR_GENERIC; 
   /*Try to open deleted file,it should open the file because file is not deleted (it is read only)*/     
    if ((fout = Fopen(testfile6,(uint8_t *)"r"))<0)
        return  ERROR_GENERIC;
    /*Try to Delete the Directory using function Fremove, it should fail*/ 
    if(Fremove(testfile5)>0)
        return  ERROR_GENERIC;
   /*close open file */
    Fclose(fout2);
	Fclose(fout);
    FlushCache();
    return NOERROR;
}
////////////////////////////////////////////////////////////////////////////////
//
//>  Name:           TestFtell
//
//   Type:           Function
//
//   Description:    Tests the Ftell to Get the current offset in the file 
//
//   Inputs:         none 

//   Outputs:        none
//                   
//   Notes:          This function will get current position of file pointer.
//<
////////////////////////////////////////////////////////////////////////////////

int   TestFtell()
{
    int32_t CurrentOffset;
    /*Open the file in read mode*/
    if ((fin=Fopen(testfile2,(uint8_t *)"r"))<0)
        return  ERROR_GENERIC;
    /*Get current position in the file*/    
    CurrentOffset=Ftell(fin); 
    /*Seek some bytes in file and again do Ftell to get Current pointer*/
    RetValue=Fseek(fin,51,SEEK_SET);
    CurrentOffset=Ftell(fin); 
    /*Open the file in Append mode and try to do Ftell it Should Return EOF*/
    if ((fin1=Fopen(testfile1,(uint8_t *)"a"))<0)
        return  ERROR_GENERIC;
    CurrentOffset=Ftell(fin1); 
   /*close open file */
    Fclose(fin);
    Fclose(fin1);
    return NOERROR;
}
////////////////////////////////////////////////////////////////////////////////
//
//>  Name:           TestMkdir
//
//   Type:           Function
//
//   Description:    Tests the Creation of Directory
// 
//   Inputs:         none 
//
//   Outputs:        none
//                   
//   Notes:          Create the directory in differnt level.
//                   Maxiumum Root Directory Possible -> FAT 12 Or FAT16 = 256
//                                                       FAT 32  = No Limit
//<
////////////////////////////////////////////////////////////////////////////////
int LoopCount=0;                          
uint8_t DirectoryName[50*3];

int TestMkdir()
{    
    /*Try to Create Directory in the roort*/ 
    if((RetValue=Mkdir(rootdir))<0)
        return  ERROR_GENERIC;
    FlushCache();
	/*Try to create directory with same name funcrion Mkdir will return error*/
    if((RetValue=Mkdir(rootdir))>0)
        return  ERROR_GENERIC;
    FlushCache();
    /*Try to Create Directory in the Diffrent level*/         
    /*Try to create the directory in level 1 from root*/
    if((RetValue=Mkdir(level1dir))<0)
        return  ERROR_GENERIC;
    FlushCache();
    /*Try to create the directory in level 2 from root*/
    if((RetValue=Mkdir(level2dir))<0)
        return ERROR_GENERIC;
    FlushCache();
    if((RetValue=Mkdir(level3dir))<0)
        return  ERROR_GENERIC;
    FlushCache();
	/*try to create the directory at level four from root directory.*/        
    if((RetValue=Mkdir(level5dir))<0)
        return   ERROR_GENERIC;
    FlushCache();
    /* Try to create Directory which is already present, it should return Error*/     
    if((RetValue=Mkdir(level6dir))>0)
        return ERROR_GENERIC;
        
    FlushCache();
    return NOERROR;
}                     
#ifdef MKDIRW_TEST
////////////////////////////////////////////////////////////////////////////////
//
//>  Name:           TestMkdirW
//
//   Type:           Function
//
//   Description:    Tests the Creation of Directory for Unicode convention.
// 
//   Inputs:         none 
//
//   Outputs:        none
//                   
//   Notes:          Create the directory in differnt level.
//                   Maxiumum Root Directory Possible -> FAT 12 Or FAT16 = 256
//                                                       FAT 32  = No Limit
//<
////////////////////////////////////////////////////////////////////////////////
int TestMkdirW()
{
    if((RetValue=Mkdirw(longdirbuf))<0)
        return  ERROR_GENERIC;
    FlushCache();
    /*Try to create the same directory again MkdirW should return ERROR_GENERIC*/
    if((RetValue=Mkdirw(longdirbuf))>0)
        return  ERROR_GENERIC;
    FlushCache();
    /*Try to create the  directory with . or .. or ...without any name, MkdirW should return ERROR_GENERIC*/
    if((RetValue=Mkdirw(longdir1buf))>0)
        return  ERROR_GENERIC;
    FlushCache();
    /*Try to create the  directory with . or .. or ...withname e.g ..1..abc123def, MkdirW should create it*/
    if((RetValue=Mkdirw(longdir2buf))<0)
        return  ERROR_GENERIC;
    FlushCache();
    /*Try to create the  directory / append after directory name, MkdirW discard / and  create it*/
    if((RetValue=Mkdirw(longdir3buf))<0)
        return  ERROR_GENERIC;
    FlushCache();

    /*Try to create the  directory at second level from root directory*/
    if((RetValue=Mkdirw(longdir4buf))<0)
        return  ERROR_GENERIC;
    FlushCache();
    /*Try to create the  directory atwith illegal character set,mkdirw should return error*/
    if((RetValue=Mkdirw(longdir5buf))>0)
        return  ERROR_GENERIC;
    FlushCache();
    if ((fout=Fopen(longdirFopen,(uint8_t *)"w"))<0)
        return ERROR_GENERIC;
    FlushCache();
    Fclose(fout);
    return NOERROR;
}    
#endif
////////////////////////////////////////////////////////////////////////////////
//
//>  Name:           TestFopen

//   Type:           Function
//
//   Description:    Tests creatiopn of files

//   Inputs:         none 

//   Outputs:        none
//                   
//   Notes:          This test will crate the file up to no free clusteris avalibale
//<
////////////////////////////////////////////////////////////////////////////////
#ifdef MAXFILEOPENTEST
                            
int TestFopen(void)
{
    int i,Byte=0,j,m,k,l=0;

    PutByte(DirectoryName,'a',l++);
    PutByte(DirectoryName,':',l++);
    PutByte(DirectoryName,'/',l++);
    PutByte(DirectoryName,'T',l++);
    PutByte(DirectoryName,'E',l++);
    PutByte(DirectoryName,'S',l++);
    PutByte(DirectoryName,'T',l++);
    PutByte(DirectoryName,'D',l++);
    PutByte(DirectoryName,'I',l++);
    PutByte(DirectoryName,'R',l++);
    PutByte(DirectoryName,'1',l++);
    PutByte(DirectoryName,'/',l++);
    PutByte(DirectoryName,'T',l++);
    PutByte(DirectoryName,'E',l++);
    PutByte(DirectoryName,'S',l++);
    PutByte(DirectoryName,'T',l++);
    PutByte(DirectoryName,'C',l++);
    PutByte(DirectoryName,'O',l++);
    PutByte(DirectoryName,'P',l++);
    PutByte(DirectoryName,'Y',l++);

	if((Mkdir(rootdir))<0)
		return ERROR_GENERIC;
   
   for(m=1; m <= 8 ; m++)
   {
	    for( i='0' ;i <= '9';i ++)
	    {
             PutByte(DirectoryName,i,l);
             for(j ='0'; j <='9'; j++)
             {
                for(k=1;k <=m; k++)
                {
                    PutByte(DirectoryName,j,k+l);
                }
                if((fout=Fopen(DirectoryName,(uint8_t *)"w")) < 0)
                {
                    FlushCache(); 
                    return -1;
                }
                Fclose(fout);
             }   
	    }
   }  
    
  return NOERROR;
}
#endif


////////////////////////////////////////////////////////////////////////////////
//
//>  Name:           TestRmdir

//   Type:           Function
//
//   Description:    Test the Deletion of Directory 

//   Inputs:         none 					   

//   Outputs:        none
//                   
//   Notes:          if dir is not empty,it will return error,
//                   if try to remove root directory or Current working directory, it will return error.                   //<
////////////////////////////////////////////////////////////////////////////////
int    TestRmdir()
{
    
    /*Try to remove Directory from root*/ 
    if((RetValue=Rmdir(rootdir))<0)
        return  ERROR_GENERIC;
    FlushCache(); 
    /*Try to Remove the directory  level 1 */
    if((RetValue=Rmdir(level1dir))<0)
        return  ERROR_GENERIC;
    FlushCache();    
    /*Try to Remove the directory in level 2 */
    if((RetValue=Rmdir(level2dir))<0)
        return  ERROR_GENERIC;
    FlushCache();    
    if((RetValue=Rmdir(level3dir))<0)
        return  ERROR_GENERIC;
    FlushCache();    
   /*Try to Remove Directory from level4*/
    if((RetValue=Rmdir(level5dir))>0)
        return  ERROR_GENERIC;
    FlushCache();
    /*Try to remove the file using this function Rmdir, it will fail.*/
    if((RetValue=Rmdir(testfile2))>0)
        return  ERROR_GENERIC;
    FlushCache();    
    /*Try to Remove Directory Which is not empty it should return error*/
    if((RetValue=Rmdir(level6dir))>0)
        return  ERROR_GENERIC;
    /*Try to remove root directory it should give error*/        
    if((RetValue=Rmdir(chroot))>0)
        return  ERROR_GENERIC;
        
    return NOERROR;
} 
////////////////////////////////////////////////////////////////////////////////
//
//>  Name:           TestFgetc

//   Type:           Function
//
//   Description:    Test the Fgetc 

//   Inputs:         none 

//   Outputs:        none
//                   
//   Notes:          Get one character from the file.                   //<
////////////////////////////////////////////////////////////////////////////////
                                                 
int    TestFgetc()
{
    int32_t FileSize;
    int32_t temp;
    
    /*Open the file in read mode*/
    if ((fout = Fopen(testget,(uint8_t *)"r"))<0)
        return  ERROR_GENERIC;
    /*Get one character from the file and put it in the buffer*/
    if((GetBuffer[1]=Fgetc(fout))<0)
        return  ERROR_GENERIC;
    /*Seek some Bytes and Get one character from the file and put it in the buffer*/
    Fseek(fout,100,SEEK_SET);
    if((GetBuffer[2]=Fgetc(fout))<0)
        return  ERROR_GENERIC;
    /*Get the file size    */
    Fseek(fout,0,SEEK_END);
    FileSize=Ftell(fout);
    if((RetValue=Fseek(fout,0,SEEK_SET))<0)
        return ERROR_GENERIC;
    /* Read whole file by getting one character from file*/
    for(i=0;i<FileSize;i++)                     
    { 
        if((GetBuffer[i]=Fgetc(fout))<0)
            return  ERROR_GENERIC;
    }
	/*Verify that we had get correct data */
    for(i=0;i<FileSize;i++)                     
    { 
       if(GetBuffer[i]!=(0x00+i))
           return ERROR_GENERIC;
    }
	Fclose(fout);
    if ((fout = Fopen(getfile,(uint8_t *)"a"))<0)
        return  ERROR_GENERIC;
        
    /*Write back whole file*/
    for(i=0;i<FileSize;i++)
    { 
        if((RetValue=Fwrite(fout,(uint8_t *)&GetBuffer[i],1))<=0)
        {
            Fclose(fout);
            return ERROR_GENERIC;
        } 
    }
    Fclose(fout);
    FlushCache();
    /*Open file i the append mode */ 
    if ((fout = Fopen(testfile2,(uint8_t *)"a"))<0)
        return  ERROR_GENERIC;
    /* Try to get one character from file it should return EOF*/
    if((temp=Fgetc(fout))>0)
        return  ERROR_GENERIC;
   /*close open file */
    Fclose(fout);
    return  NOERROR;
}
////////////////////////////////////////////////////////////////////////////////
//
//>  Name:           TestFgets

//   Type:           Function
//
//   Description:    Test the TestFgets 

//   Inputs:         none 

//   Outputs:        none
//                   
//   Notes:          Get one String from the file.                   //<
////////////////////////////////////////////////////////////////////////////////
                   
int    TestFgets()
{
    uint8_t *Char;
    uint8_t Buffer[100*3];

	/*Open the file in read mode*/
    if ((fout = Fopen(testfile2,(uint8_t *)"r"))<0)
        return  ERROR_GENERIC;
    /*Try to read one string from file*/
    Char = Fgets(fout,95,(uint8_t *)Buffer);
    /*Open the file in append mode*/
    if ((fout1 = Fopen(testfile2,(uint8_t *)"a+"))<0)
        return  ERROR_GENERIC;
    /*get back 10 bytes from EOF*/
    Fseek(fout1,-10,SEEK_CUR);
    /*get 25 bytes from current position it should read only 10 bytes*/
    Char = Fgets(fout1,25,(uint8_t *)Buffer);
   /*close open file */
    Fclose(fout);
    Fclose(fout1);
    return  NOERROR;

}
////////////////////////////////////////////////////////////////////////////////
//
//>  Name:           TestFputc

//   Type:           Function
//
//   Description:    Test the TestFputc 

//   Inputs:         none 

//   Outputs:        none
//                   
//   Notes:          Put one Character to  the file.                   //<
////////////////////////////////////////////////////////////////////////////////
int    TestFputc()
{
    LONG FileSize;

	/*Open the file in read mode*/    
    if ((fout = Fopen(testget,(uint8_t *)"r"))<0)
        return  ERROR_GENERIC;
    /*Get the file size    */
    Fseek(fout,0,SEEK_END);
    FileSize=Ftell(fout);
    if((RetValue=Fseek(fout,0,SEEK_SET))<0)
        return ERROR_GENERIC;
    /*read whole file with function Fgetc*/    
    for(i=0;i<FileSize;i++)                     
    { 
       if((GetBuffer[i]=Fgetc(fout))<0)
       {    
        Fclose(fout);
        return  ERROR_GENERIC;
       }
    }
   /*open the file in append mode to do write through function Fputc */ 
    if ((fout1 = Fopen(getfile,(uint8_t *)"a"))<0)
        return  ERROR_GENERIC;
    /*Write whole file back through function Fputc*/
    for(i=0;i<FileSize;i++)                     
    { 
        if((Fputc(fout1,GetBuffer[i]))<0)
        {                                        
            Fclose(fout1);
            return  ERROR_GENERIC;
        }
    }
   /*close open file */
    Fclose(fout);
    Fclose(fout1);
    FlushCache();
    return  NOERROR;
}

////////////////////////////////////////////////////////////////////////////////
//
//>  Name:           TestFputs

//   Type:           Function
//
//   Description:    Test the TestFputs 

//   Inputs:         none 

//   Outputs:        none
//                   
//   Notes:          Put one string from the file.                   //<
////////////////////////////////////////////////////////////////////////////////
int   TestFputs()
{
    uint8_t Buffer[100*3];
    uint8_t btitle[30*3] = "Test Put String"; 
    uint8_t *Char;
    
    /*open the file in Read mode*/
    if ((fout = Fopen(testfile2,(uint8_t *)"r"))<0)
        return  ERROR_GENERIC;
    Fseek(fout,100,SEEK_SET);
    if (Fgets(fout,55,(uint8_t *)Buffer) == (uint8_t *)0)
        return  ERROR_GENERIC;
   /*open the file in Write mode*/  
    if ((fout1 = Fopen(putfile,(uint8_t *)"w"))<0)
        return  ERROR_GENERIC;
	/*Put btitle string to the opend fie */
    Fputs(fout1,(uint8_t*)btitle);
    if((Char =Fputs(fout1,Buffer)) ==(uint8_t *)0)
        return  ERROR_GENERIC;
	/*Put btitle string to the opend fie */
    Fputs(fout1,(uint8_t*)btitle);
    /*close open file */    
    Fclose(fout);
    Fclose(fout1);
    FlushCache();
    return  NOERROR;
}
////////////////////////////////////////////////////////////////////////////////
//
//>  Name:           TestFclose

//   Type:           Function
//
//   Description:    Test the File close function

//   Inputs:         none 

//   Outputs:        none
//                   
//   Notes:          This test will check the Fclose function                 //<
////////////////////////////////////////////////////////////////////////////////

int   TestFclose()
{
    
    /*open the file*/
    if ((fout = Fopen(testfile1,(uint8_t *)"w"))<0)
        return  ERROR_GENERIC;
   /* Close the file*/     
    if((Fclose(fout))<0)
        return  ERROR_GENERIC;
    /*Again close the same file,it should return error*/
    if((Fclose(fout))>0)
        return  ERROR_GENERIC;
    /*open the file*/    
    if ((fout = Fopen(testfile1,(uint8_t *)"a"))<0)
        return  ERROR_GENERIC;
    /*Try to write some bytes*/        
    if((RetValue=Fwrite(fout, BitBuffer,755))<0)
        return ERROR_GENERIC;
    /*close the file*/    
    Fclose(fout);
    FlushCache();
    /*Try to write in closed file, it should return error*/
    if((RetValue=Fwrite(fout, BitBuffer,755))<0)
        return NOERROR;

    return  NOERROR;
}  
////////////////////////////////////////////////////////////////////////////////
//
//>  Name:           TestFseek

//   Type:           Function
//
//   Description:    Test the File seek function

//   Inputs:         none 

//   Outputs:        none
//                   
//   Notes:          This test check the Fseek function.
                //<  SEEK_SET=seek from Starting point of  file
//                   SEEK_END=sekk from end
//                   SEEK_CUR = Seek the current position of file.
////////////////////////////////////////////////////////////////////////////////
int   TestFseek()
{
    LONG Filesize;
    int32_t Currentpointer=0;
    int32_t NumBytesToRead=30;
    uint8_t *Buf=ReadBuffer;

    /*Open the file in read mode to perform test for Fseek function.*/
    if ((fin = Fopen(SeekFile,(uint8_t *)"r"))<0)
        return  ERROR_GENERIC;
    /*Get the file size*/    
    if((RetValue=Fseek(fin,0,SEEK_END))<0)
        return ERROR_GENERIC;
    Filesize=Ftell(fin);
    
    if((RetValue=Fseek(fin,0,SEEK_SET))<0)
        return ERROR_GENERIC;
    /*Seek from start to arbitarily large location  and read*/
    if ((RetValue = Fseek(fin,0xc0000,SEEK_SET))<0)
        return  ERROR_GENERIC;
    Currentpointer=Ftell(fin);
    if( (RetValue = Fread(fin,Buf,NumBytesToRead))<0)
        return ERROR_GENERIC;
    Buf += NumBytesToRead/3;
    /*Seek from start to 0 bytes and read*/
    if ((RetValue = Fseek(fin,0,SEEK_SET))<0)
        return  ERROR_GENERIC;
    if( (RetValue = Fread(fin,Buf,NumBytesToRead))<0)
        return ERROR_GENERIC;
    Buf += NumBytesToRead/3;
    /*Seek from start to whole file and read*/
    if ((RetValue = Fseek(fin,Filesize,SEEK_SET))<0)
        return  ERROR_GENERIC;
    if( (RetValue = Fread(fin,Buf,NumBytesToRead))>0)
        return ERROR_GENERIC;
    Buf += NumBytesToRead/3;
    /*Seek from end to 0 bytes and read*/
    if ((RetValue = Fseek(fin,0,SEEK_END))<0)
        return  ERROR_GENERIC;
    if( (RetValue = Fread(fin,Buf,NumBytesToRead))>0)
        return ERROR_GENERIC;
    Buf += NumBytesToRead/3;
    /*this seeks to the end of file and then WAY past the end of file */
    if ((RetValue = Fseek(fin,Filesize,SEEK_END))<0)
        return  ERROR_GENERIC;
    if( (RetValue = Fread(fin,Buf,NumBytesToRead))>0)   /* attempt to read past end of file, should return error  */
        return ERROR_GENERIC;

    Buf += NumBytesToRead/3;
    /*Set the offest to some location yo check SEEK_CUR*/
    if ((RetValue = Fseek(fin,NumBytesToRead,SEEK_SET))<0)
        return  ERROR_GENERIC;
    
    /*Seek from current to end of file */
    if ((RetValue = Fseek(fin,(-NumBytesToRead+Filesize),SEEK_CUR))<0)
        return  ERROR_GENERIC;
    if( (RetValue = Fread(fin,Buf,NumBytesToRead))>0)  /* attempt to read past end of file, should return error  */
        return ERROR_GENERIC;

    Buf += NumBytesToRead/3;
    /*close open file*/
    Fclose(fin);
    return NOERROR;                                          
    
}
////////////////////////////////////////////////////////////////////////////////
//
//>  Name:           Testfilegetattrib

//   Type:           Function
//
//   Description:    Test the Get Attribute and File set attribute function 

//   Inputs:         none 

//   Outputs:        none
//                   
//   Notes:          none                //<
////////////////////////////////////////////////////////////////////////////////

int   Testfilegetattrib()
{
    int32_t Attribute;
    /*Open the file in read mode to perform test filegetattrib*/
    if ((fin = Fopen(AttrFile,(uint8_t *)"r"))<0)
        return  ERROR_GENERIC;
    /* get the current attribute of the file*/
    Attribute=filegetattrib(AttrFile);
    Attribute=filegetattrib(AttrFile);  
    /* set the attribute*/                            
    filesetattrib(fin,READ_ONLY+ARCHIVE); 
    /*GET the Attribute*/           
    Attribute=filegetattrib(AttrFile);
	Fclose(fin);                
	/*Try to open directory using function Fopen,it should return error*/                            
    if ((fin = Fopen(Attrdir,(uint8_t *)"r"))>0)
        return  ERROR_GENERIC;                                          
    return NOERROR;                                           
}  
////////////////////////////////////////////////////////////////////////////////
//
//>  Name:           Testfilesetdate

//   Type:           Function
//
//   Description:    Test the Get Date and time and  set date and time function

//   Inputs:         none 

//   Outputs:        none
//                   
//   Notes:          none                //<
////////////////////////////////////////////////////////////////////////////////
 
int   Testfilesetdate()
{

    DIR_DATE dirdate;
    DIR_TIME dirtime; 

    if ((fin = Fopen(AttrFile,(uint8_t *)"r"))<0)
        return  ERROR_GENERIC;
    /* Get the date*/
    filegetdate(fin,CREATION_DATE,&dirdate,&dirtime);
    /* give some date to set*/
    dirdate.Day = 2;
    dirdate.Month = 9;
    dirdate.Year = 2003;
    /* set the date*/                               
    filesetdate(AttrFile,CREATION_DATE,&dirdate,&dirtime);
	/*get the date*/
    filegetdate(fin,CREATION_DATE,&dirdate,&dirtime);
	/*set modification date for file opened in read mode*/
    dirdate.Day = 3;
    dirdate.Month = 8;
    dirdate.Year = 2003;
    filesetdate(AttrFile,MODIFICATION_DATE,&dirdate,&dirtime);
    /*set modification time for file*/
    dirtime.Second = 5;   
    dirtime.Minute = 5;      
    dirtime.Hour = 10;
    filesetdate(AttrFile,MODIFICATION_TIME,&dirdate,&dirtime);
	/*closed open file*/
	Fclose(fin);
    FlushCache();
    return NOERROR;
}    
////////////////////////////////////////////////////////////////////////////////
//
//>  Name:           TestChdir

//   Type:           Function
//
//   Description:    Test the change Directory function

//   Inputs:         none 

//   Outputs:        none
//                   
//   Notes:          Chnge the directory to differnt level accoding to fatdir structure.
//                   This test also checks for the change directory to differrnt drive. 
///                                     //<
////////////////////////////////////////////////////////////////////////////////

int   TestChdir()
{
    uint8_t *buffer_1;
    
    /*change to given path*/
    if((Chdir(level6dir))<0)
    return ERROR_GENERIC;
    /*Try to open file in this Directory*/
    if ((fin = Fopen(chfile,(uint8_t *)"r"))<0)
        return  ERROR_GENERIC;
    Fclose(fin);
    /*try ro change at level1*/    
    if((Chdir(chlvel1))<0)
    return ERROR_GENERIC;
    /* get current working  Directory*/
    buffer_1 = Getcwd();
    if((Chdir(chlevel2))<0)
    return ERROR_GENERIC;
    if((Chdir(level6dir))<0)
    return ERROR_GENERIC;
    /*Change to level 3 and try to open file from there*/
    if((Chdir(chlevel3))<0)
    return ERROR_GENERIC;
    if ((fin = Fopen(chfile2,(uint8_t *)"w"))<0)
        return  ERROR_GENERIC;
    Fclose(fin);
    FlushCache();
	/*Again change to level 6 directory*/
    if((Chdir(level6dir))<0)
    return ERROR_GENERIC;
	/*Try to change 4 level from Current Working Directory*/
    if((Chdir(chlevel4))<0)
    return ERROR_GENERIC;
	/*Create the file in this directory*/
    if ((fin = Fopen(chfile2,(uint8_t *)"w"))<0)
        return  ERROR_GENERIC;
    Fclose(fin);
    FlushCache();
    buffer_1 =Getcwd();
    if((Chdir(level6dir))<0)
    return ERROR_GENERIC;
    if ((fin = Fopen(chfile,(uint8_t *)"r"))<0)
        return  ERROR_GENERIC;
    Fclose(fin);
        
    /* Try to change to Root Directory*/
    if((Chdir(chroot))<0)
    return ERROR_GENERIC;
    if ((fin = Fopen(chfile1,(uint8_t *)"r"))<0)
        return  ERROR_GENERIC;
    Fclose(fin);
    /*change directory from root to 3 level from root */
    if((Chdir(chdir))<0)
    return ERROR_GENERIC;
    if ((fin = Fopen(chfile2,(uint8_t *)"w"))<0)
        return  ERROR_GENERIC;
    Fclose(fin);
    FlushCache();
    
    if((Chdir(chdir1))<0)
    return ERROR_GENERIC;
    buffer_1 =Getcwd();
    if ((fin = Fopen(chfile3,(uint8_t *)"w"))<0)
        return  ERROR_GENERIC;
    Fclose(fin); 
    /*Again change directory from different device directory to current device directory*/
    if((Chdir(chdirl))<0)
    return ERROR_GENERIC;
    /*Try to open file in this Directory*/
    if ((fin = Fopen(chfile4,(uint8_t *)"w"))<0)
        return  ERROR_GENERIC;
    Fclose(fin);
    FlushCache();
    return NOERROR;
}
#ifdef CHDIRW_TEST
int   TestChdirW()
{
    uint8_t *buffer_1;
    int16_t RetValue;
    
    if((RetValue= Chdirw(longchdirbuf))<0)
        return  ERROR_GENERIC;
    buffer_1 = Getcwd();
        
    if ((fout=Fopen(longdirFopen,(uint8_t *)"w"))<0)
        return ERROR_GENERIC;
    FlushCache();
    Fclose(fout);
    
    if((RetValue= Chdirw(longchrootbuf))<0)
        return  ERROR_GENERIC;
    buffer_1 = Getcwd();
        
    if ((fout=Fopen(longdirFopen,(uint8_t *)"w"))<0)
        return ERROR_GENERIC;
    FlushCache();
    Fclose(fout);
    if((RetValue= Chdirw(longchdirbuf))<0)
        return  ERROR_GENERIC;
    
    if((RetValue= Chdirw(longchlvel1buf))<0)
        return  ERROR_GENERIC;
    buffer_1 = Getcwd();
    if ((fout=Fopen(longdirFopen,(uint8_t *)"w"))<0)
        return ERROR_GENERIC;
    FlushCache();
    Fclose(fout);
    if((RetValue= Chdirw(longchlevel2buf))<0)  
        return  ERROR_GENERIC;
    buffer_1 = Getcwd(); 
    
    if((RetValue= Chdirw(longchdirbuf))<0)
        return  ERROR_GENERIC;
    if((RetValue= Chdirw(longchlevel4buf))<0)
        return  ERROR_GENERIC;
    buffer_1 = Getcwd();
    if ((fout=Fopen(longdirFopen,(uint8_t *)"r"))<0)
        return ERROR_GENERIC;
    FlushCache();
    Fclose(fout);
    return NOERROR;
    
}    
#endif    


#ifdef FINDNEXT_TEST
////////////////////////////////////////////////////////////////////////////////
//
//>  Name:           TestFindNext

//   Type:           Function
//
//   Description:    

//   Inputs:         none 

//   Outputs:        none
//                   
//   Notes:          none                //<
////////////////////////////////////////////////////////////////////////////////
int TestFindNext()
{
    int32_t RetValue = 0,HandleNumber;
//    FindData_t FindData;  //modify: declared as global
	int32_t Fastfin;
    uint8_t Buffer[20];

   //modify: Diasable all memset from this module
   // memset(&FindData,0,sizeof(FindData));
    
    if((HandleNumber = FindFirst(&FindData,(uint8_t *)FindBuffer))<0)
	    return NOERROR;

	if((Fastfin = FastOpen(FindData.Key,(uint8_t *)"w+"))<0)
	   return ERROR_GENERIC;

	Fclose(Fastfin);

	//Buffer containing the file names of the given pattern
    memcpy(&FileNames[count],FindData.name,12);

    count+=12;
   // memset(&FindData,0,sizeof(FindData));
   
    while(1)
    {
        if((RetValue = FindNext(HandleNumber,&FindData)) <0)
	        break;
	    /*Open the file using function Fastopen*/                                          

		if((Fastfin = FastOpen(FindData.Key,(uint8_t *)"w+"))<0)
      	   return ERROR_GENERIC;
		/*Read some bytes from opened file*/
        if((RetValue = Fread(Fastfin,Buffer,20))<0)
             return ERROR_GENERIC; 
	   /*Seek at the end of  file*/
	    Fseek(Fastfin,0,SEEK_END);         
		/*Write some bytes at the end of file*/
	    if((RetValue =Fwrite(Fastfin,BitBuffer,100))<0)
	    {
	        Fclose(Fastfin);
	        return ERROR_GENERIC;
	    }
		/*Seek back written bytes*/
	    if((RetValue=Fseek(Fastfin,-100,SEEK_CUR))<0)
	    {
	        Fclose(Fastfin);
	        return ERROR_GENERIC;
	    }
	    /*read back data, we had written*/
	    if((RetValue = Fread(Fastfin,ReadBuffer,100))<0)
	    {
	        Fclose(Fastfin);
	        return ERROR_GENERIC;
	    }
		/*Verify the Read-write Operation of the file opened by function FastOpen*/
	    if(BitBuffer[0]!= ReadBuffer[0])
	    {
	        Fclose(Fastfin);
	        return ERROR_GENERIC;
	    }
	     
	   	Fclose(Fastfin);

        memcpy(&FileNames[count],FindData.name,12);

        count+=12;
        
	  //  memset(&FindData,0,sizeof(FindData));
    }
//    memset(&FindData,0,sizeof(FindData));

	
    //Change directory to one level up
    if((RetValue = Chdir(FindFile))<0)
	    return NOERROR;

	//Recursively called 
    if((RetValue = TestFindNext()) <0)
		return NOERROR;

	return NOERROR;
}

#endif

#ifdef NEWADDED
int TestFopenw()
{

    int32_t i=0;

    if(TestAllModes()==ERROR_GENERIC)
 		return ERROR_GENERIC;

    for(i=0;i<200;i++)
	longfile3buf[i]='\0';

    i=0;
    longfile3buf[i]='a';
    longfile3buf[i+1]='\0';
    longfile3buf[i+2]=':';
    longfile3buf[i+3]='\0';
    longfile3buf[i+4]='/';
    longfile3buf[i+5]='\0';

    //fill the buffer with y.c upto yyyy(47 times y).c
    for(i=6;i<100;i=i+2)
    {
	longfile3buf[i]='y';
	longfile3buf[i+1]='\0';
	longfile3buf[i+2]='.';
	longfile3buf[i+3]='\0';
	longfile3buf[i+4]='c';
        longfile3buf[i+5]='\0';

        //create the file if not exist
	if(TestAllModes()==ERROR_GENERIC)
		return ERROR_GENERIC;


    }
    for(i=6;i<200;i++)
	longfile3buf[i]='\0';

    //fill the buffer with z.txt upto zzzz(47 times z).txt
    for(i=6;i<100;i=i+2)
    {
	longfile3buf[i]='z';
	longfile3buf[i+1]='\0';
	longfile3buf[i+2]='.';
	longfile3buf[i+3]='\0';
	longfile3buf[i+4]='t';
        longfile3buf[i+5]='\0';
	longfile3buf[i+6]='x';
        longfile3buf[i+7]='\0';
	longfile3buf[i+8]='t';
        longfile3buf[i+9]='\0';

        //create the file if not exist
	if(TestAllModes()==ERROR_GENERIC)
 		return ERROR_GENERIC;
    }

    	i=0;
    	for(i=0;i<200;i++)
	    longfile3buf[i]='\0';

	i=0;
	longfile3buf[i]='a';
	longfile3buf[i+1]='\0';
	longfile3buf[i+2]=':';
	longfile3buf[i+3]='\0';
	longfile3buf[i+4]='/';
	longfile3buf[i+5]='\0';
	longfile3buf[i+6]='m';
	longfile3buf[i+7]='\0';
	longfile3buf[i+8]='y';
	longfile3buf[i+9]='\0';
	longfile3buf[i+10]='d';
	longfile3buf[i+11]='\0';
	longfile3buf[i+12]='i';
	longfile3buf[i+13]='\0';
	longfile3buf[i+14]='r';
	longfile3buf[i+15]='\0';
	longfile3buf[i+16]='/';
	longfile3buf[i+17]='\0';

    //fill the buffer with a.c upto aaaa(41 times a).c
    for(i=18;i<100;i=i+2)
    {
	longfile3buf[i]='a';
	longfile3buf[i+1]='\0';
	longfile3buf[i+2]='.';
	longfile3buf[i+3]='\0';
	longfile3buf[i+4]='c';
        longfile3buf[i+5]='\0';

        //create the file if not exist
	if(TestAllModes()==ERROR_GENERIC)
 	    return ERROR_GENERIC;
    }

    for(i=18;i<200;i++)
	longfile3buf[i]='\0';

    //fill the buffer with b.txt upto bbbb(41 times b).txt
    for(i=18;i<100;i=i+2)
    {
	longfile3buf[i]='b';
	longfile3buf[i+1]='\0';
	longfile3buf[i+2]='.';
	longfile3buf[i+3]='\0';
	longfile3buf[i+4]='t';
        longfile3buf[i+5]='\0';
	longfile3buf[i+6]='x';
        longfile3buf[i+7]='\0';
	longfile3buf[i+8]='t';
        longfile3buf[i+9]='\0';

        //create the file if not exist
	if(TestAllModes()==ERROR_GENERIC)
 		return ERROR_GENERIC;
    }

    return NOERROR;
}
#endif
////////////////////////////////////////////////////////////////////////////////
//
//>  Name:           TestFileread

//   Type:           Function
//
//   Description:    Test the file read .                   
//   Inputs:         none 
//
//   Outputs:        none
//                   
//   Notes:          This function test for the fread function.it read from differnt place in file
//                   and checks with the test pattern.   
//<
////////////////////////////////////////////////////////////////////////////////
int  TestFileread()
{
    int32_t wTestPattern, i, j;
    int32_t Currentoffset=0;

    /* Open file*/
    if ((fin = Fopen(testread,(uint8_t *)"r"))<0)
        return  ERROR_GENERIC;
       
    if(fin >0)
    {
          /* Complete do loop reads and compares 256 bytes*/
        i = 2;
        do
        {        

            j = 128;
            wTestPattern = 0x00;
            do
            {        
                RetValue = Fread(fin,Buffer,1);
            
                if(RetValue != ERROR_GENERIC)
                {
                    if(Buffer[0] != wTestPattern)
                    {
                        RetValue = 1;
                        break;
                    }
                    else
                        wTestPattern = wTestPattern + 0x01;       /* Calculate new test pattern*/
                }
                else
                    break;
            }while(--j);
        }while(--i);
        
        /*Test sector boundaries (position 512)*/
        if(RetValue != ERROR_GENERIC)
        {
            /* Read 6 bytes starting at byte 508*/
            Fseek(fin,0, SEEK_SET);     /* Seek to beginning of file*/
            Fseek(fin,(int32_t)508, SEEK_CUR);
            if((RetValue = Fread(fin,Buffer,6))<0)
                return ERROR_GENERIC; 
            if((Buffer[0] != 0x7C) || (Buffer[1] != 0x7d) || (Buffer[2] != 0x7e)||(Buffer[3] != 0x7f)||(Buffer[4] != 0x00)||(Buffer[5] != 0x01))
                RetValue = ERROR_GENERIC;
        }                

        if(RetValue != ERROR_GENERIC)
        {
            /* Read 3 bytes starting at 512 */
            Fseek(fin, (int32_t)512, SEEK_SET);
            if((RetValue = Fread(fin,Buffer,3))<0)
                return ERROR_GENERIC; 
            
            if((Buffer[0] != 0x00) || (Buffer[1] != 0x01) || (Buffer[2] != 0x02))
                RetValue = ERROR_GENERIC;
        }                
        
        if(RetValue != ERROR_GENERIC)
        {
            /* Read 3 bytes starting at 511*/
            Currentoffset=Ftell(fin);
            Fseek(fin,-4, SEEK_CUR);
            Currentoffset=Ftell(fin);            
            if((RetValue = Fread(fin,Buffer,3))<0)
                return ERROR_GENERIC; 
            Currentoffset=Ftell(fin);            
            if((Buffer[0] != 0x7f) || (Buffer[1] != 0x00) || (Buffer[2] != 0x01))
                return ERROR_GENERIC;
        }                
        
        Fclose(fin);
           
    }    
    return NOERROR;
}
////////////////////////////////////////////////////////////////////////////////
//
//>  Name:           TestFopenwRead
//
//   Type:           Function
//
//   Description:    Test the read mode for function Fopenw which Supports the UCS3 format.                   
//   Inputs:         none 
//
//   Outputs:        none
//                   
//   Notes:          This function is same as fopen except that passed name is in UCS3 format.
//					 if file does not exist,it will return error.
//<					 In this mode we can only read from the file
//////////////////////////////////////////////////////////////////////////////////
int  TestFopenwRead()
{    
    
    /* Open for read (will fail if file  does not exist) */
    if ((foutw = Fopenw((uint8_t *)longfile1buf,(uint8_t *)"r"))<0)
        return  ERROR_GENERIC;
	/*Do read operation from file opened in read mode*/   
    if(Fread(foutw,ReadBuffer,56)<0)
    {
        Fclose(foutw);
        return ERROR_GENERIC;
    }              
    /*Open the file which is exist in the read mode and try to do Fwrite if Fails*/   
    if(Fwrite(foutw,BitBuffer,25)>0)
    {
        Fclose(foutw);
        return ERROR_GENERIC;
    }
    Fclose(foutw);
    FlushCache();
    return NOERROR;
}   

////////////////////////////////////////////////////////////////////////////////
//
//>  Name:           TestFopenwWrite
//
//   Type:           Function
//
//   Description:    Test the write mode for function Fopenw which Supports the lUCS3 format.                   
//
//   Inputs:         none 
//
//   Outputs:        none
//                   
//   Notes:          This function is same as fopen except that passed name is in UCS3 format.
//					 if file does not exist,function will create file in this mode.
//<					 In this mode we can only write in the file
//////////////////////////////////////////////////////////////////////////////////
int  TestFopenwWrite()
{    
    int32_t fin,BytesToRead;
    int32_t Filesize,NumByteToWrite=0,NumBytesToRead;
    int BytesToWrite=512;
    
    /* Open for write mode (will create the  file if  file  does not exist) */
    if ((foutw = Fopenw((uint8_t *)longfilebuf,(uint8_t *)"w"))<0)
        return  ERROR_GENERIC;
    /* Open for Read mode for filling Readbuffer*/  
    if ((fin=Fopenw((uint8_t *)longfile1buf,(uint8_t *)"r"))<0)
        return ERROR_GENERIC;
    /*Seek to the End of file to get file size*/
    if((RetValue=Fseek(fin,0,SEEK_END))<0)
        return ERROR_GENERIC;
    Filesize=Ftell(fin);
    if((RetValue=Fseek(fin,0,SEEK_SET))<0)
        return ERROR_GENERIC;
    /* try to write  the whole file which is open in write mode*/   
    NumBytesToRead =Filesize;
    BytesToRead =0;
    while(NumByteToWrite < Filesize)
    {
        if((NumBytesToRead)>0)
        {
            if(NumBytesToRead> BytesToRead )
                BytesToRead=BytesToWrite;
            else
                BytesToRead =NumBytesToRead;
       
            if((RetValue = Fread(fin,ReadBuffer,BytesToRead))<0)
                return ERROR_GENERIC;
            if((RetValue =Fwrite(foutw,ReadBuffer,BytesToRead))<0)
                return ERROR_GENERIC;
        NumByteToWrite+=RetValue;
        }
        NumBytesToRead-=BytesToWrite ;
        if((RetValue==0) ||(NumByteToWrite==Filesize))
        {
            NumByteToWrite = Filesize+1;
        }     
    }
	/*Close opened file*/
    Fclose(fin);
    Fclose(foutw);
    FlushCache();
    return  NOERROR;
}
//////////////////////////////////////////////////////////////////////////////////
////
////>  Name:           TestFopenwAppend
////
////   Type:           Function
////
////   Description:    Test the append mode for function Fopenw                   
////   Inputs:         none 
////
////   Outputs:        none
////                   
////   Notes:          This function is same as fopen except that passed name is in UCS3 Format.
/////				   in this mode the data will append at the end of file.if file does not exist 
////				   function will create the file in this mode	
////<				   In this mode we can only write in the file
//////////////////////////////////////////////////////////////////////////////////
int  TestFopenwAppend()
{
    int32_t fin,BytesToRead;
    LONG Filesize,NumBytesToRead,NumByteToWrite=0;
    int BytesToWrite=512;

      /* Open for append mode (will create the  file if  file  does not exist) */
    if ((foutw = Fopenw((uint8_t *)longfilebuf,(uint8_t *)"a"))<0)
        return  ERROR_GENERIC;
    if ((fin=Fopen(testfile3,(uint8_t *)"r"))<0)
        return ERROR_GENERIC;
	/*GET the filesize*/        
    if((RetValue=Fseek(fin,0,SEEK_END))<0)
        return ERROR_GENERIC;
    Filesize=Ftell(fin);
    if((RetValue=Fseek(fin,0,SEEK_SET))<0)
        return ERROR_GENERIC;
	/* Append the file */
    NumBytesToRead =Filesize;
    BytesToRead =0;
    while(NumByteToWrite < Filesize)
    {
        if((NumBytesToRead)>0)
        {
            if(NumBytesToRead> BytesToRead )
                BytesToRead=BytesToWrite;
            else
                BytesToRead =NumBytesToRead;
       
            if((RetValue = Fread(fin,ReadBuffer,BytesToRead))<0)
                return ERROR_GENERIC;
            if((RetValue =Fwrite(foutw,ReadBuffer,BytesToRead))<0)
                return ERROR_GENERIC;
        NumByteToWrite+=RetValue;
        }
        NumBytesToRead-=BytesToWrite ;
        if((RetValue==0) ||(NumByteToWrite==Filesize))
        {
            NumByteToWrite = Filesize+1;
        }     
    }
	/*Close Open file*/
    Fclose(foutw);
    Fclose(fin);
    FlushCache();    
    return NOERROR;
}
////////////////////////////////////////////////////////////////////////////////
//
//>  Name:           TestReadplusMode
//
//   Type:           Function
//
//   Description:    Test the Readplus mode for function Fopenw                   
//   Inputs:         none 
//
//   Outputs:        none
//                   
//   Notes:          This function is same as fopen except that passed name is in UCS3 Format
//                   In this mode we can read as well write in the file.
//					 if file does not exist ,function should return error in this mode
//<
////////////////////////////////////////////////////////////////////////////////
int  TestFopenwReadPlus()
{
    int32_t fin,BytesToRead;
    int32_t Currentoffset=0,NumBytesToRead,Filesize,NumByteToWrite=0;
    int BytesToWrite=512;

    /*open for read plus mode(will fail if file  does not exist)*/
    if ((fout=Fopenw((uint8_t *)longfile1buf,(uint8_t *)"r+"))<0)
        return ERROR_GENERIC;

    if ((fin=Fopen(readfile,(uint8_t *)"r"))<0)
        return ERROR_GENERIC;
	/*Get the filesize*/
    if((RetValue=Fseek(fin,0,SEEK_END))<0)
        return ERROR_GENERIC;
    Filesize=Ftell(fin);
    if((RetValue=Fseek(fin,0,SEEK_SET))<0)
        return ERROR_GENERIC;
 	/*We want to write at the end of file so Seek up to end of file*/
    if((RetValue=Fseek(fout,0,SEEK_END))<0)
        return ERROR_GENERIC;
    /* Try to write  the whole file which is open in write mode*/   
    NumBytesToRead =Filesize;
    BytesToRead =0;
    while(NumByteToWrite < Filesize)
    {
        if((NumBytesToRead)>0)
        {
            if(NumBytesToRead> BytesToRead )
                BytesToRead=BytesToWrite;
            else
                BytesToRead =NumBytesToRead;
       
            if((RetValue = Fread(fin,ReadBuffer,BytesToRead))<0)
                return ERROR_GENERIC;
            Currentoffset=Ftell(fout);
            if((RetValue =Fwrite(fout,ReadBuffer,BytesToRead))<0)
                return ERROR_GENERIC;
        NumByteToWrite+=RetValue;
        }
        NumBytesToRead-=BytesToWrite ;
        if((RetValue==0) ||(NumByteToWrite==Filesize))
        {
            NumByteToWrite = Filesize+1;
        }     
    }
     Fclose(fin);
     Fclose(fout);
     FlushCache();
     return NOERROR;
}
////////////////////////////////////////////////////////////////////////////////
//
//>  Name:           TestWriteplusMode
//
//   Type:           Function
//
//   Description:    Test the writeplus mode for function Fopenw                   
//   Inputs:         none 
//
//   Outputs:        none
//                   
//   Notes:          This function is same as fopen except that passed name is in UCS3 Format.
//                   In this mode we can read as well write in the file
//                   if the file does not present , Function will creates the file in this mode.
//<
////////////////////////////////////////////////////////////////////////////////
int   TestFopenwWritePlus()
{
	/*Open the file in write mode */
    if ((fout=Fopenw((uint8_t *)longfile2buf,(uint8_t *)"w+"))<0)
        return ERROR_GENERIC;
	/*Seek at the end of file*/
    if((RetValue=Fseek(fout,0,SEEK_END))<0)
        return ERROR_GENERIC;
    /*write some data at the End of file*/    
    if((RetValue =Fwrite(fout,BitBuffer,100))<0)
    {
        Fclose(fout);
        return ERROR_GENERIC;
    }
	/*Seek back the written data*/
    if((RetValue=Fseek(fout,-100,SEEK_CUR))<0)
    {
        Fclose(fout);
        return ERROR_GENERIC;
    }
	/*Read the written data*/
    if((RetValue = Fread(fout,ReadBuffer,100))<0)
    {
        Fclose(fout);
        return ERROR_GENERIC;
    }
    /*check thr read-write operation for this mode*/
    if(BitBuffer[0]!= ReadBuffer[0])
        return ERROR_GENERIC;
     
    Fclose(fout);
    FlushCache();
    return NOERROR;
}
////////////////////////////////////////////////////////////////////////////////
//
//>  Name:           TestAppendplusMode
//
//   Type:           Function
//
//   Description:    Test the writeplus mode for function Fopenw                  
//   Inputs:         none 
//
//   Outputs:        none
//                   
//   Notes:          This function is same as fopen except that passed name is in UCS3 Format. 
//					In this mode we can read as well write in the file
//                   if the file does not present, function will create the file in this mode.
//<
////////////////////////////////////////////////////////////////////////////////

int   TestFopenwAppendPlus()
{

    /*open file in appendplus mode (will create thes file if not present)*/
    if ((fout=Fopenw((uint8_t *)longfile1buf,(uint8_t *)"a+"))<0)
        return ERROR_GENERIC;
    /*Write to the file it should erite at the end of the file*/        
    if((RetValue =Fwrite(fout,BitBuffer,150))<0)
    {
        Fclose(fout);
        return ERROR_GENERIC;
    }
    /* seek back the bytes write*/
    if((RetValue =Fseek(fout,-150,SEEK_CUR))<0)
    {
        Fclose(fout);
        return ERROR_GENERIC;
    }
    /*Try to read bytes which we had written*/
    if((RetValue = Fread(fout,ReadBuffer,150))<0)
    {
        Fclose(fout);
        return ERROR_GENERIC;
    }
	/*Verify the read write operation for this mode*/
    if(BitBuffer[0]!= ReadBuffer[0])
        return ERROR_GENERIC;
    
    Fclose(fout);
    FlushCache();   
    return NOERROR;
}

////////////////////////////////////////////////////////////////////////////////
//
//>  Name:           TestWriteFile

//   Type:           Function
//
//   Description:    Test the file write.                   
//   Inputs:         none 
//
//   Outputs:        none
//                   
//   Notes:          Checks the function fwrite.This Test verify function Fwrite,Fread and Fseek.
//				     
//<
////////////////////////////////////////////////////////////////////////////////

int  TestWriteFile()
{
    int32_t i, j,Currentposition=0;
            
    /* Fill the buffers with patterns*/
    for(i=0;i<NUM_WRITE_BYTES;i++)
    {
        pwFileBuffer[i] = 0xaa; // 0xAAAAAA
        pwCompareBuffer[i] = 0x55;
    }
    if((fout = Fopen(bFileDest,(uint8_t *)"w")) <0)
         return ERROR_GENERIC;
     FlushCache();
    /* Write the initial pattern to the file*/
    for(i=0;i<100;i++)
    {
        RetValue =Fwrite(fout,pwFileBuffer,NUM_WRITE_BYTES);
        
        if((RetValue <0) || (RetValue != NUM_WRITE_BYTES))
        {        
            /* Error writting - Close files*/
            Fclose(fout);
            FlushCache();
            return ERROR_GENERIC;
        }

    }
    if(Fseek(fout,0,SEEK_SET)<0)
         return ERROR_GENERIC;
    /* Overwrite the original pattern for every other chunk of NUM_WRITE_charS*/
    for(i=0;i<100/2;i++)
    {
        RetValue =Fwrite(fout,pwCompareBuffer,NUM_WRITE_BYTES);
       
        if((RetValue <0) || (RetValue != NUM_WRITE_BYTES))
        {        
            /* Error writting - Close files*/
            Fclose(fout);
            return ERROR_GENERIC;
        }
        if(Fseek(fout,NUM_WRITE_BYTES,SEEK_CUR)<0)
        {        
            /* Error seeking - Close files*/
            Fclose(fout);
            return ERROR_GENERIC;
        }
    }
    /* We should be at the end of the file*/
    if(Feof(fout)> 0)
    {
        /* Error seeking - Close files*/
            Fclose(fout);
            return ERROR_GENERIC;
    }
    /* Close the file then open it again in READ mode*/
    Fclose(fout);
    FlushCache(); 

    if((fin = Fopen(bFileSource,(uint8_t *)"r")) <0)
    {
        Fclose(fin);
        return ERROR_GENERIC;
    }
    /* Verify that every other chunk was changed correctly */
    /* First verify the overwritten chunks  */
    for(i=0;i<100/2;i++)
    {

        /* We're comparing words not bytes so clear the read buffer & set the
        last word to the expected pattern.*/
        for(j=0;j<NUM_WRITE_BYTES;j++)
        {
            pwFileBuffer[j] = 0;
        }
        
        /* Overwrite original data*/
        RetValue = Fread(fin,pwFileBuffer,NUM_WRITE_BYTES);
        
        if((RetValue <0) ||(RetValue != NUM_WRITE_BYTES))
        {        
            /* Error reading - Close files*/
            Fclose(fin);
            count1++;
            return ERROR_GENERIC;
        }
        Currentposition=Ftell(fin);
        /* Skip past a chunk  */
        if(Fseek(fin,NUM_WRITE_BYTES, SEEK_CUR) == ERROR_GENERIC)
        {        
            /* Error seeking - Close files*/
            Fclose(fin);
            count1++;
            return ERROR_GENERIC;
        }
        Currentposition=Ftell(fin);
        for(j=0;j<NUM_WRITE_BYTES;j++)
        {
            if(pwFileBuffer[j] != 0x55)
            {
                /* Error reading - Close files */
                Fclose(fin);
                count1++;
                return ERROR_GENERIC;
            }
        }
        count1++;
    }  
    /* Now verify the original chunks */
    /* We should be at the end of the file */
    if(Feof(fin)>0)
    {
        /* Error seeking - Close files  */
        Fclose(fin);
        return ERROR_GENERIC;
    }
    /* Rewind the file */
    if(Fseek(fin,0, SEEK_SET)<0)
    {        
        /* Error seeking - Close files */
        Fclose(fin);
        return ERROR_GENERIC;
    }
    /* Check 'em */
    for(i=0;i<100/2;i++)
    {
        /* We're comparing words not bytes so clear the read buffer & set the
        last word to the expected pattern. */
        for(j=0;j<NUM_WRITE_BYTES;j++)
        {
            pwFileBuffer[j] = 0;
        }

        /* Skip past the overwritten pattern */
        if(Fseek(fin, NUM_WRITE_BYTES, SEEK_CUR) == ERROR_GENERIC)
        {        
            /* Error seeking - Close files  */
            Fclose(fin);
            return ERROR_GENERIC;
        }
        /* Read the original data */
        RetValue = Fread(fin,pwFileBuffer,NUM_WRITE_BYTES);
        
        if((RetValue<0) || (RetValue != NUM_WRITE_BYTES))
        {        
            /* Error reading - Close files */
            Fclose(fin);
            return ERROR_GENERIC;
        }
        for(j=0;j<NUM_WRITE_BYTES;j++)
        {
            if(pwFileBuffer[j] != 0xAA)
            {
                /* Error reading - Close files*/
                Fclose(fin);
                return ERROR_GENERIC;
            }
        }
    }    
    Fclose(fin);
    Fclose(fout);
    FlushCache();
    return NOERROR;            
}

////////////////////////////////////////////////////////////////////////////////
//
//>  Name:           TestMkdirMax
//
//   Type:           Function
//
//   Description:    Test the Creation of maximun number of Directory
// 
//   Inputs:         none 
//
//   Outputs:        none
//                   
//   Notes:          Maximum Root Directory entries Possible -> FAT 12=256 Or FAT16 = 512
//                                                       FAT 32  = No Limit
//                   //<
//////////////////////////////////////////////////////////////////////////////////
#ifdef MAXDIRECTORYTEST
int TestMkdirMax(int32_t DeviceNum)
{
    int i,Byte=0,j,m,k,l=0;
	int dircount = 0;
    int LoopBreakFlag = 0;
	
	/* put the path of root directory*/
    PutByte(DirectoryName,'a',l++);
    PutByte(DirectoryName,':',l++);
    PutByte(DirectoryName,'/',l++);
    
    for(m=1; m <= 9 ; m++)
    {
	    for( i='A' ;i <= 'Z';i ++)
	    {
             PutByte(DirectoryName,i,l);
             for(j ='0'; j <='9'; j++)
             {
                for(k=1;k <=m; k++)
                {
                    PutByte(DirectoryName,j,k+l);
                }
                if((RetValue = Mkdir(DirectoryName)) < 0)
                {
                    LoopBreakFlag = 1;
                    break;
                }
				dircount++;
                
             }  

             if(LoopBreakFlag == 1)
             break; 
	    }
        if(LoopBreakFlag == 1)
        break; 

   }  
    FlushCache();
	if (FSFATType(DeviceNum) == FAT12)
	{
		if (dircount == 256)
			return NOERROR;
	}
	else if (FSFATType(DeviceNum) == FAT16)
	{
		if (dircount == 512)
			return NOERROR;
	}
	else if (FSFATType(DeviceNum) == FAT32)
	{
		if (dircount == 2340)
			return NOERROR;
	}
    return ERROR_GENERIC;
} 
#endif                                                 


void GetUnicodeString(uint8_t *filepath,uint8_t *buf,int32_t Strlen)
{
   int32_t offset=0,word=0,i;
   CHAR Byte;

   for(i=0;i<= Strlen;i++)
   {
       Byte = FSGetByte(filepath,i);
       PutByte((uint8_t *)&word,Byte,0);
       PutWord(buf,word,offset); 
       offset+=2; 
   } 

}

////////////////////////////////////////////////////////////////////////////////
//
//>  Name:           TestResourceFread
//
//   Type:           Function
//
//   Description:    Test the file system steering capability for Fread
// 
//   Inputs:         none 
//
//   Outputs:        none
//                   
//   Notes:          Right now, the meat of this test is the same as TestFread
//                   except that everywhere we call Fread, we add
//                   RESOURCE_HANDLE_MIN to the file handle to make it look like
//                   a resource file handle.  We do this because the os_rsc_XXX
//                   functions are currently stubbed and the stubs simply
//                   subtract out the RESOURCE_HANDLE_MIN value and call the
//                   normal Fread function.  Once the stubs are removed, the
//                   test should fail and that will be an indication to update
//                   this test to use the real os_rsc_XXX functions.
//                   //<
//////////////////////////////////////////////////////////////////////////////////
int TestResourceFread(void)
{
    int32_t wTestPattern, i, j;
    int32_t Currentoffset=0;

    /* Open file*/
    if ((fin = Fopen(testread,(uint8_t *)"r"))<0)
        return  ERROR_GENERIC;
       
    if(fin >0)
    {
          /* Complete do loop reads and compares 256 bytes*/
        i = 2;
        do
        {        

            j = 128;
            wTestPattern = 0x00;
            do
            {        
                RetValue = Fread(fin+RESOURCE_HANDLE_MIN,Buffer,1);
            
                if(RetValue != ERROR_GENERIC)
                {
                    if(Buffer[0] != wTestPattern)
                    {
                        RetValue = 1;
                        break;
                    }
                    else
                        wTestPattern = wTestPattern + 0x01;       /* Calculate new test pattern*/
                }
                else
                    break;
            }while(--j);
        }while(--i);
        
        /*Test sector boundaries (position 512)*/
        if(RetValue != ERROR_GENERIC)
        {
            /* Read 6 bytes starting at byte 508*/
            Fseek(fin,0, SEEK_SET);     /* Seek to beginning of file*/
            Fseek(fin,(int32_t)508, SEEK_CUR);
            if((RetValue = Fread(fin+RESOURCE_HANDLE_MIN,Buffer,6))<0)
                return ERROR_GENERIC; 
            if((Buffer[0] != 0x7C) || (Buffer[1] != 0x7d) || (Buffer[2] != 0x7e)||(Buffer[3] != 0x7f)||(Buffer[4] != 0x00)||(Buffer[5] != 0x01))
                RetValue = ERROR_GENERIC;
        }                

        if(RetValue != ERROR_GENERIC)
        {
            /* Read 3 bytes starting at 512 */
            Fseek(fin, (int32_t)512, SEEK_SET);
            if((RetValue = Fread(fin+RESOURCE_HANDLE_MIN,Buffer,3))<0)
                return ERROR_GENERIC; 
            
            if((Buffer[0] != 0x00) || (Buffer[1] != 0x01) || (Buffer[2] != 0x02))
                RetValue = ERROR_GENERIC;
        }                
        
        if(RetValue != ERROR_GENERIC)
        {
            /* Read 3 bytes starting at 511*/
            Currentoffset=Ftell(fin);
            Fseek(fin,-4, SEEK_CUR);
            Currentoffset=Ftell(fin);            
            if((RetValue = Fread(fin+RESOURCE_HANDLE_MIN,Buffer,3))<0)
                return ERROR_GENERIC; 
            Currentoffset=Ftell(fin);            
            if((Buffer[0] != 0x7f) || (Buffer[1] != 0x00) || (Buffer[2] != 0x01))
                return ERROR_GENERIC;
        }                
        
        Fclose(fin);
           
    }    
    return NOERROR;
}

int TestResourceFwrite(void)
{
    if( ( fout = Fopen(bFileDest,(uint8_t *)"w") ) < 0 )
    {
        return ERROR_GENERIC;
    }
    // Fwrite should fail
    if( Fwrite( fout+RESOURCE_HANDLE_MIN, ReadBuffer, sizeof(ReadBuffer) ) >= 0 )
    {
        // Fwrite didn't fail, return error
        Fclose( fout );
        return ERROR_GENERIC;
    }
    else
    {
        // Fwrite failed, return no error
        Fclose( fout );
        return NOERROR;
    }
}

////////////////////////////////////////////////////////////////////////////////
//
//>  Name:           TestResourceFseek
//
//   Type:           Function
//
//   Description:    Test the file system steering capability for Fseek
// 
//   Inputs:         none 
//
//   Outputs:        none
//                   
//   Notes:          Right now, the meat of this test is the same as TestFseek
//                   except that everywhere we call Fseek, we add
//                   RESOURCE_HANDLE_MIN to the file handle to make it look like
//                   a resource file handle.  We do this because the os_rsc_XXX
//                   functions are currently stubbed and the stubs simply
//                   subtract out the RESOURCE_HANDLE_MIN value and call the
//                   normal Fseek function.  Once the stubs are removed, the
//                   test should fail and that will be an indication to update
//                   this test to use the real os_rsc_XXX functions.
//                   //<
//////////////////////////////////////////////////////////////////////////////////
int TestResourceFseek(void)
{
    LONG Filesize;
    int32_t Currentpointer=0;
    int32_t NumBytesToRead=30;
    uint8_t *Buf=ReadBuffer;

    /*Open the file in read mode to perform test for Fseek function.*/
    if ((fin = Fopen(SeekFile,(uint8_t *)"r"))<0)
        return  ERROR_GENERIC;
    /*Get the file size*/    
    if((RetValue=Fseek(fin+RESOURCE_HANDLE_MIN,0,SEEK_END))<0)
        return ERROR_GENERIC;
    Filesize=Ftell(fin);
    
    if((RetValue=Fseek(fin+RESOURCE_HANDLE_MIN,0,SEEK_SET))<0)
        return ERROR_GENERIC;
    /*Seek from start to arbitarily large location  and read*/
    if ((RetValue = Fseek(fin+RESOURCE_HANDLE_MIN,0xc0000,SEEK_SET))<0)
        return  ERROR_GENERIC;
    Currentpointer=Ftell(fin);
    if( (RetValue = Fread(fin,Buf,NumBytesToRead))<0)
        return ERROR_GENERIC;
    Buf += NumBytesToRead/3;
    /*Seek from start to 0 bytes and read*/
    if ((RetValue = Fseek(fin+RESOURCE_HANDLE_MIN,0,SEEK_SET))<0)
        return  ERROR_GENERIC;
    if( (RetValue = Fread(fin,Buf,NumBytesToRead))<0)
        return ERROR_GENERIC;
    Buf += NumBytesToRead/3;
    /*Seek from start to whole file and read*/
    if ((RetValue = Fseek(fin+RESOURCE_HANDLE_MIN,Filesize,SEEK_SET))<0)
        return  ERROR_GENERIC;
    if( (RetValue = Fread(fin,Buf,NumBytesToRead))>0)
        return ERROR_GENERIC;
    Buf += NumBytesToRead/3;
    /*Seek from end to 0 bytes and read*/
    if ((RetValue = Fseek(fin+RESOURCE_HANDLE_MIN,0,SEEK_END))<0)
        return  ERROR_GENERIC;
    if( (RetValue = Fread(fin,Buf,NumBytesToRead))>0)
        return ERROR_GENERIC;
    Buf += NumBytesToRead/3;
    /*Seek from end to whole file and read*/
    if ((RetValue = Fseek(fin+RESOURCE_HANDLE_MIN,Filesize,SEEK_END))<0)
        return  ERROR_GENERIC;
    if( (RetValue = Fread(fin,Buf,NumBytesToRead))>0)   /* attempt to read past end of file, should return error */
        return ERROR_GENERIC;

    Buf += NumBytesToRead/3;
    /*Set the offest to some location yo check SEEK_CUR*/
    if ((RetValue = Fseek(fin+RESOURCE_HANDLE_MIN,NumBytesToRead,SEEK_SET))<0)
        return  ERROR_GENERIC;
    
    /*Seek from current to whole file and read*/
    if ((RetValue = Fseek(fin+RESOURCE_HANDLE_MIN,(-NumBytesToRead+Filesize),SEEK_CUR))<0)
        return  ERROR_GENERIC;
    if( (RetValue = Fread(fin,Buf,NumBytesToRead))>0)   /* attempt to read past end of file, should return error  */
        return ERROR_GENERIC;

    Buf += NumBytesToRead/3;
    /*close open file*/
    Fclose(fin);
    return NOERROR;                                          
}

////////////////////////////////////////////////////////////////////////////////
//
//>  Name:           TestResourceFclose
//
//   Type:           Function
//
//   Description:    Test the file system steering capability for Fclose
// 
//   Inputs:         none 
//
//   Outputs:        none
//                   
//   Notes:          Right now, the meat of this test is the same as TestFclose
//                   except that everywhere we call Fclose, we add
//                   RESOURCE_HANDLE_MIN to the file handle to make it look like
//                   a resource file handle.  We do this because the os_rsc_XXX
//                   functions are currently stubbed and the stubs simply
//                   subtract out the RESOURCE_HANDLE_MIN value and call the
//                   normal Fclose function.  Once the stubs are removed, the
//                   test should fail and that will be an indication to update
//                   this test to use the real os_rsc_XXX functions.
//                   //<
//////////////////////////////////////////////////////////////////////////////////
int TestResourceFclose(void)
{
    /*open the file*/
    if ((fout = Fopen(testfile1,(uint8_t *)"w"))<0)
        return  ERROR_GENERIC;
   /* Close the file*/     
    if((Fclose(fout+RESOURCE_HANDLE_MIN))<0)
        return  ERROR_GENERIC;
    /*Again close the same file,it should return error*/
    if((Fclose(fout+RESOURCE_HANDLE_MIN))>0)
        return  ERROR_GENERIC;
    /*open the file*/    
    if ((fout = Fopen(testfile1,(uint8_t *)"a"))<0)
        return  ERROR_GENERIC;
    /*Try to write some bytes*/        
    if((RetValue=Fwrite(fout, BitBuffer,755))<0)
        return ERROR_GENERIC;
    /*close the file*/    
    Fclose(fout+RESOURCE_HANDLE_MIN);
    FlushCache();
    /*Try to write in closed file, it should return error*/
    if((RetValue=Fwrite(fout, BitBuffer,755))<0)
        return NOERROR;

    return  NOERROR;
}

// Stubs to allow os_fs_test to build
bool os_pmi_bCond(void *pSm, void *pTransAttr, void *pUserData)
{
  SystemHalt();
  return 0;
}

RtStatus_t os_pmi_ChangeState(void *pSm, void *pTransAttr, void *pUserData)
{
  SystemHalt();
  return 0;
}

#ifdef NEWADDED
int TestAllModes()
{
     uint8_t writeinfile[10]="Hello  ";
     uint8_t readfromfile[10];
     int j=0;
     for(j=0;j<10;j++)
  	readfromfile[j]='\0';

/*Write mode */
if ((foutw = Fopenw((uint8_t *)longfile3buf,(uint8_t *)"w"))<0)
         return  ERROR_GENERIC;
if((RetValue =Fwrite(foutw,writeinfile,10))<0)
         return ERROR_GENERIC;

 Fclose(foutw);
 FlushCache();


if ((fin = Fopenw((uint8_t *)longfile3buf,(uint8_t *)"r"))<0)
         return  ERROR_GENERIC;
if((RetValue = Fread(fin,readfromfile,10))<0)
         return ERROR_GENERIC;

	 for(j=0;j<10;j++)
	 {     if(writeinfile[j]!=readfromfile[j])
		 return ERROR_GENERIC;
	 }
for(j=0;j<10;j++)
{  	readfromfile[j]='\0';
        writeinfile[j]='\0';
}
 Fclose(foutw);
 FlushCache();
 return NOERROR;

}
#endif
#pragma ghs endnowarning    // end nowarning 550
// eof fs_fat_test.c
//! @}
