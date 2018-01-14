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
//! \file RedBlackTree.cpp
//! \ingroup media_cache_internal
//! \brief A red black tree implementation.
//!
//! The original red black tree source had this license:
//!
//! Redistribution and use in source and binary forms, with or without
//! modification, are permitted provided that neither the name of Emin
//! Martinian nor the names of any contributors are be used to endorse or
//! promote products derived from this software without specific prior
//! written permission.
//! 
//! THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
//! "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
//! LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
//! A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
//! OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
//! SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
//! LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
//! DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
//! THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
//! (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
//! OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//!
////////////////////////////////////////////////////////////////////////////////

#include "RedBlackTree.h"

//! If the symbol CHECK_RB_TREE_ASSUMPTIONS is defined then the
//! code does a lot of extra checking to make sure certain assumptions
//! are satisfied.  This only needs to be done if you suspect bugs are
//! present or if you make significant changes and want to make sure
//! your changes didn't mess anything up.
#define CHECK_RB_TREE_ASSUMPTIONS 0

#pragma ghs section text=".init.text"
RedBlackTree::RedBlackTree()
:   m_rootStorage(),
    m_nilStorage(),
    m_root(&m_rootStorage),
    m_nil(&m_nilStorage)
{
    m_nil->m_left = m_nil->m_right = m_nil->m_parent = m_nil;
    m_nil->m_isRed = 0;
    
    m_root->m_parent = m_root->m_left = m_root->m_right = m_nil;
    m_root->m_isRed = 0;
}
#pragma ghs section text=default

RedBlackTree::~RedBlackTree()
{
}

RedBlackTree::Node * RedBlackTree::find(Key_t key) const
{
    Node * node = m_root->m_left;
    
    while (node != m_nil)
    {
        Key_t nodeKey = node->getKey();
        
        // Return the current node if matches the search key.
        if (nodeKey == key)
        {
            return node;
        }
        
        // Follow the tree.
        node = nodeKey > key ? node->m_left : node->m_right;
    }
    
    // Didn't find a matching tree node, so return NULL.
    return NULL;
}

//! Rotates as described in _Introduction_To_Algorithms by
//! Cormen, Leiserson, Rivest (Chapter 14).  Basically this
//! makes the parent of x be to the left of x, x the parent of
//! its parent before the rotation and fixes other pointers
//! accordingly. 
void RedBlackTree::leftRotate(Node* x)
{
    Node * y;
    
    // I originally wrote this function to use the sentinel for
    // m_nil to avoid checking for m_nil.  However this introduces a
    // very subtle bug because sometimes this function modifies
    // the parent pointer of m_nil.  This can be a problem if a
    // function which calls leftRotate also uses the m_nil sentinel
    // and expects the m_nil sentinel's parent pointer to be unchanged
    // after calling this function.  For example, when DeleteFixUP
    // calls leftRotate it expects the parent pointer of m_nil to be
    // unchanged.
    
    y = x->m_right;
    x->m_right = y->m_left;
    
    if (y->m_left != m_nil)
    {
        y->m_left->m_parent = x;
    }
    
    y->m_parent = x->m_parent;   
    
    // Instead of checking if x->m_parent is the m_root as in the book, we
    // count on the m_root sentinel to implicitly take care of this case
    if (x == x->m_parent->m_left)
    {
        x->m_parent->m_left = y;
    }
    else
    {
        x->m_parent->m_right = y;
    }
    y->m_left = x;
    x->m_parent = y;

#ifdef CHECK_RB_TREE_ASSUMPTIONS
    checkAssumptions();
#endif
}

//! Rotates as described in _Introduction_To_Algorithms by
//! Cormen, Leiserson, Rivest (Chapter 14).  Basically this
//! makes the parent of x be to the left of x, x the parent of
//! its parent before the rotation and fixes other pointers
//! accordingly. 
void RedBlackTree::rightRotate(Node* y)
{
    Node * x;
    
    // I originally wrote this function to use the sentinel for
    // m_nil to avoid checking for m_nil.  However this introduces a
    // very subtle bug because sometimes this function modifies
    // the parent pointer of m_nil.  This can be a problem if a
    // function which calls leftRotate also uses the m_nil sentinel
    // and expects the m_nil sentinel's parent pointer to be unchanged
    // after calling this function.  For example, when DeleteFixUP
    // calls leftRotate it expects the parent pointer of m_nil to be
    // unchanged.
    
    x = y->m_left;
    y->m_left = x->m_right;
    
    if (m_nil != x->m_right)
    {
        x->m_right->m_parent = y;
    }
    
    // Instead of checking if x->m_parent is the m_root as in the book, we
    // count on the m_root sentinel to implicitly take care of this case.
    x->m_parent = y->m_parent;
    if (y == y->m_parent->m_left)
    {
        y->m_parent->m_left = x;
    }
    else
    {
        y->m_parent->m_right = x;
    }
    x->m_right = y;
    y->m_parent = x;

#ifdef CHECK_RB_TREE_ASSUMPTIONS
    checkAssumptions();
#endif
}

//! Inserts z into the tree as if it were a regular binary tree
//! using the algorithm described in _Introduction_To_Algorithms_
//! by Cormen et al.  This funciton is only intended to be called
//! by the Insert function and not by the user
void RedBlackTree::insertFixUp(Node * z)
{
    Node * x;
    Node * y;
    
    z->m_left = m_nil;
    z->m_right = m_nil;
    y = m_root;
    x = m_root->m_left;
    
    Key_t zKey = z->getKey();
    
    while (x != m_nil)
    {
        y = x;
        if (x->getKey() > zKey)
        { 
            x = x->m_left;
        }
        else
        {
            // x->key <= z->key
            x = x->m_right;
        }
    }
    z->m_parent = y;
    
    if (y == m_root || (y->getKey() > zKey))
    { 
        y->m_left = z;
    }
    else 
    {
        y->m_right = z;
    }
}

//! Before calling InsertNode  the node x should have its key set
//!
//! This function returns a pointer to the newly inserted node
//! which is guarunteed to be valid until this node is deleted.
//! What this means is if another data structure stores this
//! pointer then the tree does not need to be searched when this
//! is to be deleted.
//!
//! Creates a node node which contains the appropriate key and
//! info pointers and inserts it into the tree.
RedBlackTree::Node * RedBlackTree::insert(Node * newNode)
{
    Node * y;
    Node * x;

    x = newNode;
    insertFixUp(x);
    x->m_isRed = 1;
    
    while (x->m_parent->m_isRed)
    {
        // Use sentinel instead of checking for m_root.
        if (x->m_parent == x->m_parent->m_parent->m_left)
        {
            y = x->m_parent->m_parent->m_right;
            if (y->m_isRed)
            {
                x->m_parent->m_isRed = 0;
                y->m_isRed = 0;
                x->m_parent->m_parent->m_isRed = 1;
                x = x->m_parent->m_parent;
            }
            else
            {
                if (x == x->m_parent->m_right)
                {
                    x = x->m_parent;
                    leftRotate(x);
                }
                x->m_parent->m_isRed = 0;
                x->m_parent->m_parent->m_isRed = 1;
                rightRotate(x->m_parent->m_parent);
            } 
        }
        else
        {
            // Case for x->m_parent == x->m_parent->m_parent->m_right.
            // This part is just like the section above with
            // left and right interchanged.
            y = x->m_parent->m_parent->m_left;
            if (y->m_isRed)
            {
                x->m_parent->m_isRed = 0;
                y->m_isRed = 0;
                x->m_parent->m_parent->m_isRed = 1;
                x = x->m_parent->m_parent;
            }
            else
            {
                if (x == x->m_parent->m_left)
                {
                    x = x->m_parent;
                    rightRotate(x);
                }
                x->m_parent->m_isRed = 0;
                x->m_parent->m_parent->m_isRed = 1;
                leftRotate(x->m_parent->m_parent);
            }
        }
    }
    m_root->m_left->m_isRed = 0;

#ifdef CHECK_RB_TREE_ASSUMPTIONS
    checkAssumptions();
#endif

    return(newNode);
}

//! This function returns the successor of x or NULL if no
//! successor exists.
RedBlackTree::Node * RedBlackTree::getSuccessorOf(Node * x) const
{ 
    Node * y;
    
    if (x == NULL)
    {
        x = m_nil;
    }
    
    if (m_nil != (y = x->m_right)) // Assignment to y is intentional.
    {
        while (y->m_left != m_nil)
        {
            // Returns the minium of the right subtree of x.
            y = y->m_left;
        }
        return y;
    }
    else
    {
        y = x->m_parent;
        while (x == y->m_right)
        {
            // Sentinel used instead of checking for m_nil.
            x = y;
            y = y->m_parent;
        }
        
        if (y == m_root)
        {
            return NULL;
        }
        
        return y;
    }
}

//! This function returns the predecessor of x or NULL if no
//! predecessor exists.
RedBlackTree::Node * RedBlackTree::getPredecessorOf(Node * x) const
{
    Node * y;
    
    if (x == NULL)
    {
        x = m_nil;
    }
    
    if (m_nil != (y = x->m_left)) // Assignment to y is intentional.
    {
        while (y->m_right != m_nil)
        {
            // Returns the maximum of the left subtree of x.
            y = y->m_right;
        }
        return y;
    }
    else
    {
        y = x->m_parent;
        while (x == y->m_left)
        { 
            if (y == m_root)
            {
                return NULL;
            }
            
            x = y;
            y = y->m_parent;
        }
        return y;
    }
}

//! \param x The child of the spliced out node in DeleteNode.
//!
//! Performs rotations and changes colors to restore red-black
//! properties after a node is deleted
void RedBlackTree::deleteFixUp(Node* x)
{
    Node * w;
    Node * rootLeft = m_root->m_left;
    
    while ((!x->m_isRed) && (rootLeft != x))
    {
        if (x == x->m_parent->m_left)
        {
            w = x->m_parent->m_right;
            if (w->m_isRed)
            {
                w->m_isRed = 0;
                x->m_parent->m_isRed = 1;
                leftRotate(x->m_parent);
                w = x->m_parent->m_right;
            }
            
            if ((!w->m_right->m_isRed) && (!w->m_left->m_isRed))
            { 
                w->m_isRed = 1;
                x = x->m_parent;
            }
            else
            {
                if (!w->m_right->m_isRed)
                {
                    w->m_left->m_isRed = 0;
                    w->m_isRed = 1;
                    rightRotate(w);
                    w = x->m_parent->m_right;
                }
                
                w->m_isRed = x->m_parent->m_isRed;
                x->m_parent->m_isRed = 0;
                w->m_right->m_isRed = 0;
                leftRotate(x->m_parent);
                x = rootLeft; // This is to exit while loop.
            }
        }
        else
        {
            // The code below is has left and right switched from above.
            w = x->m_parent->m_left;
            if (w->m_isRed)
            {
                w->m_isRed = 0;
                x->m_parent->m_isRed = 1;
                rightRotate(x->m_parent);
                w = x->m_parent->m_left;
            }
            
            if ((!w->m_right->m_isRed) && (!w->m_left->m_isRed))
            { 
                w->m_isRed = 1;
                x = x->m_parent;
            }
            else
            {
                if (!w->m_left->m_isRed)
                {
                    w->m_right->m_isRed = 0;
                    w->m_isRed = 1;
                    leftRotate(w);
                    w = x->m_parent->m_left;
                }
                
                w->m_isRed = x->m_parent->m_isRed;
                x->m_parent->m_isRed = 0;
                w->m_left->m_isRed = 0;
                rightRotate(x->m_parent);
                x = rootLeft; // This is to exit while loop
            }
        }
    }
    x->m_isRed = 0;

#ifdef CHECK_RB_TREE_ASSUMPTIONS
    checkAssumptions();
#endif
}

//! Deletes z from tree.
//!
void RedBlackTree::remove(Node * z)
{
    // Check if the node is already not a node of the tree.
    if (z->m_left == NULL || z->m_right == NULL || z->m_parent == NULL)
    {
        return;
    }
    
    Node * y;
    Node * x;
    
    y = ((z->m_left == m_nil) || (z->m_right == m_nil)) ? z : getSuccessorOf(z);
    x = (y->m_left == m_nil) ? y->m_right : y->m_left;
    
    if (m_root == (x->m_parent = y->m_parent)) // Assignment of y->p to x->p is intentional
    {
        m_root->m_left = x;
    }
    else
    {
        if (y == y->m_parent->m_left)
        {
            y->m_parent->m_left = x;
        }
        else
        {
            y->m_parent->m_right = x;
        }
    }
    
    if (y != z)
    {
        // y should not be m_nil in this case
        assert(y != m_nil);
        
        // y is the node to splice out and x is its child
        
        y->m_left = z->m_left;
        y->m_right = z->m_right;
        y->m_parent = z->m_parent;
        z->m_left->m_parent = z->m_right->m_parent=y;
        
        if (z == z->m_parent->m_left)
        {
            z->m_parent->m_left = y; 
        }
        else
        {
            z->m_parent->m_right = y;
        }
        
        if (!(y->m_isRed))
        {
            y->m_isRed = z->m_isRed;
            deleteFixUp(x);
        }
        else
        {
            y->m_isRed = z->m_isRed;
        }
    }
    else // y == z
    {
        if (!(y->m_isRed))
        {
            deleteFixUp(x);
        }
    }
    
    // Clear links of the removed node.
    z->m_parent = NULL;
    z->m_left = NULL;
    z->m_right = NULL;
    z->m_isRed = 0;

#ifdef CHECK_RB_TREE_ASSUMPTIONS
    checkAssumptions();
#endif
}

void RedBlackTree::checkAssumptions() const
{
    assert(m_nil->m_isRed == 0);
    assert(m_root->m_isRed == 0);
}
 
////////////////////////////////////////////////////////////////////////////////
// End of file
////////////////////////////////////////////////////////////////////////////////


