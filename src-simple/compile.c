// Copyright 2007-2009 Russ Cox.  All Rights Reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "regexp.h"

static Inst *pc; /* VM array */
static int count(Regexp*);
static void emit(Regexp*, int);

Prog*
compile(Regexp *r, int memoMode)
{
	int i;
	int n;
	int nextStateNum = 0;
	Prog *p;

	n = count(r) + 1;
	p = mal(sizeof *p + n*sizeof p->start[0]);
	p->start = (Inst*)(p+1);
	pc = p->start;
	emit(r, memoMode);
	pc->opcode = Match;
	pc++;
	p->len = pc - p->start;

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
 */ 
// TODO I AM HERE, studying structures via code and command-line
static void
emit(Regexp *r, int memoMode)
{
	Inst *p1, *p2, *t;

	switch(r->type) {
	default:
		fatal("bad emit");

	case Alt:
		pc->opcode = Split;
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
		emit(r->left, memoMode);
		emit(r->right, memoMode);
		break;
	
	case Lit:
		pc->opcode = Char;
		pc->c = r->ch;
		pc++;
		break;
	
	case Dot:
		pc++->opcode = Any;
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
		p1 = pc++;
		p1->x = pc;
		emit(r->left, memoMode);
		pc->opcode = Jmp;
		pc->x = p1; /* Back-edge */
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
			printf("%2d. split %d, %d\n", (int)(pc-p->start), (int)(pc->x-p->start), (int)(pc->y-p->start));
			//printf("%2d. split %d, %d\n", (int)(pc->stateNum), (int)(pc->x->stateNum), (int)(pc->y->stateNum));
			break;
		case Jmp:
			printf("%2d. jmp %d\n", (int)(pc-p->start), (int)(pc->x-p->start));
			//printf("%2d. jmp %d\n", (int)(pc->stateNum), (int)(pc->x->stateNum));
			break;
		case Char:
			printf("%2d. char %c\n", (int)(pc-p->start), pc->c);
			//printf("%2d. char %c\n", (int)(pc->stateNum), pc->c);
			break;
		case Any:
			printf("%2d. any\n", (int)(pc-p->start));
			//printf("%2d. any\n", (int)(pc->stateNum));
			break;
		case Match:
			printf("%2d. match\n", (int)(pc-p->start));
			//printf("%2d. match\n", (int)(pc->stateNum));
			break;
		case Save:
			printf("%2d. save %d\n", (int)(pc-p->start), pc->n);
			//printf("%2d. save %d\n", (int)(pc->stateNum), pc->n);
		}
	}
}

