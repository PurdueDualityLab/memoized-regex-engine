#include "memoize.h"
#include "regexp.h"
#include "log.h"

/******* Compiler phase ********/

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
            	logMsg(LOG_DEBUG, "  ancestor node %d", p->start[i].stateNum);
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

/******* Simulation ********/

/* Backreferences complicate memoization.
 * Whenever we check if we can abort, we must now test both <q, i> and current contents of the backreferenced CGs.
 *  (Really we only need to check the backreferenced CGs *that are still reachable* from the current q)
 *
 * Record the cgNum for the groups referenced in a StringCompare (backreference Inst).
 *   (aka CG_BR or CGBR)
 * Updates list, returns the number of distinct referenced groups (|CG_{BR}|). */
static int backrefdCGs(Prog *prog, int *list);

/* Turn back to a CGID, then call into that family */
#define MEMOCGID_TO_SUB_STARTP_IX(memocgbr_num) (CGID_TO_SUB_STARTP_IX( CG_BR_memo2num[memocgbr_num] ))
#define MEMOCGID_TO_SUB_ENDP_IX(memocgbr_num)   (CGID_TO_SUB_ENDP_IX(   CG_BR_memo2num[memocgbr_num] ))
#define MEMOCGID_TO_STARTP(s, memocgbr_num)     (CGID_TO_STARTP((s),    CG_BR_memo2num[(memocgbr_num)]))
#define MEMOCGID_TO_ENDP(s, memocgbr_num)       (CGID_TO_ENDP((s),      CG_BR_memo2num[(memocgbr_num)]))

static int CG_BR[MAXSUB]; /* CGs that are backreferenced */
int nCG_BR = 0;

static int CG_BR_num2memo[MAXSUB]; /* CG number to memo vertex ix -- only populated for the CGBR's */
static int CG_BR_memo2num[MAXSUB]; /* Memo vertex ix to CG number */

/* Visit table.  */

VisitTable 
initVisitTable(Prog *prog, int nChars)
{
  VisitTable visitTable;
  int nStates = prog->len;
  int i, j;

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
  logMsg(LOG_VERBOSE, "Visit: Visiting <%d, %d>", statenum, woffset);
  
  if (visitTable->visitVectors[statenum][woffset] > 0)
    logMsg(LOG_WARN, "Hmm, already visited <%d, %d>", statenum, woffset);

  assert(statenum < visitTable->nStates);
  assert(woffset < visitTable->nChars);
  
  visitTable->visitVectors[statenum][woffset]++;
}

void
freeVisitTable(VisitTable vt)
{
  int i;
  for (i = 0; i < vt.nStates; i++) {
    free(vt.visitVectors[i]);
  }
  free(vt.visitVectors);
}

/* Memo table */

Memo
initMemoTable(Prog *prog, int nChars)
{
  Memo memo;
  int cardQ = prog->len;
  int nStatesToTrack = prog->nMemoizedStates;
  int i, j;
  char *prefix = "MEMO_TABLE";

  if (usesBackreferences(prog) && prog->memoMode != MEMO_NONE) {
    logMsg(LOG_INFO, "Backreferences present and memo enabled -- coercing to ENCODING_NEGATIVE");
    prog->memoEncoding = ENCODING_NEGATIVE;
    //assert(prog->memoEncoding == ENCODING_NEGATIVE);
  }

  memo.mode = prog->memoMode;
  memo.encoding = prog->memoEncoding;
  memo.nStates = nStatesToTrack;
  memo.nChars = nChars;
  memo.backrefs = usesBackreferences(prog);

  if (memo.backrefs) {
    /* Create CG <-> Memo Ix mappings for accessing the table later */
    nCG_BR = backrefdCGs(prog, CG_BR);
    for (i = 0; i < nCG_BR; i++) {
      logMsg(LOG_DEBUG, "i %d CG_BR[i] %d", i, CG_BR[i]);
      CG_BR_num2memo[ CG_BR[i] ] = i;
      CG_BR_memo2num[ i ] = CG_BR[i];
      logMsg(LOG_DEBUG, "CG num %d memo %d", CG_BR[i], i);
    }
  }
  
  if (memo.mode != MEMO_NONE) {
    switch(memo.encoding){
    case ENCODING_NONE:
      assert(!memo.backrefs);
      logMsg(LOG_INFO, "%s: Initializing with encoding NONE", prefix);
      logMsg(LOG_INFO, "%s: cardQ = %d, Phi_memo = %d", prefix, cardQ, nStatesToTrack);

      /* Visit vectors */
      memo.visitVectors = mal(sizeof(*memo.visitVectors) * nStatesToTrack);

      logMsg(LOG_INFO, "%s: %d visit vectors x %d chars for each", prefix, nStatesToTrack, nChars);
      for (i = 0; i < nStatesToTrack; i++) {
        memo.visitVectors[i] = mal(sizeof(int) * nChars);
        for (j = 0; j < nChars; j++) {
          memo.visitVectors[i][j] = 0;
        }
      }
      break;
    case ENCODING_NEGATIVE:
      logMsg(LOG_INFO, "%s: Initializing with encoding NEGATIVE", prefix);
      memo.simPosTable = NULL;
      break;
    case ENCODING_RLE:
    case ENCODING_RLE_TUNED:
      assert(!memo.backrefs);
      if (memo.encoding == ENCODING_RLE_TUNED)
        logMsg(LOG_INFO, "%s: Initializing with encoding RLE_TUNED", prefix);
      else
        logMsg(LOG_INFO, "%s: Initializing with encoding RLE", prefix);

      memo.rleVectors = mal(sizeof(*memo.rleVectors) * nStatesToTrack);

      logMsg(LOG_INFO, "%s: %d RLE-encoded visit vectors", prefix, nStatesToTrack);
      j = -1;
      for (i = 0; i < nStatesToTrack; i++) {
        /* Find the corresponding states so we know the run lengths to use */
        while (j < prog->len) {
          j++;
          if (prog->start[j].memoInfo.shouldMemo) {
            int visitInterval = (memo.encoding == ENCODING_RLE_TUNED) ? prog->start[j].memoInfo.visitInterval : 1;
            if (visitInterval < 1)
              visitInterval = 1;
            //visitInterval = 60;
            logMsg(LOG_INFO, "%s: state %d (memo state %d) will use visitInterval %d", prefix, j, i, visitInterval);
            memo.rleVectors[i] = RLEVector_create(visitInterval, 0 /* Do not auto-validate */);
            break;
          }
        }
      }
      break;
    default:
      logMsg(LOG_INFO, "%s: Unexpected encoding %d", prefix, memo.encoding);
      assert(0);
    }
  }

  logMsg(LOG_INFO, "%s: initialized", prefix);
  return memo;
}

int
isMarked(Memo *memo, int statenum /* PC's memoStateNum */, int woffset, Sub *sub)
{
  logMsg(LOG_VERBOSE, "  isMarked: querying <%d, %d>", statenum, woffset);

  switch(memo->encoding){
  case ENCODING_NONE:
    logMsg(LOG_VERBOSE, "  isMarked: visitVectors[%i] = %p\n", statenum, memo->visitVectors[statenum]);
    return memo->visitVectors[statenum][woffset] == 1;
  case ENCODING_NEGATIVE:
  {
    // Easy to support backreferences in this scheme -- just add more info to the SimPosTable entry
    // For the other schemes we would have to allocate stupendous amounts of memory (NONE) or perhaps be creative (RLE)
    SimPosTable entry;
    SimPosTable *p;

    memset(&entry, 0, sizeof(SimPosTable));
    entry.key.stateNum = statenum;
    entry.key.stringIndex = woffset;
    if (memo->backrefs) {
      char printStr[256];
      printStr[0] = '\0';
      sprintf(printStr + strlen(printStr), "isMarked: marking < <%d, %d> -> [", statenum, woffset);
      int cgIx;
      for (cgIx = 0; cgIx < nCG_BR; cgIx++) {
        logMsg(LOG_DEBUG, "cgIx %d CG%d startp %p start %p", cgIx, CG_BR_memo2num[cgIx], MEMOCGID_TO_STARTP(sub, cgIx), sub->start);
        if (isgroupset(sub, CG_BR_memo2num[cgIx])) {
          entry.key.cgStarts[cgIx] = (int) (MEMOCGID_TO_STARTP(sub, cgIx) - sub->start);
          entry.key.cgEnds[cgIx] = (int) (MEMOCGID_TO_ENDP(sub, cgIx) - sub->start);
        } else {
          entry.key.cgStarts[cgIx] = 0;
          entry.key.cgEnds[cgIx] = 0;
        }
        sprintf(printStr + strlen(printStr), "CG%d (%d, %d), ", CG_BR_memo2num[cgIx], entry.key.cgStarts[cgIx], entry.key.cgEnds[cgIx]);
      }
      sprintf(printStr + strlen(printStr), "]");
      logMsg(LOG_DEBUG, printStr);

      /* Sanity check */
      for (cgIx = 0; cgIx < nCG_BR; cgIx++) {
        assert(0 <= entry.key.cgStarts[cgIx]);
        assert(entry.key.cgStarts[cgIx] <= entry.key.cgEnds[cgIx]);
        assert(entry.key.cgEnds[cgIx] <= strlen(sub->start));
      }
    }

    HASH_FIND(hh, memo->simPosTable, &entry.key, sizeof(SimPos), p);
    return p != NULL;
  }
  case ENCODING_RLE:
  case ENCODING_RLE_TUNED:
    return RLEVector_get(memo->rleVectors[statenum], woffset) != 0;
  }

  assert(!"Unreachable");
  return -1;
}

void
markMemo(Memo *memo, int statenum, int woffset, Sub *sub)
{
  logMsg(LOG_VERBOSE, "Memo: Marking <%d, %d>", statenum, woffset);

  if (isMarked(memo, statenum, woffset, sub)) {
    logMsg(LOG_WARN, "\n****\n\n   Hmm, already marked s%d c%d\n\n*****\n\n", statenum, woffset);
  }

  switch(memo->encoding) {
  case ENCODING_NONE:
    assert(statenum < memo->nStates);
    assert(woffset < memo->nChars);
    assert(!memo->backrefs);
    memo->visitVectors[statenum][woffset] = 1;
    break;
  case ENCODING_NEGATIVE:
  {
    SimPosTable *entry = mal(sizeof(*entry));
    memset(entry, 0, sizeof(*entry));
    entry->key.stateNum = statenum;
    entry->key.stringIndex = woffset;

    if (memo->backrefs) {
      int cgIx;
      for (cgIx = 0; cgIx < nCG_BR; cgIx++) {
        if (isgroupset(sub, CG_BR_memo2num[cgIx])) {
          entry->key.cgStarts[cgIx] = (int) (MEMOCGID_TO_STARTP(sub, cgIx) - sub->start);
          entry->key.cgEnds[cgIx] = (int) (MEMOCGID_TO_ENDP(sub, cgIx) - sub->start);
        } else {
          entry->key.cgStarts[cgIx] = 0;
          entry->key.cgEnds[cgIx] = 0;
        }
      }
    }

    HASH_ADD(hh, memo->simPosTable, key, sizeof(SimPos), entry);
    break;
  }
  case ENCODING_RLE:
  case ENCODING_RLE_TUNED:
    assert(!memo->backrefs);
    RLEVector_set(memo->rleVectors[statenum], woffset);
    break;
  default:
    assert(!"Unknown encoding\n");
  }
}

void freeMemoTable(Memo memo)
{
	//TODO (Not needed for prototype assessment).
}

static int
backrefdCGs(Prog *prog, int *list)
{
  int i, j, n, newCG;
  Inst *pc;

  /* Check each StringCompare */
  n = 0;
  for (i = 0, pc = prog->start; i < prog->len; i++, pc++) {
    if (pc->opcode == StringCompare) {
      /* Is it a new CG or one we've already seen? */
      newCG = 1;
      for (j = 0; j < n; j++) {
        if (pc->cgNum == list[j]) {
          newCG = 0;
        }
      }

      if (newCG) {
        list[n] = pc->cgNum;
        logMsg(LOG_DEBUG, "backrefdCGs: CG %d has CGBR ix %d (%d)", pc->cgNum, n, list[n]);
        n++;
      }
    }
  }

  return n;
}