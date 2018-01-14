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
//! \file wlru.h
//! \ingroup media_cache_internal
//! \brief Declaration of a weighted LRU class.
////////////////////////////////////////////////////////////////////////////////
#if !defined(_wlru_h_)
#define _wlru_h_

#include "DoubleList.h"

///////////////////////////////////////////////////////////////////////////////
// Types
///////////////////////////////////////////////////////////////////////////////

/*!
 * \brief Class to manage a weighted LRU list.
 *
 * This class maintains a doubly linked list of node sorted in LRU order, which is
 * equivalent to FIFO order. In addition to strict LRU ordering, the class features
 * support for weighted LRU ordering. That is, highly weighted objects have a
 * higher "recency" than low weighted objects. This allows the user to retain objects
 * with a high cost of loading or known high access frequency more than other objects.
 *
 * The constuctor takes a maximum window size parameter. If the computed window size
 * is larger than the maximum, then weights will be scaled down to fit. Pass 0 to
 * disable weighting entirely and enforce strict LRU ordering.
 *
 * \ingroup media_cache_internal
 */
class WeightedLRUList : public DoubleList
{
public:

    /*!
     * \brief Abstract base class for a node in an LRU list.
     * \ingroup media_cache_internal
     */
    class Node : public DoubleList::Node
    {
    public:

        //! \brief Default constructor.
        Node();
        
        //! \brief Pure virtual method that is used to determine if the node is valid.
        virtual bool isNodeValid() const = 0;
        
        //! \brief Pure virtual method to return the node's weight value.
        virtual int getWeight() const = 0;

    };

    /*!
     * \brief Abstract base class for a node matcher.
     * \ingroup media_cache_internal
     */
    class NodeMatch
    {
    public:

        //! \brief Pure virtual method to determine if node is a match.
        virtual bool isMatch(const Node * node) const = 0;
    };

public:
    //! \brief Constructor.
    WeightedLRUList(int minWeight, int maxWeight, unsigned windowSize);
    
    //! \name List operations
    //@{
    //! \brief Insert \a node into the list at or near the tail/MRU position.
    //!
    //! The node is nominally inserted at the tail/MRU position, but can be moved
    //! further away from the tail by using the weighting scheme.
    //! If weights are being used in this LRU, then a weight of \a m_maxWeight on \a node will cause
    //! insertion at the tail, and a weight of zero on \a node will cause insertion
    //! at the head.
    void insert(Node * node);
    
    //! \brief Get the oldest entry (head/LRU) in the list.
    Node * select();

    //! \brief Find a matching node in the list.
    Node * select(const NodeMatch & matcher);

    //! \brief Put \a node back on the head/LRU of the list.
    void deselect(Node * node);

protected:
    int m_maxWeight;    //!< Maximum weight value.
    int m_scaleNumerator;   //!< Scale multiplier.
    int m_scaleDenominator; //!< Scale divider.
};

#endif // _wlru_h_
////////////////////////////////////////////////////////////////////////////////
// End of file
////////////////////////////////////////////////////////////////////////////////
