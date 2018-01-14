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
//! \brief Definition of the NAND driver's mutex lock helper class.
////////////////////////////////////////////////////////////////////////////////
#if !defined(__ddi_nand_locker_h__)
#define __ddi_nand_locker_h__

#include "os/threadx/tx_api.h"
#include "simple_mutex.h"

///////////////////////////////////////////////////////////////////////////////
// Globals
///////////////////////////////////////////////////////////////////////////////
         
extern TX_MUTEX g_NANDThreadSafeMutex;

///////////////////////////////////////////////////////////////////////////////
// Classes
///////////////////////////////////////////////////////////////////////////////

namespace nand {

/*!
 * \brief Utility class to automatically lock and unlock the NAND driver.
 */
class DdiNandLocker : public SimpleMutex
{
public:
    //! \brief Locks the mutex that serialises access to the NAND driver.
    DdiNandLocker()
    :   SimpleMutex(g_NANDThreadSafeMutex)
    {
    }
    
    //! \brief Unlocks the NAND drive mutex.
    //!
    //! Before the mutex is unlocked it is prioritised, which makes sure that
    //! the highest priority thread that is blocked on the mutex will be the
    //! next in line to hold it.
    ~DdiNandLocker()
    {
        tx_mutex_prioritize(m_mutex);
    }
};

} // namespace nand

#endif // __ddi_nand_locker_h__
////////////////////////////////////////////////////////////////////////////////
// End of file
////////////////////////////////////////////////////////////////////////////////

