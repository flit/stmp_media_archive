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
//! \addtogroup ddi_media_nand_hal_gpmi_internal
//! @{
//! \file    ddi_nand_gpmi_pmi.cpp
//! \brief   NAND HAL GPMI handling functions.
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
// Includes
////////////////////////////////////////////////////////////////////////////////

#include "types.h"
#include "drivers/media/ddi_media.h"
#include "hw/digctl/hw_digctl.h"
#include "hw/profile/hw_profile.h"
#include "hw/core/hw_core.h"
#include "drivers/clocks/ddi_clocks.h"
#include "ddi_nand_gpmi_internal.h"
#include "hw/core/vmemory.h"
#include "os/thi/os_thi_api.h"

// Make these functions weak so we don't get a link error if they aren't present.
#pragma weak os_pmi_RegisterPreGpmiClkCallback
#pragma weak os_pmi_RegisterPostGpmiClkCallback

////////////////////////////////////////////////////////////////////////////////
// Code
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
// See ddi_nand_gpmi.h for documentation.
////////////////////////////////////////////////////////////////////////////////
__INIT_TEXT RtStatus_t ddi_gpmi_init_pmi(void)
{
    // Only call into PMI if it is present in the app.
    if (os_pmi_RegisterPreGpmiClkCallback && os_pmi_RegisterPostGpmiClkCallback)
    {
        os_pmi_RegisterPreGpmiClkCallback(ddi_gpmi_handle_pre_pmi_change);
        os_pmi_RegisterPostGpmiClkCallback(ddi_gpmi_handle_post_pmi_change);
    }
    return SUCCESS;
}

////////////////////////////////////////////////////////////////////////////////
//! \brief Acknowledge any outstanding PMI requests.
//!
//! Only the pre-change PMI events need acknowledgement from this function,
//! and only when there was an active DMA when the pre-change notification
//! was received. All of the post-change events are acked directly in the
//! event notification handler.
////////////////////////////////////////////////////////////////////////////////
__STATIC_TEXT void ddi_gpmi_ack_pmi_event(void)
{
    // Acknowledge the GPMI_CLK request.
    if (g_gpmiPmiStatus.waitingForGPMIAck)
    {
        uint32_t returnCode;
        
        g_gpmiPmiStatus.waitingForGPMIAck = false;
        
        // Put the ack semaphore to wake up the PMI thread which is sitting in
        // our pre-change notification handler.
        returnCode = tx_semaphore_put(&g_gpmiPmiStatus.ackSemaphore);
        assert(returnCode == TX_SUCCESS);

        // to get rid of compiler warning because asserts are removed in release builds.
        returnCode = returnCode;
    }
}

////////////////////////////////////////////////////////////////////////////////
//! \brief Wait until a PMI event is complete.
//!
//! The stall DMA flag is checked, and if set the stall DMA semaphore is
//! obtained. Because the semaphore count is always 0 when the stall DMA
//! flag is set, the caller will be blocked waiting until the semaphore is
//! put by the post-PMI event handler.
//!
//! This code depends on there being only one thread that can call this
//! function at a time. This is theoretically guaranteed because the only
//! caller of this function is NAND_HAL_Start_DMA(), which has a requirement
//! that its caller prevent multiple concurrent DMA requests.
////////////////////////////////////////////////////////////////////////////////
__STATIC_TEXT void ddi_gpmi_wait_for_pmi_event(void)
{
    uint32_t returnCode;

    // Only wait on the semaphore if the stall flag is set.
    if (g_gpmiPmiStatus.bStallDmaFlag)
    {
        // Tell the post-change handler that a DMA is stalled so it can put our semaphore.
        g_gpmiPmiStatus.isDmaStalled = true;
        
        // Wait on the stall semaphore. It is set by the post-change
        // handler when PMI is finished.
        returnCode = tx_semaphore_get(&g_gpmiPmiStatus.stallDMASemaphore, OS_MSECS_TO_TICKS(PMI_WAIT_TIMEOUT));
        assert(returnCode == TX_SUCCESS);

        // to get rid of compiler warning because asserts are removed in release builds.
        returnCode = returnCode;
        
        // We're no longer stalled.
        g_gpmiPmiStatus.isDmaStalled = false;
    }
}

////////////////////////////////////////////////////////////////////////////////
//! \brief      Handle pre-change notices from PMI.
//!
//! \fntype     Non-Reentrant
//!
//! Handle pre-change notices from PMI for pending clock changes.
//! This has an interlock with DMA to nands to synchronize changes
//!	with active DMA.
//!
//! \retval     SUCCESS
//!
//! \note This code assumes that the pre and post PMI event handlers can
//!     never be invoked concurrently on separate threads.
////////////////////////////////////////////////////////////////////////////////
RtStatus_t ddi_gpmi_handle_pre_pmi_change(void)
{
    bool saveIrqState;
    uint32_t returnCode;
    
    // Keep track of how many outstanding PMI events have occurred.
    g_gpmiPmiStatus.outstandingRequests++;

    // We always stall DMAs first.
    g_gpmiPmiStatus.bStallDmaFlag = true;
    
    // Disable IRQ. We don't want control leaving this thread between when we check
    // for an active DMA and when the waiting-for-ack flag(s) are set.
    saveIrqState = hw_core_EnableIrqInterrupt(false);
    
    // Now check to see if the DMA is currently running.
    if (g_gpmiPmiStatus.bInDmaFlag)
    {
        // Set the "waiting for ack" flag to tell the driver the PMI is waiting.
        g_gpmiPmiStatus.waitingForGPMIAck = true;
        
        // Wait until the active DMA completes.
        returnCode = tx_semaphore_get(&g_gpmiPmiStatus.ackSemaphore, OS_MSECS_TO_TICKS(PMI_WAIT_TIMEOUT));
        assert(returnCode == TX_SUCCESS);
    }

    // Re-enable interrupts.
    hw_core_EnableIrqInterrupt(saveIrqState);

    // to get rid of compiler warning because asserts are removed in release builds.
    returnCode = returnCode;

    return SUCCESS;
}

////////////////////////////////////////////////////////////////////////////////
//! \brief      Handle post-change notices from PMI.
//!
//! \fntype     Non-Reentrant
//!
//! Handle post-change notices from PMI for all clock changes. It is shared
//! between all PMI event types that the GPMI driver is interested in.
//! This has an interlock with DMA to nands to synchronize changes
//!	with active DMA.
//!
//! \retval     SUCCESS
//!
//! \note This code assumes that the pre and post PMI event handlers can
//!     never be invoked concurrently on separate threads.
////////////////////////////////////////////////////////////////////////////////
RtStatus_t ddi_gpmi_handle_post_pmi_change(void)
{
    // Do the actual update for the timers now that PMI has finished changing clocks.
   ddi_gpmi_set_timings( NULL, true /* bWriteToTheDevice */);

    // Only want to put the semaphore on the last POST event
    if (!--g_gpmiPmiStatus.outstandingRequests)
    {
        // Timing change complete so clear STALL flag.
        g_gpmiPmiStatus.bStallDmaFlag = false;

        // We only want to put the semaphore if a DMA was actually stalled. Otherwise,
        // the semaphore could end up with a count greater than 1.
        if (g_gpmiPmiStatus.isDmaStalled)
        {
            // Put the semaphore so we can start.
            uint32_t returnCode = tx_semaphore_put(&g_gpmiPmiStatus.stallDMASemaphore);
            assert(returnCode == TX_SUCCESS);

            // to get rid of compiler warning because asserts are removed in release builds.
            returnCode = returnCode;
}
    }

    return SUCCESS;
}
////////////////////////////////////////////////////////////////////////////////
// End of file
////////////////////////////////////////////////////////////////////////////////
//! @}
