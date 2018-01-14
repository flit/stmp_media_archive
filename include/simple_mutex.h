///////////////////////////////////////////////////////////////////////////////
// Copyright (c) Freescale Semiconductor. All rights reserved.
//
// Freescale Semiconductor
// Proprietary & Confidential
//
// This source code and the algorithms implemented therein constitute
// confidential information and may comprise trade secrets of Freescale Semiconductor.
// or its associates, and any use thereof is subject to the terms and
// conditions of the Confidential Disclosure Agreement pursual to which this
// source code was originally received.
///////////////////////////////////////////////////////////////////////////////
//! \file simple_mutex.h
//! \brief Defines a mutex helper class.
#ifndef _simple_mutex_h_
#define _simple_mutex_h_

#include "os/threadx/tx_api.h"

///////////////////////////////////////////////////////////////////////////////
// Types
///////////////////////////////////////////////////////////////////////////////

/*!
 * \brief Mutex helper class.
 *
 * Automatically gets the mutex in the constructor and puts the mutex in the destructor.
 * To use, allocate an instance on the stack so that it will put the mutex when it
 * falls out of scope.
 */
class SimpleMutex
{
public:
    //! \brief Constructor. Gets mutex. Takes a mutex pointer.
    inline SimpleMutex(TX_MUTEX * pIncMutex)
    :   m_mutex(pIncMutex)
    {
        if (tx_mutex_get(m_mutex, TX_WAIT_FOREVER) != TX_SUCCESS)
        {
            assert(0);
        }
    };
    
    //! \brief Constructor. Gets mutex. Takes a mutex reference.
    inline SimpleMutex(TX_MUTEX & incMutex)
    :   m_mutex(&incMutex)
    {
        if (tx_mutex_get(m_mutex, TX_WAIT_FOREVER) != TX_SUCCESS)
        {
            assert(0);
        }
    };

    //! \brief Destructor. Puts mutex.
    inline ~SimpleMutex()
    {
        if (tx_mutex_put(m_mutex) != TX_SUCCESS)
        {
            assert(0);
        }
    }

protected:
    //! \brief Pointer to the mutex object.
    TX_MUTEX * m_mutex;
};


#endif //_simple_mutex_h_
///////////////////////////////////////////////////////////////////////////////
// EOF
///////////////////////////////////////////////////////////////////////////////

