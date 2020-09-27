#include "memoize.h"
#include "regexp.h"
#include "log.h"

static void
Prog_compute_in_degrees(Prog *p)
{
	int i, j;

	/* Initialize */
	for (i = 0; i < p->len; i++) {
		p->start[i].memoInfo.inDegree = 0;
	}

	/* q0 has an in-edge */
	p->start[0].memoInfo.inDegree = 1;

	/* Increment */
	for (i = 0; i < p->len; i++) {
		switch(p->start[i].opcode) {
		default:
			fatal("in-degree: unknown type");
		case Match:
			/* Terminates search */
			break;
		case Jmp:
			/* Goes to X */
			p->start[i].x->memoInfo.inDegree++;
			break;
		case Split:
			/* Goes to X or Y */
			p->start[i].x->memoInfo.inDegree++;
			p->start[i].y->memoInfo.inDegree++;
			break;
		case SplitMany:
			/* Goes to each child */
			for (j = 0; j < p->start[i].arity; j++) {
				p->start[i].edges[j]->memoInfo.inDegree++;
			}
			break;
		case Any:
		case CharClass:
		case Char:
		case Save:
		case StringCompare:
		case InlineZeroWidthAssertion:
		case RecursiveZeroWidthAssertion:
		case RecursiveMatch:
			/* Always goes to next instr */
			p->start[i+1].memoInfo.inDegree++;
			break;
		}
	}
}

static void
Prog_find_ancestor_nodes(Prog *p)
{
	int i;

	/* Initialize */
	for (i = 0; i < p->len; i++) {
		p->start[i].memoInfo.isAncestorLoopDestination = 0;
	}

	/* Observe back-edges */
	for (i = 0; i < p->len; i++) {
		if (p->start[i].opcode == Jmp) {
            logMsg(LOG_DEBUG, "  Jmp: from %d to %d", p->start[i].stateNum, p->start[i].x->stateNum);
            if (p->start[i].stateNum > p->start[i].x->stateNum) {
                p->start[i].x->memoInfo.isAncestorLoopDestination = 1;
            }
		}
	}
}

void
Prog_determineMemoNodes(Prog *p, int memoMode)
{
	int i, nextStateNum;

	/* Determine which nodes to memoize based on memo mode. */
	switch (memoMode) {
	case MEMO_FULL:
        /* Memoize all nodes. */
        logMsg(LOG_DEBUG, "Prog_determineMemoNodes: FULL");
		for (i = 0; i < p->len; i++){
			p->start[i].memoInfo.shouldMemo = 1;
		}
		break;
	case MEMO_IN_DEGREE_GT1:
        /* Memoize nodes with in-deg > 1. */
        logMsg(LOG_DEBUG, "Prog_determineMemoNodes: IN_DEGREE");
		Prog_compute_in_degrees(p);
		for (i = 0; i < p->len; i++) {
			if (p->start[i].memoInfo.inDegree > 1) {
				p->start[i].memoInfo.shouldMemo = 1;
			}
		}
		break;
	case MEMO_LOOP_DEST:
        /* Memoize nodes that are the destination of a back-edge (i.e. a larger node number points to a smaller node number). */
        logMsg(LOG_DEBUG, "Prog_determineMemoNodes: LOOP");
        Prog_find_ancestor_nodes(p);
        for (i = 0; i < p->len; i++) {
            if (p->start[i].memoInfo.isAncestorLoopDestination) {
            	logMsg(LOG_DEBUG, "  Will memoize ancestor node %d", p->start[i].stateNum);
                p->start[i].memoInfo.shouldMemo = 1;
            }
        }
		break;
	case MEMO_NONE:
        /* Memoize no nodes. */
        logMsg(LOG_DEBUG, "Prog_determineMemoNodes: NONE");
		for (i = 0; i < p->len; i++) {
			p->start[i].memoInfo.shouldMemo = 0;
		}
		break;
	default:
		assert(!"Unknown memoMode\n");
	}

	/* Assign memoStateNum to the shouldMemo nodes */
	nextStateNum = 0;
	for (i = 0; i < p->len; i++) {
		if (p->start[i].memoInfo.shouldMemo) {
			p->start[i].memoInfo.memoStateNum = nextStateNum;
			nextStateNum++;
		} else {
			p->start[i].memoInfo.memoStateNum = -1;
		}
	}
	p->nMemoizedStates = nextStateNum;
}