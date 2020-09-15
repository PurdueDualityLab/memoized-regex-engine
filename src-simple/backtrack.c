// Copyright 2007-2009 Russ Cox.  All Rights Reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.
//
// Annotations, statistics, and memoization by James Davis, 2020.

#include "regexp.h"
#include "log.h"

#include <sys/time.h>
#include <assert.h>

static int usesBackrefs(Prog *prog);
static int backrefdCGs(Prog *prog, int *list);

static int CG_BR[MAXSUB];
static int CG_BR_num2memo[MAXSUB]; /* CG number to memo vertex ix -- only populated for the CGBR's */
static int CG_BR_memo2num[MAXSUB]; /* Memo vertex ix to CG number */
int nCG_BR = 0;

/* Given a CGID, extract elements from sub
 * This encodes the to/from mapping from CG to sub index */
#define CGID_TO_SUB_STARTP_IX(cgid) (2*(cgid))
#define CGID_TO_SUB_ENDP_IX(cgid) (2*(cgid) + 1)
#define CGID_TO_STARTP(s, cgid)   ((s)->sub[ CGID_TO_SUB_STARTP_IX( (cgid) )])
#define CGID_TO_ENDP(s, cgid)   ((s)->sub[ CGID_TO_SUB_ENDP_IX( (cgid) )])

/* Turn back to a CGID, then call into that family */
#define MEMOCGID_TO_SUB_STARTP_IX(memocgbr_num) (CGID_TO_SUB_STARTP_IX( CG_BR_memo2num[memocgbr_num] ))
#define MEMOCGID_TO_SUB_ENDP_IX(memocgbr_num)   (CGID_TO_SUB_ENDP_IX(   CG_BR_memo2num[memocgbr_num] ))
#define MEMOCGID_TO_STARTP(s, memocgbr_num)     (CGID_TO_STARTP((s),    CG_BR_memo2num[(memocgbr_num)]))
#define MEMOCGID_TO_ENDP(s, memocgbr_num)       (CGID_TO_ENDP((s),      CG_BR_memo2num[(memocgbr_num)]))

/* Misc. */
#define IS_WORD_CHAR(c) (('a' <= c && c <= 'z') || ('A' <= c && c <= 'Z') || ('0' <= c && c <= '9'))

void
vec_strcat(char **dest, int *dAlloc, char *src)
{
  int combinedLen = strlen(*dest) + strlen(src) + 5;
  // logMsg(LOG_INFO, "vec_strcat: dest %p *dest %p *dAlloc %d; string <%s>\n", dest, *dest, *dAlloc, *dest);

  if (combinedLen > *dAlloc) {
    /* Re-alloc */
    char *old = *dest;
    char *new = mal(sizeof(char) * 2 * combinedLen);

    /* Copy over */
    strcpy(new, old);

    /* Book-keeping */
    *dAlloc = sizeof(char) * 2 * combinedLen;
    free(old);
    *dest = new;
  }
  
  strcat(*dest, src);
  // logMsg(LOG_INFO, "vec_strcat: dest %p *dest %p *dAlloc %d; string <%s>\n", dest, *dest, *dAlloc, *dest);
  return;
}

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

/* Visit table */

struct VisitTable
{
  int **visitVectors; /* Counters */
  int nStates; /* |Q| */
  int nChars;  /* |w| */
};

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
freeVisitTable(VisitTable vt)
{
  int i;
  for (i = 0; i < vt.nStates; i++) {
    free(vt.visitVectors[i]);
  }
  free(vt.visitVectors);
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

/* Memo table */

Memo
initMemoTable(Prog *prog, int nChars, int memoMode, int memoEncoding)
{
  Memo memo;
  int cardQ = prog->len;
  int nStatesToTrack = prog->nMemoizedStates;
  int i, j;
  char *prefix = "MEMO_TABLE";

  memo.mode = memoMode;
  memo.encoding = memoEncoding;
  memo.nStates = nStatesToTrack;
  memo.nChars = nChars;
  memo.backrefs = usesBackrefs(prog);

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
  
  if (memoMode != MEMO_NONE) {
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
      memo.searchStateTable = NULL;
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
          if (prog->start[j].shouldMemo) {
            int visitInterval = (memo.encoding == ENCODING_RLE_TUNED) ? prog->start[j].visitInterval : 1;
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

void
freeMemoTable(Memo memo)
{
	//TODO (Not needed for prototype assessment).
}

static int
woffset(char *input, char *sp)
{
  return (int) (sp - input);
}

static int
isMarked(Memo *memo, int statenum /* PC's memoStateNum */, int woffset, Sub *sub)
{
  logMsg(LOG_VERBOSE, "  isMarked: querying <%d, %d>", statenum, woffset);

  switch(memo->encoding){
  case ENCODING_NONE:
    return memo->visitVectors[statenum][woffset] == 1;
  case ENCODING_NEGATIVE:
  {
    SearchStateTable entry;
    SearchStateTable *p;

    memset(&entry, 0, sizeof(SearchStateTable));
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

    HASH_FIND(hh, memo->searchStateTable, &entry.key, sizeof(SearchState), p);
    return p != NULL;
  }
  case ENCODING_RLE:
  case ENCODING_RLE_TUNED:
    return RLEVector_get(memo->rleVectors[statenum], woffset) != 0;
  }

  assert(!"Unreachable");
  return -1;
}

static void
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
    memo->visitVectors[statenum][woffset] = 1;
    break;
  case ENCODING_NEGATIVE:
  {
    SearchStateTable *entry = mal(sizeof(*entry));
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

    HASH_ADD(hh, memo->searchStateTable, key, sizeof(SearchState), entry);
    break;
  }
  case ENCODING_RLE:
  case ENCODING_RLE_TUNED:
    RLEVector_set(memo->rleVectors[statenum], woffset);
    break;
  default:
    assert(!"Unknown encoding\n");
  }
}

/* Summary statistics */

static uint64_t
now(void)
{
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return tv.tv_sec*(uint64_t)1000000 + tv.tv_usec;
}

/* Prints human-readable to stdout, and JSON to stderr */
static void
printStats(Prog *prog, Memo *memo, VisitTable *visitTable, uint64_t startTime, Sub *sub)
{
  int i, j, n, count;

  uint64_t endTime = now();
  uint64_t elapsed_US = endTime - startTime;

  /* Per-search state */
  int maxVisitsPerSearchState = -1;
  int vertexWithMostVisitedSearchState = -1;
  int mostVisitedOffset = -1;

  /* Sum over all offsets */
  int maxVisitsPerVertex = -1;
  int mostVisitedVertex = -1;
  int *visitsPerVertex = NULL; /* Per-vertex sum of visits over all offsets */
  int nTotalVisits = 0;

  char *prefix = "STATS";

  char memoConfig_vertexSelection[64];
  char memoConfig_encoding[64];
  char numBufForSprintf[128];
  int csv_maxObservedCostsPerMemoizedVertex_len = 2*sizeof(char);
  char *csv_maxObservedCostsPerMemoizedVertex = mal(csv_maxObservedCostsPerMemoizedVertex_len);
  vec_strcat(&csv_maxObservedCostsPerMemoizedVertex, &csv_maxObservedCostsPerMemoizedVertex_len, "");

  switch (memo->mode) {
  case MEMO_NONE:
    strcpy(memoConfig_vertexSelection, "\"NONE\"");
    break;
  case MEMO_FULL:
    strcpy(memoConfig_vertexSelection, "\"ALL\"");
    break;
  case MEMO_IN_DEGREE_GT1:
    strcpy(memoConfig_vertexSelection, "\"INDEG>1\"");
    break;
  case MEMO_LOOP_DEST:
    strcpy(memoConfig_vertexSelection, "\"LOOP\"");
    break;
	default: assert(!"Unknown memo mode\n");
  }

  switch (memo->encoding) {
  case ENCODING_NONE:
    strcpy(memoConfig_encoding, "\"NONE\"");
    break;
  case ENCODING_NEGATIVE:
    strcpy(memoConfig_encoding, "\"NEGATIVE\"");
    break;
  case ENCODING_RLE:
    strcpy(memoConfig_encoding, "\"RLE\"");
    break;
  case ENCODING_RLE_TUNED:
    strcpy(memoConfig_encoding, "\"RLE_TUNED\"");
    break;
  default:
    logMsg(LOG_ERROR, "Encoding %d", memo->encoding);
    assert(!"Unknown encoding\n");
  }

  fprintf(stderr, "{");
  /* Info about input */
  fprintf(stderr, "\"inputInfo\": { \"nStates\": %d, \"lenW\": %d }",
    visitTable->nStates,
    visitTable->nChars);

  /* Most-visited vertex */
  visitsPerVertex = mal(sizeof(int) * visitTable->nStates);
  for (i = 0; i < visitTable->nStates; i++) {
    visitsPerVertex[i] = 0;
    for (j = 0; j < visitTable->nChars; j++) {
      /* Running sums */
      visitsPerVertex[i] += visitTable->visitVectors[i][j];
      nTotalVisits += visitTable->visitVectors[i][j];

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

  logMsg(LOG_INFO, "%s: Most-visited search state: <%d, %d> (%d visits)", prefix, vertexWithMostVisitedSearchState, mostVisitedOffset, maxVisitsPerSearchState);
  logMsg(LOG_INFO, "%s: Most-visited vertex: %d (%d visits over all its search states)", prefix, mostVisitedVertex, maxVisitsPerVertex);
  /* Info about simulation */
  fprintf(stderr, ", \"simulationInfo\": { \"nTotalVisits\": %d, \"nPossibleTotalVisitsWithMemoization\": %d, \"visitsToMostVisitedSearchState\": %d, \"visitsToMostVisitedVertex\": %d, \"simTimeUS\": %llu }",
    nTotalVisits, visitTable->nStates * visitTable->nChars, maxVisitsPerSearchState, maxVisitsPerVertex, elapsed_US);

  if (memo->mode == MEMO_FULL || memo->mode == MEMO_IN_DEGREE_GT1) {
    if (maxVisitsPerSearchState > 1 && !usesBackrefs(prog)) {
      /* I have proved this is impossible. */
      assert(!"Error, too many visits per search state\n");
    }
  }

  if (memo->backrefs) {
    switch (memo->encoding) {
    case ENCODING_NONE:
      /* All memoized states cost |w| */
      logMsg(LOG_INFO, "%s: No encoding, so all memoized vertices paid the full cost of |w| = %d slots", prefix, memo->nChars);
      for (i = 0; i < memo->nStates; i++) {
        sprintf(numBufForSprintf, "%d", memo->nChars);
        vec_strcat(&csv_maxObservedCostsPerMemoizedVertex, &csv_maxObservedCostsPerMemoizedVertex_len, numBufForSprintf);
        if (i + 1 != memo->nStates) {
          vec_strcat(&csv_maxObservedCostsPerMemoizedVertex, &csv_maxObservedCostsPerMemoizedVertex_len, ",");
        }
      }
      break;
    case ENCODING_NEGATIVE:
      logMsg(LOG_DEBUG, "C");
      logMsg(LOG_INFO, "%s: %d slots used (out of %d possible)",
        prefix, HASH_COUNT(memo->searchStateTable), memo->nStates * memo->nChars);

      /* Memoized state costs vary by number of visits to each node. */
      count = 0;
      for (i = 0; i < prog->len; i++) {
        if (prog->start[i].shouldMemo) {
          count += visitsPerVertex[i];

          sprintf(numBufForSprintf, "%d", visitsPerVertex[i]);
          vec_strcat(&csv_maxObservedCostsPerMemoizedVertex, &csv_maxObservedCostsPerMemoizedVertex_len, numBufForSprintf);
          if (prog->start[i].memoStateNum + 1 != memo->nStates) {
            vec_strcat(&csv_maxObservedCostsPerMemoizedVertex, &csv_maxObservedCostsPerMemoizedVertex_len, ",");
          }
        }
      }

      if (!memo->backrefs) {
        /* Sanity check: HASH_COUNT does correspond to the number of marked search states
        * This count will be inaccurate if backrefs are enabled, because we don't know all of the subs that we encountered.
        * TODO We could enumerate them another way. */
        n = 0;
        for (i = 0; i < memo->nStates; i++) {
          for (j = 0; j < memo->nChars; j++) {
            if (isMarked(memo, i, j, sub)) {
              n++;
            }
          }
        }
        logMsg(LOG_DEBUG, "HASH_COUNT %d n %d count %d", HASH_COUNT(memo->searchStateTable), n, count);
        assert(n == HASH_COUNT(memo->searchStateTable));
        assert(n == count);
      }

      break;
    case ENCODING_RLE:
    case ENCODING_RLE_TUNED:
      logMsg(LOG_INFO, "%s: |w| = %d", prefix, memo->nChars);
      for (i = 0; i < memo->nStates; i++) {
        logMsg(LOG_INFO, "%s: memo vector %d (RL %d) has %d runs (max observed during execution: %d, max possible: %d)",
          prefix, i, RLEVector_runSize(memo->rleVectors[i]),
          RLEVector_currSize(memo->rleVectors[i]),
          RLEVector_maxObservedSize(memo->rleVectors[i]),
          (memo->nChars / RLEVector_runSize(memo->rleVectors[i])) + 1
          );

        sprintf(numBufForSprintf, "%d", RLEVector_maxObservedSize(memo->rleVectors[i]));
        vec_strcat(&csv_maxObservedCostsPerMemoizedVertex, &csv_maxObservedCostsPerMemoizedVertex_len, numBufForSprintf);
        if (i + 1 != memo->nStates) {
          vec_strcat(&csv_maxObservedCostsPerMemoizedVertex, &csv_maxObservedCostsPerMemoizedVertex_len, ",");
        }
      }
      break;
      default:
        assert(!"Unexpected encoding\n");
    }
  }
  fprintf(stderr, ", \"memoizationInfo\": { \"config\": { \"vertexSelection\": %s, \"encoding\": %s }, \"results\": { \"nSelectedVertices\": %d, \"lenW\": %d, \"maxObservedCostPerMemoizedVertex\": [%s]}}",
    memoConfig_vertexSelection, memoConfig_encoding,
    memo->nStates, memo->nChars,
    csv_maxObservedCostsPerMemoizedVertex
  );

  fprintf(stderr, "}\n");

	free(csv_maxObservedCostsPerMemoizedVertex);
	free(visitsPerVertex);
}

/* NFA simulation */
typedef struct ThreadVec ThreadVec;
struct ThreadVec
{
  Thread *threads;
  int maxThreads;

  int nThreads;
};

static ThreadVec
ThreadVec_alloc()
{
  ThreadVec tv;
  tv.nThreads = 0;
  tv.maxThreads = 1000;
  tv.threads = mal(tv.maxThreads * sizeof(*tv.threads));

  logMsg(LOG_DEBUG, "TV: alloc -- threads %p", tv.threads);
  return tv;
}

static void
ThreadVec_realloc(ThreadVec *tv)
{
  int newMaxThreads = 2 * tv->maxThreads;
  Thread *newThreads = mal(newMaxThreads * sizeof(*newThreads));

  memcpy(newThreads, tv->threads, tv->nThreads * sizeof(*newThreads));
  logMsg(LOG_DEBUG, "TV %p: realloc from %d to %d threads, %p -> %p", tv, tv->maxThreads, newMaxThreads, tv->threads, newThreads);

  tv->maxThreads = newMaxThreads;

  free(tv->threads);
  tv->threads = newThreads;
}

static void
ThreadVec_free(ThreadVec *tv)
{
  logMsg(LOG_DEBUG, "TV %p: free -- threads %p", tv, tv->threads);
  free(tv->threads);
}

static Thread
ThreadVec_pop(ThreadVec *tv)
{
  Thread t;
  assert(tv->nThreads > 0);

  t = tv->threads[tv->nThreads-1];
  logMsg(LOG_VERBOSE, "TV: Popping t %d (pc %p, sp %p, sub %p)", tv->nThreads, t.pc, t.sp, t.sub);

  tv->nThreads--;
  assert(tv->nThreads >= 0);
  return t;
}

static void
ThreadVec_push(ThreadVec *tv, Thread t)
{
  if (tv->nThreads == tv->maxThreads)
    ThreadVec_realloc(tv);

  logMsg(LOG_VERBOSE, "TV: Pushing t %d (pc %p, sp %p, sub %p)", tv->nThreads+1, t.pc, t.sp, t.sub);
  tv->threads[ tv->nThreads ] = t;
  tv->nThreads++;
  assert(tv->nThreads <= tv->maxThreads);
}

static int
_inCharClass(Inst *pc, char c)
{
  int i, j;
  int inThisRange = 0, inAnyInstCharRange = 0;

  // Test for membership in each of the CharRange conditions
  for (i = 0; i < pc->charRangeCounts; i++) {
    logMsg(LOG_DEBUG, "testing range %d of %d (inv this one? %d)", i, pc->charRangeCounts, pc->charRanges[i].invert ? 1 : 0);
    inThisRange = 0;
    for (j = 0; j < pc->charRanges[i].count; j++) {
      logMsg(LOG_DEBUG, "testing range %d.%d: [%d, %d]", i, j, pc->charRanges[i].lows[j], pc->charRanges[i].highs[j]);
      inThisRange += pc->charRanges[i].lows[j] <= (int) c && (int) c <= pc->charRanges[i].highs[j];
    }

    // Invert the inner formula
    if (pc->charRanges[i].invert)
      inThisRange = !inThisRange;

    if (inThisRange) {
      logMsg(LOG_VERBOSE, "in range %d", i);
      inAnyInstCharRange = 1;
    }
  }

  // Apply top-level inversion
  if ( (inAnyInstCharRange && !pc->invert) || (!inAnyInstCharRange && pc->invert) )
    return 1;
  return 0;
}

static int
usesBackrefs(Prog *prog)
{
  int list[MAXSUB];
  return backrefdCGs(prog, list) > 0;
}

/* Record the cgNum for the groups referenced in a StringCompare (backreference Inst).
 * Updates list, returns the number of distinct referenced groups (|CG_{BR}|). */
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

int
backtrack(Prog *prog, char *input, /* start-end pointers for each CG */ char **subp, /* Length of subp */ int nsubp)
{
  Memo memo;
  VisitTable visitTable;
  int i;
  Inst *pc; /* Current position in VM (pc) */
  char *sp; /* Current position in input */
  Sub *sub; /* submatch (capture group) */
  char *inputEOL; /* Position of \0 terminating input */
  uint64_t startTime;
  int cg_br[MAXSUB/2];
  ThreadVec *threads = NULL;
	int matched = 0;

  int inZWA = 0;
  char *sp_save = NULL;
  ThreadVec *threads_save = NULL;

  inputEOL = input + strlen(input);

  /* The use of backreferences affects the memoization policy. */
  if (backrefdCGs(prog, cg_br) && prog->memoMode != MEMO_NONE) {
    logMsg(LOG_INFO, "Backreferences present and memo enabled -- configuring memo to negative encoding");
    prog->memoEncoding = ENCODING_NEGATIVE;
  }

  /* Prep visit table */
  logMsg(LOG_VERBOSE, "Initializing visit table");
  visitTable = initVisitTable(prog, strlen(input) + 1);

  /* Prep memo table */
  logMsg(LOG_VERBOSE, "Initializing memo table");
  memo = initMemoTable(prog, strlen(input) + 1, prog->memoMode, prog->memoEncoding);

  /* Prep sub-captures */
  sub = newsub(nsubp, input);
  for(i=0; i<nsubp; i++)
    sub->sub[i] = nil;

  logMsg(LOG_INFO, "Backtrack: Simulation begins");
  startTime = now();

  /* Initial thread state is < q0, w[0], current capture group > */
  ThreadVec ready = ThreadVec_alloc();
  ThreadVec_push(&ready, thread(prog->start, input, sub));
  threads = &ready;

  /* To recurse: save the state (sp, threads) and replace threads with the new starting point */

  /* Run threads in stack order */
BACKTRACKING_SEARCH:
  while(threads->nThreads > 0) {
    Thread next = ThreadVec_pop(threads);
    pc = next.pc;
    sp = next.sp;
    sub = next.sub;
    assert(sub->ref > 0);
    for(;;) { /* Run thread to completion */
      logMsg(LOG_VERBOSE, "  search state: <%d (M: %d), %d>", pc->stateNum, pc->memoStateNum, woffset(input, sp));

      if (prog->memoMode != MEMO_NONE && pc->memoStateNum >= 0) {
        /* Check if we've been here. */
        if (isMarked(&memo, pc->memoStateNum, woffset(input, sp), sub)) {
          /* Since we return on first match, the prior visit failed.
           * Short-circuit thread */
          logMsg(LOG_VERBOSE, "marked, short-circuiting thread");
          assert(pc->opcode != Match);
          goto Dead;
        }

        /* Mark that we've been here */
        markMemo(&memo, pc->memoStateNum, woffset(input, sp), sub);
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
        if(*sp == 0 || *sp == '\n' || *sp == '\r')
          goto Dead;
        pc++;
        sp++;
        continue;
      case CharClass:
        if (*sp == 0)
          goto Dead;
        /* Look through char class mins/maxes */
        logMsg(LOG_VERBOSE, "Does char %d match CC? charClassCounts %d",
          *sp, pc->charRangeCounts);

        if (!_inCharClass(pc, *sp)) {
          logMsg(LOG_VERBOSE, "not in char class");
          goto Dead;
        }
        logMsg(LOG_VERBOSE, "char %d matched CC", *sp);
        pc++;
        sp++;  
        continue;
      case Match:
        logMsg(LOG_VERBOSE, "Match: eolAnchor %d sp %p inputEOL %p", prog->eolAnchor, sp, inputEOL);
        if (!prog->eolAnchor || (prog->eolAnchor && sp == inputEOL)) {
          for(i=0; i<nsubp; i++)
            subp[i] = sub->sub[i];
          decref(sub);

					matched = 1;
					goto CleanupAndRet;
        }
        goto Dead;
      case Jmp:
        pc = pc->x;
        continue;
      case Split: /* Non-deterministic choice */
        ThreadVec_push(threads, thread(pc->y, sp, incref(sub)));
        pc = pc->x;  /* continue current thread */
        continue;
      case SplitMany: /* Non-deterministic choice */
        for (i = 1; i < pc->arity; i++) {
          ThreadVec_push(threads, thread(pc->edges[i], sp, incref(sub)));
        }
        pc = pc->edges[0];  /* continue current thread */
        continue;
      case Save:
        logMsg(LOG_DEBUG, "  save %d at %p", pc->n, sp);
        sub = update(sub, pc->n, sp);
        pc++;
        continue;
      case WordBoundary: // TODO This would be cleaner as "fixed-width assertions". Could support: ^, \A, $, \Z, \z, \b, \B
      {
        logMsg(LOG_DEBUG, "  wordBoundary");
        int isWordBoundary = 0;
        // Python: \b is defined as the boundary between:
        //   (1) \w and a \W character
        //   (2) \w and begin/end of the string
        if (sp == input || sp == inputEOL) {
          // Condition (2)
          isWordBoundary = 1;
        } else {
          // Condition (1) -- dereference is safe because we tested Condition (2) already
          int prev_c = *(sp-1);
          int curr_c = *sp;

          // TODO This duplicates the concept of '\w'.
          // It would be better to have a dummy \w and \W PC handy to compare against.
          int prev_w = IS_WORD_CHAR(prev_c);
          int curr_w = IS_WORD_CHAR(curr_c);

          isWordBoundary = (prev_w ^ curr_w);
        } 

        if (isWordBoundary && pc->c == 'b') {
          // Boundary satisfied: Move on without updating sp
          pc++;
          continue;
        } else if (!isWordBoundary && pc->c == 'B') {
          // Boundary satisfied: Move on without updating sp
          pc++;
          continue;
        }

        goto Dead;
      }
      case StringCompare:
        /* Check if appropriate sub matches */
      {
        // CG is not set -- match the empty string
        if (sub->sub[CGID_TO_SUB_STARTP_IX(pc->cgNum)] == nil || sub->sub[CGID_TO_SUB_ENDP_IX(pc->cgNum)] == nil) {
          logMsg(LOG_DEBUG, "CG %d not set yet (startpix %d endpix %d). We match the empty string", pc->cgNum, CGID_TO_SUB_STARTP_IX(pc->cgNum), CGID_TO_SUB_ENDP_IX(pc->cgNum));
          pc++;
          continue;
        }
        logMsg(LOG_DEBUG, "CG %d set, checking match", pc->cgNum);

        char *begin = CGID_TO_STARTP(sub, pc->cgNum);
        char *end = CGID_TO_ENDP(sub, pc->cgNum);
        int charsRemaining = inputEOL - sp;
          logMsg(LOG_DEBUG, "charsRemaining %d end-begin %d", charsRemaining, end-begin);
        if (charsRemaining >= end - begin) {
          if (memcmp(begin, sp, end-begin) == 0) {
            logMsg(LOG_DEBUG, "StringCompare matched (%d chars)", end - begin);
            sp += end-begin;
            pc++;
            continue;
          }
          else
            logMsg(LOG_DEBUG, "Backref mismatch (%d chars)", end - begin);
        }
        else
            logMsg(LOG_DEBUG, "Remaining string too short");
      }
        goto Dead;
      case ZeroWidthAssertion:
      {
        // Save state: i, backtrack stack
        assert(!inZWA); // No nesting
        inZWA = 1;
        sp_save = sp;
        threads_save = threads;

        // Override
        Inst *newPC = pc+1;
        ThreadVec override = ThreadVec_alloc();
        ThreadVec_push(&override, thread(newPC, sp, sub));
        threads = &override;
        logMsg(LOG_DEBUG, "Overriding threads %p with %p -- a sub-simulation starting at <q%d, i%d>", threads_save, threads, (int)(newPC-prog->start), (int)(sp - input));
        goto BACKTRACKING_SEARCH;
      }
        assert(!"unreachable");
      case RecursiveMatch:
        logMsg(LOG_DEBUG, "Made it to %d RecursiveMatch", (int)(pc-prog->start));
        // Restore state: i, backtrack stack
        assert(inZWA);
        inZWA = 0;
        sp = sp_save; // Zero-width
        logMsg(LOG_DEBUG, "Restoring threads from %p to %p", threads, threads_save);
        ThreadVec_free(threads);
        threads = threads_save;
        threads_save = nil;

        pc++; // Advance beyond the ZWA
        logMsg(LOG_DEBUG, "Resuming execution at <q%d, i%d>\n", (int)(pc-prog->start), (int)(sp-input));
        continue; // Pick up where we left off

      default:
        logMsg(LOG_ERROR, "Unknown opcode %d", pc->opcode);
      }
    }
  Dead:
    decref(sub);
  }
  // Backtracking stack is exhausted.
  if (inZWA) {
    // No way to honor the ZWA from this point. Backtrack.
    logMsg(LOG_INFO, "Could not honor ZWA");
    inZWA = 0;
    sp = sp_save; // Zero-width
    ThreadVec_free(threads);
    threads = threads_save;
    threads_save = nil;
    goto BACKTRACKING_SEARCH;
  }
	matched = 0;

CleanupAndRet:
	//decref(&sub);
  printStats(prog, &memo, &visitTable, startTime, sub);
  ThreadVec_free(&ready);
  freeVisitTable(visitTable);
  freeMemoTable(memo);
  return matched;
}

