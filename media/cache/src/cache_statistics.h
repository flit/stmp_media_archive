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
//! \file cache_statistics.h
//! \ingroup media_cache_internal
//! \brief Contains internal declarations for media cache index utilities.
////////////////////////////////////////////////////////////////////////////////
#if !defined(__cache_statistics_h__)
#define __cache_statistics_h__

extern "C" {
#include "types.h"
}

////////////////////////////////////////////////////////////////////////////////
// Defines
////////////////////////////////////////////////////////////////////////////////

//! \addtogroup media_cache_internal
//@{

//! \def CACHE_STATISTICS
//!
//! Set this define to 1 to enable statistics recording.
#if !defined(CACHE_STATISTICS)
    #define CACHE_STATISTICS 0
#endif

//@}

////////////////////////////////////////////////////////////////////////////////
// Types
////////////////////////////////////////////////////////////////////////////////

#if CACHE_STATISTICS

/*!
 * \brief Cache statistics for a single drive.
 * \ingroup media_cache_internal
 */
struct MediaCacheDriveStatistics
{
    unsigned readCount;     //!< Number of read accesses.
    unsigned writeCount;    //!< Number of write accesses.
    unsigned evictionCount; //!< Times a valid cache entry was evicted.
    unsigned dirtyEvictionCount;    //!< How many evictions had to flush a dirty entry.
    unsigned hits;      //!< Count of cache hits.
    unsigned misses;    //!< Count of cache misses.
    unsigned errors;    //!< Number of errors for this drive.
    float hitRatio; //!< Ratio of hits to misses, where 1.0 is all hits and 0.0 is all misses.
    
    void hit()
    {
        hits++;
        computeHitRatio();
    }
    
    void miss()
    {
        misses++;
        computeHitRatio();
    }
    
    void computeHitRatio()
    {
        hitRatio = (float)hits / (float)(hits + misses);
    }
};

/*!
 * \brief Struct used for computing average operation times.
 * \ingroup media_cache_internal
 */
struct MediaCacheAverageTime
{
    uint64_t accumulator;
    unsigned count;
    unsigned averageTime;
    
    //! \brief Overloaded operator to add time to the average.
    inline MediaCacheAverageTime & operator += (uint64_t amount)
    {
        accumulator += amount;
        count++;
        computeAverage();
        return *this;
    }
    
    //! \brief Recompute the average time.
    inline void computeAverage()
    {
        averageTime = accumulator / count;
    }
};

/*!
 * \brief Bare bones microsecond timer class.
 * \ingroup media_cache_internal
 */
class SimpleTimer
{
public:
    //! \brief Constructor; takes the start timestamp.
    inline SimpleTimer()
    {
        m_start = hw_profile_GetMicroseconds();
    }
    
    //! \brief Computes and returns the elapsed time since the object was constructed.
    inline uint64_t getElapsed()
    {
        return hw_profile_GetMicroseconds() - m_start;
    }

protected:
    uint64_t m_start;   //!< The start timestamp in microseconds.
};

#endif // CACHE_STATISTICS

#endif // __cache_statistics_h__
////////////////////////////////////////////////////////////////////////////////
// End of file
////////////////////////////////////////////////////////////////////////////////

