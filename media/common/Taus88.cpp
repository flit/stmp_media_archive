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
//! \file
//! \brief Random number generator algorithm.
////////////////////////////////////////////////////////////////////////////////

#include "Taus88.h"

#define TAUS88_MASK   0xffffffffUL /* required on 64 bit machines */

/////////////////////////////////////////////////////////////////////////////////
// Code
////////////////////////////////////////////////////////////////////////////////

Taus88::Taus88(uint32_t seed)
{
    setSeed(seed);
}

void Taus88::setSeed(uint32_t seed)
{
    m_seed1 = seed;
    m_seed2 = seed - 2;
    m_seed3 = seed - 4;
    
    // Ensure the seeds are above their minimum values.
    if (m_seed1 < 2)
    {
        m_seed1 = 2 + seed;
    }
    if (m_seed2 < 8)
    {
        m_seed2 = 8 + seed;
    }
    if (m_seed2 < 16)
    {
        m_seed2 = 16 + seed;
    }
}

uint32_t Taus88::next()
{
    register uint32_t b;
    
    // Localize the members.
    register uint32_t seed1 = m_seed1;
    register uint32_t seed2 = m_seed2;
    register uint32_t seed3 = m_seed3;

    b = (((seed1 << 13) ^ seed1) >> 19);
    seed1 = ((((seed1 & 4294967294UL) << 12) & TAUS88_MASK) ^ b);
    b = ((( seed2 << 2) ^ seed2) >> 25);
    seed2 = ((((seed2 & 4294967288UL) << 4) & TAUS88_MASK) ^ b);
    b = (((seed3 << 3) ^ seed3) >> 11);
    seed3 = ((((seed3 & 4294967280UL) << 17) & TAUS88_MASK) ^ b);

    // Copy back the localized values.
    m_seed1 = seed1;
    m_seed2 = seed2;
    m_seed3 = seed3;

    return (seed1 ^ seed2 ^ seed3);
}

uint32_t Taus88::next(uint32_t max)
{
    return next() % max;
}

////////////////////////////////////////////////////////////////////////////////
// End of file
////////////////////////////////////////////////////////////////////////////////
