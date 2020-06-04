// Copyright 2007-2009 Russ Cox.  All Rights Reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#ifndef REGEXP_H
#define REGEXP_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <assert.h>
#include "uthash.h"
#include "rle.h"

#define nil ((void*)0)
#define nelem(x) (sizeof(x)/sizeof((x)[0]))

typedef struct Regexp Regexp;
typedef struct Prog Prog;
typedef struct Inst Inst;
typedef struct InstCharRange InstCharRange;
typedef struct Memo Memo;
typedef struct SearchState SearchState;
typedef struct SearchStateTable SearchStateTable;
typedef struct LanguageLengthInfo LanguageLengthInfo;

/* Possible lengths of "simple" strings in the language of this regex.
 * "simple" strings correspond to simple paths in the corresponding automaton. */
struct LanguageLengthInfo
{
	int languageLengths[16]; /* Lengths in the regex's SIMPLE language */
	int nLanguageLengths; /* Bound on languageLengths */
	int tooManyLengths; /* Flag -- too many possible lengths to trackj0f */
};

struct Regexp
{
	int type;

	int n; /* Quantifiers: Non-greedy? 1 means yes. Saves: Paren count. */
	int ch; /* Literals, CharEscape's: Character. */
	Regexp *left; /* Child for unary operators. Left child for binary operators. */
	Regexp *right; /* Left child for binary operators. */

    /* May be populated in an optimization pass that converts binary operators to *-arity.
	 * Used by Alt and CCC */
	Regexp **children;
	int arity;

	/* Anchored search? (applied to the root Regexp) */
	int bolAnchor;
	int eolAnchor;

	/* CustomCharClass */
	int plusDash; // Is an unescaped '-' part of the CCC, e.g. [-a] or [a-]?
	int ccInvert;
	int mergedRanges;

	/* CharRange */
	Regexp *ccLow;  /* Lit or CharEscape */
	Regexp *ccHigh; /* Lit or CharEscape */

	/* Backref */
	int cgNum;

	/* Do not use. */
	LanguageLengthInfo lli;
	int visitInterval;
};

enum	/* Regexp.type */
{
	Alt = 1, /* A | B -- 2-arity */
	AltList, /* A | B | ... -- *-arity */
	Cat,     /* AB */
	Lit,     /* "a" */
	Dot,     /* any char */
	CharEscape, /* \s, \S, etc. */
	CustomCharClass, /* [...] -- *-arity */
	CharRange, /* 'a' or 'a-z' */
	Paren,   /* (...) */
	Quest,   /* A? */
	Star,    /* A* */
	Plus,    /* A+ */
	Backref, /* \1 */
	Lookahead, /* (?=A) */
};

Regexp *parse(char*);
Regexp *reg(int type, Regexp *left, Regexp *right);
void printre(Regexp*);
void fatal(char*, ...);
void *mal(int);

/* Transformation pass */
Regexp *transform(Regexp *r);

struct Prog
{
	Inst *start;
	int len;
	int memoMode; /* Memo.mode */
	int memoEncoding; /* Memo.encoding */
	int nMemoizedStates;
	int eolAnchor;
};

struct InstCharRange
{
	// Big enough to hold any built-in char classes
	int lows[5];
	int highs[5]; // Inclusive
	int count;
	int invert; // For \W, \S, \D
};

struct Inst
{
	int opcode; /* Instruction. Determined by the corresponding Regex node */
	int c; /* For Lit: The literal character to match */
	int n; /* Quant: 1 means greedy. Save: 2*n and 2*n + 1 are paired. */
	int stateNum; /* 0 to Prog->len-1 */
	int shouldMemo;
	int inDegree;
	int memoStateNum; /* -1 if "don't memo", else 0 to |Phi_memo| */
	Inst *x; /* Outgoing edge -- destination 1 (default option) */
	Inst *y; /* Outgoing edge -- destination 2 (backup) */
	int gen;	// global state, oooh!

	Inst **edges; /* Outgoing edges for case of *-arity */
	int arity;
	
	/* For CharClass */
	InstCharRange charRanges[32];
	int charRangeCounts; /* Number of used slots */
	int invert;

	/* For StringCompare */
	int cgNum;

	/*  These are the intervals at which this vertex may be visited
	 *    during the automaton simulation.
	 *  Use to determine RLE lengths if we memoize this Inst.
	 */
	int visitInterval;
};

enum	/* Inst.opcode */
{
	Char = 1,
	Match,
	RecursiveMatch, // For the lookahead sub-automata
	Jmp,
	Split,
	SplitMany,
	Any,
	CharClass,
	Save,
	StringCompare,
	ZeroWidthAssertion,
};

Prog *compile(Regexp*, int);
void printprog(Prog*);

extern int gen;

enum {
	MAXSUB = 20
};

typedef struct Sub Sub;

struct Sub
{
	int ref;
	int nsub;
	char *start; /* Easy way to calculate w[i] vs. char * */
	char *sub[MAXSUB]; /* Two slots for each CG, \0 (whole string) - \9 */
};

Sub *newsub(int n, char *start);
Sub *incref(Sub*);
Sub *copy(Sub*);
Sub *update(Sub*, int, char*);
void decref(Sub*);
int isgroupset(Sub*, int);

struct SearchState
{
	int stateNum;
	int stringIndex;
	/* Used for backrefs */
	int cgStarts[MAXSUB/2];
	int cgEnds[MAXSUB/2];
};

struct SearchStateTable
{
	SearchState key;
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

	/* Carries structures for use under the various supported encodings.
     * I suppose this could be a union ;-) */

	/* ENCODING_NONE */
	int **visitVectors; /* Booleans: vv[q][i] */

	/* ENCODING_NEGATIVE */
	SearchStateTable *searchStateTable; /* < q, i [, backrefs ] > */

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

int backtrack(Prog*, char*, char**, int);
int pikevm(Prog*, char*, char**, int);
int recursiveloopprog(Prog*, char*, char**, int);
int recursiveprog(Prog*, char*, char**, int);
int thompsonvm(Prog*, char*, char**, int);

#endif /* REGEXP_H */