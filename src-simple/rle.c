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

#include "rle.h"
#include "log.h"
#include "vendor/avl_tree.h"

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>

#define BIT_ISSET(x, i) ( ( (x) & ( (1) << (i) ) ) != 0 )
#define BIT_SET(x, i) ( (x) | ( (1) << (i) ) ) /* Returns with bit set */
/* Offset within a k-bit run. [0, bitsPerRun). */
#define RUN_OFFSET(ix, bitsPerRun) ( (ix) % (bitsPerRun) ) 
#define MASK_FOR(ix, bitsPerRun) ( BIT_SET(0, RUN_OFFSET(ix, bitsPerRun)) )
/* Run number within a repeating sequence of k-bit runs. [0, nRuns). */
#define RUN_NUMBER(ix, rleStart, bitsPerRun) ( ( (ix) - (rleStart) ) / (bitsPerRun) )

static int TEST = 0;
enum {
  VERBOSE_LVL_NONE,
  VERBOSE_LVL_SOME,
  VERBOSE_LVL_ALL,
};
static int VERBOSE_LVL = VERBOSE_LVL_NONE;

/* Internal API: RLENode */
typedef struct RLENode RLENode;

static void _RLEVector_validate(RLEVector *vec);
static void _RLEVector_addRun(RLEVector *vec, RLENode *node);
static void _RLEVector_removeRun(RLEVector *vec, RLENode *node);

/* RLE Run -- Implemented as an element of an AVL tree. */
struct RLENode
{
  int offset; /* Key */
  int nRuns;  

  /* A bit representation of the run sequence.
   * We look at bits 0, 1, 2, 3, ... (right-to-left).  */
  unsigned long long run; /* 64 bits -- does this need to be longer? */
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

	// printf("tree_cmp: target %d curr (%d, %d)\n", _target->offset, _curr->offset, _curr->nRuns);
  if (_target->offset < _curr->offset) {
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
RLENode_create(int offset, int nRuns, unsigned long long run, int nBitsInRun)
{
  RLENode *node = malloc(sizeof *node);
  node->offset = offset;
  node->nRuns = nRuns;
  node->run = run;
  node->nBitsInRun = nBitsInRun;

  return node;
}

/* External API: RLEVector */

struct RLEVector
{
  struct avl_tree_node *root;
  int currNEntries;
  int mostNEntries; /* High water mark */
  int nBitsInRun; /* Length of the runs we encode */
  int autoValidate; /* Validate after every API usage. This can be wildly expensive. */
};

RLEVector *
RLEVector_create(int runLength, int autoValidate)
{
  RLENode node;
  RLEVector *vec = malloc(sizeof *vec);
  vec->root = NULL;
  vec->currNEntries = 0;
  vec->mostNEntries = 0;
  vec->nBitsInRun = runLength;

  if (runLength > 8 * sizeof(node.run)) {
    logMsg(LOG_INFO, "RLEVector_create: Need %d bits, only have %llu", runLength, 8llu * sizeof(node.run));
    vec->nBitsInRun = 1;
  }
  vec->autoValidate = autoValidate;

  logMsg(LOG_VERBOSE, "RLEVector_create: vec %p nBitsInRun %d, autoValidate %d", vec, vec->nBitsInRun, vec->autoValidate);

  if (TEST) {
    RLEVector *vec2 = malloc(sizeof *vec);

    printf("\n**********\n\n  RLEVector_create: running test\n\n**********\n\n");
    vec2->root = NULL;
    vec2->currNEntries = 0;
    vec2->mostNEntries = 0;
    vec2->nBitsInRun = 2;
    vec->autoValidate = 1;
    _RLEVector_validate(vec2);

    printf("get from empty\n");
    assert(RLEVector_currSize(vec2) == 0);
    assert(RLEVector_get(vec2, 5) == 0);

    printf("set 5\n");
    RLEVector_set(vec2, 5);
    assert(RLEVector_currSize(vec2) == 1);
    printf("set 7\n");
    RLEVector_set(vec2, 7);
    assert(RLEVector_currSize(vec2) == 1); /* Run-length 2 should accommodate this: 01-01 */
    printf("set 6\n");
    RLEVector_set(vec2, 6);
    assert(RLEVector_currSize(vec2) == 2); /* But not this: 01-11 */
    printf("set 4\n");
    RLEVector_set(vec2, 4);
    assert(RLEVector_currSize(vec2) == 1); /* But now we have 11-11 */

    printf("get 4-8\n");
    assert(RLEVector_get(vec2, 4) == 1);
    assert(RLEVector_get(vec2, 5) == 1);
    assert(RLEVector_get(vec2, 6) == 1);
    assert(RLEVector_get(vec2, 7) == 1);
    assert(RLEVector_get(vec2, 8) == 0);
    printf("\n\n  RLEVector_create: Tests passed\n\n**********\n\n");
  }

  return vec;
}

/* Performs a full walk of the tree looking for fishy business. O(n) steps. */
static void
_RLEVector_validate(RLEVector *vec)
{
  RLENode *prev = NULL, *curr = NULL;
  int nNodes = 0;

	assert(vec != NULL);
  logMsg(LOG_DEBUG, "  _RLEVector_validate: Validating vec %p (size %d, runs of length %d)", vec, vec->currNEntries, vec->nBitsInRun);

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
      logMsg(LOG_DEBUG, "rleVector_validate: prev (%d,%d,%llu) curr (%d,%d,%llu)", prev->offset, prev->nRuns, prev->run, curr->offset, curr->nRuns, curr->run);
      assert(prev->offset < curr->offset); /* In-order */
			if (RLENode_end(prev) == curr->offset) {
				assert(prev->run != curr->run); /* Adjacent are merged */
			}
      prev = curr;
      curr = avl_tree_entry(avl_tree_next_in_order(&curr->node), RLENode, node);
      nNodes++;
    }
  }

  logMsg(LOG_DEBUG, "rleVector_validate: nNodes %d currNEntries %d", nNodes, vec->currNEntries);
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

	node = avl_tree_entry(avl_tree_lookup_node_pred(vec->root, &target.node, RLENode_avl_tree_cmp), RLENode, node);
	if (node != NULL) {
		/* node is the largest run beginning <= ix */
		if (RLENode_contains(node, ix)) {
			/* = */
			rnn.b = node;
			rnn.a = avl_tree_entry(avl_tree_prev_in_order(&rnn.b->node), RLENode, node);
			rnn.c = avl_tree_entry(avl_tree_next_in_order(&rnn.b->node), RLENode, node);
		} else {
			/* < */
			rnn.a = node;
			rnn.b = NULL;
			rnn.c = avl_tree_entry(avl_tree_next_in_order(&rnn.a->node), RLENode, node);
		}
	} else {
		/* There is no run <= ix */
		rnn.a = NULL;
		rnn.b = NULL;
		rnn.c = avl_tree_entry(avl_tree_first_in_order(vec->root), RLENode, node);
	}
	
  logMsg(LOG_DEBUG, "rnn: a %p b %p c %p\n", rnn.a, rnn.b, rnn.c);
	return rnn;
}

/* Given a populated RNN, merge a-b and b-c if possible. */
static void
_RLEVector_mergeNeighbors(RLEVector *vec, RLENodeNeighbors rnn)
{
  int nBefore = vec->currNEntries;

  /* Because rnn are adjacent, we can directly manipulate offsets without
   * breaking the BST property. */
  if (rnn.a != NULL && rnn.b != NULL && RLENode_canMerge(rnn.a, rnn.b)) {
    _RLEVector_removeRun(vec, rnn.b);

    rnn.a->nRuns += rnn.b->nRuns;

    logMsg(LOG_DEBUG, "merge: Removed (%d,%d), merged with now-(%d,%d,%llu)", rnn.b->offset, rnn.b->nRuns, rnn.a->offset, rnn.a->nRuns, rnn.a->run);
    free(rnn.b);

    /* Set b to a, so that the next logic will work. */
    rnn.b = rnn.a;
  }
  if (rnn.b != NULL && rnn.c != NULL && RLENode_canMerge(rnn.b, rnn.c)) {
    _RLEVector_removeRun(vec, rnn.c);

    rnn.b->nRuns += rnn.c->nRuns;
    logMsg(LOG_DEBUG, "merge: Removed (%d,%d), merged with now-(%d,%d,%llu)", rnn.c->offset, rnn.c->nRuns, rnn.b->offset, rnn.b->nRuns, rnn.b->run);

    free(rnn.c);
  }

  logMsg(LOG_DEBUG, "mergeNeighbors: before %d after %d", nBefore, vec->currNEntries);

  if (vec->autoValidate)
    _RLEVector_validate(vec);
}

int
RLEVector_runSize(RLEVector *vec)
{
  return vec->nBitsInRun;
}

/* Set the bit at ix.
 * Invariant: always returns with vec fully merged; validate() should pass.
 */
void
RLEVector_set(RLEVector *vec, int ix)
{
  RLENodeNeighbors rnn;
  RLENode *newRun = NULL;
  int oldRunKernel = 0, newRunKernel = 0;
	int roundedIx = ix - RUN_OFFSET(ix, vec->nBitsInRun);

  logMsg(LOG_VERBOSE, "RLEVector_set: %d", ix);

  if (vec->autoValidate)
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
    newRun = RLENode_create(roundedIx, 1, newRunKernel, vec->nBitsInRun);

    _RLEVector_addRun(vec, newRun);
    rnn.b = newRun;
  } else {
    /* Case: splits a run */
    RLENode *prefixRun = NULL, *oldRun = NULL, *suffixRun = NULL;
    int ixRunNumber = 0, nRunsInPrefix = 0, nRunsInSuffix = 0;

    if (VERBOSE_LVL >= VERBOSE_LVL_SOME)
      printf("%d: Splitting the run (%d,%d,%llu)\n", ix, rnn.b->offset, rnn.b->nRuns, rnn.b->run);

    /* Calculate the run kernels */
    oldRun = rnn.b;
    oldRunKernel = oldRun->run;
    newRunKernel = oldRunKernel | MASK_FOR(ix, vec->nBitsInRun);

    /* Remove the affected run */
    _RLEVector_removeRun(vec, oldRun);

    /* Insert the new run */
		if (VERBOSE_LVL >= VERBOSE_LVL_ALL)
			printf("adding intercalary run\n");
    newRun = RLENode_create(roundedIx, 1, newRunKernel, vec->nBitsInRun);
    _RLEVector_addRun(vec, newRun);
    rnn.b = newRun;
    
    /* Insert prefix and suffix */
    ixRunNumber = RUN_NUMBER(ix, oldRun->offset, oldRun->nBitsInRun);
    nRunsInPrefix = ixRunNumber;
    nRunsInSuffix = oldRun->nRuns - (ixRunNumber + 1);

    if (nRunsInPrefix > 0) {
			if (VERBOSE_LVL >= VERBOSE_LVL_ALL)
				printf("adding prefix\n");
      prefixRun = RLENode_create(oldRun->offset, nRunsInPrefix, oldRunKernel, vec->nBitsInRun);
      _RLEVector_addRun(vec, prefixRun);

      rnn.a = prefixRun;
    }
    if (nRunsInSuffix > 0) {
			if (VERBOSE_LVL >= VERBOSE_LVL_ALL)
				printf("adding suffix\n");
      suffixRun = RLENode_create(roundedIx + vec->nBitsInRun, nRunsInSuffix, oldRunKernel, vec->nBitsInRun);
      _RLEVector_addRun(vec, suffixRun);

      rnn.c = suffixRun;
    }

    /* Clean up */
    free(oldRun);
  }

	if (VERBOSE_LVL >= VERBOSE_LVL_ALL)
		printf("Before merge: run is (%d,%d,%llu)\n", rnn.b->offset, rnn.b->nRuns, rnn.b->run);

  _RLEVector_mergeNeighbors(vec, rnn);
  /* After merging, rnn.{a,b,c} is untrustworthy. */
  
  if (vec->autoValidate)
    _RLEVector_validate(vec);

  return;
}

int
RLEVector_get(RLEVector *vec, int ix)
{
  RLENode target;
  RLENode *match = NULL;

  logMsg(LOG_DEBUG, "RLEVector_get: %d", ix);

  if (vec->autoValidate)
    _RLEVector_validate(vec);

  target.offset = ix;
  target.nRuns = -1;
  match = avl_tree_entry(
    avl_tree_lookup_node(vec->root, &target.node, RLENode_avl_tree_cmp),
    RLENode, node);

  if (match == NULL) {
    return 0;
  }

  logMsg(LOG_DEBUG, "match: %p", match);
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

static void _RLEVector_addRun(RLEVector *vec, RLENode *node)
{
  logMsg(LOG_DEBUG, "Adding run (%d,%d,%llu)", node->offset, node->nRuns, node->run);

	assert(avl_tree_insert(&vec->root, &node->node, RLENode_avl_tree_cmp) == NULL);
  vec->currNEntries++;
  if (vec->mostNEntries < vec->currNEntries) {
    vec->mostNEntries = vec->currNEntries;
  }

  if (vec->autoValidate)
    _RLEVector_validate(vec);
}

static void _RLEVector_removeRun(RLEVector *vec, RLENode *node)
{
	logMsg(LOG_DEBUG, "Removing run (%d,%d,%llu)", node->offset, node->nRuns, node->run);

	avl_tree_remove(&vec->root, &node->node);
  vec->currNEntries--;

  if (vec->autoValidate)
    _RLEVector_validate(vec);
}
