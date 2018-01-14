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
#if !defined(__Taus88_h__)
#define __Taus88_h__

#include "types.h"

/////////////////////////////////////////////////////////////////////////////////
// Declarations
////////////////////////////////////////////////////////////////////////////////

/*!
 * \brief Three-component combined Tausworthe generator by Pierre L'Ecuyer.
 *
 * No copyright for this code was claimed in the paper from which it
 * was extracted.
 *
 * Reference:
 * Pierre L'Ecuyer, "Maximally equidistributed combined Tausworthe generators",
 *   Math. of Comput., 1996, vol 65, pp 203-213.
 */
class Taus88
{
public:
    enum { kDefaultSeed = 314159265 };
    
    Taus88(uint32_t seed=kDefaultSeed);
    
    void setSeed(uint32_t seed);
    
    uint32_t next();
    uint32_t next(uint32_t max);

protected:
    uint32_t m_seed1;
    uint32_t m_seed2;
    uint32_t m_seed3;
};

#endif // __Taus88_h__
////////////////////////////////////////////////////////////////////////////////
// End of file
////////////////////////////////////////////////////////////////////////////////
