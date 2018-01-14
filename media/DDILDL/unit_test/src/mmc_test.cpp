///////////////////////////////////////////////////////////////////////////////
// Copyright (c) Freescale Semiconductor, Inc. All rights reserved.
// 
// Freescale Semiconductor, Inc.
// Proprietary & Confidential
// 
// This source code and the algorithms implemented therein constitute
// confidential information and may comprise trade secrets of Freescale Semiconductor, Inc.
// or its associates, and any use thereof is subject to the terms and
// conditions of the Confidential Disclosure Agreement pursual to which this
// source code was originally received.
///////////////////////////////////////////////////////////////////////////////
#include "drivers/media/common/media_unit_test_helpers.h"
#include "drivers/media/mmc/src/MmcMedia.h"

using namespace mmc;

////////////////////////////////////////////////////////////////////////////////
// Definitions
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
// Prototypes
////////////////////////////////////////////////////////////////////////////////

RtStatus_t run_test();

////////////////////////////////////////////////////////////////////////////////
// Code
////////////////////////////////////////////////////////////////////////////////

RtStatus_t run_test()
{
    RtStatus_t status;

    // Allocate and initialize a media object.
    LogicalMedia *media = new MmcMedia();
    assert(media);
    media->m_u32MediaNumber = 0;        // internal media
    media->m_isRemovable = false;
    media->m_PhysicalType = kMediaTypeMMC;

    // Initialize the media object.
    status = media->init();
    if (status != SUCCESS)
    {
        FASTPRINT("Media init returned 0x%08x\n", status);
        return status;
    }

    // Probe for media.
    status = media->discover();
    if (status != SUCCESS)
    {
        FASTPRINT("Media discover returned 0x%08x\n", status);
        return status;
    }

    // Shutdown media.
    status = media->shutdown();
    if (status != SUCCESS)
    {
        FASTPRINT("Media shutdown returned 0x%08x\n", status);
        return status;
    }

    tss_logtext_Flush(TX_WAIT_FOREVER);

    return SUCCESS;
}

RtStatus_t test_main(ULONG param)
{
    RtStatus_t status;
    
    // Initialize the Media
    status = SDKInitialization();

    if (status == SUCCESS)
    {
        status = run_test();
    }
    
    if (status == SUCCESS)
    {
        FASTPRINT("unit test passed!\n");
    }
    else
    {
        FASTPRINT("unit test failed: 0x%08x\n", status);
    }
    
    exit(status);
    return status;
}

