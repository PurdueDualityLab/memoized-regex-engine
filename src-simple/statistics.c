#include "statistics.h"
#include "log.h"

#include <stdio.h>
#include <sys/time.h>

static void
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

/* Prints human-readable to stdout, and JSON to stderr */
void
printStats(Prog *prog, Memo *memo, VisitTable *visitTable, uint64_t startTime, Sub *sub)
{
  int i, j, n, count;

  uint64_t endTime = now();
  uint64_t elapsed_US = endTime - startTime;

  /* Per-search state */
  int maxVisitsPerSimPos = -1;
  int vertexWithMostVisitedSimPos = -1;
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
      if (visitTable->visitVectors[i][j] > maxVisitsPerSimPos) {
        maxVisitsPerSimPos = visitTable->visitVectors[i][j];
        vertexWithMostVisitedSimPos = i;
        mostVisitedOffset = j;
      }
    }

    /* Largest overall visits per vertex? */
    if (visitsPerVertex[i]  > maxVisitsPerVertex) {
      maxVisitsPerVertex = visitsPerVertex[i];
      mostVisitedVertex = i;
    }
  }

  logMsg(LOG_INFO, "%s: Most-visited search state: <%d, %d> (%d visits)", prefix, vertexWithMostVisitedSimPos, mostVisitedOffset, maxVisitsPerSimPos);
  logMsg(LOG_INFO, "%s: Most-visited vertex: %d (%d visits over all its search states)", prefix, mostVisitedVertex, maxVisitsPerVertex);
  /* Info about simulation */
  fprintf(stderr, ", \"simulationInfo\": { \"nTotalVisits\": %d, \"nPossibleTotalVisitsWithMemoization\": %d, \"visitsToMostVisitedSimPos\": %d, \"visitsToMostVisitedVertex\": %d, \"simTimeUS\": %llu }",
    nTotalVisits, visitTable->nStates * visitTable->nChars, maxVisitsPerSimPos, maxVisitsPerVertex, elapsed_US);

  if (memo->mode == MEMO_FULL || memo->mode == MEMO_IN_DEGREE_GT1) {
    if (maxVisitsPerSimPos > 1 && !usesBackreferences(prog)) {
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
        if (prog->start[i].memoInfo.shouldMemo) {
          count += visitsPerVertex[i];

          sprintf(numBufForSprintf, "%d", visitsPerVertex[i]);
          vec_strcat(&csv_maxObservedCostsPerMemoizedVertex, &csv_maxObservedCostsPerMemoizedVertex_len, numBufForSprintf);
          if (prog->start[i].memoInfo.memoStateNum + 1 != memo->nStates) {
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

uint64_t
now(void)
{
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return tv.tv_sec*(uint64_t)1000000 + tv.tv_usec;
}
