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
//! \file
//! \brief Implementation of the nand::DeferredTask class.
////////////////////////////////////////////////////////////////////////////////

#include "DeferredTask.h"
#include "os/thi/os_thi_api.h"
#include "os/dpc/os_dpc_api.h"
#include "components/telemetry/tss_logtext.h"
#include "simple_mutex.h"
#include <algorithm>

using namespace nand;

////////////////////////////////////////////////////////////////////////////////
// Code
////////////////////////////////////////////////////////////////////////////////

#if !defined(__ghs__)
#pragma mark --DeferredTaskQueue--
#endif

#pragma ghs section text=".init.text"

DeferredTaskQueue::DeferredTaskQueue()
:   m_entries(),
    m_thread(NULL),
    m_currentTask(NULL)
{
}

RtStatus_t DeferredTaskQueue::init()
{
    RtStatus_t status;
    
    status = os_thi_ConvertTxStatus(tx_mutex_create(&m_mutex, "nand:task:mutex", TX_NO_INHERIT));
    if (status != SUCCESS)
    {
        return status;
    }
    
    return os_thi_ConvertTxStatus(tx_semaphore_create(&m_taskSem, "nand:task:sem", 0));
}

#pragma ghs section text=default

DeferredTaskQueue::~DeferredTaskQueue()
{
    // Delete any tasks remaining on the queue.
    while (!isEmpty())
    {
        DeferredTask * task = m_entries.getHead();
        m_entries.remove(task);
        delete task;
    }
    
    // Dispose of OS objects. Once the semaphore is delete, the thread (if it exists) will
    // deallocate itself.
    tx_semaphore_delete(&m_taskSem);
    tx_mutex_delete(&m_mutex);
}

RtStatus_t DeferredTaskQueue::drain()
{
    // Sleep until the queue is completely empty and there is no task being run.
    while (!isEmpty() || m_currentTask)
    {
        tx_thread_sleep(OS_MSECS_TO_TICKS(50));
    }
    
    return SUCCESS;
}

void DeferredTaskQueue::post(DeferredTask * task)
{
    assert(task);
    
    {
        // Lock the queue protection mutex.
        SimpleMutex protectQueue(m_mutex);
        
        // Ask the task if it should really be inserted.
        if (task->examine(*this))
        {
            // The task doesn't want to be placed into the queue, so just delete it and exit.
            delete task;
            return;
        }
        
        int newPriority = task->getPriority();
        
        // Create iterators.
        iterator_t it = getBegin();
        iterator_t last = getEnd();
        
        // Search for the insert position for this task. This could easily be optimized.
        for (; it != last; ++it)
        {
            if (newPriority < it->getPriority())
            {
                break;
            }
        }
        
        // Insert the new task before the search iterator.
        m_entries.insertBefore(task, it);
    }
    
    // Put the semaphore to indicate a newly available task.
    tx_semaphore_put(&m_taskSem);
    
    // Create the task thread if necessary.
    if (!m_thread)
    {
        os_txi_ThreadAllocate(&m_thread,
        	"nand:tasks",
            taskThreadStub,
            reinterpret_cast<uint32_t>(this),
            DMI_MEM_SOURCE_DONTCARE,
            kTaskThreadStackSize,
            kTaskThreadPriority,
            kTaskThreadPriority,
            TX_NO_TIME_SLICE,
            TX_AUTO_START);
        
        tss_logtext_Print(LOGTEXT_VERBOSITY_1 | LOGTEXT_EVENT_DDI_NAND_GROUP, "Nand: started deferred task thread\n");
    }
}

//! This static stub function simply passes control along to the member function of
//! the object passed in as its sole argument.
void DeferredTaskQueue::taskThreadStub(uint32_t arg)
{
    DeferredTaskQueue * _this = reinterpret_cast<DeferredTaskQueue *>(arg);
    if (_this)
    {
        _this->taskThread();
    }
}

void DeferredTaskQueue::taskThread()
{
    // Loop until the semaphore get times out, which means that there
    // have been no available tasks for some time. It may also return an error, which
    // is likely because the semaphore was deleted.
    while (tx_semaphore_get(&m_taskSem, kTaskThreadTimeoutTicks) == TX_SUCCESS)
    {
        DeferredTask * task;
        
        // Pop the head of the queue.
        {
            SimpleMutex protectQueue(m_mutex);
            task = m_entries.getHead();
            m_entries.remove(task);
            m_currentTask = task;
        }
        
        // It's conceivable that the semaphore count is greater than the number of tasks,
        // if a task modified the queue in its examine() method.
        if (task)
        {
            tss_logtext_Print(LOGTEXT_VERBOSITY_1 | LOGTEXT_EVENT_DDI_NAND_GROUP, "Nand: running deferred task 0x%08x\n", (uint32_t)task);
            
            // Execute this task, then dispose of it.
            task->run();
            
            m_currentTask = NULL;
            delete task;
        }
    }
        
    tss_logtext_Print(LOGTEXT_VERBOSITY_1 | LOGTEXT_EVENT_DDI_NAND_GROUP, "Nand: exiting deferred task thread\n");
    
    // Post a DPC to deallocate this thread. This thread's struct pointer is passed to
    // the DPC function as its argument, and we clear our member thread pointer, to prevent
    // any possible collisions in case we get a new task before the old thread has fully
    // been disposed.
    uint32_t thisThread = reinterpret_cast<uint32_t>(m_thread);
    m_thread = NULL;
    os_dpc_Send(OS_DPC_HIGH_LEVEL_DPC, disposeTaskThread, thisThread, TX_WAIT_FOREVER);
}

//! A dynamically allocated thread cannot dispose of itself. So the last thing
//! the task thread does is to post this function as a DPC in order to clean
//! itself up.
//!
//! \param param This parameter should be a pointer to the dynamically allocated
//!     thread that is to be freed.
void DeferredTaskQueue::disposeTaskThread(uint32_t param)
{
    TX_THREAD * threadToDispose = reinterpret_cast<TX_THREAD *>(param);
    assert(threadToDispose);
    os_txi_ThreadRelease(threadToDispose);
}

#if !defined(__ghs__)
#pragma mark --DeferredTask--
#endif

DeferredTask::DeferredTask(int priority)
:   DoubleList::Node(),
    m_priority(priority),
    m_callback(NULL),
    m_callbackData(0)
{
}

DeferredTask::~DeferredTask()
{
}

bool DeferredTask::getShouldExamine() const
{
    return true;
}

void DeferredTask::setCompletion(CompletionCallback_t callback, void * data)
{
    m_callback = callback;
    m_callbackData = data;
}

void DeferredTask::run()
{
    // Do the deed.
    task();
    
    // Invoke the completion callback if set.
    if (m_callback)
    {
        m_callback(this, m_callbackData);
    }
}

bool DeferredTask::examine(DeferredTaskQueue & queue)
{
    // If we don't want to examine the queue, then return false to indicate that we should
    // just be inserted into the queue as normal.
    if (!getShouldExamine())
    {
        return false;
    }
    
    DeferredTaskQueue::iterator_t it = queue.getBegin();
    DeferredTaskQueue::iterator_t last = queue.getEnd();
    
    // Search for the insert position for this task. This could easily be optimized.
    for (; it != last; ++it)
    {
        // Let's take a look at this one queue entry.
        if (examineOne(*it))
        {
            // Hold it! We don't want to be placed into the queue for some reason.
            return true;
        }
    }

    // Continue with insertion in the queue.
    return false;
}

bool DeferredTask::examineOne(DeferredTask * task)
{
    // Continue with insertion in the queue.
    return false;
}

////////////////////////////////////////////////////////////////////////////////
// EOF
////////////////////////////////////////////////////////////////////////////////
