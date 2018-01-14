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
////////////////////////////////////////////////////////////////////////////////
//! \addtogroup ddi_media_nand_hal_internals
//! @{
//! \file ddi_nand_hal_init.cpp
//! \brief Routines for initializing the NANDs.
////////////////////////////////////////////////////////////////////////////////

#include "types.h"
#include "os/pmi/os_pmi_api.h"
#include "os/eoi/os_eoi_api.h"
#include "hw/core/vmemory.h"
#include "hw/power/hw_power.h"
#include "hw/otp/hw_otp.h"
#include "drivers/media/ddi_media.h"
#include "ddi_nand_hal_internal.h"
#include "drivers/media/nand/hal/spy/ddi_nand_hal_spy.h"
#include <string.h>
#include "ddi_nand_hal_tables.h"
#include "drivers/media/nand/gpmi/ddi_nand_ecc.h"
#include "drivers/media/nand/gpmi/ddi_nand_ecc_override.h"
#include "components/telemetry/tss_logtext.h"
#include "onfi_param_page.h"
#include <stdlib.h>
#include <memory>
#include "auto_free.h"
#include "os/dmi/os_dmi_api.h"

////////////////////////////////////////////////////////////////////////////////
// Definitions
////////////////////////////////////////////////////////////////////////////////

#if !defined(PREFER_ONFI_AUTO_CONFIG)
    //! Set this macro to 1 to prefer automatic configuration for ONFI NANDs. When set to 0,
    //! the device code tables will be preferred and ONFI auto configuration will only be
    //! used if the NAND cannot be found in the tables.
    #define PREFER_ONFI_AUTO_CONFIG 1
#endif

//! These are physical parameters that can be overruled in NandInitDescriptor_t
//! by analyzing the data read during read IDs command.
typedef struct {
    uint32_t wTotalInternalDice;    //!< (1/2/4/...) - number of chips pretending to be a single chip
    uint32_t wBlocksPerDie;         //!< (wTotalBlocks / wTotalInternalDice)
} NandOverruledParameters_t;

/*!
 * \brief Class used to initialize a chip enable.
 *
 * This special concrete subclass of CommonNandBase is used to initialize GPMI for each chip
 * enable and probe for a NAND. If a NAND is present, its type will be determined through
 * the use of device code lookup tables. Then an appropriate instance of one of the 
 * type-specific subclasses of CommonNandBase will be created and initialized.
 * 
 * To use this class, create an instance and pass the chip enable number to the sole constructor.
 * Then call the initChip() method, which will return a new instance of a CommonNandBase subclass
 * if a NAND is present and there are no errors.
 */
class InitNand : public CommonNandBase
{
public:
    
    //! \brief Constructor that takes the chip enable number.
    explicit InitNand(unsigned chipEnable);
    
    //! \brief Initialize this chip enable and create the real NAND instance.
    RtStatus_t initChip();
    
    //! \brief Return the timings determined by initChip().
    inline const NAND_Timing2_struct_t * getTimings() const { return &m_timings; }
    
    //! \brief Return the new type-specific NAND instance.
    inline NandPhysicalMedia * getNewNand() { return m_newNand; }
    
protected:

    NandReadIdResponse_t m_idResponse;
    bool m_isOnfi;
    NAND_Timing2_struct_t m_timings;
    const NandDeviceCodeMap_t * m_mapEntry;
    NandOverruledParameters_t m_nandModifiedParams;
    CommonNandBase * m_newNand;
    
    //! \brief Returns number of consequetive 0 bits starting with bit 0.
    short count0Bits(short n);

    RtStatus_t configureNandByTables();
    RtStatus_t setupNandParameters();

    RtStatus_t determineNandType();
    const NandDeviceCodeMap_t * selectDeviceCodeMap(const NandReadIdResponse_t & idResponse);
    void modifyNandParameters(const NandReadIdResponse_t & idResponse);
    
    RtStatus_t configureOnfiNand();
    RtStatus_t setupOnfiNandParameters(const OnfiParamPage & onfiParams);
    RtStatus_t determineOnfiEccType(const OnfiParamPage & onfiParams);

#if defined(STMP378x)
    void overrideEccParameters(NandEccDescriptor_t * pEccDescriptor);
#endif
    
};

////////////////////////////////////////////////////////////////////////////////
// Variables
////////////////////////////////////////////////////////////////////////////////

#if defined(STMP378x)
    //! Pointer to the ECC parameters override function set by the application.
    NandEccOverrideCallback_t g_pEccOverrideCallback = NULL;
#elif !defined(STMP37xx) && !defined(STMP377x)
	#error Must define STMP37xx, STMP377x or STMP378x
#endif

#if defined(STMP378x)
//! \brief Pointer to a buffer used by the HAL to read metadata during DMA.
//! The buffer memory to which this points is dynamically allocated.
static uint8_t * stc_pMetadataBuffer = NULL;
#endif

////////////////////////////////////////////////////////////////////////////////
// Code
////////////////////////////////////////////////////////////////////////////////

RtStatus_t NandHal::init()
{
    RtStatus_t status;
    
    // For some reason, using the zero initializer caused GHS MULTI to generate a
    // memcpy() instead of memset(), and for some reason that caused an abort
    // in the ROM implementation of memcpy(). Manually calling memset() works fine.
    NAND_Timing2_struct_t timings;// = {0};
    memset(&timings, 0, sizeof(timings));
    
    // Clear the HAL context.
    memset(&g_nandHalContext, 0, sizeof(g_nandHalContext));

    // Grab the number of NAND chips.
    g_nandHalContext.chipSelectCount = hw_otp_NandNumberChips();
    g_nandHalContext.totalBlockCount = 0;

    // Construct the DMA objects in place. This is necessary because our paging apps do not
    // call the static initializer chain that would normally auto-construct these objects.
    // In non-paging apps, this is harmless.
    new (&g_nandHalContext.readDma) NandDma::ReadEccData;
    new (&g_nandHalContext.readMetadataDma) NandDma::ReadEccData;
    new (&g_nandHalContext.readFirmwareDma) NandDma::ReadEccData;
    new (&g_nandHalContext.writeDma) NandDma::WriteEccData;
    new (&g_nandHalContext.statusDma) NandDma::ReadStatus;
    new (&g_nandHalContext.eraseDma) NandDma::BlockErase;

    // Ask the HAL to initialize its synchronization objects.
    status = os_thi_ConvertTxStatus(tx_mutex_create(&g_nandHalContext.serializationMutex, "NAND_HAL_MUTEX", TX_INHERIT));
    if (status != SUCCESS)
    {
        return status;
    }

    // Probe and initialize each of the NANDs.
    unsigned i;
    for (i=0; i < g_nandHalContext.chipSelectCount; i++)
    {
        // Create a local instance of the NAND init class for this chip enable.
        InitNand nand(i);
        
        // Determine if there is a NAND there and what its type is, then fill in HAL structs.
        status = nand.initChip();
        if (status != SUCCESS)
        {
            // If chip AFTER first fails, then set nand count to good count
            if (i > 0)
            {
                g_nandHalContext.chipSelectCount = i;
                status = SUCCESS;
                break;
            }
            // If the first chip failed, return Hardware error.
            else
            {
                return status;
            }
        }
        
        // Save the new NAND instance into the array of chip enable instances.
        g_nandHalContext.nands[i] = nand.getNewNand();
        
        // Add up total number of blocks.
        g_nandHalContext.totalBlockCount += nand.getNewNand()->wTotalBlocks;
        
        // Update timings from this chip enable. For the first chip enable, just copy the
        // timings directly.
        if (i == 0)
        {
            timings = *nand.getTimings();
        }
        else
        {
            ddi_gpmi_set_most_relaxed_timings(&timings, nand.getTimings());
        }
    }

    // For Nand2 and Nand4, relax timing to allow for
    // signal distortion due to higher capacitance.
    if (g_nandHalContext.chipSelectCount > 2)
    {
        ddi_gpmi_relax_timings_by_amount(&timings, 10);
    }
    else if (g_nandHalContext.chipSelectCount > 1)
    {
        ddi_gpmi_relax_timings_by_amount(&timings, 5);
    }

    // this will set the GPFlash interface to the composite timings for the set of NANDs available
    ddi_gpmi_set_timings(&timings, true /* bWriteToTheDevice */);
    
    return status;
}

RtStatus_t NandHal::shutdown()
{
    // Dispose of NAND objects.
    unsigned i;
    for (i=0; i < g_nandHalContext.chipSelectCount; i++)
    {
        if (g_nandHalContext.nands[i])
        {
            ((CommonNandBase *)g_nandHalContext.nands[i])->cleanup();
            delete g_nandHalContext.nands[i];
            g_nandHalContext.nands[i] = NULL;
        }
    }
    
#if defined(STMP378x)
    // Dispose of the metadata buffer.
    if (stc_pMetadataBuffer)
    {
        free(stc_pMetadataBuffer);
        stc_pMetadataBuffer = NULL;
    }
#elif !defined(STMP37xx) && !defined(STMP377x)
    #error Must define STMP37xx, STMP377x or STMP378x
#endif

    // Wipe NAND parameters structure.
    memset(&g_nandHalContext.parameters, 0, sizeof(g_nandHalContext.parameters));

    // Disable the GPMI block.
    ddi_gpmi_disable();
    
    // Lastly, destroy the HAL mutex.
    tx_mutex_delete(&g_nandHalContext.serializationMutex);
    
    return SUCCESS;
}

////////////////////////////////////////////////////////////////////////////////
//! This function exists solely to provide a method to call the NAND HAL shutdown routine from
//! C linkage code.
////////////////////////////////////////////////////////////////////////////////
extern "C" void ddi_nand_hal_shutdown(void)
{
    NandHal::shutdown();
}

////////////////////////////////////////////////////////////////////////////////
//! This constructor fills in the minimum fields required to be able to send reset and
//! read ID commands. No other commands should be attempted
////////////////////////////////////////////////////////////////////////////////
InitNand::InitNand(unsigned chipEnable)
:   CommonNandBase(),
    m_isOnfi(false),
    m_mapEntry(0),
    m_newNand(0)
{
    wChipNumber = chipEnable;
    pNANDParams = &g_nandHalContext.parameters;
}

////////////////////////////////////////////////////////////////////////////////
//! \brief      Count lsb 0 bits.
//!
//! \fntype     Non-Reentrant
//!
//! Count the number of 0 bits that exist before the first 1 bit.
//!
//! \param[in]  n Number to count lsb 0s on..
//!
//! \retval     Number of lsb 0 bits.
//!
//! \note       Return 0 if input is 0.  Always pass this an even power of two.
////////////////////////////////////////////////////////////////////////////////
short InitNand::count0Bits(short n)
{
    short i = 0;

    if ( n )
    {
        while ( 0 == (n & 1) )
        {
            ++i;
            n >>= 1;
        }
    }

    return i;
}

////////////////////////////////////////////////////////////////////////////////
//! \brief Initialize the GPMI chip enable and determine the NAND.
//!
//! The chip enable specified in the InitNand constructor is initialized in
//! the GPMI driver. The chip enable is probed to detect the presence of an
//! attached NAND, and if a NAND is present then its ID information is read.
//! Using this ID, the NAND's type and specifications are determined. A new
//! concrete instance of NandPhysicalMedia is created and filled in appropriately,
//! and the shared NandParameters_t structure is filled in.
//!
//! After this function returns successfully, you can access the newly created
//! NAND instance with the getNewNand() method. The "best" timings for this
//! particular NAND are available through the getTimings() accessor method.
//!
//! \retval SUCCESS
//! \retval ERROR_DDI_NAND_HAL_NANDTYPE_MISMATCH
//! \retval ERROR_DDI_NAND_HAL_LOOKUP_ID_FAILED
////////////////////////////////////////////////////////////////////////////////
RtStatus_t InitNand::initChip()
{
    RtStatus_t Status;
    
    // Initialize the pins for the GPMI interface to the NANDs.
    Status = ddi_gpmi_init(false, wChipNumber, false, false, hw_otp_NandEnableInternalPullups());
    if (Status != SUCCESS)
    {
        return Status;
    }

    // initialize the timings to safe vaules
    ddi_gpmi_get_safe_timings(&m_timings);
    ddi_gpmi_set_timings(&m_timings, true /* bWriteToTheDevice */);

    // Reset the NAND first off. Many modern NANDs require this.
    Status = reset();
    if (Status != SUCCESS)
    {
        return Status;
    }
    
    // Call ReadID to determine NAND maker, device code, and other information. We read into
    // the shared result buffer because it is properly cache aligned and sized.
    Status = readID(g_nandHalResultBuffer);
    if (Status != SUCCESS)
    {
        return Status;
    }
    
    // Copy the result buffer contents into the member variable.
    m_idResponse = *(NandReadIdResponse_t *)g_nandHalResultBuffer;

    // Save the full value from chip 0 in a separate global.
    if (wChipNumber == 0)
    {
         g_nandHalContext.readIdResponse = m_idResponse;
    }

    // If this is an ONFI NAND, take a different discovey and configuration route.
    m_isOnfi = checkOnfiID();
    
#if PREFER_ONFI_AUTO_CONFIG
    // If the NAND is ONFI then auto-configure.
    if (m_isOnfi)
    {
        Status = configureOnfiNand();
    }
    else
    {
        Status = configureNandByTables();
    }
#else
    // Try tables first and only use ONFI auto config as a backup.
    Status = configureNandByTables();
    if (Status != SUCCESS && m_isOnfi)
    {
        Status = configureOnfiNand();
    }
#endif // PREFER_ONFI_AUTO_CONFIG
    
    // Check configuration status.
    if (Status != SUCCESS)
    {
        return Status;
    }
    
    // If we reach this point, we must have successfully created a typed NAND instance.
    assert(m_newNand);

    // NAND HAL SPY telemetry has weak linkage.  If it has been linked, then we can use it.
    if ( NULL != &ddi_nand_hal_spy_bIsLinked )
    {
        ddi_nand_hal_spy_Init( m_newNand,
                50000 /* nReadWarningThreshold */,
                5000  /* nEraseWarningThreshold */);
    }

#if defined(STMP378x)
    // Allow the application to override the ECC parameters that were loaded from the NAND table.
    overrideEccParameters(&pNANDParams->eccDescriptor);
    ddi_bch_update_parameters(wChipNumber, &pNANDParams->eccDescriptor, pNANDParams->pageTotalSize);

    if ( NULL == stc_pMetadataBuffer )
    {
        // For BCH, must obtain a separate data buffer to use for reading and
        // writing metadata.
        if (pNANDParams->eccDescriptor.isBCH())
        {
            // The buffer needs to be as large as the first ECC chunk of the page, which
            // includes both data and metadata.
            size_t bufferSize = pNANDParams->eccDescriptor.u32SizeBlock0 + pNANDParams->eccDescriptor.u32MetadataBytes;
        
            // Allocate the buffer.
            //! \todo Make sure this buffer is cache aligned!
            stc_pMetadataBuffer = (uint8_t *)os_dmi_malloc_phys_contiguous(bufferSize);
            assert(stc_pMetadataBuffer);
        }

    } // if ( NULL == stc_pMetadataBuffer )
#endif // defined(STMP378x)
    
    // Give the NAND type a chance to do any type-specific initialization on the NAND object,
    // now that we've filled in what we think are the correct values.
    Status = m_newNand->init();

    return Status;
}

////////////////////////////////////////////////////////////////////////////////
//! \brief Configure the NAND by using the device code tables.
//!
//! This method takes the Read ID results previously read into #m_idResponse
//! and tries to search for a match in the device code tables. If it succeeds,
//! it will create a new type-specific NAND instance and fill it in. Also,
//! the first time through this function the shared NAND parameters structure
//! will be filled in from the information in the device code tables.
//!
//! \retval SUCCESS A matching NAND was found and a new NAND instance was created.
//! \retval ERROR_DDI_NAND_HAL_NANDTYPE_MISMATCH The current chip enable has
//!     a NAND whose type does not match previous chip enables.
////////////////////////////////////////////////////////////////////////////////
RtStatus_t InitNand::configureNandByTables()
{
    //! \todo In the STMP37xx series chips, the NAND timing parameters
    //! are stored in the NCB on NANDs that have been loaded with SDK5
    //! software.  In that case it is possible to use those values
    //! rather than re-deriving them here. However, with NANDs that have
    //! not been loaded with SDK5 firmware, the NCB does not exist and
    //! we still need to derive the timing values.  Also,
    //! NandMediaAllocate needs the NAND timing for writing to the NCB.
    //! Therefore, this call to "DetermineNandType" persists.

    RtStatus_t Status = determineNandType();
    if (Status != SUCCESS)
    {
        return Status;
    }
    
    // Instantiate the NAND object now that we know its type.
    m_newNand = createNandOfType(m_mapEntry->pNandDescriptorSubStruct->NandType);
    
    // At this point, the nandInitStruct is ready to be copied to NandPhysicalMedia and NandParameters_t.
    m_newNand->pNANDParams = &g_nandHalContext.parameters;
    m_newNand->wChipNumber = wChipNumber;
    m_newNand->totalPages = m_mapEntry->totalBlocks * m_mapEntry->pNandDescriptorSubStruct->pagesPerBlock;
    m_newNand->wTotalBlocks = m_mapEntry->totalBlocks;
    m_newNand->wTotalInternalDice = m_nandModifiedParams.wTotalInternalDice;
    m_newNand->wBlocksPerDie = m_nandModifiedParams.wBlocksPerDie;
    m_newNand->m_firstAbsoluteBlock = m_newNand->wTotalBlocks * m_newNand->wChipNumber;
    m_newNand->m_firstAbsolutePage = m_newNand->totalPages * m_newNand->wChipNumber;

    // Fill in the global parameters struct if it hasn't already been filled in. This only needs
    // to be done for the first chip enable, as we only have one shared parameters struct.
    if (wChipNumber == 0)
    {
        Status = setupNandParameters();
        if (Status != SUCCESS)
        {
            return Status;
        }
    }
    
    // Make sure the NAND type is the same for all chip enables.
    if (pNANDParams->NandType != m_mapEntry->pNandDescriptorSubStruct->NandType)
    {
        return ERROR_DDI_NAND_HAL_NANDTYPE_MISMATCH;
    }
    
    return SUCCESS;
}

////////////////////////////////////////////////////////////////////////////////
//! \brief Fill in the NAND parameter structure.
//!
//! This method takes the init structure and other data and fills in the
//! shared NAND parameters structure that describes all chip enables. It is
//! assumed that the pNANDParams member of this instance has already been set
//! to point at the global parameters struct, and that the NAND type has
//! been appropriately determined. This method only needs to be called once
//! for all chip enables.
//!
//! \retval SUCCESS Parameters structure is filled in.
//! \retval ERROR_DDI_NAND_HAL_NANDTYPE_MISMATCH This current chip enable is
//!      a different NAND type than previous chip enables.
////////////////////////////////////////////////////////////////////////////////
RtStatus_t InitNand::setupNandParameters()
{
    // Store read ID values.
    pNANDParams->manufacturerCode = g_nandHalContext.readIdResponse.MakerCode;
    pNANDParams->deviceCode = g_nandHalContext.readIdResponse.DeviceCode;
    
    // Copy the NandType In
    pNANDParams->NandType = m_mapEntry->pNandDescriptorSubStruct->NandType;
    pNANDParams->cellType = m_mapEntry->pNandDescriptorSubStruct->cellType;
    
    // Set default bad block percentage.
    pNANDParams->maxBadBlockPercentage = kDefaultMaxBadBlockPercentage;

    // Copy the Block Descriptor In
    pNANDParams->wPagesPerBlock = m_mapEntry->pNandDescriptorSubStruct->pagesPerBlock;
    pNANDParams->pageToBlockShift = count0Bits(pNANDParams->wPagesPerBlock);
    pNANDParams->pageInBlockMask = (1 << pNANDParams->pageToBlockShift) - 1;

    // Copy the Sector Descriptor In
    pNANDParams->pageTotalSize = m_mapEntry->pNandDescriptorSubStruct->pSectorDescriptor->wTotalSize;
    pNANDParams->pageDataSize = m_mapEntry->pNandDescriptorSubStruct->pSectorDescriptor->wDataSize;
    pNANDParams->pageMetadataSize = m_mapEntry->pNandDescriptorSubStruct->pSectorDescriptor->pageMetadataSize;
    
    // Firmware page sizes start out equal other other pages.
    pNANDParams->firmwarePageTotalSize = pNANDParams->pageTotalSize;
    pNANDParams->firmwarePageDataSize = pNANDParams->pageDataSize;
    pNANDParams->firmwarePageMetadataSize = pNANDParams->pageMetadataSize;

    // Copy the Device Addressing Descriptor In
    pNANDParams->wNumRowBytes = m_mapEntry->pNandDescriptorSubStruct->rowAddressBytes;
    pNANDParams->wNumColumnBytes = m_mapEntry->pNandDescriptorSubStruct->columnAddressBytes;
    
    // Copy information about planes.
    pNANDParams->planesPerDie = m_mapEntry->pNandDescriptorSubStruct->planesPerDie;

    // Copy the ECC descriptor from the sub struct.
    pNANDParams->eccDescriptor = *m_mapEntry->pEccDescriptor;
    
    // Set initial flag values. These may be overridden in the type-specific init method.
    pNANDParams->requiresBadBlockConversion = false;
    pNANDParams->hasSmallFirmwarePages = false;
    pNANDParams->hasInternalECCEngine = false;
    pNANDParams->isONFI = m_isOnfi;
    pNANDParams->supportsDieInterleaving = false;
    pNANDParams->supportsMultiplaneWrite = false;
    pNANDParams->supportsMultiplaneErase = false;
    pNANDParams->supportsMultiplaneRead = false;
    pNANDParams->supportsCacheRead = false;
    pNANDParams->supportsCacheWrite = false;
    pNANDParams->supportsMultiplaneCacheRead = false;
    pNANDParams->supportsMultiplaneCacheWrite = false;
    pNANDParams->supportsCopyback = false;
    pNANDParams->supportsMultiplaneCopyback = false;
    
    // Save off device name table.
    g_nandHalContext.nameTable = m_mapEntry->deviceNames;

#if defined(STMP378x)
    // Allow the application to override the ECC parameters that were loaded from the NAND table.
    overrideEccParameters(&pNANDParams->eccDescriptor);
    ddi_bch_update_parameters(wChipNumber, &pNANDParams->eccDescriptor, pNANDParams->pageTotalSize);
#endif

    return SUCCESS;
}

////////////////////////////////////////////////////////////////////////////////
//! \brief Choose one of the device tables based on the ID response.
//!
//! The purpose of this function is to parse the Read ID command response and
//! select the appropriate table of device code mappings.
//!
//! \param idResponse A reference to the ID response bytes.
//! \return A pointer to the table of device mappings that best matches the ID
//!     response is returned. A NULL result means that no table was a match.
////////////////////////////////////////////////////////////////////////////////
const NandDeviceCodeMap_t * InitNand::selectDeviceCodeMap(const NandReadIdResponse_t & idResponse)
{
    const NandDeviceCodeMap_t * pNANDDeviceMap = NULL;
    
    // Test for MLC
    uint8_t manufacturer = idResponse.MakerCode & kMakerIDMask;
    if ( 0 != idResponse.CellType )
    {
        // Look for 8K page Toshiba MLC devices. The only identifyable difference between
        // the 4K and 8K page Toshiba devices with a device code of 0xd7 is the undocumented 6th
        // read ID response byte. The 4K device returns a value of 0x13 and the 8K a value of 0x54.
        // The page size field of byte 4 cannot be used because the field was redefined in the 8K
        // parts so that the value meaning "8K page" is the same as the value meaning "4K page" on
        // the 4K page devices. This test must come before the test below for Type 9 4K page devices,
        // because 8K page devices will match that test. Toshiba has verified that this is an
        // acceptable method to distinguish the two device families.
        if (manufacturer == kToshibaMakerID && idResponse.data[5] == kToshiba8KPageIDByte6)
        {
            pNANDDeviceMap = Type11DescriptorIdList;
        }
        
        // Toshiba PBA-NAND devices have a 6th byte value of 0x55. We also check to make sure the
        // "PBA-NAND" bit is set in the 5th byte.
        else if (manufacturer == kToshibaMakerID
            && idResponse.data[5] == kToshiba32nmPBANANDIDByte6
            && idResponse.TypeOfNAND == kPBANAND)    // NAND Type bit of 5th byte==1 for PBA-NAND
        {
            pNANDDeviceMap = Type16DescriptorIdList;
        }
        
        // Toshiba 24nm PBA-NAND devices have a 6th byte value of 0x56, versus 0x55 for the 32nm
        // PBA-NAND devices. We also check to make sure the "PBA-NAND" bit is set in the 5th byte.
        else if (manufacturer == kToshibaMakerID
            && idResponse.data[5] == kToshiba24nmPBANANDIDByte6
            && idResponse.TypeOfNAND == kPBANAND)    // NAND Type bit of 5th byte==1 for PBA-NAND
        {
            pNANDDeviceMap = Type16DescriptorIdList_24nm;
        }

	   	//Is this a Samsung 8K Page MLC Nand with 16 bit ECC?
		//Please note that the manufacturer asks for 24bit ECC /1KB. But the highest we can fit is 16 bit/512B.
        else if((manufacturer == kSamsungMakerID) &&
			(idResponse.ECCLevel == SAMSUNG_6BYTE_ID_ECCLEVEL_ECC24) &&
			(idResponse.PageSize == SAMSUNG_6BYTE_ID_PAGESIZE_8K))
		{
            // Then it is a Type 15 device.
            pNANDDeviceMap = Type15DescriptorIdList;
			
		}

        // Check for ECC16 Micron NAND (L73A and L74A).
        // This check must come before the check for Micron ECC12 (L63B) NAND below because they share device ID numbers.
        // We look at the 4th ID byte to distinguish between the L60 series and the L70 series.
        else if ((manufacturer == kMicronMakerID) &&
                 ((idResponse.DeviceCode == kMicron4GBperCEDeviceID && idResponse.data[3] == kMicronL73AIdByte4) ||
                  (idResponse.DeviceCode == kMicron8GBperCEDeviceID && idResponse.data[3] == kMicronL74AIdByte4) ||
                  (idResponse.DeviceCode == kMicron16GBperCEDeviceID)))
        {
            pNANDDeviceMap = BchEcc16DescriptorIdList;
        }

        // Check for ECC12 Hynix NAND.
        // We look at the 4th ID byte to distinguish some Hynix ECC12 NANDs from the similar ECC8 part.
        // For example H27UBG8T2M (ECC12) 4th byte = 0x25 whereas H27UDG8WFM (ECC8) 4th byte = 0xB6.
		else if ((manufacturer == kHynixMakerID) && 
                 ((idResponse.DeviceCode == kHynixD7DeviceID && idResponse.data[3] == HYNIX_ECC12_DEVICE_READ_ID_BYTE_4) ||
                  (idResponse.DeviceCode == kHynixD5DeviceID && idResponse.data[3] == HYNIX_ECC12_DEVICE_READ_ID_BYTE_4) ||
                  (idResponse.DeviceCode == kHynixLargeDeviceID)))
        {
            pNANDDeviceMap = BchEcc12DescriptorIdList;
        }

        // We look at the 5th ID byte to distinguish some Micron ECC12 NANDs from the similar ECC8 part.
        // For example MT29F64G08CFAAA (ECC12) 5th byte = 0x84 whereas MT29F64G08TAA (ECC8) 5th byte = 0x78.
        // We also have a special case for the Micron L63B family (256 page/block), which has unique
        // device codes but no ID fields that can easily be used to distinguish the family.
        else if ((manufacturer == kMicronMakerID)
            && ((idResponse.DeviceCode == kMicronECC12DeviceID && idResponse.data[4] == kMicronEcc12IDByte5)
                  || (idResponse.DeviceCode == kMicronECC12LargeDeviceID)
                  || (idResponse.DeviceCode == kMicron2GBperCEDeviceID)
                  || (idResponse.DeviceCode == kMicron4GBperCEDeviceID)
                  || (idResponse.DeviceCode == kMicron8GBperCEDeviceID)))
        {
            pNANDDeviceMap = BchEcc12DescriptorIdList;
        }

		//Is this a Samsung 42nm ECC8 Nand with 6byte ID?
		else if ((manufacturer == kSamsungMakerID) &&
			(idResponse.ECCLevel == SAMSUNG_6BYTE_ID_ECCLEVEL_ECC8) &&
			(idResponse.DeviceVersion == SAMSUNG_6BYTE_ID_DEVICEVERSION_40NM))
		{
            // Then it is a Type 9 device.
            pNANDDeviceMap = Type9DescriptorIdList;
			
		}
        else if (    ( (manufacturer == kSamsungMakerID) ||
                  (manufacturer == kHynixMakerID) ) && (idResponse.PageSize == kPageSize4K))
        {
            // So far, all other Samsung and Hynix 4K page devices are Type8.
            pNANDDeviceMap = Type8DescriptorIdList;
        }
        else if (((manufacturer == kToshibaMakerID) || 
                  (manufacturer == kIntelMakerID) ||
                  (manufacturer == kMicronMakerID)) && (idResponse.PageSize == kPageSize4K))
        {
            // Type 9 devices are Toshiba NANDs with 4K pages
            pNANDDeviceMap = Type9DescriptorIdList;
        }
        else
        {
            // All other MLC devices use this list
            pNANDDeviceMap = LargeMLCDescriptorIdList;
        }
    } /* if 0 != g_ReadIDDecode.CellType */
    else // SLC
    {
        if (manufacturer == kSamsungMakerID)
        {
            // Check page size on Samsung NANDs first.
            if (idResponse.PageSize == kPageSize4K)
            {
                pNANDDeviceMap = Type10DescriptorIdList;
            }            
            // Check for nand size.
            else if (idResponse.DeviceCode == kSamsung1GbDeviceID)
            {
                if (idResponse.CacheProgram == 0)
                {
                    // 128MB Samsung without cache program are type 7.
                    // The K9F1G08U0B does not support multi-plane program, so the
                    // if statement below cannot be used to identify it.
                    pNANDDeviceMap = Type7DescriptorIdList;
                }
                else
                {
                    // Smaller sizes are Type2 by default.
                    pNANDDeviceMap = Type2DescriptorIdList;
                }
            } /* if kSamsung1Gb */
            else
            {
                // Check number of simultaneously programmed pages.
                if ((idResponse.NumOfSimultProgPages > 0) && (idResponse.PlaneNumber > 0))
                {
                    // NonZero means Type7
                    pNANDDeviceMap = Type7DescriptorIdList;
                }
                else
                {
                    // Zero simultaneously programmed pages means type 2
                    pNANDDeviceMap = Type2DescriptorIdList;
                }
            } /* else if != Samsung1GB */
        }  /* if kSamsungMakerID */
        else if (manufacturer == kMicronMakerID)
        {
            // Check number of simultaneously programmed pages.
            if (idResponse.NumOfSimultProgPages > 0)
            {
                // NonZero means Type7
                pNANDDeviceMap = Type7DescriptorIdList;
            }
            else
            {
                // Zero simultaneously programmed pages means type 2
                pNANDDeviceMap = Type2DescriptorIdList;
            }
        } /* else if MicronID */
        else
        {
            // Media is Type 2
            pNANDDeviceMap = Type2DescriptorIdList;
        } /* else != kSamsungMakerID and != kMicronMakerID */
    } /* if 0 == g_ReadIDDecode.CellType */
    
    return pNANDDeviceMap;
}

////////////////////////////////////////////////////////////////////////////////
//! \brief Search the device code tables for an entry matching the current NAND.
//!
//! Uses the NAND ID that has already been read to scan the device code tables
//! looking for a match. The appropriate table to scan is selected by the
//! selectDeviceCodeMap() function.
//!
//! \retval SUCCESS
//! \retval ERROR_DDI_NAND_HAL_LOOKUP_ID_FAILED No matching entry in the device
//!     code tables was found.
//!
//! \pre The #m_idResponse member variable must have already been filled with
//!     the ID bytes from the NAND.
////////////////////////////////////////////////////////////////////////////////
RtStatus_t InitNand::determineNandType()
{
    // Select the device mapping table based on read ID results.
    const NandDeviceCodeMap_t * pNANDDeviceMap = selectDeviceCodeMap(m_idResponse);
    if (!pNANDDeviceMap)
    {
        return ERROR_DDI_NAND_HAL_LOOKUP_ID_FAILED;
    }
    
    // Extract the combined manufacturer and device code from the read ID response.
    // The manufacturer code is in the low byte, while the device code is in the next highest byte.
    uint32_t wDeviceCode = m_idResponse.MakerCode | (m_idResponse.DeviceCode << 8);

    // Scan the selected device code table for a matching entry.
    m_mapEntry = NULL;
    uint32_t iNandIdx;
    for (iNandIdx = 0;; iNandIdx++)
    {
        // Exit the loop when we reach a null descriptor.
        if (pNANDDeviceMap[iNandIdx].DeviceManufacturerCode == NULL)
        {
            break;
        }

        // If the device and manufacturer codes match the current entry, save it and
        // exit the search loop.
        if (wDeviceCode == pNANDDeviceMap[iNandIdx].DeviceManufacturerCode)
        {
            m_mapEntry = &pNANDDeviceMap[iNandIdx];
            m_timings = pNANDDeviceMap[iNandIdx].NandTimings;
            break;
        }
    }

    // Abort if not found.
    if (!m_mapEntry)
    {
        return(ERROR_DDI_NAND_HAL_LOOKUP_ID_FAILED);
    }
    
    // Perform any modifications on params from init tables.
    modifyNandParameters(m_idResponse);

    return SUCCESS;
}

////////////////////////////////////////////////////////////////////////////////
//! \brief Modify certain parameters read out of the init tables.
//!
//! There are some NAND parameters that could be different from the defined
//! NAND Init descriptors. The correct parameters are those read from
//! the READ IDs (ID1/ID2) commands.
//! The possible parameters that can be overruled are defined by the
//! NandOverruledParameters_t structure.
//!
//! \todo Get rid of this function by making the tables reflect the correct
//!     information.
////////////////////////////////////////////////////////////////////////////////
void InitNand::modifyNandParameters(const NandReadIdResponse_t & idResponse)
{
    // By default overruled parameters are initialized to the default
    // values assigned in the NAND_INIT_descriptor
    m_nandModifiedParams.wTotalInternalDice = m_mapEntry->totalInternalDice;

    // On Samsung MLC Nands, replace the TotalInternalDice with the Total Number of Planes, deduced from the
    // Plane Number reported by the Nand.
    if ((0 != idResponse.CellType) && (idResponse.MakerCode == kSamsungMakerID))
    {
        m_nandModifiedParams.wTotalInternalDice = (1 << idResponse.PlaneNumber);
    }
    
   	// On Type 9 Micron Nands change the Total internal dice based on the third ID byte
	if ((m_mapEntry->pNandDescriptorSubStruct->NandType == kNandType9) && (idResponse.MakerCode == kMicronMakerID))
	{
		m_nandModifiedParams.wTotalInternalDice = (1 << idResponse.InternalChipNumber);
	}

    m_nandModifiedParams.wBlocksPerDie = m_mapEntry->totalBlocks / m_nandModifiedParams.wTotalInternalDice;
}

////////////////////////////////////////////////////////////////////////////////
//! \brief Read NAND configuration from the ONFI parameter page.
//!
//! This function assumes that the NAND has already been determined to support
//! the ONFI specification, probably by reading the ONFI ID using the Read ID
//! command. The ONFI parameter page is read and used to configure timings
//! and select the appropriate NAND type.
//!
//! All ONFI NANDs are created as Type 6. The page size and block size don't
//! really matter, as they are read from the NAND parameters struct and not
//! tied to the NAND type.
////////////////////////////////////////////////////////////////////////////////
RtStatus_t InitNand::configureOnfiNand()
{
    // Allocate a physically contiguous instance of the param page.
    assert(sizeof(OnfiParamPage) == 256);
    auto_free<OnfiParamPage> onfiParams = os_dmi_malloc_phys_contiguous(sizeof(OnfiParamPage));
    
    // Read the param page.
    RtStatus_t status = readOnfiParameterPage(onfiParams);
    if (status != SUCCESS)
    {
        tss_logtext_Print(~0, "Failed to read ONFI parameter page: 0x%08x\n", status);
        return ERROR_DDI_NAND_HAL_LOOKUP_ID_FAILED;
    }
    
    // Select timings based on param page.
    m_timings = *(onfiParams->getFastestAsyncTimings());

    // Instantiate the new NAND object and fill it in with values from the params page.
    m_newNand = (CommonNandBase *)createNandOfType(kNandType6);
    m_newNand->pNANDParams = &g_nandHalContext.parameters;
    m_newNand->wChipNumber = wChipNumber;
    m_newNand->wTotalBlocks = onfiParams->blocksPerLun * onfiParams->lunsPerChipEnable;
    m_newNand->wTotalInternalDice = onfiParams->lunsPerChipEnable;
    m_newNand->wBlocksPerDie = onfiParams->blocksPerLun;
    m_newNand->totalPages = m_newNand->wTotalBlocks * onfiParams->pagesPerBlock;
    m_newNand->m_firstAbsoluteBlock = m_newNand->wTotalBlocks * m_newNand->wChipNumber;
    m_newNand->m_firstAbsolutePage = m_newNand->totalPages * m_newNand->wChipNumber;
    
    if (wChipNumber == 0)
    {
        status = setupOnfiNandParameters(*onfiParams);
        if (status != SUCCESS)
        {
            return status;
        }
    }
    
    return SUCCESS;
}

////////////////////////////////////////////////////////////////////////////////
//! \brief Fill in the NAND parameter structure.
//!
//! This method takes the init structure and other data and fills in the
//! shared NAND parameters structure that describes all chip enables. It is
//! assumed that the pNANDParams member of this instance has already been set
//! to point at the global parameters struct, and that the NAND type has
//! been appropriately determined. This method only needs to be called once
//! for all chip enables.
//!
//! \retval SUCCESS Parameters structure is filled in.
//! \retval ERROR_DDI_NAND_HAL_NANDTYPE_MISMATCH This current chip enable is
//!      a different NAND type than previous chip enables.
////////////////////////////////////////////////////////////////////////////////
RtStatus_t InitNand::setupOnfiNandParameters(const OnfiParamPage & onfiParams)
{
    // Store read ID values.
    pNANDParams->manufacturerCode = g_nandHalContext.readIdResponse.MakerCode;
    pNANDParams->deviceCode = g_nandHalContext.readIdResponse.DeviceCode;
    
    // Copy the NandType In
    pNANDParams->NandType = kNandType6;
    pNANDParams->cellType = (onfiParams.bitsPerCell == 1 ? kNandSLC : kNandMLC);
    
    // Calculate the bad block percentage, rounding up.
    pNANDParams->maxBadBlockPercentage = (onfiParams.maxBadBlocksPerLun * 100 + onfiParams.blocksPerLun - 1) / onfiParams.blocksPerLun;

    // Copy the Block Descriptor In
    pNANDParams->wPagesPerBlock = onfiParams.pagesPerBlock;
    pNANDParams->pageToBlockShift = count0Bits(pNANDParams->wPagesPerBlock);
    pNANDParams->pageInBlockMask = (1 << pNANDParams->pageToBlockShift) - 1;

    // Copy the Sector Descriptor In
    pNANDParams->pageTotalSize = onfiParams.dataBytesPerPage + onfiParams.spareBytesPerPage;
    pNANDParams->pageDataSize = onfiParams.dataBytesPerPage;
    pNANDParams->pageMetadataSize = onfiParams.spareBytesPerPage;
    
    // Firmware page sizes start out equal other other pages.
    pNANDParams->firmwarePageTotalSize = pNANDParams->pageTotalSize;
    pNANDParams->firmwarePageDataSize = pNANDParams->pageDataSize;
    pNANDParams->firmwarePageMetadataSize = pNANDParams->pageMetadataSize;

    // Copy the Device Addressing Descriptor In
    pNANDParams->wNumRowBytes = onfiParams.addressCycles.row;
    pNANDParams->wNumColumnBytes = onfiParams.addressCycles.column;
    
    // Copy information about planes.
    pNANDParams->planesPerDie = 1 << onfiParams.interleavedAddressBits;

    // Copy the ECC descriptor from the sub struct.
    RtStatus_t status = determineOnfiEccType(onfiParams);
    if (status != SUCCESS)
    {
        return status;
    }
    
    // Set initial flag values. These may be overridden in the type-specific init method.
    pNANDParams->requiresBadBlockConversion = false;
    pNANDParams->hasSmallFirmwarePages = false;
    pNANDParams->hasInternalECCEngine = false;
    pNANDParams->isONFI = true;
    pNANDParams->supportsDieInterleaving = onfiParams.featuresSupported.multiLunOperations;
    pNANDParams->supportsMultiplaneWrite = onfiParams.featuresSupported.interleavedWrite;
    pNANDParams->supportsMultiplaneErase = onfiParams.featuresSupported.interleavedWrite;
    pNANDParams->supportsMultiplaneRead = onfiParams.featuresSupported.interleavedRead;
    pNANDParams->supportsCacheRead = onfiParams.optionalCommandsSupported.readCacheCommands;
    pNANDParams->supportsCacheWrite = onfiParams.optionalCommandsSupported.programPageCacheMode;
    pNANDParams->supportsMultiplaneCacheRead = onfiParams.interleavedOperationAttributes.readCacheSupported;
    pNANDParams->supportsMultiplaneCacheWrite = onfiParams.interleavedOperationAttributes.programCacheSupported;
    pNANDParams->supportsCopyback = onfiParams.optionalCommandsSupported.copyback;
    pNANDParams->supportsMultiplaneCopyback = onfiParams.optionalCommandsSupported.copyback && onfiParams.featuresSupported.interleavedWrite;

#if defined(STMP378x)
    // Allow the application to override the ECC parameters that were loaded from the NAND table.
    overrideEccParameters(&pNANDParams->eccDescriptor);
    ddi_bch_update_parameters(wChipNumber, &pNANDParams->eccDescriptor, pNANDParams->pageTotalSize);
#endif

    return SUCCESS;
}

////////////////////////////////////////////////////////////////////////////////
//! \brief Figure what ECC type to use based on ONFI parameters.
//!
//! The #NandParameters_t::eccDescriptor field in the shared NAND parameters
//! structure is filled in based on the values in \a onfiParams. On the 3780,
//! the highest level of BCH ECC that will fit within the NAND's page size
//! will be used.
//!
//! On other chips that don't have BCH, the choice is between Reed-Solomon 4-bit
//! and 8-bit ECC, with their fixed page sizes. If the page data size is greater 
//! or equal to 4KB then RS8 is used. Otherwise RS4 is used. The size of the
//! page metadata area is checked to ensure that there is actually enough room
//! for the ECC parity bytes and user metadata bytes.
//!
//! \retval SUCCESS The ECC descriptor in the shared NAND parameters struct
//!     has been filled in with an appropriate ECC type.
//! \retval ERROR_GENERIC No fitting ECC type could be found.
////////////////////////////////////////////////////////////////////////////////
RtStatus_t InitNand::determineOnfiEccType(const OnfiParamPage & onfiParams)
{
#if defined(STMP378x)
    // Calculate the highest BCH level that will fit in the page.
    ddi_bch_calculate_highest_level(onfiParams.dataBytesPerPage, onfiParams.spareBytesPerPage, &pNANDParams->eccDescriptor);
#else
    // Must choose between RS4 and RS8 based on page size.
    NandEccType_t eccType;
    if (onfiParams.dataBytesPerPage >= XL_SECTOR_DATA_SIZE)
    {
        // If the page size is larger than 4KB then data will be wasted in each page. It's possible
        // to rectify this situation by storing multiple ECC4/8 pages in each physical page, but
        // that is not currently implemented.
        eccType = kNandEccType_RS8;
        
        // Make sure there is enough spare area to hold the required metadata.
        if (onfiParams.spareBytesPerPage < XL_SECTOR_REDUNDANT_SIZE)
        {
            return ERROR_GENERIC;
        }
        
        // Must force the page size to be exactly what the ECC8 engine expects.
        pNANDParams->pageTotalSize = XL_SECTOR_TOTAL_SIZE;
        pNANDParams->pageDataSize = XL_SECTOR_DATA_SIZE;
        pNANDParams->pageMetadataSize = XL_SECTOR_REDUNDANT_SIZE;
        
        // Firmware page sizes start out equal other other pages.
        pNANDParams->firmwarePageTotalSize = pNANDParams->pageTotalSize;
        pNANDParams->firmwarePageDataSize = pNANDParams->pageDataSize;
        pNANDParams->firmwarePageMetadataSize = pNANDParams->pageMetadataSize;
    }
    else if (onfiParams.dataBytesPerPage >= LARGE_SECTOR_DATA_SIZE)
    {
        eccType = kNandEccType_RS4;
        
        // Make sure there is enough spare area to hold the required metadata.
        if (onfiParams.spareBytesPerPage < LARGE_SECTOR_REDUNDANT_SIZE)
        {
            return ERROR_GENERIC;
        }
        
        // Must force the page size to be exactly what the ECC8 engine expects.
        pNANDParams->pageTotalSize = LARGE_SECTOR_TOTAL_SIZE;
        pNANDParams->pageDataSize = LARGE_SECTOR_DATA_SIZE;
        pNANDParams->pageMetadataSize = LARGE_SECTOR_REDUNDANT_SIZE;
        
        // Firmware page sizes start out equal other other pages.
        pNANDParams->firmwarePageTotalSize = pNANDParams->pageTotalSize;
        pNANDParams->firmwarePageDataSize = pNANDParams->pageDataSize;
        pNANDParams->firmwarePageMetadataSize = pNANDParams->pageMetadataSize;
    }
    else
    {
        // The page is too small.
        return ERROR_GENERIC;
    }
    pNANDParams->eccDescriptor.eccType = eccType;
#endif // defined(STMP378x)

    return SUCCESS;
}

////////////////////////////////////////////////////////////////////////////////
//! \return A new instance of the NandPhysicalMedia subclass that implements
//!     support for the NAND type specified by \a nandType. If \a nandType
//!     is not a valid value then NULL is returned.
////////////////////////////////////////////////////////////////////////////////
CommonNandBase * CommonNandBase::createNandOfType(NandType_t nandType)
{
    switch (nandType)
    {
        case kNandType2:
            return new Type2Nand;
            
        case kNandType5:
            return new Type5Nand;
            
        case kNandType6:
            return new Type6Nand;
            
        case kNandType7:
            return new Type7Nand;
            
        case kNandType8:
            return new Type8Nand;
            
        case kNandType9:
            return new Type9Nand;
            
        case kNandType10:
            return new Type10Nand;
            
        case kNandType11:
            return new Type11Nand;
            
        case kNandType12:
            return new Type12Nand;
            
        case kNandType13:
            return new Type13Nand;
            
        case kNandType14:
            return new Type14Nand;
            
        case kNandType15:
            return new Type15Nand;
        
// Disabled to get 3710 apps to build.
#if !defined(STMP37xx)
        case kNandType16:
            return new Type16Nand;

        case kNandType17:
            return new Type17Nand;

        case kNandType18:
            return new Type18Nand;
#endif //!37xx
            
        default:
            SystemHalt();
            return NULL;
    }
}

////////////////////////////////////////////////////////////////////////////////
//! \brief Initialize the NAND DMA chains.
//!
//! Much of the DMA chain can be pre-initialized to speed up subsequent events.
////////////////////////////////////////////////////////////////////////////////
void CommonNandBase::initDma()
{
    unsigned u32NumAddressBytes = pNANDParams->wNumRowBytes + pNANDParams->wNumColumnBytes;
    const EccTypeInfo_t * eccInfo = pNANDParams->eccDescriptor.getTypeInfo();
    assert(eccInfo);
    
    // Init ECC page read DMA.
    uint32_t dataCount;
    uint32_t auxCount;
    uint32_t eccMask = pNANDParams->eccDescriptor.computeMask(
        pNANDParams->pageTotalSize,   // readSize
        pNANDParams->pageTotalSize, // pageTotalSize
        kEccOperationRead,
        kEccTransferFullPage,
        &dataCount,
        &auxCount);
    
    g_nandHalContext.readDma.init(
        0,  // chip enable
        eNandProgCmdRead1,
        NULL, // addressBytes
        u32NumAddressBytes,  // addressByteCount
        eNandProgCmdRead1_2ndCycle,
        NULL,  //dataBuffer
        NULL,   //auxBuffer
        dataCount + auxCount,  //readSize
        pNANDParams->eccDescriptor,    //ecc
        eccMask);   //eccMask
    
    // Init ECC metadata read DMA.
    
    // Get the offset and length of the metadata for this page size and ECC type.
    //retval = 
    uint32_t metadataReadOffset;
    uint32_t metadataReadSize;
    eccInfo->getMetadataInfo(pNANDParams->pageDataSize, &metadataReadOffset, &metadataReadSize);
    eccMask = pNANDParams->eccDescriptor.computeMask(
        metadataReadSize,   // readSize
        pNANDParams->pageTotalSize, // pageTotalSize
        kEccOperationRead,
        kEccTransferFullPage,
        &dataCount,
        &auxCount);

    g_nandHalContext.readMetadataDma.init(
        0,  // chip enable
        eNandProgCmdRead1,
        NULL, // addressBytes
        u32NumAddressBytes,  // addressByteCount
        eNandProgCmdRead1_2ndCycle,
        NULL,  //dataBuffer
        NULL,   //auxBuffer
        dataCount + auxCount,  //readSize
        pNANDParams->eccDescriptor,    //ecc
        eccMask);   //eccMask
    
    // Init firmware page read DMA if we're using 2k firmware pages.
    if (pNANDParams->hasSmallFirmwarePages)
    {
        eccMask = pNANDParams->eccDescriptor.computeMask(
            pNANDParams->firmwarePageTotalSize,   // readSize
            pNANDParams->firmwarePageTotalSize, // pageTotalSize
            kEccOperationRead,
            kEccTransfer2kPage,
            &dataCount,
            &auxCount);
        
        g_nandHalContext.readFirmwareDma.init(
            0,  // chip enable
            eNandProgCmdRead1,
            NULL, // addressBytes
            u32NumAddressBytes,  // addressByteCount
            eNandProgCmdRead1_2ndCycle,
            NULL,  //dataBuffer
            NULL,   //auxBuffer
            dataCount + auxCount,  //readSize
            pNANDParams->eccDescriptor,    //ecc
            eccMask);   //eccMask
    }
    
    // Init ECC page write DMA.
    eccMask = pNANDParams->eccDescriptor.computeMask(
        pNANDParams->pageTotalSize,
        pNANDParams->pageTotalSize,
        kEccOperationWrite,
        kEccTransferFullPage,
        &dataCount,
        &auxCount);
    
    g_nandHalContext.writeDma.init(
        0, // chipSelect,
        eNandProgCmdSerialDataInput, // command1,
        NULL, // addressBytes,
        u32NumAddressBytes, // addressByteCount,
        eNandProgCmdPageProgram, // command2,
        NULL, // dataBuffer,
        NULL, // auxBuffer,
        dataCount + auxCount, // sendSize,
        dataCount, // dataSize,
        auxCount, // leftoverSize,
        pNANDParams->eccDescriptor,
        eccMask);
    
    // Init status read DMA.
    g_nandHalContext.statusDma.init(0, eNandProgCmdReadStatus, g_nandHalResultBuffer);
    
    // Chain the status read onto the page write.
    g_nandHalContext.writeDma >> g_nandHalContext.statusDma;
}

                                                    
//! For all NAND types, we use 2K firmware pages when using BCH ECC. This is because the BCH
//! engine does not round the start of each 512 byte chunk up to the next byte. So for all
//! but a few ECC levels, there is no way for the ROM to get to the second or Nth 2K subpage.
//! Instead of special casing the ECC levels that do align to bytes, we simply use 2K firmware
//! pages whenever BCH is enabled.
//!
//! All NANDs require bad block conversion as long as they have ECC enabled.
RtStatus_t CommonNandBase::init()
{
#if defined(STMP378x)
    m_pMetadataBuffer = stc_pMetadataBuffer;
#endif

    // Init DMA and NAND parameters if this is the first chip.
    if (wChipNumber == 0)
    {
        // All BCH configurations use 2K firmware pages. However, we only override the firmware
        // page size if the natural page data size is larger than 2048 bytes. There's no point
        // in overriding if the page is already 2K.
        pNANDParams->hasSmallFirmwarePages = pNANDParams->eccDescriptor.isBCH() && pNANDParams->pageDataSize > LARGE_SECTOR_DATA_SIZE;
        if (pNANDParams->hasSmallFirmwarePages)
        {
            // Set firmware pages to 2112 bytes.
            pNANDParams->firmwarePageTotalSize = LARGE_SECTOR_TOTAL_SIZE;
            pNANDParams->firmwarePageDataSize = LARGE_SECTOR_DATA_SIZE;
            pNANDParams->firmwarePageMetadataSize = LARGE_SECTOR_REDUNDANT_SIZE;
        }
        
        // Bad blocks must be converted if we're using ECC.
        pNANDParams->requiresBadBlockConversion = pNANDParams->eccDescriptor.isEnabled();

        // Init shared DMA descriptors
        initDma();
    }

    return SUCCESS;
}

RtStatus_t CommonNandBase::cleanup()
{
#if defined(STMP378x)
    // Forget about the metadata buffer.
    m_pMetadataBuffer = NULL;
#endif
    
    return SUCCESS;
}


#if defined(STMP378x)
////////////////////////////////////////////////////////////////////////////////
//! \brief Override BCH ECC Parameters.
//!
//! If the application has specified an override callback function, call
//! it and then update the BCH ECC parameters.
//!
//! \param[in,out] pEccDescriptor ECC descriptor to modify.
//! \retval     None.
////////////////////////////////////////////////////////////////////////////////
void InitNand::overrideEccParameters(NandEccDescriptor_t *pEccDescriptor)
{
    const nand_bch_Parameters_t *pBchParameters;

    assert(pEccDescriptor);

    if (g_pEccOverrideCallback)
    {
        pBchParameters = g_pEccOverrideCallback();

        // Update NAND parameters with values provided by override function.
        if (pBchParameters &&
            (pBchParameters->u32Block0Level <= NAND_MAX_BCH_ECC_LEVEL) &&
            (pBchParameters->u32BlockNLevel <= NAND_MAX_BCH_ECC_LEVEL))
        {
            pEccDescriptor->eccType             = ddi_bch_GetType(pBchParameters->u32BlockNLevel);
            pEccDescriptor->eccTypeBlock0       = ddi_bch_GetType(pBchParameters->u32Block0Level);
            pEccDescriptor->u32SizeBlockN       = pBchParameters->u32BlockNSize;
            pEccDescriptor->u32SizeBlock0       = pBchParameters->u32Block0Size;
            pEccDescriptor->u32NumEccBlocksN    = pBchParameters->u32BlockNCount;
            pEccDescriptor->u32MetadataBytes    = pBchParameters->u32MetadataBytes;
            pEccDescriptor->u32EraseThreshold   = pBchParameters->u32EraseThreshold;
        }
    }
}

////////////////////////////////////////////////////////////////////////////////
// See ddi_nand_ecc_override.h for documentation.
////////////////////////////////////////////////////////////////////////////////
void ddi_nand_set_ecc_override_callback(NandEccOverrideCallback_t pCallback)
{
    g_pEccOverrideCallback = pCallback;
}
#elif !defined(STMP37xx) && !defined(STMP377x)
	#error Must define STMP37xx, STMP377x or STMP378x
#endif

////////////////////////////////////////////////////////////////////////////////
// End of file
////////////////////////////////////////////////////////////////////////////////
//! @}






