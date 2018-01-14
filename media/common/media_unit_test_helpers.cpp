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

#include "media_unit_test_helpers.h"
#include "registers/regsdigctl.h"

////////////////////////////////////////////////////////////////////////////////
// Variables
////////////////////////////////////////////////////////////////////////////////

TX_THREAD g_example_test_thread;
uint32_t g_u32TestStack[EXAMPLE_TEST_TASK_STACK_SIZE/4];

#if (EXTRAS_STATIC_SECTOR_BUFFERS > 0)
//! Extra static media sector buffers.
static SECTOR_BUFFER s_extraSectorBuffers[EXTRAS_STATIC_SECTOR_BUFFERS][NOMINAL_DATA_SECTOR_ALLOC_SIZE] __BSS_NCNB;
#endif

#if (EXTRAS_STATIC_AUX_BUFFERS > 0)
//! Extra static media sector buffers.
static SECTOR_BUFFER s_extraAuxBuffers[EXTRAS_STATIC_AUX_BUFFERS][NOMINAL_AUXILIARY_SECTOR_ALLOC_SIZE] __BSS_NCNB;
#endif

extern unsigned char __ghsbegin_heap[];

uint32_t g_actualBufferBytes = kMaxBufferBytes;

//! \name Buffers
//@{
#pragma alignvar(32)
SECTOR_BUFFER s_dataBuffer[CACHED_BUFFER_SIZE_IN_WORDS(kMaxBufferBytes)];

#pragma alignvar(32)
SECTOR_BUFFER s_readBuffer[CACHED_BUFFER_SIZE_IN_WORDS(kMaxBufferBytes)];

#pragma alignvar(32)
SECTOR_BUFFER g_aux_buffer[SECTOR_BUFFER_ALLOC_SIZE(REDUNDANT_AREA_BUFFER_ALLOCATION)];

#pragma alignvar(32)
SECTOR_BUFFER g_read_aux_buffer[SECTOR_BUFFER_ALLOC_SIZE(REDUNDANT_AREA_BUFFER_ALLOCATION)];
//@}

bool g_enableFastPrint = true;
char s_printfBuffer[1024];

//! True if DMI and bufmgr have been inited.
bool g_initedMemoryHelperDrivers = false;

// You can set g_bAllocate to FALSE in the debugger, to prevent calling MediaErase() and MediaAllocate().
// That will allow you to analyze or test an existing configuration.
bool g_bAllocate = true;

Taus88 * g_rng = NULL;

////////////////////////////////////////////////////////////////////////////////
// Code
////////////////////////////////////////////////////////////////////////////////

RtStatus_t SDKInitialization()
{
    RtStatus_t status;
    // Define pointers to the first available memory
    //   and the end of free memory.  free_mem comes immediately before the heap.  some of free_mem
    //   is used for ghs startup.  here we simply start the threadx heap where the heap section begins.  this means
    //   that the unused part of free_mem is still available for use.
    uint8_t *heap_mem = (uint8_t *)__ghsbegin_heap;
    int i;

    // We only want to init DMI and the media buffer manager once, regardless of how
    // many times this function is called from the unit test.
    if (!g_initedMemoryHelperDrivers)
    {
        // Init DMI : dmi will find the actual end of the heap itself, we just need to give dmi its start.
        status = os_dmi_Init(&heap_mem, &heap_mem);
        if (status)
        {
            FASTPRINT("os_dmi_Init failed\r\n");
            return ERROR_GENERIC;
        }

        // Init the buffer manager
        if (media_buffer_init())
        {
            FASTPRINT("Failed to init buffer manager\r\n");
            return ERROR_GENERIC;
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
    
        g_initedMemoryHelperDrivers = true;
    }
    
    uint32_t pclkFreq = MAX_PCLK;
    status = ddi_clocks_SetPclkHclk(&pclkFreq, 4);
    if (status != SUCCESS)
    {
        FASTPRINT("Failed to set PCLK to max (0x%08x)!\n", status);
    }

    uint32_t gpmiFreq = 96000;
    status = ddi_clocks_SetGpmiClk(&gpmiFreq, true);
    if (status != SUCCESS)
    {
        FASTPRINT("Failed to set GPMI_CLK to 96MHz (0x%08x)!\n", status);
    }

    // Init PRNG. Start with the entropy register, and add in the microseconds counter
    // so we get variability even if the entropy register doesn't change.
    uint32_t seed = HW_DIGCTL_ENTROPY_RD();
    seed += hw_profile_GetMicroseconds();
    g_rng = new Taus88(seed);
    assert(g_rng);

    return SUCCESS;
}

double get_mb_s(uint64_t bytes, uint64_t elapsedTime)
{
    if (bytes == 0 || elapsedTime == 0)
    {
        return 0.0;
    }
    return double(bytes) / (double(elapsedTime) / 1000000.0) / 1048576.0;
}

char * bytes_to_pretty_string(uint64_t bytes)
{
    static char buf[128];

    if (bytes < 1024)
    {
        snprintf(buf, sizeof(buf), "%u bytes", unsigned(bytes));
    }
    else if (bytes < 1024LLU*1024LLU) // kB
    {
        snprintf(buf, sizeof(buf), "%.2f kB", (double(bytes) / 1024.0));
    }
    else if (bytes < 1024LLU*1024LLU*1024LLU) // MB
    {
        snprintf(buf, sizeof(buf), "%.2f MB", (double(bytes) / 1024.0 / 1024.0));
    }
    else if (bytes < 1024LLU*1024LLU*1024LLU*1024LLU) // GB
    {
        snprintf(buf, sizeof(buf), "%.2f GB", (double(bytes) / 1024.0 / 1024.0 / 1024.0));
    }
    else if (bytes < 1024LLU*1024LLU*1024LLU*1024LLU*1024LLU) // TB
    {
        snprintf(buf, sizeof(buf), "%.2f TB", (double(bytes) / 1024.0 / 1024.0 / 1024.0 / 1024.0));
    }
    
    char * result = (char *)malloc(strlen(buf) + 1);
    if (result)
    {
        strcpy(result, buf);
    }
    
    return result;
}

char * microseconds_to_pretty_string(uint64_t elapsedTime)
{
    static char buf[128];
    
    double doubleTime = elapsedTime;
    double secs = doubleTime / 1000000.0;
    int hours;
    int minutes;
    if (elapsedTime < 1000)
    {
        snprintf(buf, sizeof(buf), "%u µs", uint32_t(elapsedTime));
    }
    else if (elapsedTime < 1000000)
    {
        snprintf(buf, sizeof(buf), "%.2f ms", doubleTime / 1000.0);
    }
    else if (secs < 60.0)
    {
        snprintf(buf, sizeof(buf), "%.2f sec", secs);
    }
    else if (secs < 60.0*60.0)
    {
        minutes = int(secs) / 60;
        secs -= (double(minutes) * 60.0);
        snprintf(buf, sizeof(buf), "%u min %.2f sec", minutes, secs);
    }
    else
    {
        hours = int64_t(secs) / 3600LL;
        minutes = (int64_t(secs) - (int64_t(hours) * 3600LL)) / 60LL;
        secs -= ((double(minutes) * 60.0) + (double(hours) * 3600.0));
        snprintf(buf, sizeof(buf), "%u hr %u min %.2f sec", hours, minutes, secs);
    }
    
    char * result = (char *)malloc(strlen(buf) + 1);
    if (result)
    {
        strcpy(result, buf);
    }
    
    return result;
}

//! \brief Fill the given buffer with a pattern based on the sector number.
void fill_data_buffer(SECTOR_BUFFER * buffer, uint32_t sectorNumber, void * p)
{
    uint32_t i;
    
    for (i=0; i < SIZE_IN_WORDS(g_actualBufferBytes); ++i)
    {
        buffer[i] = (sectorNumber ^ ((~sectorNumber) << 8) ^ (sectorNumber << 16) ^ ((~sectorNumber) << 24)) ^ (uint32_t)p ^ 0x96f187e2 ^ i ^ (i << 8) ^ (i << 16) ^ (i << 24);
    }
}

void clear_buffer(SECTOR_BUFFER * buffer)
{
    memset(buffer, 0xff, kMaxBufferBytes);
}

void clear_aux()
{
    memset(g_aux_buffer, 0xff, sizeof(g_aux_buffer));
}

void fill_aux(SECTOR_BUFFER * buffer, uint32_t sectorNumber)
{
    memset(buffer, 0xff, sizeof(g_aux_buffer));
    
    int i;
    for (i=0; i<3; ++i)
    {
        buffer[i] = (sectorNumber ^ ((~sectorNumber) << 8) ^ (sectorNumber << 16) ^ ((~sectorNumber) << 24)) ^ 0x2e781f69;
    }
    
    // Make sure it doesn't look like a bad block
    ((uint8_t *)buffer)[0] = 0xff;
}

//! \param a The actual contents.
//! \param b The expected contents.
bool compare_buffers(const void * a, const void * b, uint32_t count)
{
#if 0
    const uint8_t * ca = reinterpret_cast<const uint8_t *>(a);
    const uint8_t * cb = reinterpret_cast<const uint8_t *>(b);
    uint32_t offset = 0;
    while (count--)
    {
        if (*ca++ != *cb++)
        {
            FASTPRINT("buffer mismatch at offset %u (actual:0x%02x != expected:0x%02x)\n", offset, *(ca - 1), *(cb - 1));
            return false;
        }
        
        offset++;
    }
#else
    const uint32_t * ca = reinterpret_cast<const uint32_t *>(a);
    const uint32_t * cb = reinterpret_cast<const uint32_t *>(b);
    uint32_t offset = 0;
    count /= sizeof(uint32_t);
    while (count--)
    {
        if (*ca++ != *cb++)
        {
            FASTPRINT("buffer mismatch at word %u (actual:0x%08x != expected:0x%08x)\n", offset, *(ca - 1), *(cb - 1));
            return false;
        }
        
        offset++;
    }
#endif
    
    return true;
}

unsigned count_buffer_mismatches(const void * a, const void * b, uint32_t count)
{
    unsigned mismatches = 0;
    const uint8_t * ca = reinterpret_cast<const uint8_t *>(a);
    const uint8_t * cb = reinterpret_cast<const uint8_t *>(b);
    while (count--)
    {
        if (*ca++ != *cb++)
        {
            mismatches++;
        }
    }
    
    return mismatches;
}

bool random_percent(int chance)
{
    return (g_rng->next(10000) < chance);
}

int random_range(int max)
{
    return g_rng->next(max);

//     int64_t r = rand();
//     return r * (int64_t)max / (int64_t)RAND_MAX;
}

void test_random_percent(int chance)
{
    int y = 0;
    int n = 0;
    int i;
    for (i=0; i < 1000; ++i)
    {
        bool q = random_percent(chance);
        q ? ++y : ++n;
    }
    
    float yp = (float)y * 100.0 / (float)i;
    float np = (float)n * 100.0 / (float)i;
    
    FASTPRINT("%d pct chance: yes=%g, no=%g\n", chance, yp, np);
}

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
    
    srand((unsigned)hw_profile_GetMicroseconds());
    
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

extern "C" RtStatus_t tss_Flush()
{
    return SUCCESS;
}
