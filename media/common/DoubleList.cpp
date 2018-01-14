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
//! \file DoubleList.cpp
//! \ingroup media_cache_internal
//! \brief Implementation of the doulbly linked list class.
////////////////////////////////////////////////////////////////////////////////

#include "DoubleList.h"

///////////////////////////////////////////////////////////////////////////////
// Code
///////////////////////////////////////////////////////////////////////////////

#pragma ghs section text=".init.text"
DoubleList::DoubleList()
:   m_head(0),
    m_tail(0),
    m_size(0)
{
}
#pragma ghs section text=default

DoubleList::Node::Node()
:   m_prev(NULL),
    m_next(NULL)
{
}

void DoubleList::insertAfter(Node * node, Node * insertPos)
{
    assert(node);

    if (!insertPos)
    {
        // Inserting at the head/LRU of the list.
        node->m_prev = NULL;
        node->m_next = m_head;
        
        // Update list head.
        if (m_head)
        {
            m_head->m_prev = node;
        }
        m_head = node;
        
        // Update tail. Special case for single item list.
        if (!m_tail)
        {
            m_tail = node;
        }
    }
    else
    {   
        // Insert after insertPos.
        node->m_next = insertPos->m_next;
        if (node->m_next)
        {
            node->m_next->m_prev = node;
        }
        insertPos->m_next = node;
        node->m_prev = insertPos;
        
        // Update list tail.
        if (insertPos == m_tail)
        {
            m_tail = node;
        }
    }
    
    ++m_size;
}

void DoubleList::insertBefore(Node * node, Node * insertPos)
{
    if (insertPos)
    {
        // Insert after the node previous to the given position.
        insertAfter(node, insertPos->getPrevious());
    }
    else
    {
        // The insert position was NULL, so put at the end of the list.
        insertBack(node);
    }
}

void DoubleList::remove(Node * node)
{
    // Disconnect from list.
    if (node->m_prev)
    {
        node->m_prev->m_next = node->m_next;
    }
    if (node->m_next)
    {
        node->m_next->m_prev = node->m_prev;
    }
    
    if (m_head == node)
    {
        m_head = node->m_next;
    }
    
    if (m_tail == node)
    {
        m_tail = node->m_prev;
    }
    
    // Clear node links.
    node->m_next = NULL;
    node->m_prev = NULL;
    
    --m_size;
}

void DoubleList::clear()
{
    m_head = NULL;
    m_tail = NULL;
    m_size = 0;
}

bool DoubleList::containsNode(Node * theNode)
{
    Iterator it = getBegin();
    Iterator end = getEnd();
    
    for (; it != end; ++it)
    {
        if (*it == theNode)
        {
            return true;
        }
    }
    
    return false;
}

////////////////////////////////////////////////////////////////////////////////
// End of file
////////////////////////////////////////////////////////////////////////////////
