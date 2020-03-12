// Copyright 2007-2009 Russ Cox.  All Rights Reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.
//
// Annotations, statistics, and memoization by James Davis, 2020.

#include "regexp.h"

static int VERBOSE = 0;

typedef struct Thread Thread;
typedef struct VisitTable VisitTable;

/* Introduced whenever we make a non-deterministic choice.
 * The current thread proceeds, and the other is saved to try later. */
struct Thread
{
	Inst *pc; /* Automaton vertex ~= Instruction to execute */
	char *sp; /* Offset in candidate string, w */
	Sub *sub; /* Sub-match (capture groups) */
};

static Thread
thread(Inst *pc, char *sp, Sub *sub)
{
	Thread t = {pc, sp, sub};
	return t;
}

struct VisitTable
{
	int **visitVectors; /* Counters */
	int nStates;
	int nChars;
};

VisitTable 
initVisitTable(Prog *prog, int nChars)
{
	VisitTable visitTable;
	int nStates = prog->len;
	int i, j;
	char *prefix = "VISIT_TABLE";

	visitTable.nStates = nStates;
	visitTable.nChars = nChars;
	visitTable.visitVectors = mal(sizeof(int*) * nStates);
	for (i = 0; i < nStates; i++) {
		visitTable.visitVectors[i] = mal(sizeof(int) * nChars);
		for (j = 0; j < nChars; j++) {
			visitTable.visitVectors[i][j] = 0;
		}
	}

	return visitTable;
}

void
markVisit(VisitTable *visitTable, int statenum, int woffset)
{
	if (VERBOSE) {
		printf("Visit: Visiting <%d, %d>\n", statenum, woffset);
	
		if (visitTable->visitVectors[statenum][woffset] > 0)
			printf("Hmm, already visited <%d, %d>\n", statenum, woffset);
	}
	assert(statenum < visitTable->nStates);
	assert(woffset < visitTable->nChars);
	
	visitTable->visitVectors[statenum][woffset]++;
}

Memo
initMemoTable(Prog *prog, int nChars, int memoMode)
{
	Memo memo;
	int cardQ = prog->len;
	int nStatesToTrack = prog->nMemoizedStates;
	int i, j;
	char *prefix = "MEMO_TABLE";
	
	printf("%s: cardQ = %d, Phi_memo = %d\n", prefix, cardQ, nStatesToTrack);
	
	/* Visit vectors */
	memo.nStates = nStatesToTrack;
	memo.nChars = nChars;
	memo.visitVectors = mal(sizeof(int*) * nStatesToTrack);

	printf("%s: %d visit vectors x %d chars for each\n", prefix, nStatesToTrack, nChars);
	for (i = 0; i < nStatesToTrack; i++) {
		memo.visitVectors[i] = mal(sizeof(int) * nChars);
		for (j = 0; j < nChars; j++) {
			memo.visitVectors[i][j] = 0;
		}
	}

	printf("%s: initialized\n", prefix);
	return memo;
}

static int
woffset(char *input, char *sp)
{
	return (int) (sp - input);
}

static void
markMemo(Memo *memo, int statenum, int woffset)
{
	if (VERBOSE) {
		printf("Memo: Marking <%d, %d>\n", statenum, woffset);

		if (memo->visitVectors[statenum][woffset])
			printf("\n****\n\n   Hmm, already marked s%d c%d\n\n*****\n\n", statenum, woffset);
	}

	assert(statenum < memo->nStates);
	assert(woffset < memo->nChars);

	memo->visitVectors[statenum][woffset] = 1;
}

static int
isMarked(Memo *memo, int statenum, int woffset)
{
	return memo->visitVectors[statenum][woffset] == 1;
}

static void
printStats(Memo *memo, VisitTable *visitTable, int memoMode)
{
	int i;
	int j;

	char *prefix = "STATS";

	/* Per-search state */
	int maxVisitsPerSearchState = -1;
	int vertexWithMostVisitedSearchState = -1;
	int mostVisitedOffset = -1;

	/* Sum over all offsets */
	int maxVisitsPerVertex = -1;
	int mostVisitedVertex = -1;
	int *visitsPerVertex; /* Per-vertex sum of visits over all offsets */

	/* Most-visited vertex */
	visitsPerVertex = mal(sizeof(int) * visitTable->nStates);
	for (i = 0; i < visitTable->nStates; i++) {
		visitsPerVertex[i] = 0;
		for (j = 0; j < visitTable->nChars; j++) {
			/* Running sum */
			visitsPerVertex[i] += visitTable->visitVectors[i][j];

			/* Largest individual visits over all search states? */
			if (visitTable->visitVectors[i][j] > maxVisitsPerSearchState) {
				maxVisitsPerSearchState = visitTable->visitVectors[i][j];
				vertexWithMostVisitedSearchState = i;
				mostVisitedOffset = j;
			}
		}

		/* Largest overall visits per vertex? */
		if (visitsPerVertex[i]  > maxVisitsPerVertex) {
			maxVisitsPerVertex = visitsPerVertex[i];
			mostVisitedVertex = i;
		}
	}

	printf("%s: Most-visited search state: <%d, %d> (%d visits)\n", prefix, vertexWithMostVisitedSearchState, mostVisitedOffset, maxVisitsPerSearchState);
	printf("%s: Most-visited vertex: %d (%d visits over all its search states)\n", prefix, mostVisitedVertex, maxVisitsPerVertex);

	if (memoMode == MEMO_FULL || memoMode == MEMO_IN_DEGREE_GT1) {
		/* I have proved this is impossible. */
		assert(maxVisitsPerSearchState <= 1);
	}
}

int
backtrack(Prog *prog, char *input, char **subp, int nsubp)
{
	Memo memo;
	VisitTable visitTable;
	enum { MAX = 1000 };
	Thread ready[MAX];
	int i, nready;
	Inst *pc; /* Current position in VM (pc) */
	char *sp; /* Current position in *input */
	Sub *sub; /* submatch (capture group) */

	/* Prep visit table */
	printf("Initializing visit table\n");
	visitTable = initVisitTable(prog, strlen(input) + 1);

	/* Prep memo table */
	if (prog->memoMode != MEMO_NONE) {
		printf("Initializing memo table\n");
		memo = initMemoTable(prog, strlen(input) + 1, prog->memoMode);
	}

	if (VERBOSE) {
		printStats(&memo, &visitTable, prog->memoMode);
	}

	printf("\n\n***************\n\n  Backtrack: Simulation begins\n\n************\n\n");

	/* queue initial thread */
	sub = newsub(nsubp);
	for(i=0; i<nsubp; i++)
		sub->sub[i] = nil;
	/* Initial thread state is < q0, w[0], current capture group > */
	ready[0] = thread(prog->start, input, sub);
	nready = 1;

	/* run threads in stack order */
	while(nready > 0) {
		--nready;	/* pop state for next thread to run */
		pc = ready[nready].pc;
		sp = ready[nready].sp;
		sub = ready[nready].sub;
		assert(sub->ref > 0);
		for(;;) { /* Run thread to completion */
			if (VERBOSE)
				printf("  search state: <%d (M: %d), %d>\n", pc->stateNum, pc->memoStateNum, woffset(input, sp));

			if (prog->memoMode != MEMO_NONE && pc->memoStateNum >= 0) {
				/* Check if we've been here. */
				if (isMarked(&memo, pc->memoStateNum, woffset(input, sp))) {
				    /* Since we return on first match, the prior visit failed.
					 * Short-circuit thread */
					assert(pc->opcode != Match);

					if (pc->opcode == Char || pc->opcode == Any) {
						goto Dead;
					} else {
						break;
					}
				}

				/* Mark that we've been here */
				markMemo(&memo, pc->memoStateNum, woffset(input, sp));
			}

			/* "Visit" means that we evaluate pc appropriately. */
			markVisit(&visitTable, pc->stateNum, woffset(input, sp));

			/* Proceed as normal */
			switch(pc->opcode) {
			case Char:
				if(*sp != pc->c)
					goto Dead;
				pc++;
				sp++;
				continue;
			case Any:
				if(*sp == 0)
					goto Dead;
				pc++;
				sp++;
				continue;
			case Match:
				for(i=0; i<nsubp; i++)
					subp[i] = sub->sub[i];
				decref(sub);
				printStats(&memo, &visitTable, prog->memoMode);
				return 1;
			case Jmp:
				pc = pc->x;
				continue;
			case Split: /* Non-deterministic choice */
				if(nready >= MAX)
					fatal("backtrack overflow");
				ready[nready++] = thread(pc->y, sp, incref(sub));
				pc = pc->x;	/* continue current thread */
				continue;
			case Save:
				sub = update(sub, pc->n, sp);
				pc++;
				continue;
			}
		}
	Dead:
		decref(sub);
	}

	printStats(&memo, &visitTable, prog->memoMode);
	return 0;
}

