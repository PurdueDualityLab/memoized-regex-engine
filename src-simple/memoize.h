// Copyright 2020 James C. Davis.  All Rights Reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#ifndef MEMOIZE_H
#define MEMOIZE_H

#include "regexp.h"
#include "rle.h"

/* Memoization-related compilation phase. */

void Prog_determineMemoNodes(Prog *p, int memoMode);

/* Memoization-related simulation. */

typedef struct VisitTable VisitTable;
typedef struct Memo Memo;
typedef struct SimPos SimPos;
typedef struct SimPosTable SimPosTable;

// Used to evaluate whether memoization guarantees have failed.
struct VisitTable
{
  int **visitVectors; /* Counters */
  int nStates; /* |Q| */
  int nChars;  /* |w| */
};

struct SimPos
{
    /* Relevant regex engine state: <vertex, w index> */
	int stateNum;
	int stringIndex;
	/* At vertices corresponding to backreferences, also track the CG vector. */
	int cgStarts[MAXSUB/2];
	int cgEnds[MAXSUB/2];
};

/* For hashing */
struct SimPosTable
{
	SimPos key;
	UT_hash_handle hh; /* Makes this structure hashable */
};

/* Declare here so visible for selecting vertices during compilation */
struct Memo
{
	int nStates; /* |Phi| */
	int nChars;  /* |w| */
	int mode;
	int encoding;
	int backrefs; /* Backrefs present? */

	/* Structures for each encoding scheme. */

	/* ENCODING_NONE */
	int **visitVectors; /* Booleans: visitVector[q][i] */

	/* ENCODING_NEGATIVE */
	SimPosTable *searchStateTable; /* Tuples: < q, i [, backrefs ] > */

	/* ENCODING_RLE, ENCODING_RLE_TUNED */
	RLEVector **rleVectors;
};

enum /* Memo.mode */
{
	MEMO_NONE,
	MEMO_FULL,
	MEMO_IN_DEGREE_GT1,
	MEMO_LOOP_DEST,
};

enum /* Memo.encoding */
{
	ENCODING_NONE,
	ENCODING_NEGATIVE,  /* Hash table */
	ENCODING_RLE,       /* Run-length encoding */
	ENCODING_RLE_TUNED, /* DO NOT USE -- RLE, tuned for language lengths -- DO NOT USE */
};

VisitTable initVisitTable(Prog *prog, int nChars);
void markVisit(VisitTable *visitTable, int statenum, int woffset);
void freeVisitTable(VisitTable vt);

Memo initMemoTable(Prog *prog, int nChars);
int isMarked(Memo *memo, int statenum /* PC's memoStateNum */, int woffset, Sub *sub);
void markMemo(Memo *memo, int statenum, int woffset, Sub *sub);
void freeMemoTable(Memo memo);

#endif /* MEMOIZE_H */
