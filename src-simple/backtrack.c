// Copyright 2007-2009 Russ Cox.  All Rights Reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.
//
// Annotations, statistics, and memoization by James Davis, 2020.

#include "regexp.h"
#include "log.h"

#include <sys/time.h>
#include <assert.h>

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
  
  if (memoMode != MEMO_NONE) {
    switch(memo.encoding){
    case ENCODING_NONE:
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

static int
woffset(char *input, char *sp)
{
  return (int) (sp - input);
}

static int
isMarked(Memo *memo, int statenum /* PC's memoStateNum */, int woffset)
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
markMemo(Memo *memo, int statenum, int woffset)
{
  logMsg(LOG_VERBOSE, "Memo: Marking <%d, %d>", statenum, woffset);

  if (isMarked(memo, statenum, woffset)) {
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
printStats(Prog *prog, Memo *memo, VisitTable *visitTable, uint64_t startTime)
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
    if (maxVisitsPerSearchState > 1) {
      /* I have proved this is impossible. */
      assert(!"Error, too many visits per search state\n");
    }
  }

  switch(memo->encoding) {
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
    // Sanity check: HASH_COUNT does correspond to the number of marked <q, i> search states
    n = 0;
    for (i = 0; i < memo->nStates; i++) {
      for (j = 0; j < memo->nChars; j++) {
        if (isMarked(memo, i, j)) {
          n++;
        }
      }
    }
    assert(n == HASH_COUNT(memo->searchStateTable));
    assert(n == count);

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
  fprintf(stderr, ", \"memoizationInfo\": { \"config\": { \"vertexSelection\": %s, \"encoding\": %s }, \"results\": { \"nSelectedVertices\": %d, \"lenW\": %d, \"maxObservedCostPerMemoizedVertex\": [%s]}}",
    memoConfig_vertexSelection, memoConfig_encoding,
    memo->nStates, memo->nChars,
    csv_maxObservedCostsPerMemoizedVertex
  );

  fprintf(stderr, "}\n");
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
  logMsg(LOG_DEBUG, "TV: realloc from %d to %d threads, %p -> %p", tv->maxThreads, newMaxThreads, tv->threads, newThreads);

  tv->maxThreads = newMaxThreads;

  free(tv->threads);
  tv->threads = newThreads;
}

static void
ThreadVec_free(ThreadVec *tv)
{
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

int
backtrack(Prog *prog, char *input, char **subp, int nsubp)
{
  Memo memo;
  VisitTable visitTable;
  ThreadVec ready = ThreadVec_alloc();
  int i, j, k, inCharClass;
  Inst *pc; /* Current position in VM (pc) */
  char *sp; /* Current position in input */
  Sub *sub; /* submatch (capture group) */
  char *inputEOL; /* Position of \0 terminating input */
  uint64_t startTime;

  inputEOL = input + strlen(input);

  /* Prep visit table */
  logMsg(LOG_VERBOSE, "Initializing visit table");
  visitTable = initVisitTable(prog, strlen(input) + 1);

  /* Prep memo table */
  logMsg(LOG_VERBOSE, "Initializing memo table");
  memo = initMemoTable(prog, strlen(input) + 1, prog->memoMode, prog->memoEncoding);

  logMsg(LOG_INFO, "Backtrack: Simulation begins");
  startTime = now();

  /* queue initial thread */
  sub = newsub(nsubp);
  for(i=0; i<nsubp; i++)
    sub->sub[i] = nil;
  /* Initial thread state is < q0, w[0], current capture group > */
  ThreadVec_push(&ready, thread(prog->start, input, sub));

  /* run threads in stack order */
  while(ready.nThreads > 0) {
    Thread next = ThreadVec_pop(&ready);
    pc = next.pc;
    sp = next.sp;
    sub = next.sub;
    assert(sub->ref > 0);
    for(;;) { /* Run thread to completion */
      logMsg(LOG_VERBOSE, "  search state: <%d (M: %d), %d>", pc->stateNum, pc->memoStateNum, woffset(input, sp));

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
        if(*sp == 0 || *sp == '\n' || *sp == '\r')
          goto Dead;
        pc++;
        sp++;
        continue;
      case CharClass:
        if (*sp == 0)
          goto Dead;
        /* Look through char class mins/maxes */
        logMsg(LOG_VERBOSE, "Does char %d match CC %d (inv %d)? charClassCounts %d",
          *sp, pc->c, pc->invert, pc->charClassCounts);
        inCharClass = 0;
        for (j = 0; j < pc->charClassCounts; j++) {
          logMsg(LOG_VERBOSE, "testing range [%d, %d]", pc->charClassMins[j], pc->charClassMaxes[j]);
          if (pc->charClassMins[j] <= (int) *sp && (int) *sp <= pc->charClassMaxes[j]) {
            logMsg(LOG_VERBOSE, "in range %d", j);
            inCharClass = 1;
          }
        }

        /* Check for match, honoring invert */
        if ((inCharClass && pc->invert) || (!inCharClass && !pc->invert)) {
          logMsg(LOG_VERBOSE, "no match (inCharClass %d invert %d)", inCharClass, pc->invert);
          goto Dead;
        }
        logMsg(LOG_VERBOSE, "char %d matched CC %d", *sp, pc->c);
        pc++;
        sp++;  
        continue;
      case Match:
        logMsg(LOG_VERBOSE, "Match: eolAnchor %d sp %p inputEOL %p", prog->eolAnchor, sp, inputEOL);
        if (!prog->eolAnchor || (prog->eolAnchor && sp == inputEOL)) {
          for(i=0; i<nsubp; i++)
            subp[i] = sub->sub[i];
          decref(sub);

          printStats(prog, &memo, &visitTable, startTime);
          ThreadVec_free(&ready);
          return 1;
        }
        goto Dead;
      case Jmp:
        pc = pc->x;
        continue;
      case Split: /* Non-deterministic choice */
        ThreadVec_push(&ready, thread(pc->y, sp, incref(sub)));
        pc = pc->x;  /* continue current thread */
        continue;
      case SplitMany: /* Non-deterministic choice */
        for (k = 1; k < pc->arity; k++) {
          ThreadVec_push(&ready, thread(pc->edges[k], sp, incref(sub)));
        }
        pc = pc->edges[0];  /* continue current thread */
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

  printStats(prog, &memo, &visitTable, startTime);
  ThreadVec_free(&ready);
  return 0;
}

