///////////////////////////////////////////////////////////////////////////////
//! \addtogroup common
//! @{
//
// Copyright (c) 2000-2005 SigmaTel, Inc.
//
//! \file assert.h
//! \brief Contains definitions for SystemHalt() and assert()
//! \todo [PUBS] Add definitions for TBDs in this file
///////////////////////////////////////////////////////////////////////////////

#ifndef __ASSERT_H
#define __ASSERT_H

//! \brief TBD
#if defined(_WIN32)
    #ifdef __cplusplus
        extern "C" {
    #endif
    void _assert(const char*, const char*, const int);
    #ifdef __cplusplus
        }
    #endif
    #define     SystemHalt() _assert(__FILE__, __FUNCTION__,__LINE__) 
#elif defined (__THUMB)
    #define     SystemHalt() __asm(" .half 0xbebe")
#else
    #define     SystemHalt() __asm(" .word 0xbebebebe");
#endif


//! \brief TBD
#ifndef DONT_DEFINE_ASSERT
#ifdef DEBUG
#define assert(x) do {if(!(x)) SystemHalt();} while(0)
#else
#define assert(x)
#endif
#endif

#endif //__ASSERT_H
