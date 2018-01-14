/******************************************************************************
 *	SubProject 	: CheckDisk Utility.
 *	File	   	: Main Application File
 *	Description	: main() will first decide  to run check disk depending upon the
 *				  Constant  Defined.  If   it is to be run form System Restet or
 *				  through main.  If it is to be run form start up.. it will skip
 *				  Execution  of main because CheckDisk() Function will be called
 *				  from the Appropriate Booting Functions(yet to found.......).
 *				  Else it checks the particular bit in RTC Register to determie
 *				  whether to run check disk or not.
 *	Note		: TestSkipCheckDisk() and SetSkipCheckDisk() Function Call 
 *				  avoided. Also loadchkdsk.asm is not part of project which
 *				  loads the Check Disk utility to application.
 ******************************************************************************/


/*********************
 *	Include Files
 *********************/
#include "types.h"
#include <os\fsapi.h>
#include <drivers\ddi_media.h>


void main()
{
    RtStatus_t retCode;
    // Initialize the media
    retCode = MediaInit(0);
    if( SUCCESS != retCode )
    {
        SystemHalt();
    }

    // Discover media allocation
    retCode = MediaDiscoverAllocation(0);
    if( SUCCESS != retCode )
    {
        SystemHalt();
    }
    DriveInitAll();

    // Initialize the FAT file system
    retCode = FSInit(bufx, bufy, maxdevices, maxhandles, maxcaches );
    if( SUCCESS != retCode )
    {
        SystemHalt();
    }

    CheckDisk( 0 );
	printf("Check Disk Test Completed\n");
}
