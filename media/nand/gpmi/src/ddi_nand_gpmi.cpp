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
//! \file    ddi_nand_gpmi.cpp
//! \brief   NAND HAL GPMI handling functions.
////////////////////////////////////////////////////////////////////////////////

#include "types.h"
#include "drivers/media/ddi_media.h"
#include "hw/digctl/hw_digctl.h"
#include "hw/profile/hw_profile.h"
#include "hw/core/hw_core.h"
#include "drivers/clocks/ddi_clocks.h"
#include "components/telemetry/tss_logtext.h"
#include "components/telemetry/telemetry.h"
#include "ddi_nand_gpmi_internal.h"
#include "os/eoi/os_eoi_api.h"
#include "hw/pinmux/hw_pinmux_setup.h"
#include "hw/core/vmemory.h"
#include <string.h>
#include <algorithm>

////////////////////////////////////////////////////////////////////////////////
// Definitions
////////////////////////////////////////////////////////////////////////////////

//! Controls whether GPMI timing values should be printed to TSS when they are set.
//! Set to 1 to print timings.
//! This may need an increased stack for the PMI task due to an issue in tss_logtext_Flush(0)
#define GPMI_PRINT_TIMINGS 0 // SEE NOTE ON PREVIOUS LINE about stack issues

//! Generate bitmask for use with HW_PINCTRL_MUXSELn registers
//! This was copied from ata_hal.h but should be globally available
//! in regspinctrl.h
#define BM_PINCTRL_MUXSEL_NAND(msb, lsb) \
    (uint32_t)(((4 << (2*((msb)-(lsb)))) - 1) << (2*((lsb)&0xF)))


#define MAX_DATA_SETUP_CYCLES (BM_GPMI_TIMING0_DATA_SETUP >> BP_GPMI_TIMING0_DATA_SETUP)

#if defined(STMP37xx) || defined(STMP377x)

    #define MAX_DATA_SAMPLE_DELAY_CYCLES            (uint32_t)(BM_GPMI_CTRL1_DSAMPLE_TIME >> BP_GPMI_CTRL1_DSAMPLE_TIME)
    #define GPMI_DELAY_SHIFT                        (1)         // Right shift value to get the fractional GPMI time for data delay

    #define GPMI_DATA_SETUP_NS                      (0)         // Time in nanoSeconds required for GPMI data read internal setup

    #define GPMI_GET_MAX_DELAY_NS(x)                ((MAX_DATA_SAMPLE_DELAY_CYCLES * x) / 2)    // Max data delay possible for the GPMI
    
#elif defined(STMP378x)

    #define MAX_DATA_SAMPLE_DELAY_CYCLES            (uint32_t)(BM_GPMI_CTRL1_RDN_DELAY >> BP_GPMI_CTRL1_RDN_DELAY)
    #define GPMI_DELAY_SHIFT                        (3)         // Right shift value to get the fractional GPMI time for data delay
    #define GPMI_MAX_DLL_PERIOD_NS                  (32)        // Max GPMI clock Period that the GPMI DLL works for
    #define GPMI_DLL_HALF_THRESHOLD_PERIOD_NS       (16)        // Threshold for GPMI clock Period that above thise requires a divide by two for the DLL 
    #define GPMI_WAIT_CYCLES_AFTER_DLL_ENABLE       (64)        // Number of GPMI clock cycles to wait for use of GPMI after DLL enable 

    #define GPMI_DATA_SETUP_NS                      (0)         // Time in nanoSeconds required for GPMI data read internal setup

    #define GPMI_MAX_HARDWARE_DELAY_NS              (uint32_t)(16)        // Time in nanoSeconds required for GPMI data read internal setup

    // Max data delay possible for the GPMI.  Use the min 
    // of the time (16 nS) or what will fit in the register
    // If the GPMI clock period is greater than 
    // GPMI_MAX_DLL_PERIOD_NS then can't use the delay.
    #define GPMI_GET_MAX_DELAY_NS(x)                ((x < GPMI_MAX_DLL_PERIOD_NS) ? \
                                                    (std::min( GPMI_MAX_HARDWARE_DELAY_NS, ((MAX_DATA_SAMPLE_DELAY_CYCLES * x) / u32GpmiDelayFraction))) : \
                                                    0)

#else
	#error Must define STMP37xx, STMP377x or STMP378x
#endif


///////////////////////////////////////////////////////////////////////////////
//     GPMI Timing
//////////////////////////////////////////////////////////////////////////////

//! Frequency in kHz for GPMI_CLK. PMI will do its best to give us
//! this frequency when the PLL is enabled.
#define GPMI_CLK_PLL_ON_FREQUENCY_kHZ (96000)

//! Frequency to use for GPMI_CLK when the PLL is disabled.
#define GPMI_CLK_PLL_OFF_FREQUENCY_kHZ (24000)

// Should only need 10msec (Program/Erase), and reads shouldn't be anywhere near this long..
#define FLASH_BUSY_TIMEOUT          10000  //!< Busy Timeout time in microsec. (10msec)

//! 24MHz / (TDS+TDH) => 6MHz NAND strobe, TAS=0, TDS=45, TDH=30 nsec
//! Use the worst case conditions for all supported NANDs by default.
#define NAND_GPMI_TIMING0(AddSetup, DataSetup, DataHold) \
           (BF_GPMI_TIMING0_ADDRESS_SETUP(AddSetup) | \
            BF_GPMI_TIMING0_DATA_HOLD(DataHold) | \
            BF_GPMI_TIMING0_DATA_SETUP(DataSetup))

#define DDI_NAND_HAL_GPMI_SOFT_RESET_LATENCY  (2)

////////////////////////////////////////////////////////////////////////////////
// Prototypes
////////////////////////////////////////////////////////////////////////////////

uint32_t rom_nand_hal_GpmiInitializeDataSampleDelay(uint32_t u32GpmiPeriod_ns, bool bWriteToTheDevice);
void rom_nand_hal_GpmiSetAndEnableDataSampleDelay(uint32_t u32DelayCycles, uint32_t u32GpmiPeriod_ns);
#ifdef __cplusplus
extern "C" {
#endif
void ddi_nand_hal_GpmiSetNandTiming(const NAND_Timing2_struct_t * pNAND_Timing2_struct, 
                                    uint32_t u32GpmiPeriod_ns, 
                                    uint32_t u32PropDelayMin_ns, 
                                    uint32_t u32PropDelayMax_ns,
                                    bool bWriteToTheDevice);
#ifdef __cplusplus
} //extern "C"
#endif

uint32_t rom_nand_hal_FindGpmiCyclesCeiling(uint64_t u32NandTime_ns, uint32_t u32GpmiPeriod_ns, uint32_t u32MinVal = 0);
uint32_t rom_nand_hal_FindGpmiCyclesRounded(uint64_t u32NandTime_ns, uint32_t u32GpmiPeriod_ns);

RtStatus_t ddi_nand_hal_gpmi_RegisterGpmiFreqs(os_eoi_SubscriptionId_t SubscriptionId, os_eoi_Event_t EventCode);

void ddi_nand_hal_ConfigurePinmux(bool bUse16BitData,
                                  uint32_t u32NumberOfNANDs,
                                  bool bUseAlternateChipEnables,
                                  bool bUse1_8V_Drive,
                                  bool bEnableInternalPullups);

////////////////////////////////////////////////////////////////////////////////
// Variables
////////////////////////////////////////////////////////////////////////////////

//! Global PMI interface status information.
GpmiPmiStatus_t g_gpmiPmiStatus;

//! Current GPMI timings.
NAND_Timing2_struct_t   g_zNandTiming;

//! Minimum propagation delay of GPMI signals to and from the NAND.
uint32_t                g_u32GPMIPropDelayMin_ns=5;

//! Maximum propagation delay of GPMI signals to and from the NAND.
uint32_t                g_u32GPMIPropDelayMax_ns=9;

////////////////////////////////////////////////////////////////////////////////
// Code
////////////////////////////////////////////////////////////////////////////////

#if GPMI_PRINT_TIMINGS

static void _print_dynamic_timing_summary(
    uint32_t u32GpmiPeriod_ns,
    uint32_t u32GpmiDelayFraction,
    uint32_t i32tEYE,
    uint32_t i32DelayTime_ns,
    uint32_t u32GpmiMaxDelay_ns,
    uint32_t u32DataSetupCycles,
    uint32_t u32DataSetup_ns)
{
    tss_logtext_Print(LOGTEXT_VERBOSITY_3 | LOGTEXT_EVENT_DDI_NAND_GROUP, "  GpmiPeriod = %d ns\n", u32GpmiPeriod_ns);
    tss_logtext_Flush(0);
    tss_logtext_Print(LOGTEXT_VERBOSITY_3 | LOGTEXT_EVENT_DDI_NAND_GROUP, "  GpmiDelayFraction = %d\n", u32GpmiDelayFraction);
    tss_logtext_Flush(0);
    tss_logtext_Print(LOGTEXT_VERBOSITY_3 | LOGTEXT_EVENT_DDI_NAND_GROUP, "  tEYE = %d ns\n", i32tEYE);
    tss_logtext_Flush(0);
    tss_logtext_Print(LOGTEXT_VERBOSITY_3 | LOGTEXT_EVENT_DDI_NAND_GROUP, "  DelayTime = %d ns\n", i32DelayTime_ns);
    tss_logtext_Flush(0);
    tss_logtext_Print(LOGTEXT_VERBOSITY_3 | LOGTEXT_EVENT_DDI_NAND_GROUP, "  GpmiMaxDelay = %d ns\n", u32GpmiMaxDelay_ns);
    tss_logtext_Flush(0);
    tss_logtext_Print(LOGTEXT_VERBOSITY_3 | LOGTEXT_EVENT_DDI_NAND_GROUP, "  DataSetupCycles = %d cycles\n", u32DataSetupCycles);
    tss_logtext_Flush(0);
    tss_logtext_Print(LOGTEXT_VERBOSITY_3 | LOGTEXT_EVENT_DDI_NAND_GROUP, "  DataSetup = %d ns\n", u32DataSetup_ns);
    tss_logtext_Flush(0);

    return;    
}
#endif  //GPMI_PRINT_TIMINGS

void ddi_gpmi_init_apbh()
{
    // APBH - disable reset, enable clock
    // bring APBH out of reset
    HW_APBH_CTRL0_CLR(BM_APBH_CTRL0_SFTRST);
    // Poll until the SFTRST is truly deasserted.
    while (HW_APBH_CTRL0.B.SFTRST);

    HW_APBH_CTRL0_CLR(BM_APBH_CTRL0_CLKGATE);
    // Poll until the CLKGATE is truly deasserted.
    while (HW_APBH_CTRL0.B.CLKGATE);
}

void ddi_gpmi_init_dma_channel(int u32ChipNumber)
{
    reg32_t r32ChipDmaNumber = NAND0_APBH_CH + u32ChipNumber;

    // Reset dma channel
    BW_APBH_CTRL0_RESET_CHANNEL(0x1 << r32ChipDmaNumber);
    
    // Wait for the reset to complete
    while ( HW_APBH_CTRL0.B.RESET_CHANNEL & (0x1 << r32ChipDmaNumber) );
    
    // Clear IRQ
    HW_APBH_CTRL1_CLR(0x1 << r32ChipDmaNumber);
}

void ddi_gpmi_configure_gpmi()
{
    // Put GPMI in NAND mode, disable DEVICE reset, and make certain
    // polarity is active high, sample on GPMI clock
    HW_GPMI_CTRL1_WR(
        BF_GPMI_CTRL1_DEV_RESET(BV_GPMI_CTRL1_DEV_RESET__DISABLED) |
        BF_GPMI_CTRL1_ATA_IRQRDY_POLARITY(BV_GPMI_CTRL1_ATA_IRQRDY_POLARITY__ACTIVEHIGH) |
        BW_GPMI_CTRL1_GPMI_MODE(BV_GPMI_CTRL1_GPMI_MODE__NAND));
}


////////////////////////////////////////////////////////////////////////////////
//! \brief GPMI fuction to delay by given number of microSeconds
//!
//! Delay the given number of microSeconds
//!
//! \return none
//!
//! \internal
//! To view function details, see rom_nand_hal_gpmi.c.
////////////////////////////////////////////////////////////////////////////////
void NandDelayMicroSeconds(uint32_t delayMicroSeconds)
{
    uint32_t startTime = hw_profile_GetMicroseconds();

    // account for being inbetween counts on the timer 
    delayMicroSeconds++;

    // Make sure any changes in the future are safe for timer wrap-around...
    while ((hw_profile_GetMicroseconds() - startTime) < delayMicroSeconds)
    {
    }
}

////////////////////////////////////////////////////////////////////////////////
// See ddi_nand_gpmi.h for documentation.
////////////////////////////////////////////////////////////////////////////////
__INIT_TEXT RtStatus_t ddi_gpmi_init(bool bUse16BitData,
                                   uint32_t u32ChipNumber,
                                   bool bUseAlternateChipEnables,
                                   bool bUse1_8V_Drive,
                                   bool bEnableInternalPullups)
{
    RtStatus_t status;

    // Can't boot from NAND if GPMI block is not present
    if (!(HW_GPMI_STAT_RD() & BM_GPMI_STAT_PRESENT))
    {
        return ERROR_DDI_NAND_GPMI_NOT_PRESENT;
    }

    // Init APBH DMA controller.
    ddi_gpmi_init_apbh();
    ddi_gpmi_init_dma_channel(u32ChipNumber);
    
    // Init interrupts.
    ddi_gpmi_init_interrupts(u32ChipNumber);

    // CLKGATE = 0 and DIV = 1 (we're assuming a 24MHz XTAL for this).
    // HW_CLKCTRL_GPMICLKCTRL_WR(0x01);
    // Clock dividers are now set globally for PLL bypass in startup / setup_default_clocks()
    // The divider may also be changed by drivers (like USB) that turn on the PLL
    // HW_CLKCTRL_GPMICLKCTRL_CLR(BM_CLKCTRL_GPMICLKCTRL_CLKGATE); // ungate

    // Ungate GPMICLK. Because the gate is upstream of the divider, special
    // care must be taken to make sure the divider is set correctly. Any
    // change to HW_CLKCTRL_GPMICLKCTRL.B.DIV while the clock is gated is
    // saved to the register, but *NOT* transferred to the actual divider.
    // Clearing HW_CLKCTRL_GPMICLKCTRL.B.WAIT_PLL_LOCK serves two purposes.
    // First, it forces the divider to update because it writes the control
    // register while the clock is not gated. Second, it makes sure the update
    // completes immediately by removing the PLL locked qualifier.
    HW_CLKCTRL_GPMI.B.CLKGATE = 0;

    // Set GPMI_CLK frequency.
    status = ddi_clocks_GpmiClkInit(GPMI_CLK_PLL_ON_FREQUENCY_kHZ, GPMI_CLK_PLL_OFF_FREQUENCY_kHZ);
    if (status != SUCCESS)
    {
        return status;
    }

    // Soft-reset GPMI
    ddi_gpmi_soft_reset();
    
    // Init ECC blocks.
    if (0 == u32ChipNumber)
    {
        ddi_ecc8_init();
#if defined(STMP378x)
        ddi_bch_init();
#elif !defined(STMP37xx) && !defined(STMP377x)
	#error Must define STMP37xx, STMP377x or STMP378x
#endif
    }

    // Use the failsafe timings and default 24MHz clock
    NAND_Timing2_struct_t safeTimings;
    ddi_gpmi_get_safe_timings(&safeTimings);
    ddi_nand_hal_GpmiSetNandTiming(&safeTimings, 0, g_u32GPMIPropDelayMin_ns, g_u32GPMIPropDelayMax_ns, true /* bWriteToTheDevice */);
    
    // Set the timeout for the wait for ready mode.
    ddi_gpmi_set_busy_timeout(FLASH_BUSY_TIMEOUT);

    {
        // Convert zero-based "chip number" into the quantity of NANDs to be used on the GPMI.
        uint32_t u32NumberOfNANDs = u32ChipNumber + 1;

        // Configure all of the pads that will be used for GPMI.
        ddi_nand_hal_ConfigurePinmux(bUse16BitData, u32NumberOfNANDs,
                                     bUseAlternateChipEnables, bUse1_8V_Drive, bEnableInternalPullups);
    }

    // Put GPMI in NAND mode, disable DEVICE reset, and make certain
    // polarity is active high, sample on GPMI clock
    ddi_gpmi_configure_gpmi();

    // Only want to init the PMI interface once.
    if (!g_gpmiPmiStatus.isInited)
    {
        uint32_t returnCode;
        
        //The semaphore count is initialized to 0. The design document in wiki mentions that the 
		//count should be initialized to 1. But that is under the assumption that pre_pmi_change() will
		//get the semaphore, which is not how the code is written. The document and code will need to synchronized.
		//In the mean time, we restore the value to 0, as the DMA code starts the operation when a PMI clock change operation
		//is pending and thus causes the player to crash.
        returnCode = tx_semaphore_create(&g_gpmiPmiStatus.stallDMASemaphore, "GPMI:stall", 0);
        if (TX_SUCCESS != returnCode )
        {
            SystemHalt();
        }

        // Create the semaphore used to ACK DMAs. The initial count is 0
        // Put by ddi_gpmi_ack_pmi_event(), called by ddi_gpmi_wait_for_dma().
        // Get is done by ddi_gpmi_handle_pre_pmi_change().
        // This semaphore is not documented by the Wiki page.
        returnCode = tx_semaphore_create(&g_gpmiPmiStatus.ackSemaphore, "GPMI:ack", 0);
        if (TX_SUCCESS != returnCode )
        {
            SystemHalt();
        }
        g_gpmiPmiStatus.isInited = true;
    }

    return SUCCESS;
}

////////////////////////////////////////////////////////////////////////////////
//! \brief GPMI Init Data Sample Delay.
//!
//! This function determines the fraction of GPMI period for the data sample 
//! delay period per delay count.  The data sample delay period per cycle is a 
//! fraction of the GPMI clock.  The fraction amount is a function of chip type 
//! and GPMI clock speed.  
//!
//! \param[in]  u32GpmiPeriod_ns GPMI Clock Period in nsec.
//! \param[in]  bWriteToTheDevice   A boolean that indicates whether this function should
//!                                 actually write any values to registers.
//!
//! \return Fraction (ie amount to divide by) of the GPMI period for a delay period
//!
//! \internal
//! To view function details, see rom_nand_hal_gpmi.c.
////////////////////////////////////////////////////////////////////////////////
uint32_t rom_nand_hal_GpmiInitializeDataSampleDelay(uint32_t u32GpmiPeriod_ns, bool bWriteToTheDevice)
{
    uint32_t retVal = GPMI_DELAY_SHIFT;

#if defined(STMP37xx) || defined(STMP377x)
    // do nothing
#elif defined(STMP378x)
    
    if ( bWriteToTheDevice )
    {
        BW_GPMI_CTRL1_DLL_ENABLE(0);                        // Init to a known value
        BW_GPMI_CTRL1_RDN_DELAY(0);                         // Init to a known value
    }

    if (u32GpmiPeriod_ns > GPMI_DLL_HALF_THRESHOLD_PERIOD_NS )
    {
        // the GPMI clock period is high enough that the DLL
        // requires a divide by two
        if ( bWriteToTheDevice )
        {
            BW_GPMI_CTRL1_HALF_PERIOD(1);
        }
        retVal++;                                       // Account for the half period, add 1 to shift (or / by 2)
    }

#else
	#error Must define STMP37xx, STMP377x or STMP378x
#endif

    return (1 << retVal);
}



////////////////////////////////////////////////////////////////////////////////
//! \brief GPMI Set And Start Data Sample Delay.
//!
//! This function sets the NAND Timing register which controls the delay 
//! in the data read sampling.  It then set the delay hardware to be active
//! (if needed by hardware)
//!
//! \param[in]  u32DelayCycles Delay count to put to register
//!
//! \return None
//!
//! \internal
//! To view function details, see rom_nand_hal_gpmi.c.
////////////////////////////////////////////////////////////////////////////////
void rom_nand_hal_GpmiSetAndEnableDataSampleDelay(uint32_t u32DelayCycles, uint32_t u32GpmiPeriod_ns)
{

#if defined(STMP37xx) || defined(STMP377x)
    BW_GPMI_CTRL1_DSAMPLE_TIME(u32DelayCycles);
#elif defined(STMP378x)
    
    // !!! DLL_ENABLE must be set to zero when setting RDN_DELAY or HALF_PERIOD!!!
    BW_GPMI_CTRL1_DLL_ENABLE(0);

    if ((u32DelayCycles == 0) || (u32GpmiPeriod_ns > GPMI_MAX_DLL_PERIOD_NS) )
    {
        // If no delay is desired, or if GPMI clock period is out of supported 
        // range, then don't enable the delay

        //BW_GPMI_CTRL1_DLL_ENABLE(0);    // This is already done several lines up
        BW_GPMI_CTRL1_RDN_DELAY(0);
        BW_GPMI_CTRL1_HALF_PERIOD(0);
    }
    else
    {
        // Set the delay and needed registers to run.  GPMI_CTRL1_HALF_PERIOD is 
        // assumed to have already been set properly

        uint32_t waitTimeNeeded;

        BW_GPMI_CTRL1_RDN_DELAY(u32DelayCycles);
        BW_GPMI_CTRL1_DLL_ENABLE(1);

        // After the GPMI DLL has been enable it is reqiured to wait for 
        // GPMI_WAIT_CYCLES_AFTER_DLL_ENABLE number of GPMI clock cycles before 
        // using the GPMI interface.

        // calculate out the wait time and convert from nanoSeconds to microSeconds
        waitTimeNeeded = (u32GpmiPeriod_ns * GPMI_WAIT_CYCLES_AFTER_DLL_ENABLE) / 1000;

        // wait until the required time for DLL hardware startup has passed.
        NandDelayMicroSeconds(waitTimeNeeded);

    }

#else
	#error Must define STMP37xx, STMP377x or STMP378x
#endif

}


////////////////////////////////////////////////////////////////////////////////
//! \brief Find NAND Timing.
//!
//! This function determines the NAND Timing parameter which is given in
//! units of GPMI cycles.  The GPMI period is used to determine how many
//! cycles fit into the NAND parameter.
//!
//! \param[in]  u32NandTime_ns      The quantity in nsec to be converted to units of
//!                                 GPMI cycles.
//! \param[in]  u32GpmiPeriod_ns    GPMI Clock Period in nsec.
//!
//! \return Number of GPMI cycles required for this time.
//!
//! \internal
//! To view function details, see rom_nand_hal_gpmi.c.
////////////////////////////////////////////////////////////////////////////////
uint32_t rom_nand_hal_FindGpmiCyclesCeiling(uint64_t u32NandTime_ns, uint32_t u32GpmiPeriod_ns, uint32_t u32MinVal)
{
    uint32_t retVal = ((u32NandTime_ns + (u32GpmiPeriod_ns - 1)) / u32GpmiPeriod_ns);
    return std::max(retVal, u32MinVal);
}

////////////////////////////////////////////////////////////////////////////////
//! \brief Find a NAND timing value that is rounded as appropriate.
//!
//! This function determines the NAND Timing parameter which is given in
//! units of GPMI cycles.  The GPMI period is used to determine how many
//! cycles fit into the NAND parameter, rounding up or down as appropriate.
//!
//! \param[in]  u32NandTime_ns      The quantity in nsec to be converted to units of
//!                                 GPMI cycles.
//! \param[in]  u32GpmiPeriod_ns    GPMI Clock Period in nsec.
//!
//! \return Number of GPMI cycles required for this time.
//!
//! \internal
//! To view function details, see rom_nand_hal_gpmi.c.
////////////////////////////////////////////////////////////////////////////////
uint32_t rom_nand_hal_FindGpmiCyclesRounded(uint64_t u32NandTime_ns, uint32_t u32GpmiPeriod_ns)
{
    return (u32NandTime_ns + (u32GpmiPeriod_ns>>1)) / u32GpmiPeriod_ns;
}

#ifdef __cplusplus
extern "C" {
#endif
////////////////////////////////////////////////////////////////////////////////
//! \brief Compute and setup the NAND clocks.
//!
//! This function sets the GPMI NAND timing based upon the desired NAND timings that
//! are passed in. If the GPMI clock period is non-zero it is used in the
//! calculation of the new register values. If zero is passed instead, the
//! current GPMI_CLK frequency is obtained and used to calculate the period
//! in nanoseconds.
//!
//! \param[in]  pNewNANDTiming  Pointer to a nand-timing Structure with Address Setup, Data Setup and Hold, etc.
//!                             This structure must be one of those that contains an eState element,
//!                             so this function can tell how to crack it and process it.
//! \param[in]  u32GpmiPeriod_ns GPMI Clock Period in nsec. May be zero, in
//!                             which case the actual current GPMI_CLK period is used.
//! \param[in] u32PropDelayMin_ns Minimum propagation delay in nanoseconds.
//! \param[in] u32PropDelayMax_ns Maximum propagation delay in nanoseconds.
//! \param[in]  bWriteToTheDevice   A boolean that indicates whether this function should
//!                                 actually write any values to registers.
//!
////////////////////////////////////////////////////////////////////////////////
void ddi_nand_hal_GpmiSetNandTiming(const NAND_Timing2_struct_t * pNAND_Timing2_struct, 
                                    uint32_t u32GpmiPeriod_ns, 
                                    uint32_t u32PropDelayMin_ns, 
                                    uint32_t u32PropDelayMax_ns,
                                    bool bWriteToTheDevice)
{
    uint32_t u32GpmiDelayFraction;
    uint32_t u32GpmiMaxDelay_ns;
    uint32_t u32AddressSetupCycles;
    uint32_t u32DataSetupCycles;
    uint32_t u32DataHoldCycles;
    uint32_t u32DataSampleDelayCycles;
    uint32_t u32DataSetup_ns;
    int32_t  i32tEYE;
    int32_t  i32DelayTime_ns;
    #if GPMI_PRINT_TIMINGS
    char     bPrintInterimTimings = false;
    #endif

    if ( NULL == pNAND_Timing2_struct ) return;

    // If u32GpmiPeriod is passed in as 0, we get the current GPMI_CLK frequency
    // and compute the period in ns.
    if (u32GpmiPeriod_ns == 0)
    {
        uint32_t freq_kHz = ddi_clocks_GetGpmiClkInit();
        u32GpmiPeriod_ns = 1000000 / freq_kHz;
    }

    u32GpmiDelayFraction = rom_nand_hal_GpmiInitializeDataSampleDelay(u32GpmiPeriod_ns, bWriteToTheDevice);

    u32GpmiMaxDelay_ns = GPMI_GET_MAX_DELAY_NS(u32GpmiPeriod_ns);


#if GPMI_PRINT_TIMINGS
    tss_logtext_Print(LOGTEXT_VERBOSITY_3 | LOGTEXT_EVENT_DDI_NAND_GROUP, "NAND GPMI timings:\n");
    tss_logtext_Flush(0);
#endif

    /* *******************************************************************
        Process the given AddressSetup, DataSetup, and DataHold
        parameters
    ******************************************************************* */

    // The chip hardware quantizes the setup and hold parameters to intervals of
    // the GPMI clock period.
    // Quantize the setup and hold parameters to the next-highest GPMI clock period to
    // make sure we use at least the requested times.
    //
    // For DataSetup and DataHold, the chip interprets a value of zero as the largest
    // amount of delay supported.  This is not what's intended by a zero
    // in the input parameter, so we modify the zero input parameter to
    // the smallest supported value.

    u32AddressSetupCycles = rom_nand_hal_FindGpmiCyclesCeiling(pNAND_Timing2_struct->u8AddressSetup, u32GpmiPeriod_ns, 0);
    u32DataSetupCycles    = rom_nand_hal_FindGpmiCyclesCeiling(pNAND_Timing2_struct->u8DataSetup, u32GpmiPeriod_ns, 1);
    u32DataHoldCycles     = rom_nand_hal_FindGpmiCyclesCeiling(pNAND_Timing2_struct->u8DataHold, u32GpmiPeriod_ns, 1);


    switch ( pNAND_Timing2_struct->eState )
    {
        case e_NAND_Timing_State_STATIC_DSAMPLE_TIME:
        {
            // Get delay time and include required chip read setup time
            i32DelayTime_ns = pNAND_Timing2_struct->u8DSAMPLE_TIME + GPMI_DATA_SETUP_NS;

            // Extend the Data Setup time as needed to reduce delay time below 
            // the max supported by hardware.  Also keep DataSetup in allowable range
            while ((i32DelayTime_ns > u32GpmiMaxDelay_ns) && (u32DataSetupCycles  < MAX_DATA_SETUP_CYCLES))
            {
                u32DataSetupCycles++;
                i32DelayTime_ns -= u32GpmiPeriod_ns;
                if (i32DelayTime_ns < 0)
                {
                    i32DelayTime_ns = 0;
                }
            }
            
            u32DataSampleDelayCycles = std::min( rom_nand_hal_FindGpmiCyclesCeiling( (u32GpmiDelayFraction * i32DelayTime_ns), u32GpmiPeriod_ns, 0), MAX_DATA_SAMPLE_DELAY_CYCLES);

#if GPMI_PRINT_TIMINGS
            tss_logtext_Print(LOGTEXT_VERBOSITY_3 | LOGTEXT_EVENT_DDI_NAND_GROUP, "(--static--)\n");
            tss_logtext_Flush(0);
#endif
            break;
        } // case e_NAND_Timing_State_STATIC_DSAMPLE_TIME:

        case e_NAND_Timing_State_DYNAMIC_DSAMPLE_TIME:
        {


            // Compute the times associated with the quantized number of GPMI cycles.
            u32DataSetup_ns = u32GpmiPeriod_ns * u32DataSetupCycles;

            // This accounts for chip specific GPMI read setup time on the data sample 
            // circuit.  See 378x datasheet "14.3.4. High-Speed NAND Timing"
            u32PropDelayMax_ns += GPMI_DATA_SETUP_NS;

         /* *******************************************************************
                Compute tEYE, the width of the data eye when reading from the NAND.
            ******************************************************************* */

            // Note that we use the quantized versions of setup and hold, because the chip
            // uses these quantized values, and these timings create the eye.
            //
            // end of the eye = u32PropDelayMin_ns + pNAND_Timing2_struct->u8RHOH + u32DataSetup_ns
            // start of the eye = u32PropDelayMax_ns + pNAND_Timing2_struct->u8REA
            i32tEYE = ( (int)u32PropDelayMin_ns + (int)pNAND_Timing2_struct->u8RHOH + (int)u32DataSetup_ns ) - ( (int)u32PropDelayMax_ns + (int)pNAND_Timing2_struct->u8REA );


            // The eye has to be open.  Constrain tEYE to be greater than zero
            // and the number of DataSetup cycles to fit in the timing register.
            while ( (i32tEYE <= 0) && (u32DataSetupCycles  < MAX_DATA_SETUP_CYCLES) )
            {
                // The eye is not open.  An increase in data-setup time 
                // causes a coresponding increase to size of the eye.
                u32DataSetupCycles++;                               // Give an additional DataSetup cycle
                u32DataSetup_ns += u32GpmiPeriod_ns;                // Keep DataSetup time in step with cycles
                i32tEYE += u32GpmiPeriod_ns;                        // And adjust the tEYE accordingly

            } // while ( i32tEYE


         /* *******************************************************************
                Compute the ideal point at which to sample the data
                at the center of tEYE.
            ******************************************************************* */

            // Find the delay to get the center in time-units.
            // Delay for center of the eye = ((end of the eye + start of the eye) / 2) - DataSetup
            // This simplifies to the following:
            i32DelayTime_ns = ( (int)u32PropDelayMax_ns + (int)pNAND_Timing2_struct->u8REA + (int)u32PropDelayMin_ns + (int)pNAND_Timing2_struct->u8RHOH - (int)u32DataSetup_ns ) >> 1;

            // The chip can't accomodate a negative parameter for the sample point.
            if ( i32DelayTime_ns < 0 ) i32DelayTime_ns = 0;

            //  Make sure the required DelayTime does not exceed the max allowed value.
            //  Also make sure the quantized DelayTime (at u32DataSampleDelayCycles) is 
            //  within the eye.  
            //
            //  Increasing DataSetup decreases the length of DelayTime 
            //  required to get to into the eye.  Increasing DataSetup also moves the rear 
            //  of the eye back, enlarges the eye (helpful in the case where quantized 
            //  DelayTime does not fall inside the initial eye).
            //          
            //          ____                   __________________________________________
            //  RDN         \_________________/
            //
            //                                               <----- tEYE ---->
            //                                             /--------------------\
            //  Read Data --------------------------------<                      >-------
            //                                             \--------------------/
            //              ^                 ^                      ^   tEYE/2     ^
            //              |                 |                      |              |
            //              |<---DataSetup--->|<-----DelayTime------>|              |
            //              |                 |                      |              |
            //              |                 |                                     |
            //              |                 |<------quantized DelayTime---------->|
            //              |                 |                                     |
            //                                      

            #if GPMI_PRINT_TIMINGS
            tss_logtext_Print(LOGTEXT_VERBOSITY_3 | LOGTEXT_EVENT_DDI_NAND_GROUP, "(--dynamic--)(--Start--)\n");
            _print_dynamic_timing_summary(
                u32GpmiPeriod_ns,
                u32GpmiDelayFraction,
                i32tEYE,
                i32DelayTime_ns,
                u32GpmiMaxDelay_ns,
                u32DataSetupCycles,
                u32DataSetup_ns);

            #endif
            // Extend the Data Setup time as needed to reduce delay time below 
            // the max allowable value.  Also keep DataSetup in allowable range
            while ((i32DelayTime_ns > u32GpmiMaxDelay_ns) && (u32DataSetupCycles  < MAX_DATA_SETUP_CYCLES))
            {
                #if GPMI_PRINT_TIMINGS
                if ( !bPrintInterimTimings )
                {
                    // Print an explanation once now...
                    tss_logtext_Print(LOGTEXT_VERBOSITY_3 | LOGTEXT_EVENT_DDI_NAND_GROUP, "(DelayTime > GPMI max %d) and DataSetupCycles < max %d. Adjusting DelayTime.\n",u32GpmiMaxDelay_ns,MAX_DATA_SETUP_CYCLES );
                    tss_logtext_Flush(0);
                    // ...and print an interim list of timings afterward.
                    bPrintInterimTimings = true;
                }
                #endif
                u32DataSetupCycles++;                               // Give an additional DataSetup cycle
                u32DataSetup_ns += u32GpmiPeriod_ns;                // Keep DataSetup time in step with cycles
                i32tEYE += u32GpmiPeriod_ns;                        // And adjust the tEYE accordingly
                i32DelayTime_ns -= (u32GpmiPeriod_ns >> 1);         // decrease DelayTime by one half DataSetup cycle worth, to keep in the middle of the eye
                if (i32DelayTime_ns < 0)
                {
                    i32DelayTime_ns = 0;                            // Do not allow DelayTime less than zero
                }
            }

            // The DelayTime parameter is expressed in the chip in units of fractions of GPMI clocks.
            // Convert DelayTime to an integer quantity of fractional GPMI cycles..
            u32DataSampleDelayCycles = std::min( rom_nand_hal_FindGpmiCyclesCeiling( (u32GpmiDelayFraction * i32DelayTime_ns), u32GpmiPeriod_ns, 0), MAX_DATA_SAMPLE_DELAY_CYCLES);

            #if GPMI_PRINT_TIMINGS
            if ( bPrintInterimTimings )
            {
                _print_dynamic_timing_summary(
                    u32GpmiPeriod_ns,
                    u32GpmiDelayFraction,
                    i32tEYE,
                    i32DelayTime_ns,
                    u32GpmiMaxDelay_ns,
                    u32DataSetupCycles,
                    u32DataSetup_ns);
                bPrintInterimTimings = false;
            }

            #endif
            #define DSAMPLE_IS_NOT_WITHIN_THE_DATA_EYE  ( i32tEYE>>1 < std::abs( (int32_t)((u32DataSampleDelayCycles * u32GpmiPeriod_ns) / u32GpmiDelayFraction) - i32DelayTime_ns ))

            // While the quantized DelayTime is out of the eye reduce the DelayTime or extend 
            // the DataSetup to get in the eye.  Do not allow the number of DataSetup cycles to 
            // exceed the max supported by hardware.
            while ( DSAMPLE_IS_NOT_WITHIN_THE_DATA_EYE
                    && (u32DataSetupCycles  < MAX_DATA_SETUP_CYCLES) )
            {
                #if GPMI_PRINT_TIMINGS
                if ( !bPrintInterimTimings )
                {
                    // Print an explanation once now.
                    tss_logtext_Print(LOGTEXT_VERBOSITY_3 | LOGTEXT_EVENT_DDI_NAND_GROUP, "Data sample point not within data eye.  Adjusting.\n" );
                    bPrintInterimTimings = true;
                }
                #endif
                if ( ((u32DataSampleDelayCycles * u32GpmiPeriod_ns) / u32GpmiDelayFraction) > i32DelayTime_ns )
                {
                    // If quantized DelayTime is greater than max reach of the eye decrease quantized 
                    // DelayTime to get it into the eye or before the eye

                    if (u32DataSampleDelayCycles != 0)
                    {
                        u32DataSampleDelayCycles--;
                    }
                }
                else
                {
                    // If quantized DelayTime is less than min reach of the eye, shift up the sample 
                    // point by increasing DataSetup.  This will also open the eye (helping get 
                    // quantized DelayTime in the eye)
                    u32DataSetupCycles++;                           // Give an additional DataSetup cycle
                    u32DataSetup_ns += u32GpmiPeriod_ns;            // Keep DataSetup time in step with cycles
                    i32tEYE         += u32GpmiPeriod_ns;            // And adjust the tEYE accordingly
                    i32DelayTime_ns -= (u32GpmiPeriod_ns >> 1);     // decrease DelayTime by one half DataSetup cycle worth, to keep in the middle of the eye
                    i32DelayTime_ns -= u32GpmiPeriod_ns;            // ...and one less period for DelayTime.

                    if ( i32DelayTime_ns < 0 ) i32DelayTime_ns = 0; // keep DelayTime from going negative

                    // Convert time to GPMI cycles and make sure the number of 
                    // cycles fit in the coresponding hardware register...
                    u32DataSampleDelayCycles = std::min( rom_nand_hal_FindGpmiCyclesCeiling( (u32GpmiDelayFraction * i32DelayTime_ns), u32GpmiPeriod_ns, 0), MAX_DATA_SAMPLE_DELAY_CYCLES);
                }

            }   // while ( DSAMPLE_IS_NOT_WITHIN_THE_DATA_EYE )

#if GPMI_PRINT_TIMINGS
            tss_logtext_Print(LOGTEXT_VERBOSITY_3 | LOGTEXT_EVENT_DDI_NAND_GROUP, "(--Final--)\n");
            _print_dynamic_timing_summary(
                u32GpmiPeriod_ns,
                u32GpmiDelayFraction,
                i32tEYE,
                i32DelayTime_ns,
                u32GpmiMaxDelay_ns,
                u32DataSetupCycles,
                u32DataSetup_ns);
//            NandDelayMicroSeconds(GPMI_PRINT_DELAY);
#endif

            break;
        } //case e_NAND_Timing_State_DYNAMIC_DSAMPLE_TIME:

        default:
#if GPMI_PRINT_TIMINGS
            tss_logtext_Print(LOGTEXT_VERBOSITY_3 | LOGTEXT_EVENT_DDI_NAND_GROUP, "(--unchanged--)\n");
            tss_logtext_Flush(0);
#endif
            return;
    } // switch ( pNAND_Timing2_struct->eState )

#if GPMI_PRINT_TIMINGS
    tss_logtext_Print(LOGTEXT_VERBOSITY_3 | LOGTEXT_EVENT_DDI_NAND_GROUP, "GPMI (tDS, tDH, tAS, DelayT) = (%d, %d, %d, %d) ns\n",
                    u32GpmiPeriod_ns * u32DataSetupCycles,
                    u32GpmiPeriod_ns * u32DataHoldCycles,
                    u32GpmiPeriod_ns * u32AddressSetupCycles,
                    ((u32GpmiPeriod_ns * u32DataSampleDelayCycles) / u32GpmiDelayFraction) );
    tss_logtext_Flush(0);
//    NandDelayMicroSeconds(GPMI_PRINT_DELAY);

    tss_logtext_Print(LOGTEXT_VERBOSITY_3 | LOGTEXT_EVENT_DDI_NAND_GROUP, "(DataSetup, DataHold, AddressSetup, DelayTime) = (%d, %d, %d, %d) Count\n",
                    u32DataSetupCycles,
                    u32DataHoldCycles,
                    u32AddressSetupCycles,
                    u32DataSampleDelayCycles );

    tss_logtext_Flush(0);
//    NandDelayMicroSeconds(GPMI_PRINT_DELAY);
#endif

    if ( bWriteToTheDevice )
    {
        // Set the values in the registers.
        HW_GPMI_TIMING0_WR(NAND_GPMI_TIMING0(u32AddressSetupCycles, u32DataSetupCycles, u32DataHoldCycles));

        rom_nand_hal_GpmiSetAndEnableDataSampleDelay(u32DataSampleDelayCycles, u32GpmiPeriod_ns);
    }

    return;
}
#ifdef __cplusplus
} // extern "C"
#endif

////////////////////////////////////////////////////////////////////////////////
// See ddi_nand_gpmi.h for documentation.
////////////////////////////////////////////////////////////////////////////////
void ddi_gpmi_set_busy_timeout(uint32_t busyTimeout)
{
    // Get current GPMI_CLK period in nanoseconds.
    uint32_t freq_kHz = ddi_clocks_GetGpmiClkInit();
    uint32_t u32GpmiPeriod_ns = 1000000 / freq_kHz;
    
    // Convert from microseconds to nanoseconds.
    uint64_t busyTimeout_ns = busyTimeout * 1000;
    
    // Divide the number of GPMI_CLK cycles for the timeout by 4096 as the
    // timeout register expects.
    uint32_t busyTimeout_gpmiclk = rom_nand_hal_FindGpmiCyclesCeiling(busyTimeout_ns, u32GpmiPeriod_ns) / 4096;
    
    // The busy timeout field is only 16 bits, so make sure the desired timeout will fit.
    if ((busyTimeout_gpmiclk & 0xffff0000) != 0)
    {
        // Set the timeout to the maximum value.
        busyTimeout_gpmiclk = 0xffff;
    }
    
    HW_GPMI_TIMING1_WR(BF_GPMI_TIMING1_DEVICE_BUSY_TIMEOUT(busyTimeout_gpmiclk));
}

////////////////////////////////////////////////////////////////////////////////
//! \brief Setup the Pinmux and Pad pins for the NAND.
//!
//! This function configures the pads and pinmux to support the NAND.
//!
//! \param[in]  bUse16BitData 0 for 8 bit, 1 for 16 bit NAND support.
//! \param[in]  u32NumberOfNANDs  Indicates how many chip selects need to be used.
//! \param[in]  bUseAlternateChipEnables If TRUE, use the Alternate Chip Enables.
//! \param[in]  bUse1_8V_Drive If TRUE, drive GPMI pins at 1.8V instead of 3.3V.
//! \param[in]  bEnableInternalPullups If TRUE, will enable internal pullups.
//!
//! \return void
//!
//! \internal
////////////////////////////////////////////////////////////////////////////////
__INIT_TEXT void ddi_nand_hal_ConfigurePinmux(bool bUse16BitData,
                                  uint32_t u32NumberOfNANDs,
                                  bool bUseAlternateChipEnables,
                                  bool bUse1_8V_Drive,
                                  bool bEnableInternalPullups)
{
	// Enable the pin control muxing hardware.
	hw_pinmux_enable();

	// Setup the GMPI data lines
	hw_pinmux_setup_gpmi_data(bUse16BitData);

	// Setup the control lines
	hw_pinmux_setup_gpmi_ctrl(u32NumberOfNANDs, bUseAlternateChipEnables);

	// Setup the Drive strengh for the GPMI pins
	hw_pinmux_setup_gpmi_drive(u32NumberOfNANDs, bUse16BitData, bUse1_8V_Drive, bUseAlternateChipEnables);

    if (bEnableInternalPullups)
	{
		hw_pinmux_setup_gpmi_pullups(u32NumberOfNANDs, bUseAlternateChipEnables);
	}
}

////////////////////////////////////////////////////////////////////////////////
// See ddi_nand_gpmi.h for documentation.
////////////////////////////////////////////////////////////////////////////////
void ddi_gpmi_disable()
{
    // Gate clocks to GPMI.
    BW_GPMI_CTRL0_CLKGATE(1);
    
    // Disable ECC8 as well.
    ddi_ecc8_disable();

#if defined(STMP378x)
    // Disable BCH as well.
    ddi_bch_disable();
#endif
}

////////////////////////////////////////////////////////////////////////////////
// See ddi_nand_gpmi.h for documentation.
////////////////////////////////////////////////////////////////////////////////
void ddi_gpmi_enable_writes(bool bClearOrSet)
{
    BW_GPMI_CTRL1_DEV_RESET(bClearOrSet);
}

////////////////////////////////////////////////////////////////////////////////
// See ddi_nand_gpmi.h for documentation.
////////////////////////////////////////////////////////////////////////////////
__INIT_TEXT void ddi_gpmi_soft_reset(void)
{
    int64_t musecs;

    // Reset the GPMI_CTRL0 block.
    // Prepare for soft-reset by making sure that SFTRST is not currently
    // asserted.  Also clear CLKGATE so we can wait for its assertion below.
    HW_GPMI_CTRL0_CLR(BM_GPMI_CTRL0_SFTRST);

    // Wait at least a microsecond for SFTRST to deassert.
    musecs = hw_profile_GetMicroseconds();
    while (HW_GPMI_CTRL0.B.SFTRST || (hw_profile_GetMicroseconds() - musecs < DDI_NAND_HAL_GPMI_SOFT_RESET_LATENCY));

    // Also clear CLKGATE so we can wait for its assertion below.
    HW_GPMI_CTRL0_CLR(BM_GPMI_CTRL0_CLKGATE);

    // Now soft-reset the hardware.
    HW_GPMI_CTRL0_SET(BM_GPMI_CTRL0_SFTRST);

    // Poll until clock is in the gated state before subsequently
    // clearing soft reset and clock gate.
    while (!HW_GPMI_CTRL0.B.CLKGATE)
    {
        ; // busy wait
    }

    // bring GPMI_CTRL0 out of reset
    HW_GPMI_CTRL0_CLR(BM_GPMI_CTRL0_SFTRST);

    // Wait at least a microsecond for SFTRST to deassert. In actuality, we
    // need to wait 3 GPMI clocks, but this is much easier to implement.
    musecs = hw_profile_GetMicroseconds();
    while (HW_GPMI_CTRL0.B.SFTRST || (hw_profile_GetMicroseconds() - musecs < DDI_NAND_HAL_GPMI_SOFT_RESET_LATENCY));

    HW_GPMI_CTRL0_CLR(BM_GPMI_CTRL0_CLKGATE);

    // Poll until clock is in the NON-gated state before returning.
    while (HW_GPMI_CTRL0.B.CLKGATE)
    {
        ; // busy wait
    }
}

////////////////////////////////////////////////////////////////////////////////
// See ddi_nand_gpmi.h for documentation.
////////////////////////////////////////////////////////////////////////////////
__STATIC_TEXT RtStatus_t ddi_gpmi_set_timings( NAND_Timing2_struct_t const * pNT, bool bWriteToTheDevice )
{
    // Handle a NULL pNT (means clock-change only, so use old pNT)
    if ( (pNT != NULL) && (bWriteToTheDevice) )
    {
        // We have a new set of timings at the pNT parameter.
        // Copy the new timing-table into the static table. (structure-copy)
        g_zNandTiming = *pNT;
    }

    // Pass 0 for period to use 24MHz default.
    ddi_nand_hal_GpmiSetNandTiming(pNT, 0, g_u32GPMIPropDelayMin_ns, g_u32GPMIPropDelayMax_ns, bWriteToTheDevice);

    return SUCCESS;
}

////////////////////////////////////////////////////////////////////////////////
// See ddi_nand_gpmi.h for documentation.
////////////////////////////////////////////////////////////////////////////////
void ddi_gpmi_get_safe_timings(NAND_Timing2_struct_t * timings)
{
    const NAND_Timing2_struct_t safeTimings = NAND_SAFESTARTUP_TIMINGS;
    *timings = safeTimings;
}

////////////////////////////////////////////////////////////////////////////////
// See ddi_nand_gpmi.h for documentation.
////////////////////////////////////////////////////////////////////////////////
const NAND_Timing2_struct_t * ddi_gpmi_get_current_timings()
{
    return &g_zNandTiming;
}

////////////////////////////////////////////////////////////////////////////////
// See ddi_nand_gpmi.h for documentation.
////////////////////////////////////////////////////////////////////////////////
void ddi_gpmi_get_propagation_delay(uint32_t * minDelay, uint32_t * maxDelay)
{
    *minDelay = g_u32GPMIPropDelayMin_ns;
    *maxDelay = g_u32GPMIPropDelayMax_ns;
}
    
////////////////////////////////////////////////////////////////////////////////
// See ddi_nand_gpmi.h for documentation.
////////////////////////////////////////////////////////////////////////////////
void ddi_gpmi_set_propagation_delay(uint32_t minDelay, uint32_t maxDelay)
{
    g_u32GPMIPropDelayMin_ns = minDelay;
    g_u32GPMIPropDelayMax_ns = maxDelay;
    
    // Adjust timings for new propagation delay.
    ddi_gpmi_set_timings(NULL, true /* bWriteToTheDevice */ );
}

////////////////////////////////////////////////////////////////////////////////
// See ddi_nand_gpmi.h for documentation.
////////////////////////////////////////////////////////////////////////////////
void ddi_gpmi_set_most_relaxed_timings(NAND_Timing2_struct_t * prev, NAND_Timing2_struct_t const * curr)
{
    if (curr->u8AddressSetup > prev->u8AddressSetup)
    {
        prev->u8AddressSetup = curr->u8AddressSetup;
    }
        
    if (curr->u8DSAMPLE_TIME > prev->u8DSAMPLE_TIME)
    {
        prev->u8DSAMPLE_TIME = curr->u8DSAMPLE_TIME;
    }
    
    if (curr->u8DataSetup > prev->u8DataSetup)
    {
        prev->u8DataSetup = curr->u8DataSetup;
    }
    
    if (curr->u8DataHold > prev->u8DataHold)
    {
        prev->u8DataHold = curr->u8DataHold;
    }
    
    if (curr->u8REA > prev->u8REA)
    {
        prev->u8REA = curr->u8REA;
    }
    
    if (curr->u8RLOH > prev->u8RLOH)
    {
        prev->u8RLOH = curr->u8RLOH;
    }
    
    if (curr->u8RHOH > prev->u8RHOH)
    {
        prev->u8RHOH = curr->u8RHOH;
    }
}

////////////////////////////////////////////////////////////////////////////////
// See ddi_nand_gpmi.h for documentation.
////////////////////////////////////////////////////////////////////////////////
void ddi_gpmi_relax_timings_by_amount(NAND_Timing2_struct_t *pTimings, uint32_t increment)
{
    uint32_t temp;
    
    // Adjust TSU
    temp = pTimings->u8AddressSetup;
    temp += increment;
    
    temp &= 0xff;
    
    pTimings->u8AddressSetup = temp;
    
    // Adjust TDS
    temp = pTimings->u8DataSetup;
    temp += increment;
    
    temp &= 0xff;
    
    pTimings->u8DataSetup = temp;
    
    // Adjust TDH
    temp = pTimings->u8DataHold;
    temp += increment;
    
    temp &= 0xff;
    
    pTimings->u8DataHold = temp;
}

////////////////////////////////////////////////////////////////////////////////
// See ddi_nand_gpmi.h for documentation.
////////////////////////////////////////////////////////////////////////////////
RtStatus_t ddi_gpmi_wait_for_ready(unsigned chipSelect, uint32_t timeout)
{
    RtStatus_t status = SUCCESS;

    // Return an error if the GPMI peripheral is already in use.
    if (HW_GPMI_CTRL0.B.RUN)
    {
        return ERROR_DDI_NAND_GPMI_DMA_BUSY;
    }
    
    // Compute the mask to use based on the chip select.
    uint32_t mask = BM_GPMI_DEBUG_READY0 << chipSelect; //BM_GPMI_DEBUG_WAIT_FOR_READY_END0
    
    // Read the current wait for ready end register field so we can tell when it toggles.
//    uint32_t currentWaitForReadEnd = HW_GPMI_DEBUG_RD() & mask;
    
    // Save original timeout and set the timeout to the max.
    uint16_t saveTimeout = HW_GPMI_TIMING1.B.DEVICE_BUSY_TIMEOUT;
    HW_GPMI_TIMING1_WR(BF_GPMI_TIMING1_DEVICE_BUSY_TIMEOUT(0xffff));
    
    // Set the chip select.
    assert((chipSelect & ~3) == 0);
    HW_GPMI_CTRL0.B.CS = chipSelect;
    
    // Switch to wait for ready mode.
    HW_GPMI_CTRL0.B.COMMAND_MODE = BV_GPMI_CTRL0_COMMAND_MODE__WAIT_FOR_READY;
    
    // Kick off the command.
    HW_GPMI_CTRL0_SET(BM_GPMI_CTRL0_RUN);
    
    // Sit back and wait.
    uint64_t startTime = hw_profile_GetMicroseconds();
    while ((HW_GPMI_DEBUG_RD() & mask) == 0)//currentWaitForReadEnd)
    {
        // Check the timeout and exit the loop if it has expired.
        uint32_t elapsed = hw_profile_GetMicroseconds() - startTime;
        if (elapsed >= timeout)
        {
            // Stop the wait for ready command since we timed out.
            HW_GPMI_CTRL1_SET(BM_GPMI_CTRL1_ABORT_WAIT_FOR_READY0 << chipSelect);
            
            status = ERROR_DDI_NAND_GPMI_DMA_TIMEOUT;
            break;
        }
    }
    
    // Restore the original timeout value.
    HW_GPMI_TIMING1_WR(BF_GPMI_TIMING1_DEVICE_BUSY_TIMEOUT(saveTimeout));
    
    return status;
}

////////////////////////////////////////////////////////////////////////////////
// End of file
////////////////////////////////////////////////////////////////////////////////
//! @}
