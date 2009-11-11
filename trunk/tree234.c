#include "tree234.h"

/*
 * tree234.c: reasonably generic counted 2-3-4 tree routines.
 * 
 * This file is copyright 1999-2001 Simon Tatham.
 * 
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 * 
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT.  IN NO EVENT SHALL SIMON TATHAM BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
 * CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

typedef struct node234_Tag node234;

struct tree234 {
  node234 *root;
};

struct node234_Tag {
  node234 *parent;
  node234 *kids[4];
  int counts[4];
  void *elems[3];
};

/*
 * Create a 2-3-4 tree.
 */
tree234 *
newtree234(void)
{
  tree234 *ret = new(tree234);
  ret->root = null;
  return ret;
}

/*
 * Free a 2-3-4 tree (not including freeing the elements).
 */
static void
freenode234(node234 * n)
{
  if (!n)
    return;
  freenode234(n->kids[0]);
  freenode234(n->kids[1]);
  freenode234(n->kids[2]);
  freenode234(n->kids[3]);
  free(n);
}

void
freetree234(tree234 * t)
{
  freenode234(t->root);
  free(t);
}

/*
 * Internal function to count a node.
 */
static int
countnode234(node234 * n)
{
  int count = 0;
  int i;
  if (!n)
    return 0;
  for (i = 0; i < 4; i++)
    count += n->counts[i];
  for (i = 0; i < 3; i++)
    if (n->elems[i])
      count++;
  return count;
}

/*
 * Count the elements in a tree.
 */
int
count234(tree234 * t)
{
  if (t->root)
    return countnode234(t->root);
  else
    return 0;
}

/*
 * Add an element e to a 2-3-4 tree t. Returns e on success, or if
 * an existing element compares equal, returns that.
 */
void *
addpos234(tree234 * t, void *e, int index)
{
  if (index < 0)        /* index out of range */
    return null;        /* return failure */

  node234 *n, **np, *left, *right;
  void *orig_e = e;
  int lcount, rcount;

  if (t->root == null) {
    t->root = new(node234);
    t->root->elems[1] = t->root->elems[2] = null;
    t->root->kids[0] = t->root->kids[1] = null;
    t->root->kids[2] = t->root->kids[3] = null;
    t->root->counts[0] = t->root->counts[1] = 0;
    t->root->counts[2] = t->root->counts[3] = 0;
    t->root->parent = null;
    t->root->elems[0] = e;
    return orig_e;
  }

  n = null;     /* placate gcc; will always be set below since t->root != null */
  np = &t->root;
  while (*np) {
    int childnum = 0;
    n = *np;
    if (index >= 0) {
      if (!n->kids[0]) {
       /*
        * Leaf node. We want to insert at kid position
        * equal to the index:
        * 
        *   0 A 1 B 2 C 3
        */
        childnum = index;
      }
      else {
       /*
        * Internal node. We always descend through it (add
        * always starts at the bottom, never in the
        * middle).
        */
        do {    /* this is a do ... while (0) to allow `break' */
          if (index <= n->counts[0]) {
            childnum = 0;
            break;
          }
          index -= n->counts[0] + 1;
          if (index <= n->counts[1]) {
            childnum = 1;
            break;
          }
          index -= n->counts[1] + 1;
          if (index <= n->counts[2]) {
            childnum = 2;
            break;
          }
          index -= n->counts[2] + 1;
          if (index <= n->counts[3]) {
            childnum = 3;
            break;
          }
          return null;  /* error: index out of range */
        } while (0);
      }
    }
    np = &n->kids[childnum];
  }

 /*
  * We need to insert the new element in n at position np.
  */
  left = null;
  lcount = 0;
  right = null;
  rcount = 0;
  while (n) {
    if (n->elems[1] == null) {
     /*
      * Insert in a 2-node; simple.
      */
      if (np == &n->kids[0]) {
        n->kids[2] = n->kids[1];
        n->counts[2] = n->counts[1];
        n->elems[1] = n->elems[0];
        n->kids[1] = right;
        n->counts[1] = rcount;
        n->elems[0] = e;
        n->kids[0] = left;
        n->counts[0] = lcount;
      }
      else {    /* np == &n->kids[1] */
        n->kids[2] = right;
        n->counts[2] = rcount;
        n->elems[1] = e;
        n->kids[1] = left;
        n->counts[1] = lcount;
      }
      if (n->kids[0])
        n->kids[0]->parent = n;
      if (n->kids[1])
        n->kids[1]->parent = n;
      if (n->kids[2])
        n->kids[2]->parent = n;
      break;
    }
    else if (n->elems[2] == null) {
     /*
      * Insert in a 3-node; simple.
      */
      if (np == &n->kids[0]) {
        n->kids[3] = n->kids[2];
        n->counts[3] = n->counts[2];
        n->elems[2] = n->elems[1];
        n->kids[2] = n->kids[1];
        n->counts[2] = n->counts[1];
        n->elems[1] = n->elems[0];
        n->kids[1] = right;
        n->counts[1] = rcount;
        n->elems[0] = e;
        n->kids[0] = left;
        n->counts[0] = lcount;
      }
      else if (np == &n->kids[1]) {
        n->kids[3] = n->kids[2];
        n->counts[3] = n->counts[2];
        n->elems[2] = n->elems[1];
        n->kids[2] = right;
        n->counts[2] = rcount;
        n->elems[1] = e;
        n->kids[1] = left;
        n->counts[1] = lcount;
      }
      else {    /* np == &n->kids[2] */
        n->kids[3] = right;
        n->counts[3] = rcount;
        n->elems[2] = e;
        n->kids[2] = left;
        n->counts[2] = lcount;
      }
      if (n->kids[0])
        n->kids[0]->parent = n;
      if (n->kids[1])
        n->kids[1]->parent = n;
      if (n->kids[2])
        n->kids[2]->parent = n;
      if (n->kids[3])
        n->kids[3]->parent = n;
      break;
    }
    else {
      node234 *m = new(node234);
      m->parent = n->parent;
     /*
      * Insert in a 4-node; split into a 2-node and a
      * 3-node, and move focus up a level.
      * 
      * I don't think it matters which way round we put the
      * 2 and the 3. For simplicity, we'll put the 3 first
      * always.
      */
      if (np == &n->kids[0]) {
        m->kids[0] = left;
        m->counts[0] = lcount;
        m->elems[0] = e;
        m->kids[1] = right;
        m->counts[1] = rcount;
        m->elems[1] = n->elems[0];
        m->kids[2] = n->kids[1];
        m->counts[2] = n->counts[1];
        e = n->elems[1];
        n->kids[0] = n->kids[2];
        n->counts[0] = n->counts[2];
        n->elems[0] = n->elems[2];
        n->kids[1] = n->kids[3];
        n->counts[1] = n->counts[3];
      }
      else if (np == &n->kids[1]) {
        m->kids[0] = n->kids[0];
        m->counts[0] = n->counts[0];
        m->elems[0] = n->elems[0];
        m->kids[1] = left;
        m->counts[1] = lcount;
        m->elems[1] = e;
        m->kids[2] = right;
        m->counts[2] = rcount;
        e = n->elems[1];
        n->kids[0] = n->kids[2];
        n->counts[0] = n->counts[2];
        n->elems[0] = n->elems[2];
        n->kids[1] = n->kids[3];
        n->counts[1] = n->counts[3];
      }
      else if (np == &n->kids[2]) {
        m->kids[0] = n->kids[0];
        m->counts[0] = n->counts[0];
        m->elems[0] = n->elems[0];
        m->kids[1] = n->kids[1];
        m->counts[1] = n->counts[1];
        m->elems[1] = n->elems[1];
        m->kids[2] = left;
        m->counts[2] = lcount;
       /* e = e; */
        n->kids[0] = right;
        n->counts[0] = rcount;
        n->elems[0] = n->elems[2];
        n->kids[1] = n->kids[3];
        n->counts[1] = n->counts[3];
      }
      else {    /* np == &n->kids[3] */
        m->kids[0] = n->kids[0];
        m->counts[0] = n->counts[0];
        m->elems[0] = n->elems[0];
        m->kids[1] = n->kids[1];
        m->counts[1] = n->counts[1];
        m->elems[1] = n->elems[1];
        m->kids[2] = n->kids[2];
        m->counts[2] = n->counts[2];
        n->kids[0] = left;
        n->counts[0] = lcount;
        n->elems[0] = e;
        n->kids[1] = right;
        n->counts[1] = rcount;
        e = n->elems[2];
      }
      m->kids[3] = n->kids[3] = n->kids[2] = null;
      m->counts[3] = n->counts[3] = n->counts[2] = 0;
      m->elems[2] = n->elems[2] = n->elems[1] = null;
      if (m->kids[0])
        m->kids[0]->parent = m;
      if (m->kids[1])
        m->kids[1]->parent = m;
      if (m->kids[2])
        m->kids[2]->parent = m;
      if (n->kids[0])
        n->kids[0]->parent = n;
      if (n->kids[1])
        n->kids[1]->parent = n;
      left = m;
      lcount = countnode234(left);
      right = n;
      rcount = countnode234(right);
    }
    if (n->parent)
      np =
        (n->parent->kids[0] == n ? &n->parent->kids[0] : n->parent->kids[1] ==
         n ? &n->parent->kids[1] : n->parent->kids[2] ==
         n ? &n->parent->kids[2] : &n->parent->kids[3]);
    n = n->parent;
  }

 /*
  * If we've come out of here by `break', n will still be
  * non-null and all we need to do is go back up the tree
  * updating counts. If we've come here because n is null, we
  * need to create a new root for the tree because the old one
  * has just split into two. */
  if (n) {
    while (n->parent) {
      int count = countnode234(n);
      int childnum;
      childnum =
        (n->parent->kids[0] == n ? 0 : n->parent->kids[1] ==
         n ? 1 : n->parent->kids[2] == n ? 2 : 3);
      n->parent->counts[childnum] = count;
      n = n->parent;
    }
  }
  else {
    t->root = new(node234);
    t->root->kids[0] = left;
    t->root->counts[0] = lcount;
    t->root->elems[0] = e;
    t->root->kids[1] = right;
    t->root->counts[1] = rcount;
    t->root->elems[1] = null;
    t->root->kids[2] = null;
    t->root->counts[2] = 0;
    t->root->elems[2] = null;
    t->root->kids[3] = null;
    t->root->counts[3] = 0;
    t->root->parent = null;
    if (t->root->kids[0])
      t->root->kids[0]->parent = t->root;
    if (t->root->kids[1])
      t->root->kids[1]->parent = t->root;
  }

  return orig_e;
}

/*
 * Look up the element at a given numeric index in a 2-3-4 tree.
 * Returns null if the index is out of range.
 */
void *
index234(tree234 * t, int index)
{
  node234 *n;

  if (!t->root)
    return null;        /* tree is empty */

  if (index < 0 || index >= countnode234(t->root))
    return null;        /* out of range */

  n = t->root;

  while (n) {
    if (index < n->counts[0])
      n = n->kids[0];
    else if (index -= n->counts[0] + 1, index < 0)
      return n->elems[0];
    else if (index < n->counts[1])
      n = n->kids[1];
    else if (index -= n->counts[1] + 1, index < 0)
      return n->elems[1];
    else if (index < n->counts[2])
      n = n->kids[2];
    else if (index -= n->counts[2] + 1, index < 0)
      return n->elems[2];
    else
      n = n->kids[3];
  }

 /* We shouldn't ever get here. I wonder how we did. */
  return null;
}

/*
 * Delete an element e in a 2-3-4 tree. Does not free the element,
 * merely removes all links to it from the tree nodes.
 */
void *
delpos234(tree234 * t, int index)
{
  if (index < 0 || index >= countnode234(t->root))
    return null;

  node234 *n;
  void *retval;
  int ei = -1;

  retval = 0;

  n = t->root;
  while (1) {
    while (n) {
      int ki;
      node234 *sub;

      if (index < n->counts[0]) {
        ki = 0;
      }
      else if (index -= n->counts[0] + 1, index < 0) {
        ei = 0;
        break;
      }
      else if (index < n->counts[1]) {
        ki = 1;
      }
      else if (index -= n->counts[1] + 1, index < 0) {
        ei = 1;
        break;
      }
      else if (index < n->counts[2]) {
        ki = 2;
      }
      else if (index -= n->counts[2] + 1, index < 0) {
        ei = 2;
        break;
      }
      else {
        ki = 3;
      }
     /*
      * Recurse down to subtree ki. If it has only one element,
      * we have to do some transformation to start with.
      */
      sub = n->kids[ki];
      if (!sub->elems[1]) {
        if (ki > 0 && n->kids[ki - 1]->elems[1]) {
         /*
          * Case 3a, left-handed variant. Child ki has
          * only one element, but child ki-1 has two or
          * more. So we need to move a subtree from ki-1
          * to ki.
          * 
          *                . C .                     . B .
          *               /     \     ->            /     \
          * [more] a A b B c   d D e      [more] a A b   c C d D e
          */
          node234 *sib = n->kids[ki - 1];
          int lastelem = (sib->elems[2] ? 2 : sib->elems[1] ? 1 : 0);
          sub->kids[2] = sub->kids[1];
          sub->counts[2] = sub->counts[1];
          sub->elems[1] = sub->elems[0];
          sub->kids[1] = sub->kids[0];
          sub->counts[1] = sub->counts[0];
          sub->elems[0] = n->elems[ki - 1];
          sub->kids[0] = sib->kids[lastelem + 1];
          sub->counts[0] = sib->counts[lastelem + 1];
          if (sub->kids[0])
            sub->kids[0]->parent = sub;
          n->elems[ki - 1] = sib->elems[lastelem];
          sib->kids[lastelem + 1] = null;
          sib->counts[lastelem + 1] = 0;
          sib->elems[lastelem] = null;
          n->counts[ki] = countnode234(sub);
          index += n->counts[ki - 1];
          n->counts[ki - 1] = countnode234(sib);
          index -= n->counts[ki - 1];
        }
        else if (ki < 3 && n->kids[ki + 1]
                 && n->kids[ki + 1]->elems[1]) {
         /*
          * Case 3a, right-handed variant. ki has only
          * one element but ki+1 has two or more. Move a
          * subtree from ki+1 to ki.
          * 
          *      . B .                             . C .
          *     /     \                ->         /     \
          *  a A b   c C d D e [more]      a A b B c   d D e [more]
          */
          node234 *sib = n->kids[ki + 1];
          int j;
          sub->elems[1] = n->elems[ki];
          sub->kids[2] = sib->kids[0];
          sub->counts[2] = sib->counts[0];
          if (sub->kids[2])
            sub->kids[2]->parent = sub;
          n->elems[ki] = sib->elems[0];
          sib->kids[0] = sib->kids[1];
          sib->counts[0] = sib->counts[1];
          for (j = 0; j < 2 && sib->elems[j + 1]; j++) {
            sib->kids[j + 1] = sib->kids[j + 2];
            sib->counts[j + 1] = sib->counts[j + 2];
            sib->elems[j] = sib->elems[j + 1];
          }
          sib->kids[j + 1] = null;
          sib->counts[j + 1] = 0;
          sib->elems[j] = null;
          n->counts[ki] = countnode234(sub);
          n->counts[ki + 1] = countnode234(sib);
        }
        else {
         /*
          * Case 3b. ki has only one element, and has no
          * neighbour with more than one. So pick a
          * neighbour and merge it with ki, taking an
          * element down from n to go in the middle.
          *
          *      . B .                .
          *     /     \     ->        |
          *  a A b   c C d      a A b B c C d
          * 
          * (Since at all points we have avoided
          * descending to a node with only one element,
          * we can be sure that n is not reduced to
          * nothingness by this move, _unless_ it was
          * the very first node, ie the root of the
          * tree. In that case we remove the now-empty
          * root and replace it with its single large
          * child as shown.)
          */
          node234 *sib;
          int j;

          if (ki > 0) {
            ki--;
            index += n->counts[ki] + 1;
          }
          sib = n->kids[ki];
          sub = n->kids[ki + 1];

          sub->kids[3] = sub->kids[1];
          sub->counts[3] = sub->counts[1];
          sub->elems[2] = sub->elems[0];
          sub->kids[2] = sub->kids[0];
          sub->counts[2] = sub->counts[0];
          sub->elems[1] = n->elems[ki];
          sub->kids[1] = sib->kids[1];
          sub->counts[1] = sib->counts[1];
          if (sub->kids[1])
            sub->kids[1]->parent = sub;
          sub->elems[0] = sib->elems[0];
          sub->kids[0] = sib->kids[0];
          sub->counts[0] = sib->counts[0];
          if (sub->kids[0])
            sub->kids[0]->parent = sub;

          n->counts[ki + 1] = countnode234(sub);

          free(sib);

         /*
          * That's built the big node in sub. Now we
          * need to remove the reference to sib in n.
          */
          for (j = ki; j < 3 && n->kids[j + 1]; j++) {
            n->kids[j] = n->kids[j + 1];
            n->counts[j] = n->counts[j + 1];
            n->elems[j] = j < 2 ? n->elems[j + 1] : null;
          }
          n->kids[j] = null;
          n->counts[j] = 0;
          if (j < 3)
            n->elems[j] = null;

          if (!n->elems[0]) {
           /*
            * The root is empty and needs to be
            * removed.
            */
            t->root = sub;
            sub->parent = null;
            free(n);
          }
        }
      }
      n = sub;
    }
    if (!retval)
      retval = n->elems[ei];

    if (ei == -1)
      return null;      /* although this shouldn't happen */

   /*
    * Treat special case: this is the one remaining item in
    * the tree. n is the tree root (no parent), has one
    * element (no elems[1]), and has no kids (no kids[0]).
    */
    if (!n->parent && !n->elems[1] && !n->kids[0]) {
      free(n);
      t->root = null;
      return retval;
    }

   /*
    * Now we have the element we want, as n->elems[ei], and we
    * have also arranged for that element not to be the only
    * one in its node. So...
    */

    if (!n->kids[0] && n->elems[1]) {
     /*
      * Case 1. n is a leaf node with more than one element,
      * so it's _really easy_. Just delete the thing and
      * we're done.
      */
      int i;
      for (i = ei; i < 2 && n->elems[i + 1]; i++)
        n->elems[i] = n->elems[i + 1];
      n->elems[i] = null;
     /*
      * Having done that to the leaf node, we now go back up
      * the tree fixing the counts.
      */
      while (n->parent) {
        int childnum;
        childnum =
          (n->parent->kids[0] == n ? 0 : n->parent->kids[1] ==
           n ? 1 : n->parent->kids[2] == n ? 2 : 3);
        n->parent->counts[childnum]--;
        n = n->parent;
      }
      return retval;    /* finished! */
    }
    else if (n->kids[ei]->elems[1]) {
     /*
      * Case 2a. n is an internal node, and the root of the
      * subtree to the left of e has more than one element.
      * So find the predecessor p to e (ie the largest node
      * in that subtree), place it where e currently is, and
      * then start the deletion process over again on the
      * subtree with p as target.
      */
      node234 *m = n->kids[ei];
      void *target;
      while (m->kids[0]) {
        m =
          (m->kids[3] ? m->kids[3] : m->kids[2] ? m->kids[2] : m->kids[1] ? m->
           kids[1] : m->kids[0]);
      }
      target =
        (m->elems[2] ? m->elems[2] : m->elems[1] ? m->elems[1] : m->elems[0]);
      n->elems[ei] = target;
      index = n->counts[ei] - 1;
      n = n->kids[ei];
    }
    else if (n->kids[ei + 1]->elems[1]) {
     /*
      * Case 2b, symmetric to 2a but s/left/right/ and
      * s/predecessor/successor/. (And s/largest/smallest/).
      */
      node234 *m = n->kids[ei + 1];
      void *target;
      while (m->kids[0]) {
        m = m->kids[0];
      }
      target = m->elems[0];
      n->elems[ei] = target;
      n = n->kids[ei + 1];
      index = 0;
    }
    else {
     /*
      * Case 2c. n is an internal node, and the subtrees to
      * the left and right of e both have only one element.
      * So combine the two subnodes into a single big node
      * with their own elements on the left and right and e
      * in the middle, then restart the deletion process on
      * that subtree, with e still as target.
      */
      node234 *a = n->kids[ei], *b = n->kids[ei + 1];
      int j;

      a->elems[1] = n->elems[ei];
      a->kids[2] = b->kids[0];
      a->counts[2] = b->counts[0];
      if (a->kids[2])
        a->kids[2]->parent = a;
      a->elems[2] = b->elems[0];
      a->kids[3] = b->kids[1];
      a->counts[3] = b->counts[1];
      if (a->kids[3])
        a->kids[3]->parent = a;
      free(b);
      n->counts[ei] = countnode234(a);
     /*
      * That's built the big node in a, and destroyed b. Now
      * remove the reference to b (and e) in n.
      */
      for (j = ei; j < 2 && n->elems[j + 1]; j++) {
        n->elems[j] = n->elems[j + 1];
        n->kids[j + 1] = n->kids[j + 2];
        n->counts[j + 1] = n->counts[j + 2];
      }
      n->elems[j] = null;
      n->kids[j + 1] = null;
      n->counts[j + 1] = 0;
     /*
      * It's possible, in this case, that we've just removed
      * the only element in the root of the tree. If so,
      * shift the root.
      */
      if (n->elems[0] == null) {
        t->root = a;
        a->parent = null;
        free(n);
      }
     /*
      * Now go round the deletion process again, with n
      * pointing at the new big node and e still the same.
      */
      n = a;
      index = a->counts[0] + a->counts[1] + 1;
    }
  }
}
