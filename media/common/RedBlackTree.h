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
//! \file RedBlackTree.h
//! \ingroup media_cache_internal
//! \brief Class declarations for a red black tree implementation.
////////////////////////////////////////////////////////////////////////////////
#ifndef E_REDBLACK_TREE
#define E_REDBLACK_TREE

#include "types.h"
#include <math.h>
#include <limits.h>

///////////////////////////////////////////////////////////////////////////////
// Types
///////////////////////////////////////////////////////////////////////////////

/*!
 * \brief Red black tree class.
 *
 * \note This tree class is specially optimized for the media cache and is not
 * intended to be totally general purpose. For one, it assumes that all tree
 * node objects, instances of RedBlackTreeNode, are pre-allocated and do not
 * have to be freed when they are removed from the tree.
 *
 * A sentinel is used for root and for nil.  These sentinels are
 * created when RedBlackTreeCreate is caled.  root->left should always
 * point to the node which is the root of the tree.  nil points to a
 * node which should always be black but has aribtrary children and
 * parent and no key or info.  The point of using these sentinels is so
 * that the root and nil nodes do not require special cases in the code
 *
 * \ingroup media_cache_internal
 */
class RedBlackTree
{
public:

    //! \brief Type for the key values used in the red black tree.
    typedef int64_t Key_t;

    /*!
     * \brief Abstract base class for a node of the red black tree.
     *
     * Subclasses of this node class must implement the getKey() virtual method
     * to return the actual key value for that node. The key value must not
     * change while the node is inserted in the tree, or unexpected behaviour
     * will occur (i.e., the tree will get all screwed up).
     *
     * \ingroup media_cache_internal
     */
    class Node
    {
    public:
        //! \brief Constructor.
        Node()
        :   m_isRed(0),
            m_left(0),
            m_right(0),
            m_parent(0)
        {
        }
        
        //! \brief Copy constructor.
        Node(const Node & other)
        :   m_isRed(other.m_isRed),
            m_left(other.m_left),
            m_right(other.m_right),
            m_parent(other.m_parent)
        {
        }
        
        //! \brief Destructor.
        virtual ~Node() {}
      
        //! \brief Key value accessor.
        virtual Key_t getKey() const = 0;
        
        //! \brief Red status accessor.
        inline bool isRed() const { return m_isRed; }
        
        //! \name Node link accessors
        //@{
            inline Node * getLeft() { return m_left; }
            inline Node * getRight() { return m_right; }
            inline Node * getParent() { return m_parent; }
        //@}

    protected:
        int m_isRed; //< If red=0 then the node is black.
        Node * m_left;
        Node * m_right;
        Node * m_parent;

        // The tree is our friend so it can directly access the node link pointers.
        friend class RedBlackTree;
    };
    
public:
    //! \brief Constructor.
    RedBlackTree();
    
    //! \brief Destructor.
    virtual ~RedBlackTree();

    //! \name Tree operations
    //@{
        void remove(Node * z);
        Node * insert(Node * newNode);
        Node * getPredecessorOf(Node * x) const;
        Node * getSuccessorOf(Node * x) const;
        Node * find(Key_t key) const;
    //@}
    
    //! \brief Validate certain invariants.
    void checkAssumptions() const;
    
protected:

    //! \brief Internal node subclass for the root sentinel node.
    class RootNode : public Node
    {
    public:
        virtual Key_t getKey() const { return LLONG_MAX; }
    };
    
    //! \brief Internal subclass for the nil sentinel node.
    class NilNode : public Node
    {
    public:
        virtual Key_t getKey() const { return LLONG_MIN; }
    };

    RootNode m_rootStorage;
    NilNode m_nilStorage;
    RootNode * m_root;
    NilNode * m_nil;
    
    void leftRotate(Node * x);
    void rightRotate(Node * y);
    void insertFixUp(Node * z);
    void deleteFixUp(Node * x);
};

#endif
////////////////////////////////////////////////////////////////////////////////
// End of file
////////////////////////////////////////////////////////////////////////////////
