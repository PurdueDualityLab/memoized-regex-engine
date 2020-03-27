// Copyright 2007-2009 Russ Cox.  All Rights Reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.
//
// Annotations, statistics, and memoization by James Davis, 2020.

#include "regexp.h"
#include <sys/time.h>
#include <assert.h>

void
vec_strcat(char **dest, int *dAlloc, char *src)
{
  int combinedLen = strlen(*dest) + strlen(src) + 5;
  // printf("vec_strcat: dest %p *dest %p *dAlloc %d; string <%s>\n", dest, *dest, *dAlloc, *dest);

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
  // printf("vec_strcat: dest %p *dest %p *dAlloc %d; string <%s>\n", dest, *dest, *dAlloc, *dest);
  return;
}

static int VERBOSE = 0;

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
  if (VERBOSE) {
    printf("Visit: Visiting <%d, %d>\n", statenum, woffset);
  
    if (visitTable->visitVectors[statenum][woffset] > 0)
      printf("Hmm, already visited <%d, %d>\n", statenum, woffset);
  }
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
  
  switch(memo.encoding){
  case ENCODING_NONE:
    printf("%s: Initializing with encoding NONE\n", prefix);
    printf("%s: cardQ = %d, Phi_memo = %d\n", prefix, cardQ, nStatesToTrack);
    /* Visit vectors */
    memo.visitVectors = mal(sizeof(*memo.visitVectors) * nStatesToTrack);

    printf("%s: %d visit vectors x %d chars for each\n", prefix, nStatesToTrack, nChars);
    for (i = 0; i < nStatesToTrack; i++) {
      memo.visitVectors[i] = mal(sizeof(int) * nChars);
      for (j = 0; j < nChars; j++) {
        memo.visitVectors[i][j] = 0;
      }
    }
    break;
  case ENCODING_NEGATIVE:
    printf("%s: Initializing with encoding NEGATIVE\n", prefix);
    memo.searchStateTable = NULL;
    break;
  case ENCODING_RLE:
  case ENCODING_RLE_TUNED:
    if (memo.encoding == ENCODING_RLE_TUNED)
      printf("%s: Initializing with encoding RLE_TUNED\n", prefix);
    else
      printf("%s: Initializing with encoding RLE\n", prefix);

    memo.rleVectors = mal(sizeof(*memo.rleVectors) * nStatesToTrack);

    printf("%s: %d RLE-encoded visit vectors\n", prefix, nStatesToTrack);
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
          printf("%s: state %d (memo state %d) will use visitInterval %d\n", prefix, j, i, visitInterval);
          memo.rleVectors[i] = RLEVector_create(visitInterval);
          break;
        }
      }
    }
    break;
  default:
    printf("%s: Unexpected encoding %d\n", prefix, memo.encoding);
    assert(0);
  }

  printf("%s: initialized\n", prefix);
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
  if (VERBOSE) {
    printf("  isMarked: querying <%d, %d>\n", statenum, woffset);
  }

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
  if (VERBOSE) {
    printf("Memo: Marking <%d, %d>\n", statenum, woffset);

    if (isMarked(memo, statenum, woffset)) {
      printf("\n****\n\n   Hmm, already marked s%d c%d\n\n*****\n\n", statenum, woffset);
    }
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

  char memoConfig_vertexSelection[32];
  char memoConfig_encoding[32];
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

  printf("%s: Most-visited search state: <%d, %d> (%d visits)\n", prefix, vertexWithMostVisitedSearchState, mostVisitedOffset, maxVisitsPerSearchState);
  printf("%s: Most-visited vertex: %d (%d visits over all its search states)\n", prefix, mostVisitedVertex, maxVisitsPerVertex);
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
    printf("%s: No encoding, so all memoized vertices paid the full cost of |w| = %d slots\n", prefix, memo->nChars);
    for (i = 0; i < memo->nStates; i++) {
      sprintf(numBufForSprintf, "%d", memo->nChars);
      vec_strcat(&csv_maxObservedCostsPerMemoizedVertex, &csv_maxObservedCostsPerMemoizedVertex_len, numBufForSprintf);
      if (i + 1 != memo->nStates) {
        vec_strcat(&csv_maxObservedCostsPerMemoizedVertex, &csv_maxObservedCostsPerMemoizedVertex_len, ",");
      }
    }
    break;
  case ENCODING_NEGATIVE:
    printf("%s: %d slots used (out of %d possible)\n",
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
    printf("%s: |w| = %d\n", prefix, memo->nChars);
    for (i = 0; i < memo->nStates; i++) {
      printf("%s: memo vector %d (RL %d) has %d runs (max observed during execution: %d, max possible: %d)\n",
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

int
backtrack(Prog *prog, char *input, char **subp, int nsubp)
{
  Memo memo;
  VisitTable visitTable;
  enum { MAX = 100000 };
  Thread ready[MAX];
  int i, j, inCharClass, nready;
  Inst *pc; /* Current position in VM (pc) */
  char *sp; /* Current position in input */
  Sub *sub; /* submatch (capture group) */
  char *inputEOL; /* Position of \0 terminating input */
  uint64_t startTime;

  inputEOL = input + strlen(input);

  /* Prep visit table */
  if (VERBOSE)
    printf("Initializing visit table\n");
  visitTable = initVisitTable(prog, strlen(input) + 1);

  /* Prep memo table */
  if (prog->memoMode != MEMO_NONE) {
    if (VERBOSE)
      printf("Initializing memo table\n");
    memo = initMemoTable(prog, strlen(input) + 1, prog->memoMode, prog->memoEncoding);
  }

  printf("\n\n***************\n\n  Backtrack: Simulation begins\n\n************\n\n");
  startTime = now();

  /* queue initial thread */
  sub = newsub(nsubp);
  for(i=0; i<nsubp; i++)
    sub->sub[i] = nil;
  /* Initial thread state is < q0, w[0], current capture group > */
  ready[0] = thread(prog->start, input, sub);
  nready = 1;

  /* run threads in stack order */
  while(nready > 0) {
    --nready;  /* pop state for next thread to run */
    pc = ready[nready].pc;
    sp = ready[nready].sp;
    sub = ready[nready].sub;
    assert(sub->ref > 0);
    for(;;) { /* Run thread to completion */
      if (VERBOSE)
        printf("  search state: <%d (M: %d), %d>\n", pc->stateNum, pc->memoStateNum, woffset(input, sp));

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
        if (VERBOSE)
          printf("Does char %d match CC %d (inv %d)? charClassCounts %d\n",
            *sp, pc->c, pc->invert, pc->charClassCounts);
        inCharClass = 0;
        for (j = 0; j < pc->charClassCounts; j++) {
          if (VERBOSE)
            printf("testing range [%d, %d]\n", pc->charClassMins[j], pc->charClassMaxes[j]);
          if (pc->charClassMins[j] <= (int) *sp && (int) *sp <= pc->charClassMaxes[j]) {
            if (VERBOSE)
              printf("in range %d\n", j);
            inCharClass = 1;
          }
        }

        /* Check for match, honoring invert */
        if ((inCharClass && pc->invert) || (!inCharClass && !pc->invert)) {
          if (VERBOSE)
            printf("no match (inCharClass %d invert %d)\n", inCharClass, pc->invert);
          goto Dead;
        }
        if (VERBOSE)
          printf("char %d matched CC %d\n", *sp, pc->c);
        pc++;
        sp++;  
        continue;
      case Match:
        if (VERBOSE)
          printf("Match: eolAnchor %d sp %p inputEOL %p\n", prog->eolAnchor, sp, inputEOL);
        if (!prog->eolAnchor || (prog->eolAnchor && sp == inputEOL)) {
          for(i=0; i<nsubp; i++)
            subp[i] = sub->sub[i];
          decref(sub);
          printStats(prog, &memo, &visitTable, startTime);
          return 1;
        }
        goto Dead;
      case Jmp:
        pc = pc->x;
        continue;
      case Split: /* Non-deterministic choice */
        if(nready >= MAX)
          fatal("backtrack overflow");
        ready[nready++] = thread(pc->y, sp, incref(sub));
        pc = pc->x;  /* continue current thread */
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
  return 0;
}

