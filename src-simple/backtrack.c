// Copyright 2007-2009 Russ Cox.  All Rights Reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.
//
// Annotations, statistics, and memoization by James Davis, 2020.

#include "regexp.h"
#include "memoize.h"
#include "statistics.h"
#include "log.h"

#include <assert.h>

/* Misc. */
#define IS_WORD_CHAR(c) (('a' <= c && c <= 'z') || ('A' <= c && c <= 'Z') || ('0' <= c && c <= '9'))

typedef struct Thread Thread;

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

static int
woffset(char *input, char *sp)
{
  return (int) (sp - input);
}

/* Summary statistics */

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

int
usesBackreferences(Prog *prog)
{
  Inst *pc;
  int i;

  for (i = 0, pc = prog->start; i < prog->len; i++, pc++) {
    if (pc->opcode == StringCompare) {
      return 1;
    }
  }
  return 0;
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
  ThreadVec *threads = NULL;
	int matched = 0;

  int inZWA = 0;
  char *sp_save = NULL;
  ThreadVec *threads_save = NULL;

  inputEOL = input + strlen(input);

  /* Prep sub-captures */
  sub = newsub(nsubp, input);
  for(i=0; i<nsubp; i++)
    sub->sub[i] = nil;

  /* Prep memo structures */
  logMsg(LOG_VERBOSE, "Initializing visit table");
  visitTable = initVisitTable(prog, strlen(input) + 1);
  logMsg(LOG_VERBOSE, "Initializing memo table");
  memo = initMemoTable(prog, strlen(input) + 1);

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
      logMsg(LOG_VERBOSE, "  search state: <%d (M: %d), %d>", pc->stateNum, pc->memoInfo.memoStateNum, woffset(input, sp));

      if (prog->memoMode != MEMO_NONE && pc->memoInfo.memoStateNum >= 0) {
        /* Check if we've been here. */
        if (isMarked(&memo, pc->memoInfo.memoStateNum, woffset(input, sp), sub)) {
          /* Since we return on first match, the prior visit failed.
           * Short-circuit thread */
          logMsg(LOG_VERBOSE, "marked, short-circuiting thread");
          assert(pc->opcode != Match);
          goto Dead;
        }

        /* Mark that we've been here */
        markMemo(&memo, pc->memoInfo.memoStateNum, woffset(input, sp), sub);
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

        goto Dead;
      }
      case InlineZeroWidthAssertion:
      {
        int satisfied = 0;
        switch (pc->c) {
        case 'b':
        case 'B':
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

            int prev_w = IS_WORD_CHAR(prev_c);
            int curr_w = IS_WORD_CHAR(curr_c);

            isWordBoundary = (prev_w ^ curr_w);
          } 

          if (isWordBoundary && pc->c == 'b') {
            satisfied = 1;
          } else if (!isWordBoundary && pc->c == 'B') {
            satisfied = 1;
          }
          break;
        case '^':
        case 'A':
          if (sp == input) {
            satisfied = 1;
          }
          break;
        case '$':
        case 'Z':
        case 'z':
          if (sp == inputEOL) {
            satisfied = 1;
          }
					break;
        default:
          logMsg(LOG_ERROR, "Unknown InlineZWA character %c", pc->c);
          assert(!"Unknown InlineZWA character\n");
        }

        if (satisfied) {
          pc++;
          continue;
        }
				logMsg(LOG_DEBUG, "InlineZWA %c unsatisfied", pc->c);
        goto Dead;
      }
      case RecursiveZeroWidthAssertion:
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
