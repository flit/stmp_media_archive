////////////////////////////////////////////////////////////////////////////////
//
// Copyright (c) 2006 SigmaTel, Inc.
//
//! \file utf_PreTxInit.c
//! \brief creates threads for your unit test - this is just an example.
//! \date 03/06
//!
//! Example for creating test threads in the unit test framework.  stack size
//! used here is simply for example purposes, set based on your needs.
//!
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
// Includes and external references
////////////////////////////////////////////////////////////////////////////////
#include <types.h>
#include <error.h>                  // Common SigmaTel Error Codes

#include <os\os_msg_api.h>          // Message Format API
#include <os\os_thi_api.h>          // OS Helper API
#include <os\os_dmi_api.h>          // Memory Allocation API
#include <os\os_dpc_api.h>          // Deferred Procedure Call API


#define UTF_STACK_SIZE 4096

static TX_THREAD stc_Task0;

extern void utf_TestThread_0( ULONG uLParm );

struct
{
    TX_THREAD    *pThread;
    void (*pFunction)( ULONG );
} stc_UtfThreads[] =
{
    { &stc_Task0, utf_TestThread_0 }
};


///////////////////////////////////////////////////////////////////////////////
//! utf_PreTxInit
//!
//! \brief this function must create the threads used to control your
//! unit test.  dmi is available to allocate resources if needed.  this function
//! must return to the caller once the test threads have been created.
//!
//! \fntype non-reentrant Function
//!
///////////////////////////////////////////////////////////////////////////////

void utf_PreTxInit( )

{
    RtStatus_t      Status;
    uint32_t        u32TxStatus;
    uint8_t         *pu8Stack;
    int32_t         i;


    for ( i = 0 ; i < sizeof( stc_UtfThreads ) / sizeof( *stc_UtfThreads ) ; ++i )
    {
        // let dmi allocate stack space
        Status = os_dmi_MemAlloc((void **)&pu8Stack, UTF_STACK_SIZE, FALSE, DMI_MEM_SOURCE_DONTCARE );
        if ( Status != SUCCESS )
        {
            SystemHalt();
        }
        // create test thread
        u32TxStatus = tx_thread_create( stc_UtfThreads[ i ].pThread, "UTF Test 0", stc_UtfThreads[ i ].pFunction, 0,
                                        pu8Stack, UTF_STACK_SIZE,
                                        19, 19, 10, TX_AUTO_START );
        if ( u32TxStatus != TX_SUCCESS )
        {
            SystemHalt();
        }
    }
}

////////////////////////////////////////////////////////////////////////////////
// End of file
////////////////////////////////////////////////////////////////////////////////
//! @}

