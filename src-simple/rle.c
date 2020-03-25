/*
Copyright (c) 2020, James Davis http://people.cs.vt.edu/davisjam/
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER
OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include "rle.h"
#include "vendor/avl_tree.h"

#define BIT_ISSET(x, i) ( ( (x) & ( (1) << (i) ) ) != 0 )
#define BIT_SET(x, i) ( (x) | ( (1) << (i) ) ) /* Returns with bit set */
/* Offset within a k-bit run. [0, bitsPerRun). */
#define RUN_OFFSET(ix, bitsPerRun) ( (ix) % (bitsPerRun) ) 
#define MASK_FOR(ix, bitsPerRun) ( BIT_SET(0, RUN_OFFSET(ix, bitsPerRun)) )
/* Run number within a repeating sequence of k-bit runs. [0, nRuns). */
#define RUN_NUMBER(ix, rleStart, bitsPerRun) ( ((ix) / (bitsPerRun)) - (rleStart) )

static int TEST = 1;
static int RUN_TIME_CHECKS = 1;
enum {
  VERBOSE_LVL_NONE,
  VERBOSE_LVL_SOME,
  VERBOSE_LVL_ALL,
};
static int VERBOSE_LVL = VERBOSE_LVL_NONE;

/* Internal API: RLENode */
typedef struct RLENode RLENode;

static void _RLEVector_validate(RLEVector *vec);

/* For counting high water mark. Call in conjunction with avl insert/remove. */
/* TODO These should wrap avl insert/remove */
static void _RLEVector_addRun(RLEVector *vec);
static void _RLEVector_subtractRun(RLEVector *vec);

/* RLE Run -- Implemented as an element of an AVL tree. */
struct RLENode
{
  int offset; /* Key */
  int nRuns;  

  /* A bit representation of the run sequence.
   * We look at bits 0, 1, 2, 3, ... (right-to-left).  */
  int run; 
  int nBitsInRun; /* How many bits to look at */

  struct avl_tree_node node;
};

static int
RLENode_nBits(RLENode *node)
{
  return node->nRuns * node->nBitsInRun;
}

/* First index not captured in this run */
static int
RLENode_end(RLENode *node)
{
  return node->offset + RLENode_nBits(node);
}

static int
RLENode_contains(RLENode *node, int ix)
{
  return node->offset <= ix && ix < RLENode_end(node);
}

static int
RLENode_canMerge(RLENode *l, RLENode *r)
{
  return (l->run == r->run) && \
    RLENode_end(l) == r->offset;
}

/* Returns 0 if target's offset lies within curr. */
int
RLENode_avl_tree_cmp(const struct avl_tree_node *target, const struct avl_tree_node *curr)
{
  RLENode *_target = avl_tree_entry(target, RLENode, node);
  RLENode *_curr = avl_tree_entry(curr, RLENode, node);

  if (VERBOSE_LVL >= VERBOSE_LVL_ALL)
    printf("Hello in RLENode_avl_tree_cmp\n");

  if (_target->offset < _curr->offset) {
    /* _target is smaller than _curr */
    return -1;
  } else if (RLENode_contains(_curr, _target->offset)) {
    /* _target falls within _curr */
    return 0;
  } else {
    /* _target falls after _curr */
    return 1;
  }
}

static RLENode *
RLENode_create(int offset, int nRuns, int run, int nBitsInRun)
{
  RLENode *node = malloc(sizeof *node);
  node->offset = offset;
  node->nRuns = nRuns;
  node->run = run;
  node->nBitsInRun = nBitsInRun;

  return node;
}

void
RLENode_extendRight(RLENode *node)
{
  node->nRuns++;
}

void
RLENode_extendLeft(RLENode *node)
{
  node->offset -= node->nBitsInRun;
  node->nRuns++;
}

/* External API: RLEVector */

struct RLEVector
{
  struct avl_tree_node *root;
  int currNEntries;
  int mostNEntries; /* High water mark */
  int nBitsInRun; /* Length of the runs we encode */
};

RLEVector *
RLEVector_create(int runLength)
{
  RLEVector *vec = malloc(sizeof *vec);
  vec->root = NULL;
  vec->currNEntries = 0;
  vec->mostNEntries = 0;
  vec->nBitsInRun = runLength;
  printf("RLEVector_create: vec %p nBitsInRun %d\n", vec, vec->nBitsInRun);

  if (TEST) {
    RLEVector *vec2 = malloc(sizeof *vec);

    printf("RLEVector_create: running test\n");
    vec2->root = NULL;
    vec2->currNEntries = 0;
    vec2->mostNEntries = 0;
    vec2->nBitsInRun = runLength;
    _RLEVector_validate(vec2);

    printf("get from empty\n");
    assert(RLEVector_currSize(vec2) == 0);
    assert(RLEVector_get(vec2, 5) == 0);

    printf("set 5\n");
    RLEVector_set(vec2, 5);
    assert(RLEVector_currSize(vec2) == 1);
    printf("set 7\n");
    RLEVector_set(vec2, 7);
    assert(RLEVector_currSize(vec2) == 2);
    printf("set 6\n");
    RLEVector_set(vec2, 6);
    assert(RLEVector_currSize(vec2) == 1);

    printf("get 4-8\n");
    assert(RLEVector_get(vec2, 4) == 0);
    assert(RLEVector_get(vec2, 5) == 1);
    assert(RLEVector_get(vec2, 6) == 1);
    assert(RLEVector_get(vec2, 7) == 1);
    assert(RLEVector_get(vec2, 8) == 0);
    printf("Tests passed\n");
  }

  return vec;
}

/* Performs a full walk of the tree looking for fishy business. */
static void
_RLEVector_validate(RLEVector *vec)
{
  RLENode *prev = NULL, *curr = NULL;
  int nNodes = 0;

  if (!RUN_TIME_CHECKS)
    return;

  if (VERBOSE_LVL >= VERBOSE_LVL_ALL) {
    printf("  _RLEVector_validate: Validating vec %p (size %d, runs of length %d)\n", vec, vec->currNEntries, vec->nBitsInRun);
  }


  if (vec->currNEntries == 0) {
    return;
  }

  prev = avl_tree_entry(avl_tree_first_in_order(vec->root), RLENode, node);
  if (prev != NULL) {
    curr = avl_tree_entry(avl_tree_next_in_order(&prev->node), RLENode, node);

    if (prev != NULL) {
      nNodes++;
    }

    while (prev != NULL && curr != NULL) {
      assert(prev->offset < curr->offset); /* In-order */
      assert(RLENode_end(prev) < curr->offset); /* Adjacent are merged */
      prev = curr;
      curr = avl_tree_entry(avl_tree_next_in_order(&curr->node), RLENode, node);
      nNodes++;
    }
  }

  if (VERBOSE_LVL >= VERBOSE_LVL_ALL) {
    printf("nNodes %d currNEntries %d\n", nNodes, vec->currNEntries);
  }
  assert(vec->currNEntries == nNodes);
}

typedef struct RLENodeNeighbors RLENodeNeighbors;
struct RLENodeNeighbors
{
  RLENode *a; /* Predecessor -- first before */
  RLENode *b; /* Current, if it exists */
  RLENode *c; /* Successor -- first after */
};

static RLENodeNeighbors
RLEVector_getNeighbors(RLEVector *vec, int ix)
{
  RLENode target, *node;
  RLENodeNeighbors rnn;
  rnn.a = NULL;
  rnn.b = NULL;
  rnn.c = NULL;

  target.offset = ix;
  target.nRuns = -1;

  /* Find a */
  rnn.a = avl_tree_entry(
    avl_tree_lookup_node_pred(vec->root, &target.node, RLENode_avl_tree_cmp),
    RLENode, node);
  
  /* Find b or c */
  if (rnn.a == NULL) {
    node = avl_tree_entry(avl_tree_first_in_order(vec->root), RLENode, node);
  } else {
    node = avl_tree_entry(avl_tree_next_in_order(&rnn.a->node), RLENode, node);
  }

  if (RLENode_contains(node, ix)) {
    /* We have b, let's add c */
    rnn.b = node;
    rnn.c = avl_tree_entry(avl_tree_next_in_order(&rnn.b->node), RLENode, node);
  } else {
    /* We have c */
    rnn.b = NULL;
    rnn.c = node;
  }

  return rnn;
}

static void
RLEVector_mergeNeighbors(RLEVector *vec, RLENodeNeighbors rnn)
{
  int nBefore = vec->currNEntries;

  /* Because rnn are adjacent, we can directly manipulate offsets without
   * breaking the BST property. */
  if (RLENode_canMerge(rnn.a, rnn.b)) {
    avl_tree_remove(&vec->root, &rnn.b->node);
    _RLEVector_subtractRun(vec);

    rnn.a->nRuns += rnn.b->nRuns;

    free(rnn.b);

    /* Set b to a, so that the next logic will work. */
    rnn.b = rnn.a;
  }
  if (RLENode_canMerge(rnn.b, rnn.c)) {
    avl_tree_remove(&vec->root, &rnn.c->node);
    _RLEVector_subtractRun(vec);

    rnn.b->nRuns += rnn.c->nRuns;

    free(rnn.c);
  }

  if (VERBOSE_LVL >= VERBOSE_LVL_SOME)
    printf("mergeNeighbors: before %d after %d\n", nBefore, vec->currNEntries);
}

void
RLEVector_set(RLEVector *vec, int ix)
{
  RLENodeNeighbors rnn;
  RLENode *newRun = NULL;
  int oldRunKernel = 0, newRunKernel = 0;

  if (VERBOSE_LVL >= VERBOSE_LVL_ALL)
    printf("RLEVector_set: %d\n", ix);

  _RLEVector_validate(vec);
  assert(RLEVector_get(vec, ix) == 0); /* Shouldn't be set already */

  rnn = RLEVector_getNeighbors(vec, ix);

  /* Handle the "new" and "split" cases.
   * Update rnn.{a,b,c} as we go. */
  if (rnn.b == NULL) {
    /* Case: creates a run */
    if (VERBOSE_LVL >= VERBOSE_LVL_SOME)
      printf("%d: Creating a run\n", ix);

    newRunKernel = MASK_FOR(ix, vec->nBitsInRun);
    newRun = RLENode_create(ix, 1, newRunKernel, vec->nBitsInRun);

    assert(avl_tree_insert(&vec->root, &newRun->node, RLENode_avl_tree_cmp) == NULL);
    _RLEVector_addRun(vec);
    rnn.b = newRun;
  } else {
    /* Case: splits a run */
    RLENode *prefixRun = NULL, *oldRun = NULL, *suffixRun = NULL;
    int ixRunNumber = 0, nRunsInPrefix = 0, nRunsInSuffix = 0;

    if (VERBOSE_LVL >= VERBOSE_LVL_SOME)
      printf("%d: Splitting a run\n", ix);

    /* Calculate the run kernels */
    oldRun = rnn.b;
    oldRunKernel = oldRun->run;
    newRunKernel = oldRunKernel | MASK_FOR(ix, vec->nBitsInRun);

    /* Remove the affected run */
    avl_tree_remove(&vec->root, &oldRun->node);
    _RLEVector_subtractRun(vec);

    /* Insert the new run */
    newRun = RLENode_create(ix, 1, newRunKernel, vec->nBitsInRun);
    _RLEVector_addRun(vec);
    rnn.b = newRun;
    
    /* Insert prefix and suffix */
    ixRunNumber = RUN_NUMBER(ix, oldRun->offset, oldRun->nBitsInRun);
    nRunsInPrefix = ixRunNumber;
    nRunsInSuffix = oldRun->nRuns - (ixRunNumber + 1);

    if (nRunsInPrefix > 0) {
      prefixRun = RLENode_create(oldRun->offset, nRunsInPrefix, oldRunKernel, vec->nBitsInRun);
      assert(avl_tree_insert(&vec->root, &prefixRun->node, RLENode_avl_tree_cmp) == NULL);
      _RLEVector_addRun(vec);

      rnn.a = prefixRun;
    }
    if (nRunsInSuffix > 0) {
      suffixRun = RLENode_create(ix, nRunsInSuffix, oldRunKernel, vec->nBitsInRun);
      assert(avl_tree_insert(&vec->root, &suffixRun->node, RLENode_avl_tree_cmp) == NULL);
      _RLEVector_addRun(vec);

      rnn.c = suffixRun;
    }

    /* Clean up */
    free(oldRun);
  }

  RLEVector_mergeNeighbors(vec, rnn);
  /* After merging, rnn.{a,b,c} is untrustworthy. */
  
  _RLEVector_validate(vec);
  return;
}

int
RLEVector_get(RLEVector *vec, int ix)
{
  RLENode target;
  RLENode *match = NULL;

  if (VERBOSE_LVL >= VERBOSE_LVL_ALL)
    printf("RLEVector_get: %d\n", ix);
  _RLEVector_validate(vec);

  target.offset = ix;
  target.nRuns = -1;
  match = avl_tree_entry(
    avl_tree_lookup_node(vec->root, &target.node, RLENode_avl_tree_cmp),
    RLENode, node);

  if (match == NULL) {
    return 0;
  }

  if (VERBOSE_LVL >= VERBOSE_LVL_ALL)
    printf("match: %p\n", match);
  return BIT_ISSET(match->run, ix % match->nBitsInRun);
}

int
RLEVector_currSize(RLEVector *vec)
{
  return vec->currNEntries;
}

int
RLEVector_maxObservedSize(RLEVector *vec)
{
  return vec->mostNEntries;
}

void
RLEVector_destroy(RLEVector *vec)
{
  /* TODO */
  return;
}

static void _RLEVector_addRun(RLEVector *vec)
{
  vec->currNEntries++;
  if (vec->mostNEntries < vec->currNEntries) {
    vec->mostNEntries = vec->currNEntries;
  }
}

static void _RLEVector_subtractRun(RLEVector *vec)
{
  vec->currNEntries--;
}
