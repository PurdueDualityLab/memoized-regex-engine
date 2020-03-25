// Copyright 2007-2009 Russ Cox.  All Rights Reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "regexp.h"
#include <ctype.h>

static Inst *pc; /* VM array */
static int count(Regexp*);
static void determineSimpleLanguageLengths(Regexp *r);
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
	printf("LLI: %d lengths: ", lli->nLanguageLengths);
	for (i = 0; i < lli->nLanguageLengths; i++) {
		printf("%d,", lli->languageLengths[i]);
	}
	printf("\n");

	return;
}

static int
lli_chooseRunLength(LanguageLengthInfo *lli)
{
	int i, product = 1, largest = -1, possibleLCM;
	if (lli->tooManyLengths)
		return 2; /* A good default -- no worse than 1, and maybe better */

	/* Find the LCM of the language lengths */

	/* Compute upper bound on LCM, and get largest length. */
	printf("LCM: Values: ");
	for (i = 0; i < lli->nLanguageLengths; i++) {
		if (lli->languageLengths[i] > 0) {
			printf("%d,", lli->languageLengths[i]);
			product *= lli->languageLengths[i];
			if (lli->languageLengths[i] > largest) {
				largest = lli->languageLengths[i];
			}
		}
	}
	printf("\nLCM: largest %d, product: %d\n", largest, product);

	if (largest < 1)
		return 1;

	/* Consider all values from largest to product. */
	possibleLCM = largest;
	while (possibleLCM < product) {
		int isLCM = 1;
		for (i = 0; i < lli->nLanguageLengths; i++) {
			if (lli->languageLengths[i] > 1 && possibleLCM % lli->languageLengths[i] != 0) {
				isLCM = 0;
				break;
			}
		}
		if (isLCM) {
			printf("LCM: %d\n", possibleLCM);
			return possibleLCM;
		}

		possibleLCM++;
	}

	printf("LCM: %d\n", product);
	return product;
}


Prog*
compile(Regexp *r, int memoMode)
{
	int i;
	int n;
	int nextStateNum = 0;
	Prog *p;

	n = count(r) + 1;
	determineSimpleLanguageLengths(r);

	p = mal(sizeof *p + n*sizeof p->start[0]);
	p->start = (Inst*)(p+1);
	pc = p->start;
	for (i = 0; i < n; i++) {
		pc[i].runLength = 1; /* A good default */
	}
	emit(r, memoMode);
	pc->opcode = Match;
	pc++;
	p->len = pc - p->start;
	p->eolAnchor = r->eolAnchor;

	/* Assign state numbers */
	for (i = 0; i < p->len; i++) {
		p->start[i].stateNum = i;
	}

	/* Determine which nodes to memoize based on memo mode. */
	if (memoMode == MEMO_FULL) {
		for (i = 0; i < p->len; i++){
			p->start[i].shouldMemo = 1;
		}
	}
	else if (memoMode == MEMO_IN_DEGREE_GT1) {
		/* Compute in-degrees */

		/* q0 has an in-edge */
		p->start[0].inDegree = 1;
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

		for (i = 0; i < p->len; i++) {
			if (p->start[i].inDegree > 1) {
				p->start[i].shouldMemo = 1;
			}
		}
	}
	else if (memoMode == MEMO_LOOP_DEST) {
		/* This is done in emit(). */
	}
	else if (memoMode == MEMO_NONE) {
		for (i = 0; i < p->len; i++) {
			p->start[i].shouldMemo = 0;
		}
	}

	/* Assign memoStateNum to the shouldMemo nodes */
	for (i = 0; i < p->len; i++) {
		if (p->start[i].shouldMemo) {
			p->start[i].memoStateNum = nextStateNum;
			nextStateNum++;
		} else {
			p->start[i].memoStateNum = -1;
		}
	}
	p->nMemoizedStates = nextStateNum;
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
determineSimpleLanguageLengths(Regexp *r)
{
	int i, j;
	switch(r->type) {
	default:
		fatal("bad count");
	case Alt:
		determineSimpleLanguageLengths(r->left);
		determineSimpleLanguageLengths(r->right);
		
		/* Combine: A giant OR */
		r->lli.nLanguageLengths = 0;
		for (i = 0; i < r->left->lli.nLanguageLengths; i++) {
			lli_addEntry(&r->lli, r->left->lli.languageLengths[i]);
		}
		for (i = 0; i < r->right->lli.nLanguageLengths; i++) {
			lli_addEntry(&r->lli, r->right->lli.languageLengths[i]);
		}

		printf("LLI: Alt\n");
		lli_print(&r->lli);
		break;
	case Cat:
		determineSimpleLanguageLengths(r->left);
		determineSimpleLanguageLengths(r->right);

		/* Combine: Cartesian product */
		r->lli.nLanguageLengths = 0;
		for (i = 0; i < r->left->lli.nLanguageLengths; i++) {
			for (j = 0; j < r->right->lli.nLanguageLengths; j++) {
				lli_addEntry(&r->lli, r->left->lli.languageLengths[i] + r->right->lli.languageLengths[j]);
			}
		}

		printf("LLI: Cat\n");
		lli_print(&r->lli);
		break;
	case Lit:
	case Dot:
	case CharEscape:
		r->lli.nLanguageLengths = 1;
		r->lli.languageLengths[0] = 1;
		printf("LLI: Lit,Dot,CharEscape\n");
		lli_print(&r->lli);
		break;
	case Paren:
		determineSimpleLanguageLengths(r->left);
		r->lli = r->left->lli;
		printf("LLI: Paren\n");
		lli_print(&r->lli);
		break;
	case Quest:
		determineSimpleLanguageLengths(r->left);
		r->lli = r->left->lli;
		lli_addEntry(&r->lli, 0);
		printf("LLI: Quest:\n");
		lli_print(&r->lli);
		break;
	case Star:
		determineSimpleLanguageLengths(r->left);
		r->lli = r->left->lli;
		lli_addEntry(&r->lli, 0);
		printf("LLI: Star\n");
		lli_print(&r->lli);
		break;
	case Plus:
		determineSimpleLanguageLengths(r->left);
		r->lli = r->left->lli;
		printf("LLI: Plus\n");
		lli_print(&r->lli);
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
 *     some pc's induce jumps (Jmp, Split)
 *     The rest just advance the pc to the next (adjacent) instruction
 * 
 *   We use memoMode here because Alt and Star are compiled into similar-looking opcodes
 *   Easiest to handle MEMO_LOOP_DEST during emit().
 * 
 *   Call after determineSimpleLanguageLengths.
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
		pc->runLength = lli_chooseRunLength(&r->lli);
		p1 = pc++;
		p1->x = pc;
		emit(r->left, memoMode);
		pc->opcode = Jmp;
		pc->runLength = 1; /* ? */
		p2 = pc++;
		p1->y = pc;
		emit(r->right, memoMode);
		p2->x = pc;
		break;

	case Cat:
		/* Need to do anything for pc->runLength? */
		emit(r->left, memoMode);
		emit(r->right, memoMode);
		break;
	
	case Lit:
		pc->opcode = Char;
		pc->runLength = 1;
		pc->c = r->ch;
		pc++;
		break;

	case CharEscape:
		pc->c = r->ch;
		pc->runLength = 1;
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
		pc->runLength = 1;
		break;

	case Paren:
		pc->opcode = Save;
		pc->n = 2*r->n;
		pc++;
		emit(r->left, memoMode);
		pc->opcode = Save;
		pc->n = 2*r->n + 1;
		pc++;
		break;
	
	case Quest:
		pc->opcode = Split;
		pc->runLength = lli_chooseRunLength(&r->lli);
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
		pc->runLength = lli_chooseRunLength(&r->lli);
		p1 = pc++;
		p1->x = pc;
		emit(r->left, memoMode);
		pc->opcode = Jmp;
		pc->x = p1; /* Back-edge */
		pc->x->runLength = lli_chooseRunLength(&r->left->lli);
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
		pc->x->runLength = lli_chooseRunLength(&r->left->lli);
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
			printf("%2d. split %d, %d (memo? %d -- state %d, runLength %d)\n", (int)(pc-p->start), (int)(pc->x-p->start), (int)(pc->y-p->start), pc->shouldMemo, pc->memoStateNum, pc->runLength);
			//printf("%2d. split %d, %d\n", (int)(pc->stateNum), (int)(pc->x->stateNum), (int)(pc->y->stateNum));
			break;
		case Jmp:
			printf("%2d. jmp %d (memo? %d -- state %d, runLength %d)\n", (int)(pc-p->start), (int)(pc->x-p->start), pc->shouldMemo, pc->memoStateNum, pc->runLength);
			//printf("%2d. jmp %d\n", (int)(pc->stateNum), (int)(pc->x->stateNum));
			break;
		case Char:
			printf("%2d. char %c (memo? %d -- state %d, runLength %d)\n", (int)(pc-p->start), pc->c, pc->shouldMemo, pc->memoStateNum, pc->runLength);
			//printf("%2d. char %c\n", (int)(pc->stateNum), pc->c);
			break;
		case Any:
			printf("%2d. any (memo? %d -- state %d, runLength %d)\n", (int)(pc-p->start), pc->shouldMemo, pc->memoStateNum, pc->runLength);
			//printf("%2d. any\n", (int)(pc->stateNum));
			break;
		case CharClass:
			printf("%2d. charClass (memo? %d -- state %d, runLength %d)\n", (int)(pc-p->start), pc->shouldMemo, pc->memoStateNum, pc->runLength);
			//printf("%2d. any\n", (int)(pc->stateNum));
			break;
		case Match:
			printf("%2d. match (memo? %d -- state %d, runLength %d)\n", (int)(pc-p->start), pc->shouldMemo, pc->memoStateNum, pc->runLength);
			//printf("%2d. match\n", (int)(pc->stateNum));
			break;
		case Save:
			printf("%2d. save %d (memo? %d -- state %d, runLength %d)\n", (int)(pc-p->start), pc->n, pc->shouldMemo, pc->memoStateNum, pc->runLength);
			//printf("%2d. save %d\n", (int)(pc->stateNum), pc->n);
		}
	}
}

