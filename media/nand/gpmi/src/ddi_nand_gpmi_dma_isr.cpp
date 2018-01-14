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
//! \file    ddi_nand_gpmi_dma_isr.cpp
//! \brief   Routines for handling the DMA completion ISR for the NANDs.
//!
//! This file contains the NAND HAL DMA completion Interrupt Subroutine.
//!
////////////////////////////////////////////////////////////////////////////////

#include "types.h"
#include "ddi_nand_gpmi_internal.h"
#include "registers/regsapbh.h"
#include "components/profile/cmp_profile.h"
#include "drivers/ddi_subgroups.h"
#include "os/thi/os_thi_api.h"
#include "os/dmi/os_dmi_api.h"
#include "hw/icoll/hw_icoll.h"
#include "registers/hw_irq.h"
#include "drivers/icoll/ddi_icoll.h"

#ifdef DEBUG_DMA_TOUT
extern uint32_t DmaStatus1, DmaStatus2;
#endif

///////////////////////////////////////////////////////////////////////////////
// Definitions
///////////////////////////////////////////////////////////////////////////////

#define ENABLEVECTOR_BEFORE  (0)

///////////////////////////////////////////////////////////////////////////////
// Prototypes
///////////////////////////////////////////////////////////////////////////////

void ddi_nand_GpmiDmaIsrHandler(void *pParam);
void ddi_nand_Ecc8IsrHandler(void *pParam);
void ddi_nand_BchIsrHandler(void *pParam);

///////////////////////////////////////////////////////////////////////////////
// Code
///////////////////////////////////////////////////////////////////////////////

inline void ddi_gpmi_clear_dma_command_complete_irq(int chip)
{
    // Clear the command-complete IRQ for this chip...
    HW_APBH_CTRL1_CLR((BM_APBH_CTRL1_CH4_CMDCMPLT_IRQ << chip));
    
    // ...and do a dummy read-back to make sure the value has been written.
    volatile uint32_t u32Dummy = HW_APBH_CTRL1_RD();
}

inline void ddi_gpmi_set_dma_irq_enabled(int chip)
{
    HW_APBH_CTRL1_SET(BM_APBH_CTRL1_CH4_CMDCMPLT_IRQ_EN << chip);
}

inline void ddi_gpmi_clear_ecc8_ctrl_complete_irq()
{
    // Clear the ECC-complete IRQ...
    HW_ECC8_CTRL_CLR(BM_ECC8_CTRL_COMPLETE_IRQ);
    
    // ...and do a dummy read-back to make sure the value has been written.
    volatile uint32_t u32Dummy = HW_ECC8_CTRL_RD();
}

inline void ddi_gpmi_set_ecc8_irq_enabled()
{
    BF_SET(ECC8_CTRL, COMPLETE_IRQ_EN);
}

#if defined(STMP378x)
inline void ddi_gpmi_clear_bch_ctrl_complete_irq()
{
    // Clear the ECC-complete IRQ...
    HW_BCH_CTRL_CLR(BM_BCH_CTRL_COMPLETE_IRQ);
    
    // ...and do a dummy read-back to make sure the value has been written.
    volatile uint32_t u32Dummy = HW_BCH_CTRL_RD();
}

inline void ddi_gpmi_set_bch_irq_enabled()
{
    BF_SET(BCH_CTRL, COMPLETE_IRQ_EN);
}
#endif // STMP378x

RtStatus_t ddi_gpmi_init_interrupts(uint32_t u32ChipNumber)
{
    UINT retCode;

    // Only need to register the handler for the first chip
    // since the same DMA interrupt is used for all channels.
    if (u32ChipNumber == 0)
    {
        // Create the DMA complete Semaphore.
        retCode = tx_semaphore_create(&g_gpmiDmaInfo.pSemaphore, "GPMI:DMA", 0);
        if (TX_SUCCESS != retCode )
        {
            SystemHalt();
        }

#ifdef RTOS_THREADX

        // setup and enable the isr for the ECC8 IRQ
        ddi_icoll_RegisterIrqHandler(
            VECTOR_IRQ_ECC8,
            ddi_nand_Ecc8IsrHandler,
            &g_gpmiDmaInfo,
            IRQ_HANDLER_DIRECT,
            ICOLL_PRIORITY_LEVEL_0);

#if defined(STMP378x)
        // setup and enable the isr for the BCH IRQ
        ddi_icoll_RegisterIrqHandler(
            VECTOR_IRQ_BCH,
            ddi_nand_BchIsrHandler,
            &g_gpmiDmaInfo,
            IRQ_HANDLER_DIRECT,
            ICOLL_PRIORITY_LEVEL_0);
#elif !defined(STMP37xx) && !defined(STMP377x)
    #error Must define STMP37xx, STMP377x or STMP378x
#endif

        // setup and enable the isr for the GPMI DMA IRQ
        ddi_icoll_RegisterIrqHandler(
            VECTOR_IRQ_GPMI_DMA,
            ddi_nand_GpmiDmaIsrHandler,
            &g_gpmiDmaInfo,
            IRQ_HANDLER_DIRECT,
            ICOLL_PRIORITY_LEVEL_0);

#if ENABLEVECTOR_BEFORE
        hw_icoll_EnableVector(VECTOR_IRQ_GPMI_DMA, true);
        hw_icoll_EnableVector(VECTOR_IRQ_ECC8, true);
#if defined(STMP378x)
        hw_icoll_EnableVector(VECTOR_IRQ_BCH, true);
#endif
#endif
#endif // RTOS_THREADX

    }   // if (u32ChipNumber == 0)

    // clear and enable the APBH DMA IRQs
    ddi_gpmi_clear_dma_command_complete_irq(u32ChipNumber);
    ddi_gpmi_set_dma_irq_enabled(u32ChipNumber);

    // clear and enable the ECC IRQs
    ddi_gpmi_clear_ecc8_ctrl_complete_irq();
    ddi_gpmi_set_ecc8_irq_enabled();
#if defined(STMP378x)
    ddi_gpmi_clear_bch_ctrl_complete_irq();
    ddi_gpmi_set_bch_irq_enabled();
#endif

#if defined(RTOS_THREADX) && !ENABLEVECTOR_BEFORE
    hw_icoll_EnableVector(VECTOR_IRQ_GPMI_DMA, true);
    hw_icoll_EnableVector(VECTOR_IRQ_ECC8, true);
#if defined(STMP378x)
    hw_icoll_EnableVector(VECTOR_IRQ_BCH, true);
#endif
#endif // defined(RTOS_THREADX) && !ENABLEVECTOR_BEFORE

    return SUCCESS;
}

#ifdef RTOS_THREADX
////////////////////////////////////////////////////////////////////////////////
//! \brief      ISR for the GPMI dma complete IRQ.
//!
//! \fntype     Interrupt Service
//!
//! Signals GPMI dma completed by signaling the NAND semaphore.
//! This is dependent on the state of pParam->u16DmaWaitMask.
//!
//! Then clears and enables the interrupt.
//!
//! \param[in]  pParam  Pointer to GpmiDmaInfo_t.
////////////////////////////////////////////////////////////////////////////////
void ddi_nand_GpmiDmaIsrHandler(void *pParam)
{
    GpmiDmaInfo_t * pWaitStruct = (GpmiDmaInfo_t *)pParam;
#if defined(DEBUG_DMA_TOUT) && defined(ENABLE_SDRAM_DEBUG_MEM) && defined(CMP_PROFILE_ENABLE)
    {
        uint32_t    Handle;
        Handle = cmp_profile_start(DDI_NAND_GROUP, "ddi_nand_GpmiDmaIsrHandler start");
        cmp_profile_stop(Handle);
    }
#endif // defined(DEBUG_DMA_TOUT) && defined(ENABLE_SDRAM_DEBUG_MEM) && defined(CMP_PROFILE_ENABLE)

    //
    // Aside: We cannot check the SEMA.PHORE register here.
    // The DMA engine in the 37xx can trigger the ISR at the end of the DMA, before decrementing
    // the SEMA.PHORE count, thus creating a race condition that lets us find a
    // nonzero SEMA.PHORE value here.
    //

#ifdef DEBUG_DMA_TOUT
    // Sanity-check: Alert me if a DMA chain is currently running!!
    if (BF_RD(GPMI_CTRL0, RUN))
    {
        DmaStatus1 = HW_APBH_CHn_CURCMDAR_RD(NAND0_APBH_CH);
        DmaStatus2 = HW_APBH_CHn_CURCMDAR_RD(NAND0_APBH_CH + 1);
        //SystemHalt();
    }
#endif

    bool bSomeError = FALSE;
#if defined(STMP378x)
    // Check for an error on the DMA channel assigned to this chip
    if (HW_APBH_CTRL2_RD() & (BM_APBH_CTRL2_CH4_ERROR_IRQ << (pWaitStruct->u16CurrentChip)) != 0)
    {
        // If an error occured, clear it.  The ddi_gpmi_wait_for_dma function
        // will reset the channel.
        HW_APBH_CTRL2_CLR(BP_APBH_CTRL2_CH4_ERROR_IRQ << pWaitStruct->u16CurrentChip);
        bSomeError = TRUE;
    }
#elif !defined(STMP37xx) && !defined(STMP377x)
    #error Must define STMP37xx, STMP377x or STMP378x
#endif

    // Note that this ISR has run.
    pWaitStruct->u16DmaWaitStatus |= kNandGpmiDmaWaitMask_GpmiDma;

    // See if all criteria have been met, to declare the DMA finished.
    if (pWaitStruct->u16DmaWaitMask == pWaitStruct->u16DmaWaitStatus)
    {

        // There is code waiting for the completion of ECC.
        if ( !bSomeError )
        {
            // If there was no error, then signal the completion.
            tx_semaphore_put(&pWaitStruct->pSemaphore);
        }
    }

    // clear the APBH dma IRQ and re-enable the associated vector in icoll.
    ddi_gpmi_clear_dma_isr_enable( pWaitStruct->u16CurrentChip );

#if defined(DEBUG_DMA_TOUT) && defined(ENABLE_SDRAM_DEBUG_MEM) && defined(CMP_PROFILE_ENABLE)
    {
        uint32_t    Handle;
        Handle = cmp_profile_start(DDI_NAND_GROUP, "ddi_nand_GpmiDmaIsrHandler end");
        cmp_profile_stop(Handle);
    }
#endif // defined(DEBUG_DMA_TOUT) && defined(ENABLE_SDRAM_DEBUG_MEM) && defined(CMP_PROFILE_ENABLE)
}


void ddi_gpmi_clear_dma_isr_enable(uint16_t u16CurrentChip)
{
    // clear the APBH dma IRQ and re-enable the associated vector in icoll.

#if ENABLEVECTOR_BEFORE
    hw_icoll_EnableVector(VECTOR_IRQ_GPMI_DMA, true);
#endif

    ddi_gpmi_clear_dma_command_complete_irq(u16CurrentChip);

#if !ENABLEVECTOR_BEFORE
    hw_icoll_EnableVector(VECTOR_IRQ_GPMI_DMA, true);
#endif
}

void ddi_gpmi_clear_ecc_isr_enable(void)
{
    // Clear the ECC Complete IRQ and re-enable the associated vector in icoll.

#if ENABLEVECTOR_BEFORE
    hw_icoll_EnableVector(VECTOR_IRQ_ECC8, true);
#if defined(STMP378x)
    hw_icoll_EnableVector(VECTOR_IRQ_BCH, true);
#endif
#endif
    
    ddi_gpmi_clear_ecc8_ctrl_complete_irq();
    ddi_gpmi_set_ecc8_irq_enabled();
#if defined(STMP378x)
    ddi_gpmi_clear_bch_ctrl_complete_irq();
    ddi_gpmi_set_bch_irq_enabled();
#endif

#if !ENABLEVECTOR_BEFORE
    hw_icoll_EnableVector(VECTOR_IRQ_ECC8, true);
#if defined(STMP378x)
    hw_icoll_EnableVector(VECTOR_IRQ_BCH, true);
#endif
#endif
}

////////////////////////////////////////////////////////////////////////////////
//! \brief      ISR for the APBH ECC8-complete IRQ.
//!
//! \fntype     Interrupt Service
//!
//! Possibly signals APBH ECC completed by signaling the NAND semaphore.
//! This is dependent on the state of pParam->u16DmaWaitMask.
//!
//! Then clears and enables the interrupt.
//!
//! \param[in]  pParam  Pointer to GpmiDmaInfo_t.
////////////////////////////////////////////////////////////////////////////////
void ddi_nand_Ecc8IsrHandler(void *pParam)
{
    GpmiDmaInfo_t * pWaitStruct = (GpmiDmaInfo_t *)pParam;
#if defined(DEBUG_DMA_TOUT) && defined(ENABLE_SDRAM_DEBUG_MEM) && defined(CMP_PROFILE_ENABLE)
    {
        uint32_t    Handle;
        Handle = cmp_profile_start(DDI_NAND_GROUP, "ddi_nand_Ecc8IsrHandler start");
        cmp_profile_stop(Handle);
    }
#endif // defined(DEBUG_DMA_TOUT) && defined(ENABLE_SDRAM_DEBUG_MEM) && defined(CMP_PROFILE_ENABLE)

    // There are several possible causes for the VECTOR_IRQ_ECC8 interrupt.
    // 
    //     IRQ source                          IRQ enable                          Meaning
    //     ----------                          ----------                          -------
    //     hw_ecc8_ctrl_complete_irq           hw_ecc8_ctrl_complete_irq_en        DMA and associated ECC are complete.
    //     hw_ecc8_ctrl_debug_write_irq        hw_ecc8_ctrl_debug_write_irq_en     debug
    //     hw_ecc8_ctrl_debug_stall_irq        hw_ecc8_ctrl_debug_stall_irq_en     debug
    //     hw_ecc8_ctrl_bm_error_irq           N/A                                 Bus-master error on APBH
    // 
    // The first and last interrupts are particularly of interest.

    // Note that this ISR has run.
    pWaitStruct->u16DmaWaitStatus |= kNandGpmiDmaWaitMask_Ecc;

    // See if all criteria have been met, to declare the DMA finished.
    if (pWaitStruct->u16DmaWaitMask == pWaitStruct->u16DmaWaitStatus)
    {
        bool    bSomeError = FALSE;

        // See if this interrupt came from a bus-error on APBH,
        // possibly because the address of the transaction was invalid.
        if (BF_RD(ECC8_CTRL, BM_ERROR_IRQ))
        {
		    // If an error occured, clear it.  The ddi_gpmi_wait_for_dma function
		    // will reset the channel.
	        HW_ECC8_CTRL_CLR(BM_ECC8_CTRL_BM_ERROR_IRQ);
            bSomeError = TRUE;
        }

#if defined(STMP378x)
	    // Check for an error on the DMA channel assigned to
	    // this chip
	    if (HW_APBH_CTRL2_RD() & (BM_APBH_CTRL2_CH4_ERROR_IRQ << (pWaitStruct->u16CurrentChip)) != 0)
	    {
		    // If an error occured, clear it.  The ddi_gpmi_wait_for_dma function
		    // will reset the channel.
	        HW_APBH_CTRL2_CLR(BP_APBH_CTRL2_CH4_ERROR_IRQ << pWaitStruct->u16CurrentChip);

            bSomeError = TRUE;
 	    }
        
#elif !defined(STMP37xx) && !defined(STMP377x)
    #error Must define STMP37xx, STMP377x or STMP378x
#endif

        // There is code waiting for the completion of ECC.
        if ( !bSomeError )
        {
            // If there was no error, then signal the completion.
            tx_semaphore_put(&pWaitStruct->pSemaphore);
        }
    }

    // At this point we DO NOT
    //  * Clear the ECC IRQ
    //  * Re-enable the associated vector in icoll.
    // Reason: The ECC STATUS must be preserved until the client
    // application can read it, to see the ECC results.
    // It is the client application's responsibility to
    // perform the aforementioned actions after the ECC STATUS
    // has been read.

#if defined(DEBUG_DMA_TOUT) && defined(ENABLE_SDRAM_DEBUG_MEM) && defined(CMP_PROFILE_ENABLE)
    {
        uint32_t    Handle;
        Handle = cmp_profile_start(DDI_NAND_GROUP, "ddi_nand_Ecc8IsrHandler end");
        cmp_profile_stop(Handle);
    }
#endif // defined(DEBUG_DMA_TOUT) && defined(ENABLE_SDRAM_DEBUG_MEM) && defined(CMP_PROFILE_ENABLE)
}

#if defined(STMP378x)
////////////////////////////////////////////////////////////////////////////////
//! \brief      ISR for the APBH BCH-complete IRQ.
//!
//! \fntype     Interrupt Service
//!
//! Possibly signals APBH ECC completed by signaling the NAND semaphore.
//! This is dependent on the state of pParam->u16DmaWaitMask.
//!
//! Then clears and enables the interrupt.
//!
//! \param[in]  pParam  Pointer to GpmiDmaInfo_t.
////////////////////////////////////////////////////////////////////////////////
void ddi_nand_BchIsrHandler(void *pParam)
{
    GpmiDmaInfo_t * pWaitStruct = (GpmiDmaInfo_t *)pParam;
#if defined(DEBUG_DMA_TOUT) && defined(ENABLE_SDRAM_DEBUG_MEM) && defined(CMP_PROFILE_ENABLE)
    {
        uint32_t    Handle;
        Handle = cmp_profile_start(DDI_NAND_GROUP, "ddi_nand_BchIsrHandler start");
        cmp_profile_stop(Handle);
    }
#endif // defined(DEBUG_DMA_TOUT) && defined(ENABLE_SDRAM_DEBUG_MEM) && defined(CMP_PROFILE_ENABLE)

    // Note that this ISR has run.
    pWaitStruct->u16DmaWaitStatus |= kNandGpmiDmaWaitMask_Ecc;

    // See if all criteria have been met, to declare the DMA finished.
    if (pWaitStruct->u16DmaWaitMask == pWaitStruct->u16DmaWaitStatus)
    {
        bool    bSomeError = FALSE;

        // See if this interrupt came from a bus-error on APBH,
        // possibly because the address of the transaction was invalid.
        if (BF_RD(BCH_CTRL, BM_ERROR_IRQ))
        {
	        // If an error occured, clear it.  The ddi_gpmi_wait_for_dma function
	        // will reset the channel.
            HW_BCH_CTRL_CLR(BM_BCH_CTRL_BM_ERROR_IRQ);
            bSomeError = TRUE;
        }

#if defined(STMP378x)
	    // Check for an error on the DMA channel assigned to
	    // this chip
	    if (HW_APBH_CTRL2_RD() & (BM_APBH_CTRL2_CH4_ERROR_IRQ << (pWaitStruct->u16CurrentChip)) != 0)
	    {
		    // If an error occured, clear it.  The ddi_gpmi_wait_for_dma function
		    // will reset the channel.
	        HW_APBH_CTRL2_CLR(BP_APBH_CTRL2_CH4_ERROR_IRQ << pWaitStruct->u16CurrentChip);

            bSomeError = TRUE;
 	    }
        
#elif !defined(STMP37xx) && !defined(STMP377x)
    #error Must define STMP37xx, STMP377x or STMP378x
#endif

        // There is code waiting for the completion of ECC.
        if ( !bSomeError )
        {
            // If there was no error, then signal the completion.
            tx_semaphore_put(&pWaitStruct->pSemaphore);
        }
    }

    // At this point we DO NOT
    //  * Clear the ECC IRQ
    //  * Re-enable the associated vector in icoll.
    // Reason: The ECC STATUS must be preserved until the client
    // application can read it, to see the ECC results.
    // It is the client application's responsibility to
    // perform the aforementioned actions after the ECC STATUS
    // has been read.

#if defined(DEBUG_DMA_TOUT) && defined(ENABLE_SDRAM_DEBUG_MEM) && defined(CMP_PROFILE_ENABLE)
    {
        uint32_t    Handle;
        Handle = cmp_profile_start(DDI_NAND_GROUP, "ddi_nand_BchIsrHandler end");
        cmp_profile_stop(Handle);
    }
#endif // defined(DEBUG_DMA_TOUT) && defined(ENABLE_SDRAM_DEBUG_MEM) && defined(CMP_PROFILE_ENABLE)
}
#endif // STMP378x

#endif // RTOS_THREADX

// EOF
//! @}
