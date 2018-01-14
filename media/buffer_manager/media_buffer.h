///////////////////////////////////////////////////////////////////////////////
// Copyright (c) SigmaTel, Inc. All rights reserved.
// 
// SigmaTel, Inc.
// Proprietary & Confidential
// 
// This source code and the algorithms implemented therein constitute
// confidential information and may comprise trade secrets of SigmaTel, Inc.
// or its associates, and any use thereof is subject to the terms and
// conditions of the Confidential Disclosure Agreement pursual to which this
// source code was originally received.
///////////////////////////////////////////////////////////////////////////////
#if !defined(_media_buffer_h_)
#define _media_buffer_h_

#include "media_buffer_manager.h"
#include "drivers/media/ddi_media.h"
#include "drivers/media/sectordef.h"
#include <string.h>

////////////////////////////////////////////////////////////////////////////////
// Types
////////////////////////////////////////////////////////////////////////////////

#if defined(__cplusplus)

/*!
 * \brief Utility class to manage a media buffer.
 */
class MediaBuffer
{
public:
    //! \brief Default constructor.
    inline MediaBuffer()
    :   m_buffer(NULL)
    {
    }
    
    //! \brief Constructor taking a previously acquired buffer.
    inline MediaBuffer(void * buf)
    :   m_buffer((SECTOR_BUFFER *)buf)
    {
        if (buf)
        {
#if DEBUG
            assert(media_buffer_retain(m_buffer) == SUCCESS);
#else
            // We cannot return an error, so ignore it.
            media_buffer_retain(m_buffer);
#endif
        }
    }
    
    //! \brief Copy constructor.
    //!
    //! When copying another buffer object, the other's buffer is simply retained.
    MediaBuffer(const MediaBuffer & other)
    :   m_buffer(other.m_buffer)
    {
#if DEBUG
        assert(media_buffer_retain(m_buffer) == SUCCESS);
#else
        // We cannot return an error, so ignore it.
        media_buffer_retain(m_buffer);
#endif
    }
    
    //! \brief Destructor that releases the buffer.
    inline ~MediaBuffer()
    {
        release();
    }
    
    //! \brief Acquire a new buffer.
    //!
    //! It is an error to call this method when the instance already has a buffer associated
    //! with it.
    inline RtStatus_t acquire(MediaBufferType_t bufferType, uint32_t flags=kMediaBufferFlag_None)
    {
        // Make sure we don't already have a buffer.
        release();
        
        // Allocate or reuse a buffer.
        return media_buffer_acquire(bufferType, flags, &m_buffer);
    }
    
    //! \brief Set the buffer to a previously acquired one.
    //! \param buf Pointer to a buffer returned from media_buffer_acquire(). You can also
    //!     pass NULL to clear the internal buffer pointer.
    inline RtStatus_t set(void * buf)
    {
        // Release any previous buffer.
        release();
        
        // Save the provided buffer address.
        m_buffer = reinterpret_cast<SECTOR_BUFFER *>(buf);

        // Check for a non-NULL buffer.
        if (buf)
        {
            // Retain the buffer we were given.
            return media_buffer_retain(m_buffer);
        }
        else
        {
            return SUCCESS;
        }
    }
    
    //! \brief Assignment operator. Simply retains the buffer.
    MediaBuffer & operator = (SECTOR_BUFFER * buf)
    {
        set(buf);
        return *this;
    }
    
    //! \brief Returns whether the buffer was succesfully acquired.
    inline bool hasBuffer() const { return m_buffer != NULL; }
    
    //! \brief Accessor for the buffer.
    inline SECTOR_BUFFER * getBuffer() const { return m_buffer; }
    
    //! \brief Get the size of the buffer in bytes.
    //!
    //! If there is no buffer associated with this object, then 0 will be returned.
    inline unsigned getLength() const { return hasBuffer() ? getProperty<uint32_t>(kMediaBufferProperty_Size) : 0; }
    
    //! \name Conversion operators
    //@{
    //! \brief
    inline SECTOR_BUFFER * operator * () { return m_buffer; }

    //! \brief
    inline const SECTOR_BUFFER * operator * () const { return m_buffer; }
    
    //! \brief
    inline operator SECTOR_BUFFER * () { return m_buffer; }
    
    //! \brief
    inline operator const SECTOR_BUFFER * () const { return m_buffer; }
    
    //! \brief
    inline operator void * () { return m_buffer; }
    
    //! \brief
    inline operator const void * () const { return m_buffer; }
    
    //! \brief
    inline operator uint8_t * () { return reinterpret_cast<uint8_t *>(m_buffer); }
    
    //! \brief
    inline operator const uint8_t * () const { return reinterpret_cast<uint8_t *>(m_buffer); }
    
    //! \brief
    inline operator bool () const { return hasBuffer(); }
    //@}
    
    //!  \brief Fill the buffer with a pattern.
    inline void fill(uint8_t value)
    {
        memset(m_buffer, value, getLength());
    }
    
    //! \brief Get a property of the buffer.
    template <typename T>
    inline T getProperty(uint32_t which) const
    {
        return media_buffer_get_property<T>(m_buffer, which);
    }
    
    //! \brief Get a property of the buffer.
    inline RtStatus_t getProperty(uint32_t which, void * value) const
    {
        return media_buffer_get_property(m_buffer, which, value);
    }
    
    //! \brief Release the buffer back to the buffer manager's control.
    inline void release()
    {
        if (hasBuffer())
        {
            media_buffer_release(m_buffer);
            m_buffer = NULL;
        }
    }
    
    //! \brief Clear the buffer pointer without releasing the buffer.
    void relinquish()
    {
        m_buffer = NULL;
    }
    
protected:
    SECTOR_BUFFER * m_buffer;   //!< The media buffer.
    
};

/*!
 * \brief Wraps a sector-sized media buffer.
 */
class SectorBuffer : public MediaBuffer
{
public:
    //! \brief Constructor.
    inline SectorBuffer() : MediaBuffer() {}
    
    //! \brief Constructor taking a previously allocated buffer.
    inline SectorBuffer(void * buf) : MediaBuffer(buf) {}
    
    //! \brief Copy constructor.
    inline SectorBuffer(const SectorBuffer & other) : MediaBuffer(other) {}
    
    //! \brief Acquire a sector buffer.
    inline RtStatus_t acquire() { return MediaBuffer::acquire(kMediaBufferType_Sector); }
};

/*!
 * \brief Wraps an auxiliary buffer.
 */
class AuxiliaryBuffer : public MediaBuffer
{
public:
    //! \brief Constructor.
    inline AuxiliaryBuffer() : MediaBuffer() {}
    
    //! \brief Constructor taking a previously allocated buffer.
    inline AuxiliaryBuffer(void * buf) : MediaBuffer(buf) {}
    
    //! \brief Copy constructor.
    inline AuxiliaryBuffer(const AuxiliaryBuffer & other) : MediaBuffer(other) {}
    
    //! \brief Acquire an auxiliary buffer.
    inline RtStatus_t acquire() { return MediaBuffer::acquire(kMediaBufferType_Auxiliary); }
};

#endif // defined(__cplusplus)

////////////////////////////////////////////////////////////////////////////////
// End of file
////////////////////////////////////////////////////////////////////////////////
#endif // _media_buffer_h_
//! @}


