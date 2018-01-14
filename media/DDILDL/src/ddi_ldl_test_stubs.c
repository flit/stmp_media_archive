#include "types.h"
#include <drivers/ddi_media.h>
#include <drivers/sectordef.h>

#include "ddi_media_internal.h"
#include <error.h>

////////////////////////////////////////////////////////////////////////////////
//
//>  Name:          ddi_mmc_MediaShutdown
//
//   Type:          Function
//
//   Description:
//
//   Inputs:        wLogMediaNumber     Logical Media Number
//
//   Outputs:       RtStatus_t
//
//   Notes:         none
//<
////////////////////////////////////////////////////////////////////////////////
RtStatus_t ddi_mmc_MediaShutdown(LogicalMedia_t * pDescriptor)
{
    
    return SUCCESS;
}

