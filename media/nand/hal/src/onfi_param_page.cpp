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
//! \defgroup ddi_media_nand_hal_internals
//! @{
//! \file   onfi_param_page.h
//! \brief  Definition of the ONFI parameter page structure.
////////////////////////////////////////////////////////////////////////////////

#include "onfi_param_page.h"

///////////////////////////////////////////////////////////////////////////////
// Constants
///////////////////////////////////////////////////////////////////////////////

//! Important timing parameters for each mode.
//! <table>
//! <tr><td></td><th>Mode 0</th><th>Mode 1</th><th>Mode 2</th><th>Mode 3</th><th>Mode 4</th><th>Mode 5</th></tr>
//! <tr><th>tWC</th>  <td>100</td> <td>45</td> <td>35</td> <td>30</td> <td>25</td> <td>20</td></tr>
//! <tr><th>tRC</th>  <td>100</td> <td>50</td> <td>35</td> <td>30</td> <td>25</td> <td>20</td></tr>
//! <tr><th>tCLS</th> <td>50</td>  <td>25</td> <td>15</td> <td>10</td> <td>10</td> <td>10</td></tr>
//! <tr><th>tALS</th> <td>50</td>  <td>25</td> <td>15</td> <td>10</td> <td>10</td> <td>10</td></tr>
//! <tr><th>tWP</th>  <td>50</td>  <td>25</td> <td>17</td> <td>15</td> <td>12</td> <td>10</td></tr>
//! <tr><th>tDS</th>  <td>40</td>  <td>20</td> <td>15</td> <td>10</td> <td>10</td> <td>7</td></tr>
//! <tr><th>tWH</th>  <td>30</td>  <td>15</td> <td>15</td> <td>10</td> <td>10</td> <td>7</td></tr>
//! <tr><th>tDH</th>  <td>20</td>  <td>10</td> <td>5</td>  <td>5</td>  <td>5</td>  <td>5</td></tr>
//! <tr><th>tREA</th> <td>40</td>  <td>30</td> <td>25</td> <td>20</td> <td>20</td> <td>16</td></tr>
//! <tr><th>tRLOH</th><td>0</td>   <td>0</td>  <td>0</td>  <td>0</td>  <td>5</td>  <td>5</td></tr>
//! <tr><th>tRHOH</th><td>0</td>   <td>15</td> <td>15</td> <td>15</td> <td>15</td> <td>15</td></tr>
//! </table>
//!
//! Calculations used to compute actual timing parameters listed below.
//! - tSU = MAX(tCLS, tALS)
//! - tDSx = MAX(tWP, tDS)
//! - tDHx = MAX(tWH, tDH)
//! - tCYCLE = tDSx + tDH
//!
//! Finally, tDSx + tDHx must be >= MAX(tRC, tWC). If this is not true, tDSx and/or tDHx must be
//! incremented until it is. Usually, tDSx is increased before tDHx.
const NAND_Timing2_struct_t kOnfiAsyncTimingModeTimings[] = {
        // ONFI asynchronous timing mode 0 (100 ns).
        MK_NAND_TIMINGS_DYNAMIC(
            50,     // tSU
            6,      // dsample
            60,     // tDSx
            40,     // tDHx
            40,     // tREA
            0,      // tRLOH
            0 ),    // tRHOH
        
        // ONFI asynchronous timing mode 1 (50 ns).
        MK_NAND_TIMINGS_DYNAMIC(
            25,     // tSU
            6,      // dsample
            30,     // tDSx
            20,     // tDHx
            30,     // tREA
            0,      // tRLOH
            15 ),    // tRHOH
        
        // ONFI asynchronous timing mode 2 (35 ns).
        MK_NAND_TIMINGS_DYNAMIC(
            15,     // tSU
            6,      // dsample
            20,     // tDSx
            15,     // tDHx
            25,     // tREA
            0,      // tRLOH
            15 ),    // tRHOH
        
        // ONFI asynchronous timing mode 3 (30 ns).
        MK_NAND_TIMINGS_DYNAMIC(
            10,     // tSU
            6,      // dsample
            18,     // tDSx
            12,     // tDH
            20,     // tREA
            0,      // tRLOH
            15 ),    // tRHOH
        
        // ONFI asynchronous timing mode 4 (25 ns).
        MK_NAND_TIMINGS_DYNAMIC(
            10,     // tSU
            6,      // dsample
            14,     // tDSx
            11,     // tDHx
            20,     // tREA
            5,      // tRLOH
            15 ),    // tRHOH
        
        // ONFI asynchronous timing mode 5 (20 ns).
        MK_NAND_TIMINGS_DYNAMIC(
            10,     // tSU
            6,      // dsample
            12,     // tDSx
            8,     // tDHx
            16,     // tREA
            5,      // tRLOH
            15 ),    // tRHOH
    };

///////////////////////////////////////////////////////////////////////////////
// Code
///////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
//! Strings in the ONFI parameter page have trailing space characters (0x20)
//! and no NULL terminator. This utility function copies such a string to a
//! destination buffer. It also inserts a NULL terminator such that any trailing
//! spaces are removed.
//!
//! \param dest Buffer to which the \a src string will be copied. Must be at least
//!     (\a maxSrcLen + 1) bytes in size, to accomodate the NULL terminator.
//! \param src The source string from the parameter page.
//! \param maxSrcLen The maximum possible length of the source string.
//! \return The truncated length of the string.
////////////////////////////////////////////////////////////////////////////////
unsigned OnfiParamPage::copyOnfiString(char * dest, const char * src, unsigned maxSrcLen)
{
    // Search backwards to find last unique character.
    unsigned actualSrcLen = maxSrcLen;
    while (actualSrcLen > 0 && src[actualSrcLen - 1] == ' ')
    {
        --actualSrcLen;
    }
    
    // Now copy the string forward into the dest buffer.
    unsigned i = 0;
    while (i < actualSrcLen)
    {
        dest[i] = src[i];
        ++i;
    }
    
    // Null terminate the dest string.
    dest[i] = 0;
    
    return actualSrcLen;
}

////////////////////////////////////////////////////////////////////////////////
//! \brief Determine the highest supported timing mode.
//!
//! The #OnfiParamPage::timingModeSupport bitfield of the provided ONFI
//! parameter page is examined to determine the fastest supported timing mode
//! of the NAND. This timings for this mode are returned as a pointer to
//! the respective timing mode structure.
//!
//! \param onfiParams Reference to the ONFI parameter page for the NAND.
//! \return Pointer to timings for the fastest supported timing mode.
////////////////////////////////////////////////////////////////////////////////
const NAND_Timing2_struct_t * OnfiParamPage::getFastestAsyncTimings() const
{
    unsigned timingMode;
    if (timingModeSupport.timingMode5)
    {
        timingMode = 5;
    }
    else if (timingModeSupport.timingMode4)
    {
        timingMode = 4;
    }
    else if (timingModeSupport.timingMode3)
    {
        timingMode = 3;
    }
    else if (timingModeSupport.timingMode2)
    {
        timingMode = 2;
    }
    else if (timingModeSupport.timingMode1)
    {
        timingMode = 1;
    }
    else
    {
        // Timing mode 0 is always supported.
        timingMode = 0;
    }
    
    assert(timingMode <= 5);
    return &kOnfiAsyncTimingModeTimings[timingMode];
}

////////////////////////////////////////////////////////////////////////////////
//! This helper function calculates the CRC-16 value over the actual bytes
//! in the parameter page. It uses a bit by bit algorithm without augmented zero bytes.
//! 
//! From the ONFI 2.2 specification:
//! The Integrity CRC (Cyclic Redundancy Check) field is used to verify that the contents of the 
//! parameter page were transferred correctly to the host.  The CRC of the parameter page is a word 
//! (16-bit) field.  The CRC calculation covers all of data between byte 0 and byte 253 of the 
//! parameter page inclusive.   
//! 
//! The CRC shall be calculated on byte (8-bit) quantities starting with byte 0 in the parameter page. 
//! The bits in the 8-bit quantity are processed from the most significant bit (bit 7) to the least 
//! significant bit (bit 0).   
//!  
//! The CRC shall be calculated using the following 16-bit generator polynomial:   
//!  G(X) = X16 + X15 + X2 + 1 
//! This polynomial in hex may be represented as 8005h. 
//!  
//! The CRC value shall be initialized with a value of 4F4Eh before the calculation begins.  There is 
//! no XOR applied to the final CRC value after it is calculated.  There is no reversal of the data 
//! bytes or the CRC calculated value. 
////////////////////////////////////////////////////////////////////////////////
uint16_t OnfiParamPage::computeCRC()
{
    const uint16_t crcinit = 0x4F4E; // Initial CRC value in the shift register
    const int order = 16;                                 // Order of the CRC-16
    const uint16_t polynom = 0x8005;                 // Polynomial
    const uint16_t crcmask = ((((uint16_t)1 << (order - 1)) - 1) << 1) | 1;
    const uint16_t crchighbit = (uint16_t)1 << (order - 1);

    // Taking this address requires this struct to remain a POD type.
    uint8_t * bytes = (uint8_t *)this;
    uint16_t crc = crcinit; // Initialize the shift register with 0x4F4E
    
    // Scan over bytes 0-253 of the param page.
    uint16_t i;
    for (i = 0; i < 254; ++i)
    {
        uint16_t c = *bytes++;
        
        // Bits processed from MSB to LSB.
        uint16_t j;
        for (j = 0x80; j; j >>= 1)
        {
            uint16_t bit = crc & crchighbit;
            crc <<= 1;
            if (c & j)
            {
                bit ^= crchighbit;
            }
            if (bit)
            {
                crc ^= polynom;
            }
        }
        crc &= crcmask;
    }
    
    return crc;
}

//! @}
///////////////////////////////////////////////////////////////////////////////
// EOF
///////////////////////////////////////////////////////////////////////////////
