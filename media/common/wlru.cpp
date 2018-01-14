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
//! \file wlru.cpp
//! \ingroup media_cache_internal
//! \brief Implementation of a weighted LRU class.
////////////////////////////////////////////////////////////////////////////////

#include "wlru.h"

///////////////////////////////////////////////////////////////////////////////
// Code
///////////////////////////////////////////////////////////////////////////////

#pragma ghs section text=".init.text"
WeightedLRUList::WeightedLRUList(int minWeight, int maxWeight, unsigned windowSize)
:   DoubleList(),
    m_maxWeight(maxWeight),
    m_scaleNumerator(1),
    m_scaleDenominator(1)
{
    // Compute window size.
    if (windowSize == 0)
    {
        m_scaleNumerator = 0;
        m_scaleDenominator = 0;
    }
    else
    {
        int k = m_maxWeight - minWeight;
        
        if (k > windowSize)
        {
            // Round up to fit within the window.
            m_scaleDenominator = (k + windowSize - 1) / windowSize;
        }
    }
}
#pragma ghs section text=default

WeightedLRUList::Node::Node()
:   DoubleList::Node()
{
}

void WeightedLRUList::insert(Node * node)
{
    Node * insertPos;
    
    // Find where to insert the entry.
    if (!node->isNodeValid())
    {
        // Invalid cache entries get inserted at the head/LRU of the list,
        // because we want to use them immediately.
        insertPos = NULL;
    }
    else
    {
        // If the window size is 0 then we just insert at the tail/MRU.
        insertPos = static_cast<Node *>(getTail());

        if (m_scaleDenominator > 0)
        {
            // The LRU window size is nonzero, so we need to use the weight on
            // this node to choose an insertion point proportionally toward the head/LRU.
            int ki = (m_maxWeight - node->getWeight()) * m_scaleNumerator / m_scaleDenominator;
            int i = 0;
            
            // Work from tail to head to find the insert position in the window for this
            // node's weight.
            while (insertPos && i < ki)
            {
                insertPos = static_cast<Node *>(insertPos->getPrevious());
                i++;
            }
            
        }
    }
    
    // Insert in sorted position.
    insertAfter(node, insertPos);
}

WeightedLRUList::Node * WeightedLRUList::select()
{
    Node * node = static_cast<Node *>(getHead());
    if (node)
    {
        remove(node);
    }
    return node;
}

WeightedLRUList::Node * WeightedLRUList::select(const NodeMatch & matcher)
{
    Iterator myit = getBegin();

    for (; myit != getEnd(); ++myit)
    {
        Node * node = static_cast<Node *>(*myit);
        if (matcher.isMatch(node))
        {
            remove(node);
            return node;
        }
    }
    return 0;
}

void WeightedLRUList::deselect(Node * node)
{
    insertFront(node);
}

////////////////////////////////////////////////////////////////////////////////
// End of file
////////////////////////////////////////////////////////////////////////////////
