// Copyright 2007-2009 Russ Cox.  All Rights Reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "regexp.h"

typedef struct Thread Thread;
typedef struct VisitTable VisitTable;


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
	int i;
	char *prefix = "VISIT_TABLE";

	visitTable.nStates = nStates;
	visitTable.nChars = nChars;
	visitTable.visitVectors = mal(sizeof(int*) * nStates);
	for (i = 0; i < nStates; i++) {
		visitTable.visitVectors[i] = mal(sizeof(int) * nChars);
		memset(visitTable.visitVectors[i], 0, nChars);
	}

	return visitTable;
}

void
markVisit(VisitTable *visitTable, int statenum, int woffset)
{
	visitTable->visitVectors[statenum][woffset]++;
}

Memo
initMemoTable(Prog *prog, int nChars, int memoMode)
{
	Memo memo;
	int cardQ = prog->len;
	int nStatesToTrack = prog->len;
	int i;
	char *prefix = "MEMO_TABLE";
	
	printf("%s: cardQ = %d\n", prefix, cardQ);
	
	if (memoMode == MEMO_FULL) {
		printf("%s: %d visit vectors\n", prefix, nStatesToTrack);
		/* Visit vectors */
		memo.visitVectors = mal(sizeof(char*) * nStatesToTrack);

		printf("%s: %d visit vectors x %d chars for each\n", prefix, nStatesToTrack, nChars);
		for (i = 0; i < nStatesToTrack; i++) {
			memo.visitVectors[i] = mal(sizeof(char) * nChars);
			memset(memo.visitVectors[i], 0, nChars);
		}

		return memo;
	}

}

static int
statenum(Prog *prog, Inst *pc)
{
	return (int) (pc - prog->start);
}

static int
woffset(char *input, char *sp)
{
	return (int) (sp - input);
}

static void
markMemo(Memo *memo, int statenum, int woffset)
{
	memo->visitVectors[statenum][woffset] = 1;
}

static int
isMarked(Memo *memo, int statenum, int woffset)
{
	return memo->visitVectors[statenum][woffset] == 1;
}

static void
printStats(Memo *memo, VisitTable *visitTable)
{
	int i;
	int j;
	int mostVisitedState = -1;
	int maxVisits = -1;
	int *visitsPerState;

	/* Most-visited vertex */
	visitsPerState = mal(sizeof(int) * visitTable->nStates);
	for (i = 0; i < visitTable->nStates; i++) {
		visitsPerState[i] = 0;
		for (j = 0; j < visitTable->nChars; j++) {
			visitsPerState[i] += visitTable->visitVectors[i][j];
		}

		if (visitsPerState[i]  > maxVisits) {
			mostVisitedState = i;
			maxVisits = visitsPerState[i];
		}
	}

	/* TODO Why is this >1 for FULL? */
	printf("Most-visited vertex: %d (%d) visits\n", mostVisitedState, maxVisits);
}

int
backtrack(Prog *prog, char *input, char **subp, int nsubp)
{
	Memo memo;
	VisitTable visitTable;
	enum { MAX = 1000 };
	Thread ready[MAX];
	int i, nready;
	Inst *pc;
	char *sp;
	Sub *sub;

	/* Prep visit table */
	visitTable = initVisitTable(prog, strlen(input));

	/* Prep memo table */
	if (prog->memoMode != MEMO_NONE) {
		printf("Initializing memo table\n");
		memo = initMemoTable(prog, strlen(input), prog->memoMode);
	}

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

			if (prog->memoMode != MEMO_NONE) {
				/* Check if we've been here. */
				if (isMarked(&memo, pc->stateNum, woffset(input, sp))) {
				    /* Since we return on first match, the prior visit failed.
					 * Short-circuit thread */
					goto Dead;
				}

				/* Mark that we've been here */
				markMemo(&memo, pc->stateNum, woffset(input, sp));
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
				printStats(&memo, &visitTable);
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

	printStats(&memo, &visitTable);
	return 0;
}

