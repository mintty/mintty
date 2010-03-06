#ifndef TREE234_H
#define TREE234_H

/*
 * tree234.h: header defining functions in tree234.c.
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

/*
 * This typedef is opaque outside tree234.c itself.
 */
typedef struct tree234 tree234;

/*
 * Create a 2-3-4 tree.
 */
tree234 *newtree234(void);

/*
 * Free a 2-3-4 tree (not including freeing the elements).
 */
void freetree234(tree234 *t);

/*
 * Add an element e to a 2-3-4 tree t. Returns e on success,
 * null on failure. (Failure should only occur if the index is out of
 * range.)
 * 
 * Index range can be from 0 to the tree's current element count,
 * inclusive.
 */
void *addpos234(tree234 *t, void *e, int index);

/*
 * Look up the element at a given numeric index in a 2-3-4 tree.
 * Returns null if the index is out of range.
 * 
 * One obvious use for this function is in iterating over the whole
 * of a tree:
 * 
 *   for (i = 0; (p = index234(tree, i)) != null; i++) consume(p);
 * 
 * or
 * 
 *   int maxcount = count234(tree);
 *   for (i = 0; i < maxcount; i++) {
 *       p = index234(tree, i);
 *       assert(p != null);
 *       consume(p);
 *   }
 */
void *index234(tree234 *t, int index);

/*
 * Delete an element in a 2-3-4 tree. Does not free the element,
 * merely removes all links to it from the tree nodes.
 * 
 * Returns a pointer to the element deleted, for the user to free
 * or pass on elsewhere or whatever. Returns null if the index is
 * out of range.
 */
void *delpos234(tree234 *t, int index);

/*
 * Return the total element count of a tree234.
 */
int count234(tree234 *t);

#endif /* TREE234_H */
