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

#include "types.h"
#include "errordefs.h"
#include "drivers/media/sectordef.h"
#include <stdio.h>
#include "drivers/media/buffer_manager/media_buffer_manager.h"

extern "C" {
#include "os/thi/os_thi_api.h"
#include "hw/lradc/hw_lradc.h"
#include "os/eoi/os_eoi_api.h"
#include "drivers/rtc/ddi_rtc.h"
}

extern RtStatus_t test_main(ULONG param);
extern "C" void basic_os_entry(void *threadx_avail_mem);

#define     EXAMPLE_TEST_TASK_PRIORITY          9
#define     EXAMPLE_TEST_TASK_STACK_SIZE        4000

#define EXTRAS_STATIC_SECTOR_BUFFERS 2
#define EXTRAS_STATIC_AUX_BUFFERS 2

TX_THREAD g_example_test_thread;

uint32_t g_u32TestStack[EXAMPLE_TEST_TASK_STACK_SIZE/4];

#if (EXTRAS_STATIC_SECTOR_BUFFERS > 0)
//! Extra static media sector buffers.
static SECTOR_BUFFER s_extraSectorBuffers[EXTRAS_STATIC_SECTOR_BUFFERS][NOMINAL_DATA_SECTOR_ALLOC_SIZE];
#endif

#if (EXTRAS_STATIC_AUX_BUFFERS > 0)
//! Extra static media sector buffers.
static SECTOR_BUFFER s_extraAuxBuffers[EXTRAS_STATIC_AUX_BUFFERS][NOMINAL_AUXILIARY_SECTOR_ALLOC_SIZE];
#endif

extern unsigned char __ghsbegin_heap[];

///////////////////////////////////////////////////////////////////////////////
//! basic_os_entry
//!
//! \brief this function is the main entry point for the basic_os framework.
//!
//! \fntype non-reentrant Function
//!
///////////////////////////////////////////////////////////////////////////////
void basic_os_entry(void *threadx_avail_mem)
{
#ifdef OS_VMI_ENABLED
    hw_core_EnableIrqInterrupt(TRUE);
#endif
    
    hw_lradc_Init(true, LRADC_CLOCK_2MHZ);
    ddi_rtc_Init();
    os_eoi_Init();

    uint8_t *heap_mem = (uint8_t *)__ghsbegin_heap;
    int i;
    RtStatus_t status;
    
    // Init DMI : dmi will find the actual end of the heap itself, we just need to give dmi its start.
    status = os_dmi_Init(&heap_mem, &heap_mem);
    if (status)
    {
        printf("os_dmi_Init failed\r\n");
        return;
    }

    // Init the buffer manager
    if (media_buffer_init())
    {
        printf("Failed to init buffer manager\r\n");
        return;
    }

#if (EXTRAS_STATIC_SECTOR_BUFFERS > 0)
    // Add extra static NCNB sector buffers to the media buffer manager.
    for (i=0; i < EXTRAS_STATIC_SECTOR_BUFFERS; i++)
    {
        media_buffer_add(kMediaBufferType_Sector, kMediaBufferFlag_None, s_extraSectorBuffers[i]);
    }
#endif
    
#if (EXTRAS_STATIC_AUX_BUFFERS > 0)
    // Add extra static NCNB auxiliary buffers to the media buffer manager.
    for (i=0; i < EXTRAS_STATIC_AUX_BUFFERS; ++i)
    {
        media_buffer_add(kMediaBufferType_Auxiliary, kMediaBufferFlag_None, s_extraAuxBuffers[i]);
    }
#endif
    
    tx_thread_create(&g_example_test_thread,
                    "EXAMPLE TEST TASK", 
                    (void(*)(ULONG))test_main, 
                    0,
                    g_u32TestStack, 
                    EXAMPLE_TEST_TASK_STACK_SIZE, 
                    EXAMPLE_TEST_TASK_PRIORITY, 
                    EXAMPLE_TEST_TASK_PRIORITY,
                    TX_NO_TIME_SLICE,
                    TX_AUTO_START);
}
