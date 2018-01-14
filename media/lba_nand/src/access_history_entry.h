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
//! \file   access_history_entry.h
//! \brief  Definition of the AccessHistoryEntry class.
////////////////////////////////////////////////////////////////////////////////
#if !defined(__access_history_entry_h__)
#define __access_history_entry_h__

#include "types.h"
#include "drivers/media/include/ddi_media_timers.h"
#include "os/thi/os_thi_api.h"

///////////////////////////////////////////////////////////////////////////////
// Definitions
///////////////////////////////////////////////////////////////////////////////

/*!
 * \brief Entry in the read/write history.
 */
struct AccessHistoryEntry
{
    typedef enum {
        kNone,
        kRead,
        kWrite,
        kFlush
    } Operation_t;
    
    enum
    {
        kMaxTasks = 8
    };
    
    Operation_t m_op;
    uint32_t m_sector;
    uint32_t m_count;
    AverageTime m_time;
    TX_THREAD * m_thread;
    uint16_t m_partition;
    uint16_t m_taskCount;
    const char * m_tasks[kMaxTasks];
    
    //! \brief Default constructor.
    inline AccessHistoryEntry() : m_op(kNone), m_sector(0), m_count(0), m_time(), m_thread(0), m_taskCount(0) {}
    
    //! \brief Constructor.
    inline AccessHistoryEntry(unsigned mode, Operation_t op, uint32_t sector, uint32_t count)
    :   m_op(op), m_sector(sector), m_count(count), m_time(), m_thread(tx_thread_identify()), m_partition(mode)
    {
        m_taskCount = ddi_ldl_get_media_task_stack(m_tasks, kMaxTasks);
    }
    
    //! \brief Alternate constructor taking an average time for the operation.
    inline AccessHistoryEntry(unsigned mode, Operation_t op, uint32_t sector, uint32_t count, const AverageTime & avg)
    :   m_op(op), m_sector(sector), m_count(count), m_time(avg), m_thread(tx_thread_identify()), m_partition(mode)
    {
        m_taskCount = ddi_ldl_get_media_task_stack(m_tasks, kMaxTasks);
    }
};

#endif // __access_history_entry_h__
///////////////////////////////////////////////////////////////////////////////
// EOF
///////////////////////////////////////////////////////////////////////////////
