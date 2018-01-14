///////////////////////////////////////////////////////////////////////////////
// Copyright (c) Freescale Semiconductor. All rights reserved.
// 
// Freescale Semiconductor
// Proprietary & Confidential
// 
// This source code and the algorithms implemented therein constitute confidential
// information and may comprise trade secrets of Freescale Semiconductor or its
// associates, and any use thereof is subject to the terms and conditions of the
// Confidential Disclosure Agreement pursual to which this source code was
// originally received.
////////////////////////////////////////////////////////////////////////////////
//! \addtogroup ddi_media
//! @{
//! \file ddi_ldl_util.c
//! \brief Utilities used by the logical drive layer.
////////////////////////////////////////////////////////////////////////////////

#include "types.h"
#include "errordefs.h"
#include "ddi_media_internal.h"
#include "drivers/media/ddi_media.h"
#include "hw/core/vmemory.h"
#include <algorithm>

////////////////////////////////////////////////////////////////////////////////
// Code
////////////////////////////////////////////////////////////////////////////////

#if DEBUG && DDI_LDL_ENABLE_MEDIA_TASKS

// All of this code needs to be in static memory since it is invoked from other static code.
#pragma ghs section text=".static.text"

class MediaTaskStack
{
public:
    //! \brief Constructor.
    MediaTaskStack() : m_count(0) {}
    
    void reset()
    {
        m_count = 0;
        memset(m_stack, 0, sizeof(m_stack));
    }
    
    void push(const char * task)
    {
        if (m_count < kMaxTasks)
        {
            m_stack[m_count] = task;
        }
        
        // We always increment the count even if there are no slots to record the task,
        // since we want the pops to be balanced.
        m_count++;
    }
    
    void pop()
    {
        if (m_count)
        {
            // Zero out the previous top so the stack is easy to read in the debugger.
            if (m_count <= kMaxTasks)
            {
                m_stack[m_count - 1] = 0;
            }
            m_count--;
        }
    }
    
    inline unsigned getCount() const { return m_count; }
    
    //! Tasks are returned in reverse order, with the top of the stack first.
    unsigned getStack(const char ** tasks, unsigned maxTasks)
    {
        // Clear out the target array.
        memset(tasks, 0, maxTasks * sizeof(const char *));
        
        // Limit the number we copy to the maximum the caller passed in.
        // We can't use std::min<> here because it is not in static text.
        unsigned actualCount = m_count;
        if (kMaxTasks < actualCount)
        {
            actualCount = kMaxTasks;
        }
        if (maxTasks < actualCount)
        {
            actualCount = maxTasks;
        }
        
        // Copy task names in reverse order, so the top of the stack is
        // the first in the list.
        unsigned i = 0;
        unsigned stackIndex = m_count - 1;
        for (; i < actualCount; ++i, --stackIndex)
        {
            tasks[i] = m_stack[stackIndex];
        }
        return actualCount;
    }
    
protected:
    enum
    {
        kMaxTasks = 20
    };
    
    unsigned m_count;   //!< Number of tasks in the stack.
    const char * m_stack[kMaxTasks];    //!< Stack of task names, with the bottom of the stack at index zero.
};

/*!
 *
 */
class MediaTaskManager
{
public:
    //! \brief Constructor.
    MediaTaskManager()
    :   m_count(0)
    {
        memset(m_threads, 0, sizeof(m_threads));
    }
    
    //! Returns the stack for the current thread, adding an entry for the
    //! thread if there isn't one already.
    MediaTaskStack & getCurrenThreadStack()
    {
        unsigned i;
        TX_THREAD * thisThread = tx_thread_identify();
        
        for (i = 0; i < m_count; ++i)
        {
            Thread & info = m_threads[i];
            if (thisThread == info.m_thread)
            {
                return info.m_stack;
            }
        }
        
        // Must have room to insert. (Hey, this is only debug code!)
        assert(m_count < kMaxThreads);
        
        Thread & info = m_threads[m_count++];
        info.m_thread = thisThread;
        info.m_stack.reset();
        return info.m_stack;
    }
    
protected:
    /*!
     *
     */
    struct Thread
    {
        TX_THREAD * m_thread;
        MediaTaskStack m_stack;
    };
    
    enum
    {
        kMaxThreads = 32
    };
    
    unsigned m_count;
    Thread m_threads[kMaxThreads];
};

// This definition must come after the class definitions.
MediaTaskManager g_ldlMediaTasks;

void ddi_ldl_push_media_task(const char * taskName)
{
    g_ldlMediaTasks.getCurrenThreadStack().push(taskName);
}

void ddi_ldl_pop_media_task(void)
{
    g_ldlMediaTasks.getCurrenThreadStack().pop();
}

unsigned ddi_ldl_get_media_task_count(void)
{
    return g_ldlMediaTasks.getCurrenThreadStack().getCount();
}

unsigned ddi_ldl_get_media_task_stack(const char ** tasks, unsigned maxTasks)
{
    return g_ldlMediaTasks.getCurrenThreadStack().getStack(tasks, maxTasks);
}

#pragma ghs section text=default

#endif // DEBUG
//! @}


