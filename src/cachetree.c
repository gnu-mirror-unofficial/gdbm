/* cachetree.c - Implementation of the red-black tree for cache lookups. */

/* This file is part of GDBM, the GNU data base manager.
   Copyright (C) 2019 Free Software Foundation, Inc.

   GDBM is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3, or (at your option)
   any later version.

   GDBM is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with GDBM. If not, see <http://www.gnu.org/licenses/>.   */

#include "autoconf.h"
#include "gdbmdefs.h"
#define NDEBUG
#include "assert.h"

enum cache_node_color { RED, BLACK };

typedef struct cache_node cache_node;
struct cache_node
{
  cache_node *left, *right, *parent; 
  enum cache_node_color color;
  cache_elem *elem;
};

struct cache_tree
{
  cache_node *root;   /* Root of the tree */
  cache_node *avail;  /* List of available nodes, linked by parent field */
  GDBM_FILE dbf;      /* Database this tree belongs to */
};

/* Allocate and return a new node.  Pick the head item from the avail
   list and update the avail pointer.  If the list is empty, malloc
   a new node.
   All members in the returned node are filled with 0.
*/
static cache_node *
rbt_node_alloc (cache_tree *tree)
{
  cache_node *n;

  n = tree->avail;
  if (n)
    tree->avail = n->parent;
  else
    {
      n = malloc (sizeof (*n));
      if (!n)
	return NULL;
    }
  memset (n, 0, sizeof (*n));
  return n;
}

/* Return the node N to the avail list in TREE. */
static void
rbt_node_dealloc (cache_tree *tree, cache_node *n)
{
  n->parent = tree->avail;
  tree->avail = n;
}

/* Red-black tree properties:
     1. Each node is either red or black.
     2. The root node is black.
     3. All leaves are black and contain no data.
     4. Every red node has two children, and both are black.
        IOW, the parent of every red node is black.
     5. All paths from any given node to its leaf nodes contain the same
        number of black nodes.
 */

/* Auxiliary functions for accessing nodes. */

/* Return the grandparent node of N.
   Prerequisite: N may not be root.
*/
static inline cache_node *
grandparent (cache_node *n)
{
  return n->parent->parent;
}

/* Return the sibling node of N.
   Prerequisite: N may not be root.
*/   
static inline cache_node *
sibling (cache_node *n)
{
  return (n == n->parent->left) ? n->parent->right : n->parent->left;
}

/* Return the uncle node of N.
   Prerequisite: N must be at least 2 nodes away from root.
*/   
static inline cache_node *
uncle (cache_node *n)
{
  return sibling (n->parent);
}

/* Returns the color of the node N.
   Empty leaves are represented by NULL, therefore NULL is assumed to
   be black (see property 3).
*/
static inline enum cache_node_color
node_color (cache_node *n)
{
  return n == NULL ? BLACK : n->color;
}

/* Replace the OLDN with NEWN.
   Does not modify OLDN. */
static void
replace_node (cache_tree *tree, cache_node *oldn, cache_node *newn)
{
  if (oldn->parent == NULL)
    tree->root = newn;
  else if (oldn == oldn->parent->left)
    oldn->parent->left = newn;
  else
    oldn->parent->right = newn;

  if (newn != NULL)
    newn->parent = oldn->parent;
}

/* Rotate the TREE left over the node N. */
static void
rotate_left (cache_tree *tree, cache_node *n)
{
  cache_node *right = n->right;
  replace_node (tree, n, right);
  n->right = right->left;
  if (right->left != NULL)
    right->left->parent = n;
  right->left = n;
  n->parent = right;
}

/* Rotate the TREE right over the node N. */
static void
rotate_right (cache_tree *tree, cache_node *n)
{
  cache_node *left = n->left;
  replace_node (tree, n, left);
  n->left = left->right;
  if (left->right != NULL)
    left->right->parent = n;
  left->right = n;
  n->parent = left;
}

/* Node deletion */
static void rbt_delete_fixup (cache_tree *tree, cache_node *n);

/* Remove N from the TREE. */
void
_gdbm_cache_tree_delete (cache_tree *tree, cache_node *n)
{
  cache_node *child;

  /* If N has both left and right children, reduce the problem to
     the node with only one child.  To do so, find the in-order
     predecessor of N, copy its value (elem) to N and then delete
     the predecessor. */
  if (n->left != NULL && n->right != NULL)
    {
      cache_node *p;
      for (p = n->left; p->right; p = p->right)
        ;
      n->elem = p->elem;
      n->elem->ca_node = n;
      n = p;
    }

  /* N has only one child. Select it. */
  child = n->left ? n->left : n->right;
  if (node_color (n) == BLACK)
    {
      n->color = node_color (child);
      rbt_delete_fixup (tree, n);
    }
  replace_node (tree, n, child);
  if (n->parent == NULL && child != NULL)	/* root should be black */
    child->color = BLACK;

  /* Return N to the avail pool */
  rbt_node_dealloc (tree, n);
}

static void
rbt_delete_fixup (cache_tree *tree, cache_node *n)
{
  while (1)
    {
      if (n->parent == NULL)
	{
	  /* If N has become the root node, deletion resulted in removing
	     one black node (prior root) from every path, so all properties
	     still hold.
	  */
	  return;
	}
      else
	{
	  /* If N has a red sibling, change the colors of the parent and
	     sibling and rotate about the parent.  Thus, the sibling becomes
	     grandparent and we can proceed to the next case.
	  */
	  if (node_color (sibling (n)) == RED)
	    {
	      n->parent->color = RED;
	      sibling (n)->color = BLACK;
	      if (n == n->parent->left)
		rotate_left (tree, n->parent);
	      else
		rotate_right (tree, n->parent);
	    }

	  /* If the parent, sibling and nephews are all black, paint the
	     sibling red.  This means one black node was removed from all
	     paths passing through the parent, so we recurse to the beginning
	     of the loop with parent as the argument to restore the properties.
	     This is the only branch that loops.
	  */
	  if (node_color (n->parent) == BLACK
	      && node_color (sibling (n)) == BLACK
	      && node_color (sibling (n)->left) == BLACK
	      && node_color (sibling (n)->right) == BLACK)
	    {
	      sibling (n)->color = RED;
	      n = n->parent;
	      continue;
	    }
	  else
	    {
	      /* If the sibling and nephews are black but the parent is red,
		 swap the colors of the sibling and parent.  The properties
		 are then restored.
	      */
	      if (node_color (n->parent) == RED
		  && node_color (sibling (n)) == BLACK
		  && node_color (sibling (n)->left) == BLACK
		  && node_color (sibling (n)->right) == BLACK)
		{
		  sibling (n)->color = RED;
		  n->parent->color = BLACK;
		}
	      else
		{
		  /* N is the left child of its parent, its sibling is black,
		     and the sibling's right child is black. Swap the colors
		     of the sibling and its left sibling and rotate right
		     over the sibling.
		  */
		  if (n == n->parent->left
		      && node_color (sibling (n)) == BLACK
		      && node_color (sibling (n)->left) == RED
		      && node_color (sibling (n)->right) == BLACK)
		    {
		      sibling (n)->color = RED;
		      sibling (n)->left->color = BLACK;
		      rotate_right (tree, sibling (n));
		    }
		  else if (n == n->parent->right
			   && node_color (sibling (n)) == BLACK
			   && node_color (sibling (n)->right) == RED
			   && node_color (sibling (n)->left) == BLACK)
		    {
		      /* The mirror case is handled similarly. */
		      sibling (n)->color = RED;
		      sibling (n)->right->color = BLACK;
		      rotate_left (tree, sibling (n));
		    }
		  /* N is the left child of its parent, its sibling is black
		     and the sibling's right child is red.  Swap the colors
		     of the parent and sibling, paint the sibling's right
		     child black and rotate left at the parent.  Similarly
		     for the mirror case.  This achieves the following:
	     
		     . A black node is added to all paths passing through N;
		     . A black node is removed from all paths through the
		       sibling's red child.
		     . The latter is painted black which restores missing
		       black node in all paths through the sibling's red child.

		     Another sibling's child becomes a child of N's parent
		     during the rotation and is therefore not affected.
		  */
		  sibling (n)->color = node_color (n->parent);
		  n->parent->color = BLACK;
		  if (n == n->parent->left)
		    {
		      sibling (n)->right->color = BLACK;
		      rotate_left (tree, n->parent);
		    }
		  else
		    {
		      sibling (n)->left->color = BLACK;
		      rotate_right (tree, n->parent);
		    }
		}
	    }
	}
      break;
    }
}

/* Insertion */
static void rbt_insert_fixup (cache_tree *tree, cache_node *n);

/* Look up the node with elem->ca_adr equal to ADR.
   If found, put it in *RETVAL and return node_found.

   Otherwise, if INSERT is TRUE, create a new node and insert it in the
   appropriate place in the tree.  Store the address of the newly created
   node in *RETVAL and return node_new.  If a new node cannot be created
   (memory exhausted), return node_failure.

   Otherwise, if INSERT is FALSE, store NULL in *RETVAL and return node_new.
*/
static int
cache_tree_lookup (cache_tree *tree, off_t adr, int insert,
		   cache_node **retval)
{
  int res;
  cache_node *node, *parent = NULL;
  cache_node **nodeptr;

  nodeptr = &tree->root;
  while ((node = *nodeptr) != NULL)
    {
      if (adr == node->elem->ca_adr)
	break;
      parent = node;
      if (adr < node->elem->ca_adr)
	nodeptr = &node->left;
      else
	nodeptr = &node->right;
    }
  
  if (node)
    {
      res = node_found;
    }
  else
    {
      if (insert)
	{
	  node = rbt_node_alloc (tree);
	  if (!node)
	    return node_failure;
	  node->elem = _gdbm_cache_elem_new (tree->dbf, adr);
	  if (!node->elem)
	    {
	      rbt_node_dealloc (tree, node);
	      return node_failure;
	    }
	  node->elem->ca_node = node;
	  *nodeptr = node;
	  node->parent = parent;
	  rbt_insert_fixup (tree, node);
	}
      res = node_new;
    }
  *retval = node;
  return res;
}

static void
rbt_insert_fixup (cache_tree *tree, cache_node *n)
{
  while (1)
    {
      if (n->parent == NULL)
	{
	  /* Node was inserted at the root of the tree.
	     The root node must be black (property 2).  Changing its color
	     to black would add one black node to every path, which means
	     the property 5 would remain satisfied.  So we simply paint the
	     node black.
	  */
	  n->color = BLACK;
	}
      else if (node_color (n->parent) == BLACK)
	{
	  /* The node has black parent.
	     All properties are satisfied.  There's no need to change anything.
	  */
	  return;
	}
      else if (node_color (uncle (n)) == RED)
	{
	  /* The uncle node is red.
	     Repaint the parent and uncle black and the grandparent red.  This
	     would satisfy 4.  However, if the grandparent is root, this would
	     violate the property 2.  So we repaint the grandparent by
	     re-entering the fixup loop with grandparent as the node.
	     This is the only branch that loops.
	  */
	  n->parent->color = BLACK;
	  uncle (n)->color = BLACK;
	  n = grandparent (n);
	  n->color = RED;
	  continue;
	}
      else
	{
	  /* The new node is the right child of its parent and the parent is
	     the left child of the grandparent.  Rotate left about the parent.
	     Mirror case: The new node is the left child of its parent and the
	     parent is the right child of the grandparent.  Rotate right about
	     the parent.  This fixes the properties for the rbt_insert_5.
	  */
	  if (n == n->parent->right && n->parent == grandparent (n)->left)
	    {
	      rotate_left (tree, n->parent);
	      n = n->left;
	    }
	  else if (n == n->parent->left && n->parent == grandparent (n)->right)
	    {
	      rotate_right (tree, n->parent);
	      n = n->right;
	    }

	  /* The new node is the left child of its parent and the parent is the
	     left child of the grandparent. Rotate right about the grandparent.
	     Mirror case: The new node is the right child of its parent and the
	     parent
	     is the right child of the grandparent. Rotate left.
	  */
	  n->parent->color = BLACK;
	  grandparent (n)->color = RED;
	  if (n == n->parent->left && n->parent == grandparent (n)->left)
	    {
	      rotate_right (tree, grandparent (n));
	    }
	  else
	    {
	      rotate_left (tree, grandparent (n));
	    }
	}
      break;
    }
}

/* Interface functions */

/* Create a cache tree structure for the database file DBF. */
cache_tree *
_gdbm_cache_tree_alloc (GDBM_FILE dbf)
{
  cache_tree *t = malloc (sizeof (*t));
  if (t)
    {
      t->root = NULL;
      t->avail = NULL;
      t->dbf = dbf;
    }
  return t;
}

/* Free the memory used by the TREE. */
void
_gdbm_cache_tree_destroy (cache_tree *tree)
{
  cache_node *n;
  while ((n = tree->root) != NULL)
    _gdbm_cache_tree_delete (tree, n);
  while ((n = tree->avail) != NULL)
    {
      tree->avail = n->parent;
      free (n);
    }
  free (tree);
}

/* Look up the node with elem->ca_adr equal to ADR.
   If found, store its pointer in *RETVAL and return node_found.
   Otherwise, return node_new and don't touch RETVAL.
*/
int
_gdbm_cache_tree_lookup (cache_tree *tree, off_t adr, cache_elem **retval)
{
  cache_node *n;
  int rc = cache_tree_lookup (tree, adr, TRUE, &n);
  if (rc != node_failure)
    *retval = n->elem;
  return rc;
}

