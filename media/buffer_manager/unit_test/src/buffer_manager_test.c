////////////////////////////////////////////////////////////////////////////////
// Copyright(C) SigmaTel, Inc. 2003-2007
//
// Filename:    buffer_manager_test.c
// Description:
////////////////////////////////////////////////////////////////////////////////

#include "drivers/media/buffer_manager/media_buffer_manager.h"
#include "hw/core/vmemory.h"
#include "os/threadx/tx_api.h"
#include "os/dmi/os_dmi_api.h"
#include "drivers/sectordef.h"
#include <stdlib.h>

#define EXTRAS_STATIC_SECTOR_BUFFERS 2
#define EXTRAS_STATIC_AUX_BUFFERS 1

extern unsigned char __ghsbegin_heap[];

SECTOR_BUFFER g_test_buffer_1[1000] __OCRAM_BSS_NCNB;

#if (EXTRAS_STATIC_SECTOR_BUFFERS > 0)
//! Extra static media sector buffers.
static SECTOR_BUFFER s_extraSectorBuffers[EXTRAS_STATIC_SECTOR_BUFFERS][NOMINAL_DATA_SECTOR_ALLOC_SIZE] __BSS_NCNB;
#endif

#if (EXTRAS_STATIC_AUX_BUFFERS > 0)
//! Extra static media sector buffers.
static SECTOR_BUFFER s_extraAuxBuffers[EXTRAS_STATIC_AUX_BUFFERS][NOMINAL_AUXILIARY_SECTOR_ALLOC_SIZE] __BSS_NCNB;
#endif

RtStatus_t BufferManagerTest(void)
{
    // Define pointers to the first available memory
    //   and the end of free memory.  free_mem comes immediately before the heap.  some of free_mem
    //   is used for ghs startup.  here we simply start the threadx heap where the heap section begins.  this means
    //   that the unused part of free_mem is still available for use.
    uint8_t *heap_mem = (uint8_t *)__ghsbegin_heap;
    RtStatus_t status;
    SECTOR_BUFFER * buffer;
    SECTOR_BUFFER * buffer2;
    SECTOR_BUFFER * buffer3;
    void * bumpOnALog;
#if (EXTRAS_STATIC_SECTOR_BUFFERS > 0) || (EXTRAS_STATIC_AUX_BUFFERS > 0)
    int i;
#endif

    // Init DMI. It will find the actual end of the heap itself, we just need to give it the start.
    status = os_dmi_Init(&heap_mem, &heap_mem);
    if (status)
    {
        printf("os_dmi_Init failed: 0x%08x\n", status);
        return status;
    }

    // Init.
    status = media_buffer_init();
    if (status)
    {
        printf("buffer_init failed: 0x%08x\n", status);
        return status;
    }

#if (EXTRAS_STATIC_SECTOR_BUFFERS > 0)
    // Add extra static NCNB sector buffers to the media buffer manager.
    for (i=0; i < EXTRAS_STATIC_SECTOR_BUFFERS; i++)
    {
        media_buffer_add(kMediaBufferType_Sector, kMediaBufferFlag_NCNB, s_extraSectorBuffers[i]);
    }
#endif

#if (EXTRAS_STATIC_AUX_BUFFERS > 0)
    // Add extra static NCNB auxiliary buffers to the media buffer manager.
    for (i=0; i < EXTRAS_STATIC_AUX_BUFFERS; ++i)
    {
        media_buffer_add(kMediaBufferType_Auxiliary, kMediaBufferFlag_NCNB, s_extraAuxBuffers[i]);
    }
#endif

    // Get a buffer.
    status = media_buffer_acquire(kMediaBufferType_Sector, kMediaBufferFlag_None, &buffer);
    if (status)
    {
        printf("media_buffer_acquire failed: 0x%08x\n", status);
        return status;
    }
    printf("Acquired buffer 0x%08x\n", buffer);

    // Free it.
    status = media_buffer_release(buffer);
    if (status)
    {
        printf("media_buffer_release failed: 0x%08x\n", status);
        return status;
    }
    printf("Released buffer 0x%08x\n", buffer);

    // Add a fast mem NCNB buffer.
    status = media_buffer_add(kMediaBufferType_Auxiliary, kMediaBufferFlag_FastMemory | kMediaBufferFlag_NCNB, g_test_buffer_1);
    if (status)
    {
        printf("media_buffer_add failed: 0x%08x\n", status);
        return status;
    }
    printf("Added buffer 0x%08x\n", g_test_buffer_1);

    // Try to acquire the buffer just added.
    status = media_buffer_acquire(kMediaBufferType_Auxiliary, kMediaBufferFlag_FastMemory, &buffer);
    if (status)
    {
        printf("media_buffer_acquire failed: 0x%08x\n", status);
        return status;
    }
    
    if (buffer != g_test_buffer_1)
    {
        printf("Unexpected buffer was acquired (#1)! (%x != %x)\n", buffer, g_test_buffer_1);
    }
    else
    {
        printf("Acquired buffer 0x%08x\n", buffer);
    }

    // Acquire a second fast mem buffer, which shouldn't exist, causing a
    // temporary one to be created.
    status = media_buffer_acquire(kMediaBufferType_Auxiliary, kMediaBufferFlag_FastMemory, &buffer2);
    if (status)
    {
        printf("media_buffer_acquire failed: 0x%08x\n", status);
        return status;
    }
    printf("Acquired buffer 0x%08x\n", buffer2);

    // Release the temp buffer.
    status = media_buffer_release(buffer2);
    if (status)
    {
        printf("media_buffer_release failed: 0x%08x\n", status);
        return status;
    }
    printf("Released buffer 0x%08x\n", buffer2);
    
    // Sleep for a bit.
    printf("Sleeping 5 ticks\n");
    tx_thread_sleep(5);
    
    // Allocate some memory.
    bumpOnALog = os_dmi_malloc_fastmem(640);
    ((uint8_t *)bumpOnALog)[0] = 1; // So the compiler doesn't complain about us not using the variable.

    // Acquire again. This should reacquire the temp buffer just released.
    status = media_buffer_acquire(kMediaBufferType_Auxiliary, kMediaBufferFlag_FastMemory, &buffer3);
    if (status)
    {
        printf("media_buffer_acquire failed: 0x%08x\n", status);
        return status;
    }
    
    if (buffer2 != buffer3)
    {
        printf("Unexpected buffer was acquired (#2)! (%x != %x)\n", buffer3, buffer2);
    }
    else
    {
        printf("Acquired buffer 0x%08x\n", buffer3);
    }

    // Release the temp buffer, again
    status = media_buffer_release(buffer3);
    if (status)
    {
        printf("media_buffer_release failed: 0x%08x\n", status);
        return status;
    }
    printf("Released buffer 0x%08x\n", buffer3);
    
    // Sleep for long enough to cause the temp buffer to be truly freed.
    printf("Sleeping 30 ticks\n");
    tx_thread_sleep(30);
    
    // Allocate some memory. This should use the temp buffer that was just freed.
    bumpOnALog = os_dmi_malloc_fastmem(588);
    ((uint8_t *)bumpOnALog)[0] = 1; // So the compiler doesn't complain about us not using the variable.

    // Acquire a third time. This will cause another temp allocation.
    status = media_buffer_acquire(kMediaBufferType_Auxiliary, kMediaBufferFlag_FastMemory, &buffer3);
    if (status)
    {
        printf("media_buffer_acquire failed: 0x%08x\n", status);
        return status;
    }
    
    if (buffer3 == buffer2)
    {
        printf("Unexpected buffer was acquired (#3)! (%x == %x)\n", buffer3, buffer2);
    }
    else
    {
        printf("Acquired buffer 0x%08x\n", buffer3);
    }

    // Release the temp buffer.
    status = media_buffer_release(buffer3);
    if (status)
    {
        printf("media_buffer_release failed: 0x%08x\n", status);
        return status;
    }
    printf("Released buffer 0x%08x\n", buffer3);

    // Release the first buffer.
    status = media_buffer_release(buffer);
    if (status)
    {
        printf("media_buffer_release failed: 0x%08x\n", status);
        return status;
    }
    printf("Released buffer 0x%08x\n", buffer);

    return SUCCESS;
}

RtStatus_t test_main(ULONG param)
{
    RtStatus_t status;

    status = BufferManagerTest();
    if (status == SUCCESS)
    {
        printf("Test passed!\r\n");
    }
    else
    {
        printf("Test failed with error: 0x%08x\r\n", (unsigned int)status);
    }

    exit(status);
    return status;
}

#define     EXAMPLE_TEST_TASK_PRIORITY          9
#define     EXAMPLE_TEST_TASK_STACK_SIZE        4000

TX_THREAD g_example_test_thread;

uint32_t g_u32TestStack[EXAMPLE_TEST_TASK_STACK_SIZE/4];

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

//     hw_lradc_Init(true, LRADC_CLOCK_2MHZ);
//     ddi_rtc_Init();
//     os_eoi_Init();

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

