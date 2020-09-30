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
typedef struct LanguageLengthInfo LanguageLengthInfo;
typedef struct InstInfoForMemoSelPolicy InstInfoForMemoSelPolicy;

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

	/* Curly */
	int curlyMin; /* Use -1 if no lower bound */
	int curlyMax; /* Use -1 if no upper bound */

	/* Backref */
	int cgNum;

	/* Do not use. */
	LanguageLengthInfo lli;
	int visitInterval;
};

// Caller can fill in additional details
Regexp *reg(int type, Regexp *left, Regexp *right);
// Deep copy
Regexp *copyreg(Regexp *r);
// Print the AST represented by this Regexp
void printre(Regexp *r);
// Recursively free the AST represented by this Regexp
void freereg(Regexp *r);

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
	Curly,	 /* A{} */
	Backref, /* \1 */
	Lookahead, /* (?=A) */
	InlineZWA, /* ^, \A, \b, \B, $, \z, \Z */
};

// Used to support InlineZWA: \b \B 
#define IS_WORD_CHAR(c) (('a' <= c && c <= 'z') || ('A' <= c && c <= 'Z') || ('0' <= c && c <= '9'))

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

struct InstInfoForMemoSelPolicy
{
	int shouldMemo;
	int inDegree;
	int isAncestorLoopDestination;
	int memoStateNum; /* -1 if "don't memo", else 0 to |Phi_memo| */

	/*  (NOT WORKING). These are the intervals at which this vertex may be visited
	 *    during the automaton simulation.
	 *  Use to determine RLE lengths if we memoize this Inst.
	 */
	int visitInterval;
};

struct Inst
{
	int opcode; /* Instruction. Determined by the corresponding Regex node */
	int c; /* For Lit or Boundary: The literal character */
	int n; /* Quant: 1 means greedy. Save: 2*n and 2*n + 1 are paired. */
	int stateNum; /* 0 to Prog->len-1 */
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

	/* Debug */
	int startMark;
	int visitMark;

	InstInfoForMemoSelPolicy memoInfo;
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
	InlineZeroWidthAssertion,
	RecursiveZeroWidthAssertion,
};

Prog *compile(Regexp*, int);
void Prog_assertNoInfiniteLoops(Prog *p);
void printprog(Prog*);

extern int gen;

/* Support for captures -- this covers \0-\9 */
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

/* Backreference helpers */
int usesBackreferences(Prog *p);

// Given a CGID, which sub are we looking at?
#define CGID_TO_SUB_STARTP_IX(cgid) (2*(cgid))
#define CGID_TO_SUB_ENDP_IX(cgid) (2*(cgid) + 1)
// Given a CGID, get string start/end pointers
#define CGID_TO_STARTP(s, cgid)   ((s)->sub[ CGID_TO_SUB_STARTP_IX( (cgid) )])
#define CGID_TO_ENDP(s, cgid)   ((s)->sub[ CGID_TO_SUB_ENDP_IX( (cgid) )])

/* (Extended-)NFA simulations */
int backtrack(Prog*, char*, char**, int);
int pikevm(Prog*, char*, char**, int);
int recursiveloopprog(Prog*, char*, char**, int);
int recursiveprog(Prog*, char*, char**, int);
int thompsonvm(Prog*, char*, char**, int);

#endif /* REGEXP_H */
