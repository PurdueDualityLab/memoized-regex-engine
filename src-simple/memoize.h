// Copyright 2020 James C. Davis.  All Rights Reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#ifndef MEMOIZE_H
#define MEMOIZE_H

#include "regexp.h"

/* Memoization-related compilation phase. */

/* Memoization-related simulation. */

typedef struct Memo Memo;
typedef struct SimPos SimPos;
typedef struct SimPosTable SimPosTable;

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


#endif /* MEMOIZE_H */
