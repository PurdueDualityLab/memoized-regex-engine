// Copyright 2007-2009 Russ Cox.  All Rights Reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "regexp.h"
#include <ctype.h>

static int LOG_LLI = 1;

static Inst *pc; /* VM array */
static int count(Regexp*);
static void Regexp_calcLLI(Regexp *r);
static void Regexp_calcVisitInterval(Regexp *r);
static void emit(Regexp*, int);

static void
lli_addEntry(LanguageLengthInfo *lli, int newLength)
{
	int i;

	/* Already too full? */
	if (lli->tooManyLengths)
		return;

	/* Check if it's present already */
	for (i = 0; i < lli->nLanguageLengths; i++) {
		if (newLength == lli->languageLengths[i]) {
			return;
		}
	}

	/* Insert. */
	if (lli->nLanguageLengths >= nelem(lli->languageLengths)) {
		/* No space. */
		lli->tooManyLengths = 1;
		return;
	}

	lli->languageLengths[ lli->nLanguageLengths ] = newLength;
	lli->nLanguageLengths++;
	return;
}

/* printf, ends in a newline */
static void
lli_print(LanguageLengthInfo *lli)
{
	int i;

	/* Already too full? */
	if (lli->tooManyLengths)
		printf("LLI: Over-full\n");

	/* Check if it's present already */
	if (LOG_LLI) {
		printf("LLI: %d lengths: ", lli->nLanguageLengths);
		for (i = 0; i < lli->nLanguageLengths; i++) {
			printf("%d,", lli->languageLengths[i]);
		}
		printf("\n");
	}

	return;
}

/* Computes the LCM of the integers >= 1 in arr. */
static int
leastCommonMultiple(int arr[], int n)
{
	int i, smallest = -1, product = 1, possibleLCM = -1;

	/* Compute an overall product -- this is the maximum possible LCM.
	 * Also obtain the smallest entry > 1. */
	for (i = 0; i < n; i++) {
		if (1 < arr[i]) {
			product *= arr[i];
		}

		if (smallest == -1 || (1 < arr[i] && arr[i] < smallest))
			smallest = arr[i];
	}
	if (smallest == -1)
		smallest = 1;
	
	/* The LCM contains all of the factors of smallest.
	 * Look at its multiples until we find the LCM. */
	possibleLCM = smallest;
	while (possibleLCM < product) {
		int nDivisible = 0;
		for (i = 0; i < n; i++) {
			if (arr[i] <= 0 || possibleLCM % arr[i] == 0)
				nDivisible++;
		}

		if (nDivisible == n)
			return possibleLCM;

		possibleLCM += smallest;
	}

	return product;
}

static int
leastCommonMultiple2(int a, int b)
{
	int nums[2] = { a, b };
	return leastCommonMultiple(nums, 2);
}

/* LCM */
static int
lli_smallestPeriod(LanguageLengthInfo *lli)
{
	if (lli->tooManyLengths) {
		printf("Run length overflow\n");
		return 2; /* A good default -- no worse than 1, and maybe better */
	}

	/* Find the LCM of the language lengths */
	return leastCommonMultiple(lli->languageLengths, lli->nLanguageLengths);
}

void
Prog_compute_in_degrees(Prog *p)
{
	int i;

	/* Initialize */
	for (i = 0; i < p->len; i++) {
		p->start[i].inDegree = 0;
	}
	/* q0 has an in-edge */
	p->start[0].inDegree = 1;

	/* Increment */
	for (i = 0; i < p->len; i++) {
		switch(p->start[i].opcode) {
		default:
			fatal("bad count");
		case Char:
			/* Always goes to next instr */
			p->start[i+1].inDegree++;
			break;
		case Match:
			/* Terminates search */
			break;
		case Jmp:
			/* Goes to X */
			p->start[i].x->inDegree++;
			break;
		case Split:
			/* Goes to X or Y */
			p->start[i].x->inDegree++;
			p->start[i].y->inDegree++;
			break;
		case Any:
			/* Always goes to next instr */
			p->start[i+1].inDegree++;
			break;
		case CharClass:
			/* Always goes to next instr */
			p->start[i+1].inDegree++;
			break;
		case Save:
			/* Always goes to next instr */
			p->start[i+1].inDegree++;
			break;
		}
	}
}

static void
Prog_assignStateNumbers(Prog *p)
{
	int i;
	for (i = 0; i < p->len; i++) {
		p->start[i].stateNum = i;
	}
}

static void
Prog_determineMemoNodes(Prog *p, int memoMode)
{
	int i, nextStateNum;

	/* Determine which nodes to memoize based on memo mode. */
	switch (memoMode) {
	case MEMO_FULL:
		for (i = 0; i < p->len; i++){
			p->start[i].shouldMemo = 1;
		}
		break;
	case MEMO_IN_DEGREE_GT1:
		Prog_compute_in_degrees(p);
		for (i = 0; i < p->len; i++) {
			if (p->start[i].inDegree > 1) {
				p->start[i].shouldMemo = 1;
			}
		}
		break;
	case MEMO_LOOP_DEST:
		/* This is done in emit(). */
		break;
	case MEMO_NONE:
		for (i = 0; i < p->len; i++) {
			p->start[i].shouldMemo = 0;
		}
		break;
	default:
		assert(!"Unknown memoMode\n");
	}

	/* Assign memoStateNum to the shouldMemo nodes */
	nextStateNum = 0;
	for (i = 0; i < p->len; i++) {
		if (p->start[i].shouldMemo) {
			p->start[i].memoStateNum = nextStateNum;
			nextStateNum++;
		} else {
			p->start[i].memoStateNum = -1;
		}
	}
	p->nMemoizedStates = nextStateNum;
}


Prog*
compile(Regexp *r, int memoMode)
{
	int i, n;
	Prog *p;

	n = count(r) + 1;
	Regexp_calcLLI(r);
	Regexp_calcVisitInterval(r);

	p = mal(sizeof *p + n*sizeof p->start[0]);
	p->start = (Inst*)(p+1);
	pc = p->start;
	for (i = 0; i < n; i++) {
		p->start[i].visitInterval = 1; /* A good default */
	}
	emit(r, memoMode);
	pc->opcode = Match;
	pc++;
	p->len = pc - p->start;
	p->eolAnchor = r->eolAnchor;

	/*
	for (i = 0; i < n; i++) {
		if (p->start[i] == 
	}
	*/

	Prog_assignStateNumbers(p);
	Prog_determineMemoNodes(p, memoMode);
	printf("Will memoize %d states\n", p->nMemoizedStates);

	return p;
}

// how many instructions does r need?
static int
count(Regexp *r)
{
	switch(r->type) {
	default:
		fatal("bad count");
	case Alt:
		return 2 + count(r->left) + count(r->right);
	case Cat:
		return count(r->left) + count(r->right);
	case Lit:
	case Dot:
	case CharEscape:
		return 1;
	case Paren:
		return 2 + count(r->left);
	case Quest:
		return 1 + count(r->left);
	case Star:
		return 2 + count(r->left);
	case Plus:
		return 1 +  count(r->left);
	}
}

// Determine size of simple languages for r
// Recursively populates sub-patterns
static void
Regexp_calcLLI(Regexp *r)
{
	int i, j;
	switch(r->type) {
	default:
		fatal("bad count");
	case Alt:
		Regexp_calcLLI(r->left);
		Regexp_calcLLI(r->right);
		
		/* Combine: A giant OR */
		r->lli.nLanguageLengths = 0;
		for (i = 0; i < r->left->lli.nLanguageLengths; i++) {
			lli_addEntry(&r->lli, r->left->lli.languageLengths[i]);
		}
		for (i = 0; i < r->right->lli.nLanguageLengths; i++) {
			lli_addEntry(&r->lli, r->right->lli.languageLengths[i]);
		}

		if (LOG_LLI) {
			printf("LLI: Alt\n");
			lli_print(&r->lli);
		}
		break;
	case Cat:
		Regexp_calcLLI(r->left);
		Regexp_calcLLI(r->right);

		/* Combine: Cartesian product */
		r->lli.nLanguageLengths = 0;
		for (i = 0; i < r->left->lli.nLanguageLengths; i++) {
			for (j = 0; j < r->right->lli.nLanguageLengths; j++) {
				lli_addEntry(&r->lli, r->left->lli.languageLengths[i] + r->right->lli.languageLengths[j]);
			}
		}

		if (LOG_LLI) {
			printf("LLI: Cat\n");
			lli_print(&r->lli);
		}
		break;
	case Lit:
	case Dot:
	case CharEscape:
		r->lli.nLanguageLengths = 1;
		r->lli.languageLengths[0] = 1;

		if (LOG_LLI) {
			printf("LLI: Lit,Dot,CharEscape\n");
			lli_print(&r->lli);
		}
		break;
	case Paren:
		Regexp_calcLLI(r->left);
		r->lli = r->left->lli;

		if (LOG_LLI) {
			printf("LLI: Paren\n");
			lli_print(&r->lli);
		}
		break;
	case Quest:
		Regexp_calcLLI(r->left);
		r->lli = r->left->lli;
		lli_addEntry(&r->lli, 0);

		if (LOG_LLI) {
			printf("LLI: Quest:\n");
			lli_print(&r->lli);
		}
		break;
	case Star:
		Regexp_calcLLI(r->left);
		r->lli = r->left->lli;
		lli_addEntry(&r->lli, 0);

		if (LOG_LLI) {
			printf("LLI: Star\n");
			lli_print(&r->lli);
		}
		break;
	case Plus:
		Regexp_calcLLI(r->left);
		r->lli = r->left->lli;

		if (LOG_LLI) {
			printf("LLI: Plus\n");
			lli_print(&r->lli);
		}
		break;
	}
}

// Determine visit intervals for r
// Call after all LLI are known
// Recursively populates sub-patterns
static void
Regexp_calcVisitInterval(Regexp *r)
{
	switch(r->type) {
	default:
		fatal("bad count");
	case Alt:
		Regexp_calcVisitInterval(r->left);
		Regexp_calcVisitInterval(r->right);

		/*
		r->visitInterval = leastCommonMultiple2(r->left->visitInterval, \
			r->right->visitInterval);
		*/
		r->visitInterval = leastCommonMultiple2(
			lli_smallestPeriod(&r->left->lli),
			lli_smallestPeriod(&r->right->lli)
		);

		if (LOG_LLI)
			printf("Alt: VI %d\n", r->visitInterval);
		break;
	case Cat:
		Regexp_calcVisitInterval(r->left);
		Regexp_calcVisitInterval(r->right);

		r->visitInterval = leastCommonMultiple2(r->left->visitInterval, \
			lli_smallestPeriod(&r->right->lli));
		/* Right incurs intervals from left. */
		r->right->visitInterval = r->visitInterval;

		if (LOG_LLI)
			printf("Cat: VI self, R have %d\n", r->visitInterval);
		break;
	case Lit:
	case Dot:
	case CharEscape:
		r->visitInterval = 1;
		break;
	case Paren:
		Regexp_calcVisitInterval(r->left);
		r->visitInterval = r->left->visitInterval;

		if (LOG_LLI)
			printf("Paren: VI %d\n", r->visitInterval);
		break;
	case Quest:
	case Star:
	case Plus:
		Regexp_calcVisitInterval(r->left);
		r->visitInterval = lli_smallestPeriod(&r->left->lli);
		if (LOG_LLI)
			printf("Quest|Star|Plus: VI %d\n", r->visitInterval);
		break;
	}
}

/* Populate pc for r
 *   emit() produces instructions corresponding to r
 *   and saves them into the global pc array
 * 
 *   Instructions are emitted sequentially into pc,
 *     whose size is calculated by walking r in count()
 *     and adding values based on the number of states emit()'d
 *     by each op type
 * 
 *   emit() is defined recursively
 * 
 * 	 Each call to emit() starts at the largest unused pc
 *   
 *   During simulation,
 *     - some pc's can skip around (Jmp, Split)
 *     - others just advance the pc to the next (adjacent) instruction
 * 
 *   We use memoMode here because Alt and Star are compiled into similar-looking opcodes
 *   Easiest to handle MEMO_LOOP_DEST during emit().
 * 
 *   Call after Regexp_calcLLI.
 */ 
static void
emit(Regexp *r, int memoMode)
{
	Inst *p1, *p2, *t;

	switch(r->type) {
	default:
		fatal("bad emit");

	case Alt:
		pc->opcode = Split;
		pc->visitInterval = r->visitInterval;
		p1 = pc++;
		p1->x = pc;
		emit(r->left, memoMode);
		pc->opcode = Jmp;
		p2 = pc++;
		p1->y = pc;
		emit(r->right, memoMode);
		p2->x = pc;
		break;

	case Cat:
		/* Need to do anything for pc->visitInterval? */
		if (LOG_LLI) {
			printf("\n  CAT\n  \n");
			printf("Cat: lli self\n");
			lli_print(&r->lli);
			printf("Cat: lli left\n");
			lli_print(&r->left->lli);
			printf("Cat: lli right\n");
			lli_print(&r->right->lli);
			printf("\n\n");
		}

		p1 = pc;
		emit(r->left, memoMode);
		p2 = pc;
		emit(r->right, memoMode);

		//p2->visitInterval = leastCommonMultiple(nums, n);
		p2->visitInterval = r->right->visitInterval;
		break;
	
	case Lit:
		pc->opcode = Char;
		pc->visitInterval = 0;
		pc->c = r->ch;
		pc++;
		break;

	case CharEscape:
		pc->c = r->ch;
		pc->visitInterval = 0;
		switch (r->ch) {
		case 's':
		case 'S':
			/* space, newline, tab, vertical wsp, a few others */
			pc->opcode = CharClass;
			pc->charClassMins[0] = 9; pc->charClassMaxes[0] = 13;
			pc->charClassMins[1] = 28; pc->charClassMaxes[1] = 32;
			pc->charClassCounts = 2;
			pc->invert = isupper(r->ch);
			break;
		case 'w':
		case 'W':
			/* a-z A-Z 0-9 */
			pc->opcode = CharClass;
			pc->charClassMins[0] = 97; pc->charClassMaxes[0] = 122;
			pc->charClassMins[1] = 65; pc->charClassMaxes[1] = 90;
			pc->charClassMins[2] = 48; pc->charClassMaxes[2] = 57;
			pc->charClassCounts = 3;
			pc->invert = isupper(r->ch);
			break;
		case 'd':
		case 'D':
			/* 0-9 */
			pc->opcode = CharClass;
			pc->charClassMins[0] = 48; pc->charClassMaxes[0] = 57;
			pc->charClassCounts = 1;
			pc->invert = isupper(r->ch);
			break;
		default: 
			/* Not a char class, treat as the char itself */
			pc->opcode = Char;

			// a la "raw mode", treat the 2-char sequences \n and \t as literal newline and tab
			if (r->ch == 'n') {
				pc->c = '\n';
			}
			else if (r->ch == 't'){
				pc->c = '\t';
			}
			else if (r->ch == 'b') {
				pc->c = '\b';
			}
			else
				pc->c = r->ch;
		}
		pc++;
		break;
	
	case Dot:
		pc++->opcode = Any;
		pc->visitInterval = 0;
		break;

	case Paren:
		pc->opcode = Save;
		pc->n = 2*r->n;
		printf("Save: r->VI %d r->left->VI %d r->left->smallestPeriod %d\n", r->visitInterval, r->left->visitInterval, lli_smallestPeriod(&r->left->lli));
		//pc->visitInterval = lli_smallestPeriod(&r->left->lli);
		pc->visitInterval = r->visitInterval;
		pc++;
		emit(r->left, memoMode);
		pc->opcode = Save;
		pc->n = 2*r->n + 1;
		// pc->visitInterval = lli_smallestPeriod(&r->left->lli);
		// pc->visitInterval = lli_smallestPeriod(&r->left->lli);
		//pc->visitInterval = lli_smallestPeriod(&r->left->lli);
		pc->visitInterval = r->visitInterval;
		pc++;
		break;
	
	case Quest:
		pc->opcode = Split;
		pc->visitInterval = r->visitInterval;
		p1 = pc++;
		p1->x = pc;
		emit(r->left, memoMode);
		p1->y = pc;
		if(r->n) {	// non-greedy
			t = p1->x;
			p1->x = p1->y;
			p1->y = t;
		}
		break;

	case Star:
		pc->opcode = Split;
		pc->visitInterval = r->visitInterval;
		// pc->visitInterval = lli_smallestPeriod(&r->lli);
		p1 = pc++;
		p1->x = pc;
		emit(r->left, memoMode);
		pc->opcode = Jmp;
		pc->x = p1; /* Back-edge */
		pc->visitInterval = r->visitInterval;
		// pc->visitInterval = lli_smallestPeriod(&r->lli);
		if (memoMode == MEMO_LOOP_DEST) {
			pc->x->shouldMemo = 1;
		}
		pc++;
		p1->y = pc;
		if(r->n) {	// non-greedy
			t = p1->x;
			p1->x = p1->y;
			p1->y = t;
		}
		break;

	case Plus:
		p1 = pc;
		emit(r->left, memoMode);
		pc->opcode = Split;
		pc->x = p1; /* Back-edge */
		p1->visitInterval = r->visitInterval;
		pc->visitInterval = r->visitInterval;
		// pc->visitInterval = lli_smallestPeriod(&r->left->lli);
		if (memoMode == MEMO_LOOP_DEST) {
			pc->x->shouldMemo = 1;
		}
		p2 = pc;
		pc++;
		p2->y = pc;
		if(r->n) {	// non-greedy
			t = p2->x;
			p2->x = p2->y;
			p2->y = t;
		}
		break;
	}
}

void
printprog(Prog *p)
{
	Inst *pc, *e;
	
	pc = p->start;
	e = p->start + p->len;
	
	for(; pc < e; pc++) {
		switch(pc->opcode) {
		default:
			fatal("printprog");
		case Split:
			printf("%2d. split %d, %d (memo? %d -- state %d, visitInterval %d)\n", (int)(pc-p->start), (int)(pc->x-p->start), (int)(pc->y-p->start), pc->shouldMemo, pc->memoStateNum, pc->visitInterval);
			//printf("%2d. split %d, %d\n", (int)(pc->stateNum), (int)(pc->x->stateNum), (int)(pc->y->stateNum));
			break;
		case Jmp:
			printf("%2d. jmp %d (memo? %d -- state %d, visitInterval %d)\n", (int)(pc-p->start), (int)(pc->x-p->start), pc->shouldMemo, pc->memoStateNum, pc->visitInterval);
			//printf("%2d. jmp %d\n", (int)(pc->stateNum), (int)(pc->x->stateNum));
			break;
		case Char:
			printf("%2d. char %c (memo? %d -- state %d, visitInterval %d)\n", (int)(pc-p->start), pc->c, pc->shouldMemo, pc->memoStateNum, pc->visitInterval);
			//printf("%2d. char %c\n", (int)(pc->stateNum), pc->c);
			break;
		case Any:
			printf("%2d. any (memo? %d -- state %d, visitInterval %d)\n", (int)(pc-p->start), pc->shouldMemo, pc->memoStateNum, pc->visitInterval);
			//printf("%2d. any\n", (int)(pc->stateNum));
			break;
		case CharClass:
			printf("%2d. charClass (memo? %d -- state %d, visitInterval %d)\n", (int)(pc-p->start), pc->shouldMemo, pc->memoStateNum, pc->visitInterval);
			//printf("%2d. any\n", (int)(pc->stateNum));
			break;
		case Match:
			printf("%2d. match (memo? %d -- state %d, visitInterval %d)\n", (int)(pc-p->start), pc->shouldMemo, pc->memoStateNum, pc->visitInterval);
			//printf("%2d. match\n", (int)(pc->stateNum));
			break;
		case Save:
			printf("%2d. save %d (memo? %d -- state %d, visitInterval %d)\n", (int)(pc-p->start), pc->n, pc->shouldMemo, pc->memoStateNum, pc->visitInterval);
			//printf("%2d. save %d\n", (int)(pc->stateNum), pc->n);
		}
	}
}

