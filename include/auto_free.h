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
//! \file auto_free.h
//! \brief Defines a class to ensure release of memory allocated with malloc().
#ifndef _auto_free_h_
#define _auto_free_h_

#include "types.h"
#include <stdlib.h>

///////////////////////////////////////////////////////////////////////////////
// Types
///////////////////////////////////////////////////////////////////////////////

/*!
 * \brief Template helper class to automatically free memory.
 *
 * When the instance of this class falls out of scope, it will free the pointer it
 * owns with the standard C library free() API.
 */
template <typename T>
class auto_free
{
    typedef T * t_ptr_t;
    typedef const T * const_t_ptr_t;
    typedef T & t_ref_t;
    typedef const T & const_t_ref_t;
    
    //! The pointer we own.
    t_ptr_t m_ptr;
    
public:
    //! \brief Default constructor, sets pointer to NULL.
    inline auto_free() : m_ptr(0) {}
    
    //! \brief Constructor taking a pointer to own.
    inline auto_free(t_ptr_t p) : m_ptr(p) {}
    
    //! \brief Constructor taking a void pointer to own.
    inline auto_free(void * p) : m_ptr(reinterpret_cast<t_ptr_t>(p)) {}
    
    //! \brief Copy constructor.
    inline auto_free(auto_free<T> & o) : m_ptr(o.release()) {}
    
    //! \brief Copy constructor for compatible pointer type.
    template <typename O>
    inline auto_free(auto_free<O> & o) : m_ptr(o.release()) {}
    
    //! \brief Destructor.
    inline ~auto_free()
    {
        free();
    }
    
    //! \name Pointer control
    //@{
    //! \brief Clear the pointer we own.
    inline void reset()
    {
        m_ptr = 0;
    }
    
    //! \brief Return the pointer and clear it.
    inline t_ptr_t release()
    {
        t_ptr_t tmp = m_ptr;
        reset();
        return tmp;
    }
    
    //! \brief Free the memory occupied by the pointer.
    //!
    //! Does nothing if the pointer is NULL.
    inline void free()
    {
        if (m_ptr)
        {
            ::free(m_ptr);
            reset();
        }
    }
    //@}
    
    //! \name Get and set
    //@{
    //! \brief Return the owned pointer.
    inline t_ptr_t get() { return m_ptr; }
    
    //! \brief Return the owned pointer.
    inline const_t_ptr_t get() const { return m_ptr; }
    
    //! \brief Changed the owned pointer to a new value.
    //!
    //! If there was a previous pointer, it will be freed first. If \a p is NULL or the same
    //! as the currently owned pointer then nothing will be done.
    inline void set(t_ptr_t p)
    {
        if (p && p != m_ptr)
        {
            free();
        }
        m_ptr = p;
    }
    
    //! \brief Variant of set() taking a void pointer.
    inline void set(void * p) { set(reinterpret_cast<t_ptr_t>(p)); }
    //@}
    
    //! \name Conversion Operators
    //@{
    inline operator t_ptr_t () { return m_ptr; }

    inline operator const_t_ptr_t () const { return m_ptr; }
    
    inline operator bool () const { return m_ptr != NULL; }
    //@}
    
    //! \name Access operators
    //@{
    inline t_ref_t operator * () { return *m_ptr; }
    
    inline const_t_ref_t operator * () const { return *m_ptr; }
    
    inline t_ptr_t operator -> () { return m_ptr; }
    
    inline const_t_ptr_t operator -> () const { return m_ptr; }
    //@}
    
    //! \brief Assignment operator.
    //! \see set()
    inline auto_free<T> & operator = (t_ptr_t p)
    {
        set(p);
        return *this;
    }
    
    //! \brief Compatible assignment operator.
    template <typename O>
    inline auto_free<T> & operator = (auto_free<O> & o)
    {
        set(o.release());
        return *this;
    }

};

/*!
 * \brief Template helper class to automatically free memory.
 *
 * When the instance of this class falls out of scope, it will free the pointer it
 * owns with the delete operator. This class is much like std::auto_ptr but adds
 * some useful functionality. Plus, it resembles auto_free.
 */
template <typename T>
class auto_delete
{
    typedef T * t_ptr_t;
    typedef const T * const_t_ptr_t;
    typedef T & t_ref_t;
    typedef const T & const_t_ref_t;
    
    //! The pointer we own.
    t_ptr_t m_ptr;
    
public:
    //! \brief Default constructor, sets pointer to NULL.
    inline auto_delete() : m_ptr(0) {}
    
    //! \brief Constructor taking a pointer to own.
    inline auto_delete(t_ptr_t p) : m_ptr(p) {}
    
    //! \brief Constructor taking a void pointer to own.
    inline auto_delete(void * p) : m_ptr(reinterpret_cast<t_ptr_t>(p)) {}
    
    //! \brief Copy constructor.
    inline auto_delete(auto_delete<T> & o) : m_ptr(o.release()) {}
    
    //! \brief Copy constructor for compatible pointer type.
    template <typename O>
    inline auto_delete(auto_delete<O> & o) : m_ptr(o.release()) {}
    
    //! \brief Destructor.
    inline ~auto_delete()
    {
        free();
    }
    
    //! \name Pointer control
    //@{
    //! \brief Clear the pointer we own without deleting it.
    inline void reset()
    {
        m_ptr = 0;
    }
    
    //! \brief Return the pointer and clear it.
    inline t_ptr_t release()
    {
        t_ptr_t tmp = m_ptr;
        reset();
        return tmp;
    }
    
    //! \brief Free the memory occupied by the pointer.
    //!
    //! Does nothing if the pointer is NULL.
    inline void free()
    {
        if (m_ptr)
        {
            delete m_ptr;
            reset();
        }
    }
    //@}
    
    //! \name Get and set
    //@{
    //! \brief Return the owned pointer.
    inline t_ptr_t get() { return m_ptr; }
    
    //! \brief Return the owned pointer.
    inline const_t_ptr_t get() const { return m_ptr; }
    
    //! \brief Changed the owned pointer to a new value.
    //!
    //! If there was a previous pointer, it will be freed first. If \a p is NULL or the same
    //! as the currently owned pointer then nothing will be done.
    inline void set(t_ptr_t p)
    {
        if (m_ptr && p != m_ptr)
        {
            free();
        }
        m_ptr = p;
    }
    
    //! \brief Variant of set() taking a void pointer.
    inline void set(void * p) { set(reinterpret_cast<t_ptr_t>(p)); }
    //@}
    
    //! \name Conversion Operators
    //@{
    inline operator t_ptr_t () { return m_ptr; }

    inline operator const_t_ptr_t () const { return m_ptr; }
    
    inline operator bool () const { return m_ptr != NULL; }
    //@}
    
    //! \name Access operators
    //@{
    inline t_ref_t operator * () { return *m_ptr; }
    
    inline const_t_ref_t operator * () const { return *m_ptr; }
    
    inline t_ptr_t operator -> () { return m_ptr; }
    
    inline const_t_ptr_t operator -> () const { return m_ptr; }
    //@}
    
    //! \brief Assignment operator.
    //! \see set()
    inline auto_delete<T> & operator = (t_ptr_t p)
    {
        set(p);
        return *this;
    }
    
    //! \brief Compatible assignment operator.
    template <typename O>
    inline auto_delete<T> & operator = (auto_free<O> & o)
    {
        set(o.release());
        return *this;
    }

};

/*!
 * \brief Template helper class to automatically free memory.
 *
 * When the instance of this class falls out of scope, it will free the pointer it
 * owns with the array delete operator. This class is much like std::auto_ptr but adds
 * some works with arrays. The only other addition on top of auto_delete<> is a
 * new array indexing operator.
 */
template <typename T>
class auto_array_delete
{
    typedef T * t_ptr_t;
    typedef const T * const_t_ptr_t;
    typedef T & t_ref_t;
    typedef const T & const_t_ref_t;
    
    //! The pointer we own.
    t_ptr_t m_ptr;
    
public:
    //! \brief Default constructor, sets pointer to NULL.
    inline auto_array_delete() : m_ptr(0) {}
    
    //! \brief Constructor taking a pointer to own.
    inline auto_array_delete(t_ptr_t p) : m_ptr(p) {}
    
    //! \brief Constructor taking a void pointer to own.
    inline auto_array_delete(void * p) : m_ptr(reinterpret_cast<t_ptr_t>(p)) {}
    
    //! \brief Copy constructor.
    inline auto_array_delete(auto_array_delete<T> & o) : m_ptr(o.release()) {}
    
    //! \brief Copy constructor for compatible pointer type.
    template <typename O>
    inline auto_array_delete(auto_array_delete<O> & o) : m_ptr(o.release()) {}
    
    //! \brief Destructor.
    inline ~auto_array_delete()
    {
        free();
    }
    
    //! \name Pointer control
    //@{
    //! \brief Clear the pointer we own without deleting it.
    inline void reset()
    {
        m_ptr = 0;
    }
    
    //! \brief Return the pointer and clear it.
    inline t_ptr_t release()
    {
        t_ptr_t tmp = m_ptr;
        reset();
        return tmp;
    }
    
    //! \brief Free the memory occupied by the pointer.
    //!
    //! Does nothing if the pointer is NULL.
    inline void free()
    {
        if (m_ptr)
        {
            delete [] m_ptr;
            reset();
        }
    }
    //@}
    
    //! \name Get and set
    //@{
    //! \brief Return the owned pointer.
    inline t_ptr_t get() { return m_ptr; }
    
    //! \brief Return the owned pointer.
    inline const_t_ptr_t get() const { return m_ptr; }
    
    //! \brief Changed the owned pointer to a new value.
    //!
    //! If there was a previous pointer, it will be freed first. If \a p is NULL or the same
    //! as the currently owned pointer then nothing will be done.
    inline void set(t_ptr_t p)
    {
        if (m_ptr && p != m_ptr)
        {
            free();
        }
        m_ptr = p;
    }
    
    //! \brief Variant of set() taking a void pointer.
    inline void set(void * p) { set(reinterpret_cast<t_ptr_t>(p)); }
    //@}
    
    //! \name Conversion Operators
    //@{
    inline operator t_ptr_t () { return m_ptr; }

    inline operator const_t_ptr_t () const { return m_ptr; }
    
    inline operator bool () const { return m_ptr != NULL; }
    //@}
    
    //! \name Access operators
    //@{
    inline t_ref_t operator * () { return *m_ptr; }
    
    inline const_t_ref_t operator * () const { return *m_ptr; }
    
    inline t_ptr_t operator -> () { return m_ptr; }
    
    inline const_t_ptr_t operator -> () const { return m_ptr; }
    //@}
    
    //! \name Array index operators
    //@{
    inline t_ref_t operator [] (unsigned index) { return m_ptr[index]; }
    
    inline const_t_ref_t operator [] (unsigned index) const { return m_ptr[index]; }
    //@}
    
    //! \brief Assignment operator.
    //! \see set()
    inline auto_array_delete<T> & operator = (t_ptr_t p)
    {
        set(p);
        return *this;
    }
    
    //! \brief Compatible assignment operator.
    template <typename O>
    inline auto_array_delete<T> & operator = (auto_free<O> & o)
    {
        set(o.release());
        return *this;
    }

};

#endif //_auto_free_h_
///////////////////////////////////////////////////////////////////////////////
// EOF
///////////////////////////////////////////////////////////////////////////////

