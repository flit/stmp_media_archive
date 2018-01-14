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
//! \addtogroup ddi_nand_media
//! @{
//! \file DeferredTask.h
//! \brief Definition of the nand::DeferredTask class.
////////////////////////////////////////////////////////////////////////////////
#if !defined(__deferred_task_h__)
#define __deferred_task_h__

#include "types.h"
#include "os/thi/os_thi_api.h"
#include "DoubleList.h"

////////////////////////////////////////////////////////////////////////////////
// Definitions
////////////////////////////////////////////////////////////////////////////////

namespace nand
{

// Forward declaration
class DeferredTask;

/*!
 * \brief Priority queue of deferred task objects.
 *
 * This class is not only a priority queue but the manager for the thread that executes the
 * tasks inserted into the queue.
 *
 * Users of a queue must ensure that the drain() method is called prior to destructing the queue
 * if they want all tasks to be executed. Otherwise, the destructor will simply delete any
 * tasks remaining on the queue.
 */
class DeferredTaskQueue
{
public:

    //! \brief Constants for the task execution thread.
    enum _task_thread_constants
    {
        kTaskThreadStackSize = 2048,
        kTaskThreadPriority = 12,
        kTaskThreadTimeoutTicks = OS_MSECS_TO_TICKS(500)
    };

    //! \name Queue types
    //@{
    typedef DoubleListT<DeferredTask> queue_t;
    typedef DoubleListT<DeferredTask>::Iterator iterator_t;
    //@}
    
    //! \brief Constructor.
    DeferredTaskQueue();
    
    //! \brief Destructor.
    ~DeferredTaskQueue();
    
    //! \brief Initializer.
    RtStatus_t init();
    
    //! \brief Wait for all current tasks to complete.
    RtStatus_t drain();
    
    //! \brief Add a new task to the queue.
    void post(DeferredTask * task);
    
    //! \brief Returns whether the queue is empty.
    bool isEmpty() const { return m_entries.isEmpty(); }
    
    iterator_t getBegin() { return m_entries.getBegin(); }
    iterator_t getEnd() { return m_entries.getEnd(); }
    
    //! \brief Returns the task that is currently being executed.
    DeferredTask * getCurrentTask() { return m_currentTask; }

protected:
    
    TX_MUTEX m_mutex;   //!< Mutex protecting the queue.
    queue_t m_entries;  //!< List of queue entries.
    TX_THREAD * m_thread;   //!< Thread used to execute tasks.
    TX_SEMAPHORE m_taskSem; //!< Semaphore to signal availability of tasks to the thread.
    DeferredTask * volatile m_currentTask;   //!< Task being executed.
    
    //! \brief Static entry point for the task thread.
    static void taskThreadStub(uint32_t arg);
    
    //! \brief The main entry point for the task thread.
    void taskThread();
    
    //! \brief Function to dispose of the task thread.
    static void disposeTaskThread(uint32_t param);

};

/*!
 * \brief Deferred task abstract base class.
 *
 * Subclasses must implement the task() and getTaskTypeID() methods. They can optionally
 * override the getShouldExamine(), examineOne(), and examine() methods to modify how the task
 * looks at a queue prior to being inserted, to determine whether it should be inserted at all
 * or perhaps perform some other operation.
 *
 * Task priorities are inverted, in the sense that the highest priority is 0 and they go
 * down in priority as the priority value increases. The priority is passed to the constructor
 * and must not change over the lifetime of the task object.
 *
 * To actually perform the task, call the run method(). It will internally invoke the pure
 * abstract task() method that subclasses must provide. If you need more complex behaviour, then
 * you may override run().
 *
 * A completion callback is supported. When set, the default implementation of the run() method
 * will call the completion callback after task() returns. If you override run(), then be sure
 * to invoke the callback before returning.
 */
class DeferredTask : public DoubleList::Node
{
public:

    //! \brief Type for a completion callback function.
    typedef void (*CompletionCallback_t)(DeferredTask * completedTask, void * data);
    
    //! \brief Constructor.
    DeferredTask(int priority);
    
    //! \brief Destructor.
    virtual ~DeferredTask();
    
    //! \name Properties
    //@{
    //! \brief Return a unique ID for this task type.
    virtual uint32_t getTaskTypeID() const = 0;
    
    //! \brief Returns whether the task wants to examine queue entries before insertion.
    //!
    //! By default, we do want to examine queue entries. However, 
    //! examineOne(DeferredTask * task) must be overridden by the subclass to modify
    //! the default behaviour of always being inserted into the queue.
    virtual bool getShouldExamine() const;
    
    //! \brief Return the task's priority.
    int getPriority() const { return m_priority; }
    //@}
    
    //! \name Operations
    //@{
    //! \brief Execute the task.
    virtual void run();
    
    //! \brief Optionally review current queue entries and take action.
    //!
    //! This method will iterate over all of the tasks currently in \a queue, from beginning
    //! to end. It will call examineOne(DeferredTask * task) on each entry for detailed
    //! examination. If that call returns true then iteration is stopped and true returned to
    //! the caller immediately.
    //!
    //! If getShouldExamine() returns false, then the queue will not be examined and no other
    //! action will be taken. In this case, false will always be returned to indicate that the
    //! task should be inserted into the queue.
    //!
    //! \retval true Indicates that this task should not be inserted into the queue. It is
    //!     up to the calling queue to delete the task.
    //! \retval false Continue with inserting the new task into the queue.
    virtual bool examine(DeferredTaskQueue & queue);
    
    //! \brief Optionally review a single current queue entry and take action.
    //! \retval true Indicates that this task should not be inserted into the queue.
    //! \retval false Continue with inserting the new task into the queue.
    virtual bool examineOne(DeferredTask * task);
    //@}
    
    //! \name Completion callbacks
    //@{
    //! \brief Set the completion callback.
    void setCompletion(CompletionCallback_t callback, void * data);
    //@}

protected:

    int m_priority;  //!< The priority level for this task.
    CompletionCallback_t m_callback;    //!< An optional completion callback function.
    void * m_callbackData;  //! Arbitrary data passed to the callback.
    
    //! \brief The task entry point provided by a concrete subclass.
    virtual void task() = 0;
    
};

} // namespace nand

#endif // __deferred_task_h__
////////////////////////////////////////////////////////////////////////////////
// EOF
////////////////////////////////////////////////////////////////////////////////
