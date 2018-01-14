///////////////////////////////////////////////////////////////////////////////
// Copyright (c) SigmaTel, Inc. All rights reserved.
// 
// SigmaTel, Inc.
// Proprietary & Confidential
// 
// This source code and the algorithms implemented therein constitute
// confidential information and may comprise trade secrets of SigmaTel, Inc.
// or its associates, and any use thereof is subject to the terms and
// conditions of the Confidential Disclosure Agreement pursual to which this
// source code was originally received.
///////////////////////////////////////////////////////////////////////////////
//! \addtogroup ddi_media_nand_hal_gpmi_internal
//! @{
//! \file ddi_nand.h
//! \brief Contains public declarations for the NAND driver.
//!
//! This file contains declarations of public interfaces to
//! the NAND driver.
//!
///////////////////////////////////////////////////////////////////////////////
#if !defined(_ddi_nand_h_)
#define _ddi_nand_h_

#include "ddi_nand_gpmi.h"
#include "ddi_nand_ecc.h"

///////////////////////////////////////////////////////////////////////////////
// Definitions
///////////////////////////////////////////////////////////////////////////////

//! \brief Timeout for waiting for PMI event completion.
//!
//! This is the maximum time DMAs will be held off while waiting for PMI
//! to send its event completion notification. The units are milliseconds.
#define PMI_WAIT_TIMEOUT (5000)

///////////////////////////////////////////////////////////////////////////////
// Types
///////////////////////////////////////////////////////////////////////////////

/*! \brief Status information for the GPMI interface to PMI.
 *
 * This structure contains everything needed to keep track of the
 * interaction between the PMI, GPMI driver, and the DMA engine.
 */
typedef struct GpmiPmiStatus
{
    //! Client ID for GPMI clock
    int8_t gpmiClientId;
    
    //! Number of concurrent outstanding PMI requests.
    volatile unsigned outstandingRequests;
    
    //! Semaphore used to stall DMAs.
    TX_SEMAPHORE stallDMASemaphore;
    
    //! Semaphore to hold the pre-change PMI notification off until an active DMA completes.
    TX_SEMAPHORE ackSemaphore;
    
    //! \name Flags
    //!
    //! All flags are grouped together in a single bitfield.
    //@{
    volatile unsigned isInited:1;  //!< Is the PMI interface initialised yet?
    volatile unsigned bInDmaFlag:1;    //!< Is a DMA currently in progress?
    volatile unsigned bStallDmaFlag:1; //!< Should the next DMA be held off?
    volatile unsigned waitingForGPMIAck:1;  //!< Need to acknowledge the GPMI_CLK pre-change event.
    volatile unsigned isDmaStalled:1;   //!< True if a DMA has been stalled.
    //@}
} GpmiPmiStatus_t;

/*!
 * \brief Structure for holding information used to start and stop DMAs to the NAND(s).
 *
 * The functions ddi_gpmi_start_dma() and ddi_gpmi_wait_for_dma() use this information
 * to manage DMAs with the NAND chips.
 */
typedef struct GpmiDmaInfo
{
    //! \brief Semaphore used for synchronization with interrupts.
    TX_SEMAPHORE  pSemaphore;

    //! \brief Index of the chip-enable for this DMA.  Range [0..number-of-CEs).
    uint16_t        u16CurrentChip;

    //! \brief A bitmask used to indicate criteria for terminating the DMA.
    //!
    //! See #_nand_gpmi_dma_wait_mask.
    uint16_t        u16DmaWaitMask;

    //! \brief A bitmask used to indicate status of the DMA.
    //!
    //! See #_nand_gpmi_dma_wait_mask.
    //! When (u16DmaWaitStatus == u16DmaWaitMask) the DMA is complete.
    uint16_t        u16DmaWaitStatus;

    uint64_t uStartDMATime; //!< Only used for non-ThreadX builds.
} GpmiDmaInfo_t;

///////////////////////////////////////////////////////////////////////////////
// Externs
///////////////////////////////////////////////////////////////////////////////

extern GpmiPmiStatus_t g_gpmiPmiStatus;
extern GpmiDmaInfo_t g_gpmiDmaInfo;

///////////////////////////////////////////////////////////////////////////////
// Prototypes
///////////////////////////////////////////////////////////////////////////////
    
void ddi_gpmi_clear_dma_isr_enable(uint16_t u16CurrentChip);
void ddi_gpmi_clear_ecc_isr_enable(void);

void ddi_gpmi_ack_pmi_event(void);
void ddi_gpmi_wait_for_pmi_event(void);

//! \name PMI callbacks
//!
//! These functions are the callbacks invoked by PMI before and after an event
//! takes place. In this case, the event in question is when PMI is going
//! to modify the GPMI_CLK clock signal.
//@{

    RtStatus_t ddi_gpmi_handle_pre_pmi_change(void);
    RtStatus_t ddi_gpmi_handle_post_pmi_change(void);

//@}

////////////////////////////////////////////////////////////////////////////////
//! \brief      Initialize the NAND Interrupts.
//!
//! Primarily for Interrupt creation.
//! initialize the Mutex.
//!
//! \param[in] u32ChipNum
//!
//! \retval     SUCCESS
////////////////////////////////////////////////////////////////////////////////
RtStatus_t ddi_gpmi_init_interrupts(uint32_t u32ChipNum);

////////////////////////////////////////////////////////////////////////////////
// End of file
////////////////////////////////////////////////////////////////////////////////
#endif // _ddi_nand_h_
//! @}



