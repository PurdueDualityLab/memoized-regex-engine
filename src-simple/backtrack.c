// Copyright 2007-2009 Russ Cox.  All Rights Reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "regexp.h"

typedef struct Thread Thread;
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

Memo initMemoTable(Prog *prog, int nChars, int memoMode)
{
	Memo memo;
	int nStatesToTrack = prog->len;
	int i;
	
	printf("Memo table: %d visit vectors\n", nStatesToTrack);
	/* Visit vectors */
	memo.visitVectors = mal(sizeof(char*) * nStatesToTrack);

	printf("Memo table: %d visit vectors x %d chars for each\n", nStatesToTrack, nChars);
	for (i = 0; i < nStatesToTrack; i++) {
		memo.visitVectors[i] = mal(sizeof(char) * nChars);
		memset(memo.visitVectors[i], 0, nChars);
	}

	return memo;
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

int
backtrack(Prog *prog, char *input, char **subp, int nsubp)
{
	Memo memo;
	enum { MAX = 1000 };
	Thread ready[MAX];
	int i, nready;
	Inst *pc;
	char *sp;
	Sub *sub;

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
				if (isMarked(&memo, statenum(prog, pc), woffset(input, sp))) {
				    /* Since we return on first match, the prior visit failed.
					 * Short-circuit thread */
					goto Dead;
				}

				/* Mark that we've been here */
				markMemo(&memo, statenum(prog, pc), woffset(input, sp));
			}

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
	return 0;
}

