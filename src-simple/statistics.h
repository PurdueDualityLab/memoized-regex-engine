// Copyright 2020 James C. Davis.  All Rights Reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#ifndef STATISTICS_H
#define STATISTICS_H

#include "regexp.h"
#include "memoize.h"

uint64_t
now(void);

void printStats(Prog *prog, Memo *memo, VisitTable *visitTable, uint64_t startTime, Sub *sub);

#endif /* STATISTICS_H */