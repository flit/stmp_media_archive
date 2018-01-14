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
//! \file DoubleList.h
//! \ingroup media_cache_internal
//! \brief Declaration of a doubly linked list class.
////////////////////////////////////////////////////////////////////////////////
#if !defined(__double_list_h__)
#define __double_list_h__

#include "types.h"

///////////////////////////////////////////////////////////////////////////////
// Types
///////////////////////////////////////////////////////////////////////////////

/*!
 * \brief Doubly linked list.
 *
 * This list class maintains a double linked list of Node objects. It is intended to work
 * similarly to the STL std::list class, but is generally smaller in size and does not
 * support all of the STL features such as reverse iterators.
 *
 * All nodes of the list must be a subclass of DoubleList::Node. This class itself is not a
 * template in order to keep the code simple and to reduce duplicate code. Most users will
 * actually want to use the DoubleListT<T> template subclass that lets you specify your
 * Node subclass in the template argument, and thus works more like std::list.
 *
 * The user of this class must manage memory for nodes; this class assumes no responsibility
 * for creating or deleting node objects. In particular, when the list object is deleted it
 * will not automatically delete any nodes in the list for you. Thus, you must take special
 * care to walk the list and delete nodes before deleting the list. This is done to allow
 * users greater control over the memory lifetimes of node objects. If you need a certain
 * behaviour, you may want to subclass DoubleList to add your own memory management.
 *
 * To walk a list, use an iterator object very much like you would for std::list. Get the
 * iterator with getBegin(), and compare against the iterator returned by getEnd() to tell
 * when there are no more nodes to return.
 *
 * Example list walking code:
 * \code
 *  DoubleList mylist;
 *  DoubleList::Iterator myit = mylist.getBegin();
 *  
 *  for (; myit != mylist.getEnd(); ++myit)
 *  {
 *      DoubleList::Node * n = *myit;
 *      // use the node
 *  }
 * \endcode
 *
 * It is also possible to use a simpler form of iteration, like this:
 * \code
 *  DoubleList::Iterator myit = mylist.getBegin();
 *  DoubleList::Node * n;
 *
 *  while ((n = *myit++))
 *  {
 *      // use the node
 *  }
 * \endcode
 *
 * Notice that this second form of iteration uses the postfix increment operator supported
 * by the Iterator class. Both styles of iteration are equally efficient, so you can use
 * whichever one you prefer.
 */
class DoubleList
{
public:

    /*!
     * \brief List node base class.
     *
     * Subclass this node class to add your own data. You may also use this class as a
     * mix-in with other classes. The next and previous link data members provided by this
     * class are what allow it to be inserted in a DoubleList.
     */
    class Node
    {
    public:
        //! \brief Default constructor.
        Node();
        
        //! \name Sibling access
        //@{
        Node * getNext() { return m_next; }
        const Node * getNext() const { return m_next; }
        
        Node * getPrevious() { return m_prev; }
        const Node * getPrevious() const { return m_prev; }
        //@}
    
    protected:
        Node * m_prev;   //!< Pointer to previous node in the list. 
        Node * m_next;   //!< Pointer to next node in the list.
        
        friend class DoubleList;
    };
    
    /*!
     * \brief List iterator class.
     *
     * An iterator with a NULL node pointer represents the item after the end of the list.
     *
     * Both prefix and postfix increment and decrement operators are supported. This allows
     * you to use an alternate iteration style that uses a while loop instead of the for
     * loop and comparison against the end iterator that simulates the STL style.
     */
    class Iterator
    {
    public:
        //! \brief Constructor.
        Iterator(Node * theNode) : m_current(theNode) {}
        
        //! \brief Copy constructor.
        Iterator(const Iterator & other) : m_current(other.m_current) {}
        
        //! \brief Assignment operator.
        Iterator & operator = (const Iterator & other)
        {
            m_current = other.m_current;
            return *this;
        }
        
        //! \name Prefix increment/decrement
        //@{
        void operator ++ () { if (m_current) m_current = m_current->getNext(); }
        void operator -- () { if (m_current) m_current = m_current->getPrevious(); }
        //@}

        //! \name Postfix increment/decrement
        //@{
        void operator ++ (int) { ++(*this); }
        void operator -- (int) { --(*this); }
        //@}
        
        //! \name Conversion operators
        //@{
        Node * operator * () { return m_current; }
        Node * operator -> () { return m_current; }
        operator Node * () { return m_current; }
        //@}
        
        //! \name Comparison operators
        //@{
        bool operator == (const Iterator & other) { return m_current == other.m_current; }
        bool operator != (const Iterator & other) { return !(*this == other); }
        //@}
    
    protected:
        //! \brief The current node pointed to by this iterator.
        //!
        //! This pointer will be NULL when the iterator represents the end of the list.
        Node * m_current;
    };

    //! \brief Default constructor.
    DoubleList();
    
    //! \name List operations
    //@{
    //! \brief Insert \a node into the list at the start of the list.
    void insertFront(Node * node) { insertAfter(node, NULL); }

    //! \brief Append the node onto the end of the list.
    void insertBack(Node * node) { insertAfter(node, getTail()); }

    //! \brief Insert \a node into the list after node \a insertPos.
    //!
    //! If (NULL == \a insertPos), then \a node is inserted at the head of the list instead.
    void insertAfter(Node * node, Node * insertPos);

    //! \brief Insert \a node into the list before node \a insertPos.
    //!
    //! If (NULL == \a insertPos), then \a node is inserted at the tail of the list instead.
    void insertBefore(Node * node, Node * insertPos);

    //! \brief Remove \a node from its place in the list.
    void remove(Node * node);
    
    //! \brief Remove all items fom the list.
    void clear();
    //@}
    
    //! \name Access
    //@{
    //! \brief Returns the first item in the list.
    Node * getHead() { return m_head; }

    //! \brief Returns the first item in the list.
    const Node * getHead() const { return m_head; }

    //! \brief Returns the first item in the list.
    Node * getTail() { return m_tail; }

    //! \brief Returns the first item in the list.
    const Node * getTail() const { return m_tail; }
    //@}
    
    //! \name Iterator creation
    //@{
    Iterator getBegin() { return Iterator(m_head); }
    Iterator getEnd() { return Iterator(NULL); }
    //@}
    
    //! \name List info
    //@{
    //! \brief Returns true if the list has no items in it.
    bool isEmpty() const { return m_head == NULL; }
    
    //! \brief Returns the number of items currently in the list.
    int getSize() const { return m_size; }
    
    //! \brief Searches the list for the given node.
    bool containsNode(Node * theNode);
    //@}

protected:
    Node * m_head;   //!< The head of the list.
    Node * m_tail;   //!< The tail of the list.
    int m_size;     //!< Number of items in the list.
};

/*!
 * \brief Template subclass of DoubleList to simplify its usage.
 *
 * This template class extends its DoubleList superclass so that the type of all node objects
 * is the type specified in the template argument. This allows you to use the class without
 * having to cast to your custom Node subclass all over the place. The same concept applies
 * to the DoubleList<T>::Iterator class defined herein.
 *
 * A major reason for having a non-template base class is so that the code can be easily placed
 * in the correct linker section. It also reduces duplication of code dramatically. If the
 * entire list class were a template, then all of its code may be duplicated by the compiler
 * for each template variant.
 */
template <class T>
class DoubleListT : public DoubleList
{
public:

    /*!
     * \brief List iterator that works with the template node class.
     */
    class Iterator : public DoubleList::Iterator
    {
    public:
        //! \brief Constructor.
        Iterator(T * theNode) : DoubleList::Iterator(theNode) {}
        
        //! \brief Copy constructor.
        Iterator(const Iterator & other) : DoubleList::Iterator(other.m_current) {}
        
        //! \brief Assignment operator.
        Iterator & operator = (const Iterator & other)
        {
            m_current = other.m_current;
            return *this;
        }
        
        //! \name Conversion operators
        //@{
        T * operator * () { return static_cast<T*>(m_current); }
        T * operator -> () { return static_cast<T*>(m_current); }
        operator T * () { return static_cast<T*>(m_current); }
        //@}
    };

#pragma ghs section text=".init.text"
    //! \brief Default constructor.
    DoubleListT() : DoubleList() {}
#pragma ghs section text=default

    //! \name Access
    //@{
    //! \brief Returns the first item in the list.
    T * getHead() { return static_cast<T *>(m_head); }

    //! \brief Returns the first item in the list.
    const T * getHead() const { return static_cast<T *>(m_head); }

    //! \brief Returns the first item in the list.
    T * getTail() { return static_cast<T *>(m_tail); }

    //! \brief Returns the first item in the list.
    const T * getTail() const { return static_cast<T *>(m_tail); }
    //@}
    
    //! \name Iterator creation
    //@{
    Iterator getBegin() { return Iterator(static_cast<T*>(m_head)); }
    Iterator getEnd() { return Iterator(NULL); }
    //@}
    
    //! \brief Searches the list for the given node.
    bool containsNode(T * theNode) { return DoubleList::containsNode(theNode); }
    
};

#endif // __double_list_h__
////////////////////////////////////////////////////////////////////////////////
// End of file
////////////////////////////////////////////////////////////////////////////////
