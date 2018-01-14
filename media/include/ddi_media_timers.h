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
//! \file ddi_media_timers.h
//! \brief Timer utility classes used in the media drivers.
////////////////////////////////////////////////////////////////////////////////
#if !defined(__ddi_media_timers_h__)
#define __ddi_media_timers_h__

#if defined(__cplusplus)

#include "types.h"
#include "hw/profile/hw_profile.h"
#include <algorithm>

/*!
 * \brief Bare bones microsecond timer class.
 */
class SimpleTimer
{
public:
    //! \brief Constructor; takes the start timestamp.
    inline SimpleTimer()
    {
        m_start = hw_profile_GetMicroseconds();
    }
    
    //! \brief Start the timer over again.
    inline void restart()
    {
        m_start = hw_profile_GetMicroseconds();
    }
    
    //! \brief Computes and returns the elapsed time since the object was constructed.
    inline uint64_t getElapsed() const
    {
        return hw_profile_GetMicroseconds() - m_start;
    }
    
    //! \brief Operator to make reading the elapsed time very easy.
    inline operator uint64_t () const
    {
        return getElapsed();
    }

protected:
    uint64_t m_start;   //!< The start timestamp in microseconds.
};

/*!
 * \brief Stack allocated utility class to add elapsed time to an accumulator variable.
 *
 * Use this class by allocating an instance on the stack and passing in an accumulator
 * variable to the constructor. Then when the instance falls out of scope, the
 * elapsed time since construction in microseconds will be added to the accumulator.
 *
 * \code
 * uint64_t myAccum;
 *
 * ElapsedTimeAdder timer(myAccum);
 * \endcode
 */
class ElapsedTimerAdder : public SimpleTimer
{
public:
    //! \brief Constructor. Takes a reference to the accumulator variable.
    inline ElapsedTimerAdder(uint64_t & accumulator)
    :   SimpleTimer(), m_accum(accumulator)
    {
    }
    
    //! \brief Destructor. Adds elapsed time to the accumulator variable.
    inline ~ElapsedTimerAdder()
    {
        m_accum += getElapsed();
    }
    
protected:
    uint64_t & m_accum;
};

/*!
 * \brief Struct used for computing average operation times.
 */
class AverageTime
{
public:
    //! \brief Constructor to init counts to zero.
    inline AverageTime() : accumulator(0), count(0), averageTime(0), minTime(~0), maxTime(0) {}
    
    //! \brief Add time to the average.
    inline void add(uint64_t amount, unsigned c=1)
    {
        accumulator += amount;
        count += c;
        averageTime = accumulator / count;
        maxTime = std::max<unsigned>(maxTime, amount/c);
        minTime = std::min<unsigned>(minTime, amount/c);
    }
    
    //! \brief Overloaded operator to add time to the average.
    inline AverageTime & operator += (uint64_t amount)
    {
        add(amount);
        return *this;
    }
    
    //! \name Accessor methods
    //@{
    inline unsigned getCount() const { return count; }
    inline unsigned getAverage() const { return averageTime; }
    inline unsigned getMin() const { return minTime; }
    inline unsigned getMax() const { return maxTime; }
    //@}
    
    //! Clear the accumulator and counter and reset times.
    inline void reset()
    {
        accumulator = 0;
        count = 0;
        averageTime = 0;
        minTime = ~0;
        maxTime = 0;
    }

protected:
    uint64_t accumulator;
    unsigned count;
    unsigned averageTime;
    unsigned minTime;
    unsigned maxTime;
};

/*!
 * \brief Records elapsed times in a histogram chart.
 *
 * Can be used directly in place of AverageTime, as it has the same interface.
 */
class ElapsedTimeHistogram
{
public:
    //! Available modes for how bands are created.
    typedef enum {
        kLinear,        //!< Bands are evenly spaced and have the same width.
        kLogarithmic    //!< Bands are logarithmically spaced.
    } ScalingMode_t;
    
    /*!
     * \brief One band of the histogram.
     */
    struct Band
    {
        uint32_t low;       //!< Lower boundary.
        uint32_t high;      //!< Upper boundary.
        AverageTime time;   //!< Average time and count for this band.
        
        //! \brief Default constructor.
        inline Band() : low(0), high(0), time() {}
        
        //! \brief Constructor taking default boundary values.
        inline Band(uint32_t theLow, uint32_t theHigh) : low(theLow), high(theHigh), time() {}
    };
    
    //! \brief Initializer.
    void init(ScalingMode_t mode, uint32_t min, uint32_t max, unsigned bands)
    {
        assert(min < max);
        assert(bands > 0);
        
        m_scaling = mode;
        
        // Determine number of bands. Automatically add lower and upper bands
        // if necessary to catch values outside the passed-in range.
        m_bandCount = bands;
        unsigned startIndex = 0;
        unsigned endIndex = bands;
        if (min > 0)
        {
            m_bandCount++;
            startIndex++;
            endIndex++;
        }
        if (max < ~0)
        {
            m_bandCount++;
        }
        
        // Allocate the bands. We use malloc() instead of new[] because
        // the vector new operator is not available at init time in paging apps.
        m_bands = (Band *)malloc(sizeof(Band) * m_bandCount);
        
        // Fill in the band boundaries.
        unsigned i;
        uint32_t width = (max - min) / bands;
        for (i=startIndex; i < endIndex; ++i)
        {
            uint32_t low;
            uint32_t high;
            switch (m_scaling)
            {
                //! \todo Implement logarithmic scaling.
                case kLinear:
                case kLogarithmic:
                    low = min + (i - startIndex) * width;
                    high = low + width - 1;
                    break;
            }

            new(&m_bands[i]) Band(low, high);
        }

        // Fill in automatically added bands.
        if (min > 0)
        {
            // Added low band catches everything below passed-in min.
            new(&m_bands[0]) Band(0, min - 1);
        }
        if (max < ~0)
        {
            // Added top band ranges from the passed-in max to the maximum 32-bit value.
            new(&m_bands[m_bandCount - 1]) Band(max + 1, ~0);
        }
    }
    
    //! \brief Cleanup.
    void cleanup()
    {
        if (m_bands)
        {
            free(m_bands);
        }
    }
    
    //! \brief
    void insert(uint64_t elapsed)
    {
        unsigned i;
        for (i=0; i < m_bandCount; ++i)
        {
            Band & band = m_bands[i];
            if (elapsed <= band.high)
            {
                // Found the band this elapsed time fits into, so update its average.
                band.time += elapsed;
                break;
            }
        }
    }
    
    //! \brief Retun the total number of bands.
    inline unsigned getBandCount() const { return m_bandCount; }
    
    //! \brief Return a reference to an individual band.
    inline const Band & getBand(unsigned index) const
    {
        assert(index < m_bandCount);
        return m_bands[index];
    }
    
    //! \brief
    inline ElapsedTimeHistogram & operator += (uint64_t elapsed)
    {
        insert(elapsed);
        return *this;
    }

protected:
    ScalingMode_t m_scaling;
    unsigned m_bandCount;
    Band * m_bands;
};

#endif // __cplusplus

#endif // __ddi_media_timers_h__
// EOF
