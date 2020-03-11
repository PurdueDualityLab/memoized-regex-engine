// Copyright 2007-2009 Russ Cox.  All Rights Reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <assert.h>

#define nil ((void*)0)
#define nelem(x) (sizeof(x)/sizeof((x)[0]))

typedef struct Regexp Regexp;
typedef struct Prog Prog;
typedef struct Inst Inst;
typedef struct Memo Memo;

struct Regexp
{
	int type;
	int n;
	int ch;
	Regexp *left;
	Regexp *right;
};

enum	/* Regexp.type */
{
	Alt = 1,
	Cat,
	Lit,
	Dot,
	Paren,
	Quest,
	Star,
	Plus,
};

Regexp *parse(char*);
Regexp *reg(int type, Regexp *left, Regexp *right);
void printre(Regexp*);
void fatal(char*, ...);
void *mal(int);

struct Prog
{
	Inst *start;
	int len;
	int memoMode; /* MEMO_X */
};

struct Inst
{
	int opcode;
	int c;
	int n;
	int stateNum; /* 0 to Prog->len-1 */
	Inst *x; /* Outoging edge -- destination 1 */
	Inst *y; /* Outoging edge -- destination 2 */
	int gen;	// global state, oooh!
};

enum	/* Inst.opcode */
{
	Char = 1,
	Match,
	Jmp,
	Split,
	Any,
	Save,
};

Prog *compile(Regexp*);
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
	char *sub[MAXSUB];
};

Sub *newsub(int n);
Sub *incref(Sub*);
Sub *copy(Sub*);
Sub *update(Sub*, int, char*);
void decref(Sub*);

struct Memo
{
	int *pc2vv; /* Map a */
	int **visitVectors; /* Booleans */
	int mode;
};

enum /* Memo.mode */
{
	MEMO_NONE,
	MEMO_FULL,
	MEMO_IN_DEGREE_GT1,
	MEMO_LOOP_DEST,
};

int backtrack(Prog*, char*, char**, int);
int pikevm(Prog*, char*, char**, int);
int recursiveloopprog(Prog*, char*, char**, int);
int recursiveprog(Prog*, char*, char**, int);
int thompsonvm(Prog*, char*, char**, int);
