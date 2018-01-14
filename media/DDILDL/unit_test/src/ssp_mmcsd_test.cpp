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
#include "drivers/ssp/mmcsd/ddi_ssp_mmcsd.h"
#include "drivers/ssp/mmcsd/ddi_ssp_mmcsd_board.h"

using namespace mmchal;

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
    // Initialize the HAL.
    MmcHal::init();

    // Get the SSP port ID associated with internal media.
    SspPortId_t portId = ddi_ssp_mmcsd_GetMediaPortId(0);

    // Initialize the port to support non-removable media.
    RtStatus_t status = MmcHal::initPort(portId, false);
    if (status != SUCCESS)
    {
        FASTPRINT("MmcHal::initPort returned 0x%08x\n", status);
        return status;
    }

    // Probe the port for attached media.
    MmcSdDevice* device = 0;
    status = MmcHal::probePort(portId, &device);
    if (status != SUCCESS)
    {
        FASTPRINT("MmcHal::probePort returned 0x%08x\n", status);
        return status;
    }
    assert(device);

    // Print the product name.
    FASTPRINT("Device product name is [%s]\n", device->getProductName());

    // Release the device.
    MmcHal::releaseDevice(portId);
    device = 0;

    // Shutdown the HAL.
    MmcHal::shutdown();

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

