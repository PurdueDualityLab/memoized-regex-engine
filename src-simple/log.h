// Copyright 2020 James Davis.  All Rights Reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file

#ifndef LOG_H
#define LOG_H

/* Log level */
enum
{
  LOG_SILENT=0,
  LOG_ERROR,
  LOG_WARN,
  LOG_INFO,
  LOG_VERBOSE,
  LOG_DEBUG,
  LOG_MAX=LOG_DEBUG
};

/* Verbosity is controlled via the env var MEMOIZATION_LOGLVL="error"|...|"verbose"
 * Logging is performed at or below the specified level.
 * Default level is SILENT */

void logMsg(int level, const char* message, ...);

#endif