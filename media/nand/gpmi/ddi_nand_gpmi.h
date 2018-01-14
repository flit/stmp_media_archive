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
//! \addtogroup ddi_media_nand_hal_gpmi
//! @{
//! \file ddi_nand_gpmi.h
//! \brief Specific prototypes for setting up the NAND GPMI.
//!
//! This file includes the prototypes and defines required
//! to properly setup the NAND GPMI.
//!
//! \see ddi_nand_hal_gpmi.c
///////////////////////////////////////////////////////////////////////////////
#ifndef NAND_HAL_GPMI_H
#define NAND_HAL_GPMI_H 1

#include "error.h"
#include "types.h"
#include "registers/regs.h"
#include "registers/regspinctrl.h"
#include "registers/regsgpmi.h"
#include "registers/regsclkctrl.h"
#include "registers/regsapbh.h"
#include "registers/regsdigctl.h"
#include "os/pmi/os_pmi_api.h"
#include "os/eoi/os_eoi_api.h"
#include "os/threadx/tx_api.h"
#include "os/vmi/os_vmi_api.h"

////////////////////////////////////////////////////////////////////////////////
// Definitions
////////////////////////////////////////////////////////////////////////////////

//! \brief The maximum number of chip selects the GPMI peripheral supports.
//!
//! Despite the name, this has nothing to do with physical devices. What we
//! really care about is chip selects and the number of devices to which we're
//! connected is independent of that.
#define MAX_NAND_DEVICES  (4)

// Bit-shift for the zeroth-NAND's APBH DMA channel
#define NAND0_APBH_CH            (4)

//! \brief Mask bits for #GpmiDmaInfo_t::u16DmaWaitMask.
//!
//! Use these mask bits to select which interrupts are required to be waited
//! on by the GPMI DMA before the DMA is considered finished. Bits can be
//! combined to wait on multiple events.
enum _nand_gpmi_dma_wait_mask
{
    //! \brief No wait-criteria for the DMA to be finished.
    kNandGpmiDmaWaitMask_Nothing = 0,

    //! \brief DMA is finished when the GPMI DMA is finished.
    kNandGpmiDmaWaitMask_GpmiDma = 1,

    //! \brief DMA is finished when the ECC is finished and has transferred data on the APBH bus.
    kNandGpmiDmaWaitMask_Ecc = 2
};

////////////////////////////////////////////////////////////////////////////////
// Types
////////////////////////////////////////////////////////////////////////////////

struct NAND_Timing2_struct_t;

/*!
 * \brief Basic DMA descriptor layout in memory.
 */
typedef struct _dma_cmd
{
    struct _dma_cmd    *pNxt;
    hw_apbh_chn_cmd_t   cmd;
    void                *pBuf;
    hw_gpmi_ctrl0_t     ctrl;
    hw_gpmi_compare_t   cmp;
} dma_cmd_t;

#pragma pack(1)
//! \brief Characterizes timings for the NAND hardware interface.
//!
//! \warning This data structure comprises four bytes and therefore fits in a
//! single 32-bit integer. This enables us to pass around timing characteristics
//! with 32-bit integer assignments. Changing the size of this data structure
//! could have wide-spread implications.
//!
//! \sa NAND_Timing_t
typedef struct NAND_Timing1_struct_t
{
    uint8_t m_u8DataSetup;      //!< The data setup time, in nanoseconds.
    uint8_t m_u8DataHold;       //!< The data hold time, in nanoseconds.
    uint8_t m_u8AddressSetup;   //!< The address setup time, in nanoseconds.
    uint8_t m_u8DSAMPLE_TIME;   //!< The data sample time, in nanoseconds.

#if __cplusplus
    //! \brief Assignment operator to assign a timing 1 struct to a timing 2 struct.
    inline NAND_Timing1_struct_t & operator = (const NAND_Timing2_struct_t & other);
#endif // __cplusplus
} NAND_Timing1_struct_t;
#pragma pack()

//! Enables viewing the timing characteristics structure as a single 32-bit integer.
typedef union {	    		    // All fields in nanoseconds

    //! The 32-bit integer view of the timing characteristics structure.
    // By placing this word before the bitfield it allows structure copies to be done
    //  safely by assignment rather than by memcpy.

    uint32_t initializer;

    //! The timing characteristics structure.
    //! This structure holds the timing for the NAND.  This data is used by
    //! rom_nand_hal_GpmiSetNandTiming to setup the GPMI hardware registers.
    NAND_Timing1_struct_t NAND_Timing;

} NAND_Timing_t;

//! \brief Holds state and limited version information about the NAND timing struct.
typedef enum e_NAND_Timing_State
{
    e_NAND_Timing_State_FIRST                   = 0,

    //! \brief Indicates that the timing struct contains no info and should not be used.
    e_NAND_Timing_State_UNINITIALIZED           = e_NAND_Timing_State_FIRST,

    //! \brief Indicates that the timing struct contains a precomputed DSAMPLE_TIME, as was used for MK_NAND_TIMINGS( tsu, dsample, tds, tdh ).
    e_NAND_Timing_State_STATIC_DSAMPLE_TIME     = 254,  // Value is specifically chosen to be distinct from actual NAND timing parameters.

    //! \brief Indicates that the timing struct contains timing values (REA, RLOH, RHOH) needed to compute DSAMPLE_TIME dynamically.
    e_NAND_Timing_State_DYNAMIC_DSAMPLE_TIME    = 255,

    // Note: This quantity must fit in NAND_Timing_State_t.
    e_NAND_Timing_State_LAST = e_NAND_Timing_State_DYNAMIC_DSAMPLE_TIME
    
} e_NAND_Timing_State;

typedef uint8_t NAND_Timing_State_t;

#pragma pack(1)
//! \brief NAND Timing structure for setting up the GPMI timing.
//!
//! This structure holds the timing for the NAND.  This data is used by
//! rom_nand_hal_GpmiSetNandTiming to setup the GPMI hardware registers.
typedef struct NAND_Timing2_struct_t
{
    NAND_Timing_State_t eState;             //!< One of enum e_NAND_Timing_State.

    uint8_t             u8DataSetup;        //!< The data setup time (tDS), in nanoseconds.

    uint8_t             u8DataHold;         //!< The data hold time (tDH), in nanoseconds.

    uint8_t             u8AddressSetup;     //!< The address setup time (tSU), in nanoseconds.
                                            //! This value amalgamates the NAND parameters tCLS, tCS, and tALS.

    uint8_t             u8DSAMPLE_TIME;     //!< The data sample time, in nanoseconds.

    uint8_t             u8REA;              //!< From the NAND datasheet.

    uint8_t             u8RLOH;             //!< From the NAND datasheet.
                                            //! This is the amount of time that the last
                                            //! contents of the data lines will persist
                                            //! after the controller drives the -RE
                                            //! signal true.
                                            //! EDO Mode: This time is from the NAND spec, and the persistence of data
                                            //! is determined by (tRLOH + tDH).
                                            //! Non-EDO Mode: This time is ignored, because the persistence of data
                                            //! is determined by tRHOH.

    uint8_t             u8RHOH;             //!< From the NAND datasheet.
                                            //! This is the amount of time
                                            //! that the last contents of the data lines will persist after the
                                            //! controller drives the -RE signal false.
                                            //! EDO Mode: This time is ignored, because the persistence of data
                                            //! is determined by (tRLOH + tDH).
                                            //! Non-EDO Mode: This time is totally due to capacitive effects of the
                                            //! hardware.  For reliable behavior it should be set to zero, unless there is
                                            //! specific knowledge of the trace capacitance and the persistence of the
                                            //! data values.

#if __cplusplus
    //! \brief Assignment operator to assign a timing 2 struct to a timing 1 struct.
    inline NAND_Timing2_struct_t & operator = (const NAND_Timing1_struct_t & other);
#endif // __cplusplus

} NAND_Timing2_struct_t;
#pragma pack()

//! \brief Combines all versions/formats of NAND timing information.
typedef union NAND_Timing_Union_t
{
    //! \brief Contains a precomputed DSAMPLE_TIME, as was used for MK_NAND_TIMINGS( tsu, dsample, tds, tdh ).
    NAND_Timing1_struct_t    NAND_Timing_struct;

    //! \brief Varying format as indicated by its e8State element.
    NAND_Timing2_struct_t   NAND_Timing2_struct;
    
} NAND_Timing_Union_t;

//! \name Timing-Related Macros
//!
//! These macros assist in contruction timing characteristics for various needs.
//!
//! \sa
//!     - NAND_Timing1_struct_t
//!     - NAND_Timing2_struct_t
//!     - NAND_Timing_t
//!
//! @{

    //! The average t<sub>sample</sub> time, in nanoseconds.
    #define AVG_TSAMPLE_TIME  (6) // ns
    
    //! \brief Construct packed timing characteristics.
    //!
    //! This macro makes it convenient to type in easy-to-read timing values in
    //! any base and automatically generate the packed timing characteristics
    //! value used by most of the code.
    //!
    //! When generating timing characteristics with this macro, it's important
    //! to know there's a function called \c ddi_gpmi_relax_timings_by_amount() that
    //! adjusts t<sub>SU</sub>, t<sub>DS</sub>, and t<sub>DH</sub> at run time
    //! for NANDs with multiple chip enables:
    //!
    //! <pre>
    //!
    //!      Number of       Runtime
    //!     Chip Enables    Adjustment
    //!     ------------    ----------
    //!          1              0 ns
    //!          2             +5 ns
    //!          4            +10 ns
    //!
    //! </pre>
    //!
    //! Thus, for a family of NAND devices with with different numbers of chip
    //! enables, the timings increase for parts with more chip enables. This
    //! enables timings to be statically set for the single-chip enable part and
    //! still be compatible at runtime with slower two- and four-chip enable
    //! parts.
    //!
    //! \param[in]  tsu         t<sub>SU</sub> - Address setup time.
    //! \param[in]  dsample     t<sub>sample</sub> - Data sample time.
    //! \param[in]  tds         t<sub>DS</sub> - Data setup time.
    //! \param[in]  tdh         t<sub>DH</sub> - Data hold time.
    //!
    //! \return The timing characteristics.
    #define MK_NAND_TIMINGS_STATIC( tsu, dsample, tds, tdh )                    \
        {                                                                       \
                e_NAND_Timing_State_STATIC_DSAMPLE_TIME,                        \
                tds     ,       /* u8DataSetup;    */                           \
                tdh     ,       /* u8DataHold;     */                           \
                tsu     ,       /* u8AddressSetup; */                           \
                dsample ,       /* u8DSAMPLE_TIME; */                           \
                0       ,       /* u8REA;          */                           \
                0       ,       /* u8RLOH;         */                           \
                0               /* u8RHOH;         */                           \
        }   
    
    #define MK_NAND_TIMINGS_DYNAMIC( tsu, dsample, tds, tdh, trea, trloh, trhoh )                    \
        {                                                                       \
                e_NAND_Timing_State_DYNAMIC_DSAMPLE_TIME,                       \
                tds     ,       /* u8DataSetup;    */                           \
                tdh     ,       /* u8DataHold;     */                           \
                tsu     ,       /* u8AddressSetup; */                           \
                dsample ,       /* u8DSAMPLE_TIME; */                           \
                trea    ,       /* u8REA;          */                           \
                trloh   ,       /* u8RLOH;         */                           \
                trhoh           /* u8RHOH;         */                           \
         }
    
    //! Timing characteristics that are safe for many devices.
    //!
    //! These timing characteristics are used with a number of devices.
    //!
    //! \sa ddi_nand_hal_tables.h
    #define NAND_FAILSAFE_TIMINGS   MK_NAND_TIMINGS_STATIC( 0, AVG_TSAMPLE_TIME, 45, 32 )
    
    //! Timing characteristics that work with every device we've ever known.
    //!
    //! These timing characteristics are used during startup in \c NandHalInit(),
    //! So they have to work for absolutely every device we know.
    //!
    //! It used to be the case that \c NandHalInit() used
    //! \c NAND_FAILSAFE_TIMINGS. Hynix devices can't tolerate an address setup
    //! time (t<sub>SU</sub>) of zero, so we had introduce a "super safe" value
    //! that works with even those devices.
    #define NAND_SAFESTARTUP_TIMINGS  MK_NAND_TIMINGS_STATIC( 25, AVG_TSAMPLE_TIME, 80, 60 )

//! @}

//! \brief Some handy constants for GPMI DMA functions.
enum _gpmi_dma_constants
{
    //! \brief Tells ddi_gpmi_start_dma() to not flush the data cache.
    kGpmiDontFlushCache = 0
};

////////////////////////////////////////////////////////////////////////////////
// Prototypes
////////////////////////////////////////////////////////////////////////////////

#ifdef __cplusplus
extern "C" {
#endif

//! \name Init and shutdown
//@{

    ////////////////////////////////////////////////////////////////////////////////
    //! \brief Enable and initialize the GPMI driver.
    //!
    //! This function configures the GPMI block using the HW registers based upon
    //! the desired chip and the number of bits. You must call this API for each
    //! chip select that will be used in the application configuration.
    //!
    //! \param[in] bUse16BitData 0 for 8 bit, 1 for 16 bit NAND support.
    //! \param[in] u32ChipNumber The zero-based \em index
    //!     of the chip select to initialize.
    //! \param[in] bUseAlternateChipEnables If TRUE, use the Alternate Chip Enables.
    //! \param[in] bUse1_8V_Drive If TRUE, drive GPMI pins at 1.8V instead of 3.3V.
    //! \param[in] bEnableInternalPullups If TRUE, will enable internal pullups.
    //!
    //! \return SUCCESS
    //! \return ERROR_DDI_NAND_GPMI_NOT_PRESENT
    //!
    //! \todo Make this function truly take a number of NANDs so it only has to
    //!     be called once.
    //! \todo Get rid of bUseAlternateChipEnables parameter, and maybe bUse16BitData
    //!     as well.
    ////////////////////////////////////////////////////////////////////////////////
    RtStatus_t ddi_gpmi_init(bool bUse16BitData,
                                   uint32_t u32ChipNumber,
                                   bool bUseAlternateChipEnables,
                                   bool bUse1_8V_Drive,
                                   bool bEnableInternalPullups);

    ////////////////////////////////////////////////////////////////////////////////
    //! \brief Setup GPMI/PMI interaction.
    //!
    //! The handlers for PMI clock change notifications are installed by this function.
    //! This is separated from ddi_gpmi_init() because it cannot be done until after
    //! PMI itself is initialized, which is after GPMI and NAND in a paging system.
    //!
    //! \return SUCCESS
    ////////////////////////////////////////////////////////////////////////////////
    RtStatus_t ddi_gpmi_init_pmi(void);

    ////////////////////////////////////////////////////////////////////////////////
    //! \brief Resets the GPMI block.
    //!
    //! A soft reset can take multiple clocks to complete, so do NOT gate the
    //! clock when setting soft reset. The reset process will gate the clock
    //! automatically. Poll until this has happened before subsequently
    //! clearing soft reset and clock gate.
    ////////////////////////////////////////////////////////////////////////////////
    void ddi_gpmi_soft_reset(void);

    ////////////////////////////////////////////////////////////////////////////////
    //! \brief Disable the GPMI driver.
    //!
    //! This function gates the clock to the GPMI peripheral.
    ////////////////////////////////////////////////////////////////////////////////
    void ddi_gpmi_disable(void);

//@}

//! \name Miscellaneous
//@{

    ////////////////////////////////////////////////////////////////////////////////
    //! \brief Enable writes via the Write Protect line of the NAND.
    //!
    //! Enable or disable writes via the /WP pin of the NAND.  This WP line is
    //! shared amongst all the NANDs.
    //!
    //! \param[in]  bClearOrSet Enable writes (1) or Disable writes (0) (/WP pin)
    ////////////////////////////////////////////////////////////////////////////////
    void ddi_gpmi_enable_writes(bool bClearOrSet);
    
    ////////////////////////////////////////////////////////////////////////////////
    //! \brief Perform a software controlled wait for ready.
    //! \param chipSelect The chip select number to wait on. [0-3]
    //! \param timeout Maximum time to wait before erroring out. Time is in
    //!     microseconds.
    //! \retval SUCCESS
    //! \retval ERROR_DDI_NAND_GPMI_DMA_TIMEOUT
    //! \retval ERROR_DDI_NAND_GPMI_DMA_BUSY
    ////////////////////////////////////////////////////////////////////////////////
    RtStatus_t ddi_gpmi_wait_for_ready(unsigned chipSelect, uint32_t timeout);

//@}

//! \name DMA Utilities
//@{

    ////////////////////////////////////////////////////////////////////////////////
    //! \brief Start the appropriate DMA channel
    //!
    //! Starts a NAND DMA command on the channel associated with the chip select
    //! in the \a wNANDDeviceNum parameter. Before the DMA is started, the region of memory
    //! starting from \a pDmaCmd and running for \a dmaCommandLength bytes is flushed from
    //! the data cache ("cleaned" in ARM parlance). However, if \a dmaCommandLength is set
    //! to 0 instead of a valid length, then the data cache will not be flushed. This can
    //! be useful in cases where the calling code has already flushed the entire cache.
    //! The constant #kGpmiDontFlushCache is provided to help describe the length parameter
    //! value.
    //!
    //! \param[in] pDmaCmd          Pointer to dma command structure. Must be the virtual address,
    //!     as it is converted to a physical address before the DMA is started.
    //! \param[in] dmaCommandLength Length in bytes of the DMA command chain pointed
    //!                             to by \a pDmaCmd.
    //! \param[in] u32NANDDeviceNum Which NAND should be started.
    //! \param[in] u16DmaWaitMask   A bitmask used to indicate criteria for terminating the DMA.
    //!                             See #GpmiDmaInfo_t::u16DmaWaitMask and #_nand_gpmi_dma_wait_mask.
    //!
    //! \retval SUCCESS The DMA is started.
    //! \retval ERROR_DDI_NAND_GPMI_DMA_BUSY Another DMA is already running.
    ////////////////////////////////////////////////////////////////////////////////
    RtStatus_t ddi_gpmi_start_dma(dma_cmd_t * pDmaCmd, uint32_t dmaCommandLength, uint32_t wNANDDeviceNum, uint16_t u16DmaWaitMask);

    ////////////////////////////////////////////////////////////////////////////////
    //! \brief Wait for DMA to complete.
    //!
    //! Waits for NAND DMA command chain corresponding to wNANDDeviceNum to complete.
    //!
    //! \param[in] u32usec Number of microseconds to wait before timing out. If
    //!     zero is passed for the timeout, then this function will wait forever.
    //! \param[in] u32NANDDeviceNum Which NAND should be polled.
    //!
    //! \retval SUCCESS DMA completed without an error.
    //! \retval ERROR_DDI_NAND_GPMI_DMA_TIMEOUT DMA never completed or is still
    //!     running. This value is returned explicitly from this function when
    //!     the DMA semphore times out, and most DMA chains also return this
    //!     error (see below for how) when the GPMI device busy timeout expires.
    //!
    //! \note Uses the BAR field of the last DMA command to signal                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                  
    //!             result of the DMA chain.
    ////////////////////////////////////////////////////////////////////////////////
    RtStatus_t ddi_gpmi_wait_for_dma(uint32_t usec, uint32_t wNANDDeviceNum);

    ////////////////////////////////////////////////////////////////////////////////
    //! \brief Returns a Boolean indicating if a DMA is currently running.
    //!
    //! \param u32NANDDeviceNum Specifies which DMA channel to inspect.
    //! \retval true A DMA is currently running.
    //! \retval false The DMA channel is free.
    ////////////////////////////////////////////////////////////////////////////////
    bool ddi_gpmi_is_dma_active(uint32_t u32NANDDeviceNum);

//@}

//! \name Timing Control Functions
//! @{
    
    ////////////////////////////////////////////////////////////////////////////////
    //! \brief Fill the given timing structure with the safe timings.
    //!
    //! This function is used to get the timings characteristics that work with
    //! every device we've ever known. These timings should be used during initialization
    //! and device discovery. Once the device type is known,  timings specific to
    //! that device should be set.  Remember to actually set the safe timings once
    //! you get them by calling ddi_gpmi_set_timings().
    //!
    //! \param[out] timings The timings structure that will be set to the safe
    //!     timings upon return.
    ////////////////////////////////////////////////////////////////////////////////
    void ddi_gpmi_get_safe_timings(NAND_Timing2_struct_t * timings);

    ////////////////////////////////////////////////////////////////////////////////
    //! \brief Adjust the NAND timings.
    //!
    //! The tables include Nand timings so the access times can be tuned for each
    //! NAND. Updates the current NAND_TIMINGS structure to reflect the most
    //! relaxed memory timings between the two chips.
    //!
    //! \param[in]  prev Previous NAND timings. These values are updated.
    //! \param[in]  curr Current NAND timings.
    ////////////////////////////////////////////////////////////////////////////////
    void ddi_gpmi_set_most_relaxed_timings(NAND_Timing2_struct_t * prev, NAND_Timing2_struct_t const * curr);
    
    ////////////////////////////////////////////////////////////////////////////////
    //! \brief Relaxes timings by a specified amount.
    //!
    //! The \a timing structure passed in is modified so that its timing values are
    //! relaxed by the \a increment specified in nanoseconds. This is used primarily
    //! to adjust timings for additional capacitance on the GPMI signal traces due
    //! to multiple chips.
    //!
    //! \param[in] timing Data structure containing timing information to be updated.
    //! \param[in] increment Increment to add to Adress Setup, Data Setup and Data Hold
    //!             times. Value is in nanoseconds.
    ////////////////////////////////////////////////////////////////////////////////
    void ddi_gpmi_relax_timings_by_amount(NAND_Timing2_struct_t * timing, uint32_t increment);
    
    ////////////////////////////////////////////////////////////////////////////////
    //! \brief Set the flash timing for optimal NAND performance.
    //!
    //! Set the optimal NAND timings based upon the passed in NAND timings.
    //!
    //! \param[in]  pNT                 Pointer to timing table for this NAND.
    //! \param[in]  bWriteToTheDevice   True means that this function is permitted
    //!                                 to actually change the timing settings.
    //!                                 False means that this function will execute any
    //!                                 other logic or printing, but will not change the settings.
    //!
    //! \retval SUCCESS Timings were set successfully.
    //!
    //! \warning This function assumes all NAND I/O is halted.
    ////////////////////////////////////////////////////////////////////////////////
    RtStatus_t ddi_gpmi_set_timings( NAND_Timing2_struct_t const * pNT, bool bWriteToTheDevice);

    ////////////////////////////////////////////////////////////////////////////////
    //! \brief Set the timeout value for wait for ready.
    //!
    //! The timeout value set here is used for the GPMI wait for ready mode. It
    //! will have the most effect upon DMA operations.
    //!
    //! \param[in] busyTimeout Timeout value in microseconds.
    ////////////////////////////////////////////////////////////////////////////////
    void ddi_gpmi_set_busy_timeout(uint32_t busyTimeout);
    
    ////////////////////////////////////////////////////////////////////////////////
    //! \brief Returns the current GPMI timings values.
    //!
    //! \return A pointer to the current timing values used by the GPMI block.
    //!     This will be the last set of timings passed to ddi_gpmi_set_timings().
    ////////////////////////////////////////////////////////////////////////////////
    const NAND_Timing2_struct_t * ddi_gpmi_get_current_timings();
    
    ////////////////////////////////////////////////////////////////////////////////
    //! \brief Returns the current signal propagation delay values.
    ////////////////////////////////////////////////////////////////////////////////
    void ddi_gpmi_get_propagation_delay(uint32_t * minDelay, uint32_t * maxDelay);
    
    ////////////////////////////////////////////////////////////////////////////////
    //! \brief Changes the signal propagation delay.
    //!
    //! Modifying the propagation delay values will not immediately have any
    //! impact. They only affect timing calculations made when ddi_gpmi_set_timings()
    //! is invoked.
    ////////////////////////////////////////////////////////////////////////////////
    void ddi_gpmi_set_propagation_delay(uint32_t minDelay, uint32_t maxDelay);

//! @}

////////////////////////////////////////////////////////////////////////////////
//! \brief Inline function to convert virtual address to physical.
////////////////////////////////////////////////////////////////////////////////
inline void * nand_virtual_to_physical(const void * virtualAddress)
{
    uint32_t physicalAddress;
    os_vmi_VirtToPhys((uint32_t)virtualAddress, &physicalAddress);
    return (void *)physicalAddress;
}

#ifdef __cplusplus
} // extern "C"
#endif

#if __cplusplus
//! \name Timing structure operators
//!
//! These utility functions are intended to help work with the two different timing structures
//! with different types: the simple (#NAND_Timing1_struct_t) and complex (#NAND_Timing2_struct_t)
//! timing structures. Use the comparison operators to test for equality or inequality, and use
//! the assignment operators to copy the different timing structures into each other.
//@{

    //! \brief Equality operator to compare different types of timing structure.
    inline bool operator == (const NAND_Timing1_struct_t & pNT1, const NAND_Timing2_struct_t & pNT2)
    {
        return  pNT1.m_u8DataSetup     == pNT2.u8DataSetup
                && pNT1.m_u8DataHold      == pNT2.u8DataHold
                && pNT1.m_u8AddressSetup  == pNT2.u8AddressSetup
                && pNT1.m_u8DSAMPLE_TIME  == pNT2.u8DSAMPLE_TIME;
    }

    //! \brief Inequality operator for timing structures.
    inline bool operator != (const NAND_Timing1_struct_t & pNT1, const NAND_Timing2_struct_t & pNT2)
    {
        return !(pNT1 == pNT2);
    }

    NAND_Timing1_struct_t & NAND_Timing1_struct_t::operator = (const NAND_Timing2_struct_t & other)
    {
        m_u8DataSetup = other.u8DataSetup;
        m_u8DataHold = other.u8DataHold;
        m_u8AddressSetup = other.u8AddressSetup;
        m_u8DSAMPLE_TIME = other.u8DSAMPLE_TIME;
        return *this;
    }

    inline NAND_Timing2_struct_t & NAND_Timing2_struct_t::operator = (const NAND_Timing1_struct_t & other)
    {
        eState = e_NAND_Timing_State_STATIC_DSAMPLE_TIME;
        u8DataSetup = other.m_u8DataSetup;
        u8DataHold = other.m_u8DataHold;
        u8AddressSetup = other.m_u8AddressSetup;
        u8DSAMPLE_TIME = other.m_u8DSAMPLE_TIME;
        u8REA = 0;
        u8RLOH = 0;
        u8RHOH = 0;
        return *this;
    }

//@}
#endif // __cplusplus

#endif // NAND_HAL_GPMI_H
////////////////////////////////////////////////////////////////////////////////
// End of file
////////////////////////////////////////////////////////////////////////////////
//! @}
