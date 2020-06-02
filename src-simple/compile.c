// Copyright 2007-2009 Russ Cox.  All Rights Reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "regexp.h"
#include "log.h"

#include <ctype.h>

static Inst *pc; /* VM array */
static int count(Regexp*);
static void Regexp_calcLLI(Regexp *r);
static void Regexp_calcVisitInterval(Regexp *r);
static void emit(Regexp*, int);
static void printre_VI(Regexp *r);

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
		logMsg(LOG_VERBOSE, "LLI: Over-full");

	/* Check if it's present already */
	logMsg(LOG_VERBOSE, "LLI: %d lengths: ", lli->nLanguageLengths);
	for (i = 0; i < lli->nLanguageLengths; i++) {
		logMsg(LOG_VERBOSE, "%d,", lli->languageLengths[i]);
	}
	logMsg(LOG_VERBOSE, "\n");

	return;
}

/* Computes the LCM of the integers >= 1 in arr. */
static int
leastCommonMultiple(int arr[], int n, int max)
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

		if (possibleLCM > max)
			return smallest <= max ? smallest : 2;
	}

	return product;
}

static int
leastCommonMultiple2(int a, int b)
{
	int nums[2] = { a, b };
	return leastCommonMultiple(nums, 2, 64);
}

/* LCM */
static int
lli_smallestUniversalPeriod(LanguageLengthInfo *lli)
{
	if (lli->tooManyLengths) {
		printf("Run length overflow\n");
		return 2; /* A good default -- no worse than 1, and maybe better */
	}

	/* Find the LCM of the language lengths */
	return leastCommonMultiple(lli->languageLengths, lli->nLanguageLengths, 64);
}

void
Prog_compute_in_degrees(Prog *p)
{
	int i, j;

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
			fatal("in-degree: unknown type");
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
		case SplitMany:
			/* Goes to each child */
			for (j = 0; j < p->start[i].arity; j++) {
				p->start[i].edges[j]->inDegree++;
			}
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

// Optimization passes
Regexp* _optimizeAltGroups(Regexp *r);
Regexp* _mergeCustomCharClassRanges(Regexp *r);

/* Update this Regexp AST to make it more amenable to compilation
 *  - replace Alt-chains with a "flat" AltList with one child per Alt entity
 *  - replace a CustomCharClass's CharRange chain with a flat list of CharRange's within the CCC
 */
Regexp*
optimize(Regexp *r)
{
	Regexp *ret;

	logMsg(LOG_INFO, "Optimizing regex");
	ret = r;
	ret = _optimizeAltGroups(ret);
	ret = _mergeCustomCharClassRanges(ret);
	return ret;
}

int
_countAltListSize(Regexp *r)
{
	if (r->type != Alt) {
		// Base case -- some child of an Alt
		return 1;
	}
	// Left-recursive: A|B|C -> Alt(Alt(A|B), C)
	return 1 + _countAltListSize(r->left);
}

// Fill the children array in left-to-right order
// Returns the smallest unused index
int
_fillAltChildren(Regexp *r, Regexp **children, int i)
{
	if (r->type == Alt) {
		// Recursively populate the left children first
		int next = _fillAltChildren(r->left, children, i);
		// Now populate right child
		assert(r->right->type != Alt); // I think?
		children[next] = r->right;
		return next + 1;
	} else {
		// End of the recursion
		children[i] = r;
		return i + 1;
	}
}

Regexp*
_optimizeAltGroups(Regexp *r)
{
	Regexp *altList = NULL;
	int groupSize = 0, i = 0;

	switch(r->type) {
	default:
		fatal("optimizeAltGroups: unknown type");
		return NULL;
	case Alt:
		/* Prepare an AltList node */
		logMsg(LOG_DEBUG, "Converting an Alt to an AltList");
		groupSize = _countAltListSize(r);
		logMsg(LOG_DEBUG, "  groupSize %d", groupSize);
		assert(groupSize >= 2);

		altList = mal(sizeof(*altList));
		altList->type = AltList;
		altList->children = mal(groupSize * sizeof(altList));
		altList->arity = groupSize;
		logMsg(LOG_DEBUG, "  Populating children array");
		_fillAltChildren(r, altList->children, 0);

		/* Optimize the children */
		logMsg(LOG_DEBUG, "  Passing buck to children");
		for (i = 0; i < groupSize; i++) {
			altList->children[i] = _optimizeAltGroups(altList->children[i]);
		}

		return altList;
	case Cat:
		/* Binary operator -- pass the buck. */
		logMsg(LOG_DEBUG, "  optimize: Cat: passing buck");
		r->left = _optimizeAltGroups(r->left);
		r->right = _optimizeAltGroups(r->right);
		return r;
	case Quest:
	case Star:
    case Plus:
	case Paren:
	case CustomCharClass:
		/* Unary operators -- pass the buck. */
		logMsg(LOG_DEBUG, "  optimize: Quest/Star/Plus/Paren/CCC: passing buck");
		r->left = _optimizeAltGroups(r->left);
		return r;
	case Lit:
	case Dot:
	case CharEscape:
	case CharRange:
		/* Terminals */
		logMsg(LOG_DEBUG, "  optimize: ignoring terminal");
		return r;
	}
	return r;
}

int
_countCCCNRanges(Regexp *r)
{
	if (r->type != CharRange)
		fatal("countCCCNRanges: unexpected type");

	int nChildren = 1;
	if (r->left != NULL) {
		// Left-recursive: A|B|C -> Alt(Alt(A|B), C)
		nChildren += _countCCCNRanges(r->left);
	}
	return nChildren;
}

// Fill the children array in left-to-right order
// Returns the smallest unused index
int
_fillCCCChildren(Regexp *r, Regexp **children, int i)
{
	if (r->type != CharRange)
		fatal("fillCCCChildren: unexpected type");

	int next = i;
	if (r->left != NULL) {
		// Recursively populate the left children first
		next = _fillCCCChildren(r->left, children, i);
		r->left = NULL;
	}
	// Now populate "right child" -- the node itself
	children[next] = r;
	return next + 1;
}

Regexp*
_mergeCustomCharClassRanges(Regexp *r)
{
	int i;
	int groupSize = 0;

	switch(r->type) {
	default:
		logMsg(LOG_ERROR, "type %d", r->type);
		fatal("mergeCustomCharClassRanges: unknown type");
		return NULL;
	case CustomCharClass:
		logMsg(LOG_DEBUG, "In-place updating a CCC to have all its children in one place");
		groupSize = _countCCCNRanges(r->left);
		logMsg(LOG_DEBUG, "  groupSize %d", groupSize);

		r->children = mal(groupSize * sizeof(Regexp *));
		r->arity = groupSize;
		logMsg(LOG_DEBUG, "  Populating children array");
		_fillCCCChildren(r->left, r->children, 0);

		r->mergedRanges = 1;
		r->left = NULL;
		r->right = NULL;

		return r;
	case AltList:
		/* *-ary operator -- pass the buck. */
		for (i = 0; i < r->arity; i++) {
			r->children[i] = _mergeCustomCharClassRanges(r->children[i]);
		}
		return r;
	case Alt:
	case Cat:
		/* Binary operator -- pass the buck. */
		logMsg(LOG_DEBUG, "  optimize: Cat: passing buck");
		r->left = _mergeCustomCharClassRanges(r->left);
		r->right = _mergeCustomCharClassRanges(r->right);
		return r;
	case Quest:
	case Star:
    case Plus:
	case Paren:
		/* Unary operators -- pass the buck. */
		logMsg(LOG_DEBUG, "  optimize: Quest/Star/Plus/Paren/CCC: passing buck");
		r->left = _mergeCustomCharClassRanges(r->left);
		return r;
	case Lit:
	case Dot:
	case CharEscape:
		/* Terminals */
		logMsg(LOG_DEBUG, "  optimize: ignoring terminal");
		return r;
	}
	return r;
}

// Compile into a Prog
Prog*
compile(Regexp *r, int memoMode)
{
	int i, n;
	Prog *p;

	n = count(r) + 1;
	Regexp_calcLLI(r);
	Regexp_calcVisitInterval(r);
	printre_VI(r);
	printf("\n");

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
	logMsg(LOG_INFO, "Will memoize %d states", p->nMemoizedStates);

	return p;
}

// How many instructions does r need?
static int
count(Regexp *r)
{
	int _count = 0, i;
	switch(r->type) {
	default:
		fatal("count: unknown type");
	case Alt:
		return 2 + count(r->left) + count(r->right);
    case AltList:
		_count = 0;
		for (i = 0; i < r->arity; i++) {
			// Each branch adds 1 jump
			_count += count(r->children[i]) + 1;
		}
		// Need a SplitMany as well
		return 1 + _count;
	case Cat:
		return count(r->left) + count(r->right);
	case Lit:
	case Dot:
	case CharEscape:
	case CustomCharClass:
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
// TODO This is a WIP. Do not use this.
static void
Regexp_calcLLI(Regexp *r)
{
	int i, j;
	switch(r->type) {
	default:
		fatal("calcLLI: unknown type");
	case AltList:
	case CustomCharClass:
	case CharRange:
		return;
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

		logMsg(LOG_VERBOSE, "LLI: Alt");
		lli_print(&r->lli);
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

		logMsg(LOG_VERBOSE, "LLI: Cat");
		lli_print(&r->lli);
		break;
	case Lit:
	case Dot:
	case CharEscape:
		r->lli.nLanguageLengths = 1;
		r->lli.languageLengths[0] = 1;

		logMsg(LOG_VERBOSE, "LLI: Lit,Dot,CharEscape");
		lli_print(&r->lli);
		break;
	case Paren:
		Regexp_calcLLI(r->left);
		r->lli = r->left->lli;

		logMsg(LOG_VERBOSE, "LLI: Paren");
		lli_print(&r->lli);
		break;
	case Quest:
		Regexp_calcLLI(r->left);
		r->lli = r->left->lli;
		lli_addEntry(&r->lli, 0);

		logMsg(LOG_VERBOSE, "LLI: Quest:");
		lli_print(&r->lli);
		break;
	case Star:
		Regexp_calcLLI(r->left);
		r->lli = r->left->lli;
		lli_addEntry(&r->lli, 0);

		logMsg(LOG_VERBOSE, "LLI: Star");
		lli_print(&r->lli);
		break;
	case Plus:
		Regexp_calcLLI(r->left);
		r->lli = r->left->lli;

		logMsg(LOG_VERBOSE, "LLI: Plus");
		lli_print(&r->lli);
		break;
	}
}

static void
printre_VI(Regexp *r)
{
	return;

	switch(r->type) {
	default:
		printf("???");
		break;
	
	case Alt:
		printf("Alt-%d(", r->visitInterval);
		printre_VI(r->left);
		printf(", ");
		printre_VI(r->right);
		printf(")");
		break;

	case Cat:
		printf("Cat-%d(", r->visitInterval);
		printre_VI(r->left);
		printf(", ");
		printre_VI(r->right);
		printf(")");
		break;
	
	case Lit:
		printf("Lit(%c)", r->ch);
		break;
	
	case Dot:
		printf("Dot");
		break;

	case CharEscape:
		printf("Esc(%c)", r->ch);
		break;

	case Paren:
		printf("Paren-%d(%d, ", r->visitInterval, r->n);
		printre_VI(r->left);
		printf(")");
		break;
	
	case Star:
		if(r->n)
			printf("Ng");
		printf("Star-%d(", r->visitInterval);
		printre_VI(r->left);
		printf(")");
		break;
	
	case Plus:
		if(r->n)
			printf("Ng");
		printf("Plus-%d(", r->visitInterval);
		printre_VI(r->left);
		printf(")");
		break;
	
	case Quest:
		if(r->n)
			printf("Ng");
		printf("Quest-%d(", r->visitInterval);
		printre_VI(r->left);
		printf(")");
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
		fatal("calcVI: unknown type");
	case AltList:
	case CustomCharClass:
		return;
	case Alt:
		Regexp_calcVisitInterval(r->left);
		Regexp_calcVisitInterval(r->right);

		/*
		r->visitInterval = leastCommonMultiple2(r->left->visitInterval, \
			r->right->visitInterval);
		*/
		r->visitInterval = leastCommonMultiple2(
			lli_smallestUniversalPeriod(&r->left->lli),
			lli_smallestUniversalPeriod(&r->right->lli)
		);

		//r->visitInterval = r->left->visitInterval + r->right->visitInterval;
		//r->visitInterval = lli_smallestUniversalPeriod(&r->left->lli) + lli_smallestUniversalPeriod(&r->right->lli);

		logMsg(LOG_VERBOSE, "Alt: VI %d", r->visitInterval);
		break;
	case Cat:
		Regexp_calcVisitInterval(r->left);
		Regexp_calcVisitInterval(r->right);

#if 0
		/* This helps dotStar-1 */
		r->visitInterval = leastCommonMultiple2(
			lli_smallestUniversalPeriod(&r->left->lli), // <-- This
			lli_smallestUniversalPeriod(&r->right->lli)
		);

		/* This helps concat-1 */
		r->visitInterval = leastCommonMultiple2(
			r->left->visitInterval,
			lli_smallestUniversalPeriod(&r->right->lli)
		);

		/* Experimental.
		 * TODO This is a hack. Concatenation SUPs get longer and longer,
		 * but we only need to care when we reach a vertex with its own VI? */
		r->visitInterval = leastCommonMultiple2(
			leastCommonMultiple2(r->left->visitInterval, lli_smallestUniversalPeriod(&r->left->lli)),
			leastCommonMultiple2(r->right->visitInterval, lli_smallestUniversalPeriod(&r->right->lli))
		);

		// TODO Experimenting.
		if (r->right->visitInterval > 1) {
		} else{

		}

		/* Right incurs intervals from left. */
		r->right->visitInterval = leastCommonMultiple2(
			r->left->visitInterval,
			lli_smallestUniversalPeriod(&r->right->lli)
		);

		/* Whole takes on only left. */
		r->visitInterval = r->right->visitInterval;
		
		r->visitInterval = leastCommonMultiple2(
			//leastCommonMultiple2(r->left->visitInterval, lli_smallestUniversalPeriod(&r->left->lli)),
			//lli_smallestUniversalPeriod(&r->left->lli),
			r->left->visitInterval,
			//leastCommonMultiple2(r->right->visitInterval, lli_smallestUniversalPeriod(&r->right->lli))
			//r->right->visitInterval
			lli_smallestUniversalPeriod(&r->right->lli)
		);
#endif

		// B will be visited at intervals of left 
		//   e.g. Cat(A, B)
		// B may be visited at intervals of itself
		//   e.g. Cat(A, Star(B))
		r->right->visitInterval = leastCommonMultiple2(
			lli_smallestUniversalPeriod(&r->left->lli),
			lli_smallestUniversalPeriod(&r->right->lli)
		);
		// Propagate this down through any Parens to the thing they are wrapping.
		if (r->right->type == Paren) {
			Regexp *tmpRight = NULL;
			int vi = r->right->visitInterval;

			logMsg(LOG_VERBOSE, "Propagating vi %d past Parens", vi);
			tmpRight = r->right;
			while (tmpRight->type == Paren) {
				tmpRight->visitInterval = vi;
				tmpRight = tmpRight->left;
			}
			// We have our target, the first non-paren entity.
			// This also takes on the VI.
			tmpRight->visitInterval = vi;
		}

		// The Cat node takes on both.
		//  - It can be visited after repetitions of A and B
		//    (e.g. Star(Cat(A, B)) )
		r->visitInterval = leastCommonMultiple2(
			r->left->visitInterval,
			r->right->visitInterval
		);

		logMsg(LOG_VERBOSE, "Cat: VI self %d l->vi %d l->SUP %d r->vi %d r->SUP %d", r->visitInterval, r->left->visitInterval, lli_smallestUniversalPeriod(&r->left->lli), r->right->visitInterval, lli_smallestUniversalPeriod(&r->right->lli));
		if (r->left->type == Paren) {
			logMsg(LOG_VERBOSE, "Cat: L = Paren");
		}
		if (r->right->type == Paren) {
			logMsg(LOG_VERBOSE, "Cat: R = Paren");
		}
		break;
	case Lit:
	case Dot:
	case CharEscape:
		r->visitInterval = 1;
		break;
	case Paren:
		Regexp_calcVisitInterval(r->left);
		r->visitInterval = r->left->visitInterval;

		logMsg(LOG_VERBOSE, "Paren: VI %d", r->visitInterval);
		break;
	case Quest:
	case Star:
	case Plus:
		Regexp_calcVisitInterval(r->left);
		r->visitInterval = lli_smallestUniversalPeriod(&r->left->lli);
		logMsg(LOG_VERBOSE, "Quest|Star|Plus: VI %d", r->visitInterval);
		break;
	}
}

static void
_emitRegexpCharEscape2InstCharRange(Regexp *r, InstCharRange *instCR)
{
	if (r->type != CharEscape) {
		assert(!"emitrcr2instCR: Unexpected type");
	}

	switch (r->ch) {
	case 's':
	case 'S':
		/* space, newline, tab, vertical wsp, a few others */
		instCR->lows[0] = 9; instCR->highs[0] = 13;
		instCR->lows[1] = 28; instCR->highs[1] = 32;
		instCR->count = 2;
		instCR->invert = isupper(r->ch);
		return;
	case 'w':
	case 'W':
		/* a-z A-Z 0-9 */
		instCR->lows[0] = 97; instCR->highs[0] = 122;
		instCR->lows[1] = 65; instCR->highs[1] = 90;
		instCR->lows[2] = 48; instCR->highs[2] = 57;
		instCR->count = 3;
		instCR->invert = isupper(r->ch);
		return;
	case 'd':
	case 'D':
		/* 0-9 */
		instCR->lows[0] = 48; instCR->highs[0] = 57;
		instCR->count = 1;
		instCR->invert = isupper(r->ch);
		return;
	/* Not a built-in CC */
	// Handle special escape sequences
	case 'r': // UNIX-style!
	case 'n':
		instCR->lows[0] = '\n'; instCR->highs[0] = '\n';
		instCR->count = 1;
		return;
	case 't':
		instCR->lows[0] = '\t'; instCR->highs[0] = '\t';
		instCR->count = 1;
		return;
	// By default, treat it as "not an escape": \a is just a literal "a"
	default:
		instCR->lows[0] = r->ch; instCR->highs[0] = r->ch;
		instCR->count = 1;
		return;
	}
}

static void
_emitRegexpCharRange2Inst(Regexp *r, Inst *inst)
{
	InstCharRange *next = &inst->charRanges[ inst->charRangeCounts ];
	switch (r->type) {
    default:
		assert(!"emitrcr2int: Unexpected type");
	case CharEscape: /* e.g. \w (built-in CC) or \a (nothing) */
		_emitRegexpCharEscape2InstCharRange(r, next);
		break;
	case CharRange:
		switch (r->ccLow->type) {
		case Lit: /* 'a-z' */
			assert(r->ccHigh->type == Lit); /* 'a', or 'a-z' (but not 'a-\w') */
			next->lows[0] = r->ccLow->ch; next->highs[0] = r->ccHigh->ch;
			next->count = 1;
			break;
		case CharEscape:
			assert(r->ccLow->ch == r->ccHigh->ch); // '\w', not '\w-\s'
			_emitRegexpCharEscape2InstCharRange(r->ccLow, next);
			break;
		default:
			assert(!"emitrcr2int: CharRange: Unexpected child type");
		}
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
	Inst *p1, *p2, *t, **t2;
	int i;

	switch(r->type) {
	default:
		fatal("emit: unknown type");

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

	case AltList:
		pc->opcode = SplitMany;
		pc->arity = r->arity;
		pc->edges = mal(r->arity * sizeof(Inst **));

		/* The Jmp nodes associated with each branch */
		t2 = mal(r->arity * sizeof(Inst **));

		/* Emit the branches */
		p1 = pc++;
		p1->x = pc;
		for (i = 0; i < r->arity; i++) {
			/* Emit a branch */
			p1->edges[i] = pc;
			emit(r->children[i], memoMode);
			/* Emit a Jmp node and save it so we can set its destination once we exhaust the AltList */
			pc->opcode = Jmp;
			t2[i] = pc;
			/* Ready for the next branch */
			pc++;
		}

		/* Revisit the Jmp nodes and set the destinations */
		for (i = 0; i < r->arity; i++) {
			t2[i]->x = pc;
		}
		free(t2);

		break;

	case Cat:
		p1 = pc;
		emit(r->left, memoMode);
		p2 = pc;
		emit(r->right, memoMode);

		printf("cat: vi %d l->vi %d l->SUP %d r->vi %d r->SUP %d\n", r->visitInterval, r->left->visitInterval, lli_smallestUniversalPeriod(&r->left->lli), r->right->visitInterval, lli_smallestUniversalPeriod(&r->right->lli));
		p2->visitInterval = r->right->visitInterval;
		p2->visitInterval = r->visitInterval;
		break;
	
	case Lit:
		pc->opcode = Char;
		pc->visitInterval = 0;
		pc->c = r->ch;
		pc++;
		break;

	case CustomCharClass:
		assert(r->mergedRanges);
		pc->opcode = CharClass;
		if (r->arity > nelem(pc->charRanges))
			fatal("Too many ranges in char class");

		for (i = 0; i < r->arity; i++) {
			_emitRegexpCharRange2Inst(r->children[i], pc);
			pc->charRangeCounts++;
		}
		pc->charRangeCounts = r->arity;
		pc->invert = r->ccInvert;
		pc++;
		break;

	case CharEscape:
		pc->opcode = CharClass;
		pc->visitInterval = 0;

		_emitRegexpCharRange2Inst(r, pc);
		pc->charRangeCounts = 1;

		pc++;
		break;
	
	case Dot:
		pc++->opcode = Any;
		pc->visitInterval = 0;
		break;

	case Paren:
		pc->opcode = Save;
		pc->n = 2*r->n;
		printf("Save: r->VI %d r->left->VI %d r->left->smallestUniversalPeriod %d\n", r->visitInterval, r->left->visitInterval, lli_smallestUniversalPeriod(&r->left->lli));
		//pc->visitInterval = lli_smallestUniversalPeriod(&r->left->lli);
		pc->visitInterval = r->visitInterval;
		pc++;
		emit(r->left, memoMode);
		pc->opcode = Save;
		pc->n = 2*r->n + 1;
		// pc->visitInterval = lli_smallestUniversalPeriod(&r->left->lli);
		// pc->visitInterval = lli_smallestUniversalPeriod(&r->left->lli);
		//pc->visitInterval = lli_smallestUniversalPeriod(&r->left->lli);
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
		// pc->visitInterval = lli_smallestUniversalPeriod(&r->lli);
		p1 = pc++;
		p1->x = pc;
		emit(r->left, memoMode);
		pc->opcode = Jmp;
		pc->x = p1; /* Back-edge */
		pc->visitInterval = r->visitInterval;
		// pc->visitInterval = lli_smallestUniversalPeriod(&r->lli);
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
		// pc->visitInterval = lli_smallestUniversalPeriod(&r->left->lli);
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
	int i;
	
	pc = p->start;
	e = p->start + p->len;
	
	for(; pc < e; pc++) {
		switch(pc->opcode) {
		default:
			fatal("printprog: unknown opcode");
		case Split:
			printf("%2d. split %d, %d (memo? %d -- state %d, visitInterval %d)\n", (int)(pc-p->start), (int)(pc->x-p->start), (int)(pc->y-p->start), pc->shouldMemo, pc->memoStateNum, pc->visitInterval);
			//printf("%2d. split %d, %d\n", (int)(pc->stateNum), (int)(pc->x->stateNum), (int)(pc->y->stateNum));
			break;
		case SplitMany:
			printf("%2d. splitmany ", (int) (pc - p->start));
			for (i = 0; i < pc->arity; i++) {
				printf("%d", (int) (pc->edges[i]-p->start));
				if (i + 1 < pc->arity)
					printf(",");
			}
			printf(" (memo? %d -- state %d, visitInterval %d)\n", pc->shouldMemo, pc->memoStateNum, pc->visitInterval);
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

