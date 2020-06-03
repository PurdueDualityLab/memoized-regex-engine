#!/usr/bin/env python3
# Test libLF.SimpleRegex for SL behavior under various memoization schemes.
# This analysis includes time measurements for performance.

# Import libMemo
import libMemo

# Import libLF
import os
import sys
sys.path.append(os.path.join(os.environ['MEMOIZATION_PROJECT_ROOT'], 'eval', 'lib'))
import libLF

import re
import json
import tempfile
import argparse
import traceback
import time
import subprocess
import statistics
import math

import pandas as pd
import seaborn as sns
import matplotlib.pyplot as plt

# Shell dependencies

shellDeps = [ libMemo.ProtoRegexEngine.CLI ]

SAVE_TMP_FILES = True

# Other globals
MAX_MATCH_MS = 1*1000 # TODO
NTRIALS = 5 # Take the median of this many trials

def loadRegexFile(regexFile):
  """Return a list of (nick, libMemo.SimpleRegex)"""
  regexes = []
  libLF.log('Loading regexes from {}'.format(regexFile))
  with open(regexFile, 'r') as inStream:
    for line in inStream:
      line = line.strip()
      if len(line) == 0:
        continue
      
      try:
        # Build a Regex
        nick = json.loads(line)['nick']
        if nick == 'Baseline':
          continue
        regex = libMemo.SimpleRegex()
        regex.initFromNDJSON(line)
        regexes.append((nick, regex))
      except KeyboardInterrupt:
        raise
      except BaseException as err:
        libLF.log('Exception parsing line:\n  {}\n  {}'.format(line, err))
        traceback.print_exc()

    libLF.log('Loaded {} regexes from {}'.format(len(regexes), regexFile))
    return regexes

################

def evalRegex(nick, regex):
  """Build the "growth curve" for this regex, with and without memoization

  Returns: data[] dicts with keys: {nPumps, nonMemoMatchTimeMS, memoMatchTimeMS}, sorted by increasing nPumps
  """
  # Track these for the second pass
  nPumps = []
  nonMemoMatchTimes = []

  pumpsToTry = list(range(1, 10, 1)) + list(range(10, 30, 2)) + list(range(50, 100, 10)) + list(range(100, 200, 25)) + list(range(200, 1000, 100)) + list(range(1000, 10000, 1000))

  matchTimeMS = 0
  pumpIx = 0
  while matchTimeMS < MAX_MATCH_MS and pumpIx < len(pumpsToTry):
    times = []
    # Query
    libLF.log("nick {} nPumps {} prev matchTimeMS {}".format(nick, pumpsToTry[pumpIx], matchTimeMS))
    queryFile = libMemo.ProtoRegexEngine.buildQueryFile(regex.pattern, regex.evilInputs[0].build(pumpsToTry[pumpIx]))
    for i in range(NTRIALS):
      em = libMemo.ProtoRegexEngine.query(libMemo.ProtoRegexEngine.SELECTION_SCHEME.SS_None, libMemo.ProtoRegexEngine.ENCODING_SCHEME.ES_None, queryFile)
      matchTimeMS = max(int(em.si_simTimeUS/1000), 10)
      times.append(matchTimeMS)
    matchTimeMS = statistics.median(times)
    os.unlink(queryFile)

    # Record
    nonMemoMatchTimes.append(matchTimeMS)
    nPumps.append(pumpsToTry[pumpIx])
    libLF.log("nPumps {} matchTimeMS {}".format(nPumps[-1], nonMemoMatchTimes[-1]))

    # Grow
    pumpIx += 1
  
  # Now try with memoization
  libLF.log("Now trying with memoization")
  memoMatchTimes = []
  for currPump in nPumps:
    # Query
    queryFile = libMemo.ProtoRegexEngine.buildQueryFile(regex.pattern, regex.evilInputs[0].build(currPump))

    times = []
    for i in range(NTRIALS):
      em = libMemo.ProtoRegexEngine.query(libMemo.ProtoRegexEngine.SELECTION_SCHEME.SS_InDeg, libMemo.ProtoRegexEngine.ENCODING_SCHEME.ES_None, queryFile)
      matchTimeMS = max(int(em.si_simTimeUS/1000), 10)
      times.append(matchTimeMS)
    matchTimeMS = statistics.median(times)
    os.unlink(queryFile)

    # Record
    memoMatchTimes.append(matchTimeMS)

  return [
    {"nPumps": np, "nonMemoMatchTimeMS": nmmt, "memoMatchTimeMS": mmt}
    for np, nmmt, mmt in zip(nPumps, nonMemoMatchTimes, memoMatchTimes)
  ]

def main(regexFile):
  libLF.log('regexFile {}' \
    .format(regexFile))

  #### Check dependencies
  libLF.checkShellDependencies(shellDeps)

  #### Load data
  regexes = loadRegexFile(regexFile)
  print([nick for nick, reg in regexes])

  #### Collect data
  nick2dat = {}
  for nick, regex in regexes:
    nick2dat[nick] = evalRegex(nick, regex)
  
  ### Munge into DF
  maxPumpsForNonMemoMatchTime = 0
  records = []
  for nick, dat in nick2dat.items():
    for rec in dat:
      records += [
        {"Regex": nick, "Memoized": False, "nPumps": rec["nPumps"], "matchTimeMS": rec["nonMemoMatchTimeMS"] },
        {"Regex": nick, "Memoized": True, "nPumps": rec["nPumps"], "matchTimeMS": rec["memoMatchTimeMS"] },
      ]
      if nick != "Baseline":
        maxPumpsForNonMemoMatchTime = max(rec["nPumps"], maxPumpsForNonMemoMatchTime)
      print(records[-2:])
  df = pd.DataFrame(columns=['Regex', 'Memoized', 'nPumps', 'matchTimeMS'], data=records)
  print(df.describe())

  #### Emit results
  caseStudy_fname = os.path.join('figs', 'caseStudy.pdf')
  plt.figure(1)
  ax = sns.lineplot(x='nPumps', y='matchTimeMS',
        hue='Regex',
        style='Memoized',
        marker='o',
        data=df)
  ax.set_ylim(bottom=0, top=MAX_MATCH_MS)
  ax.set_xlim(left=0, right=maxPumpsForNonMemoMatchTime)

  ax.set_ylabel('Match time (ms)')
  ax.set_xlabel('Number of pumps')
  plt.title('Case studies with and without memoization')
  print("Saving to {}".format(caseStudy_fname))
  plt.savefig(fname=caseStudy_fname, bbox_inches='tight')

#####################################################

# Parse args
parser = argparse.ArgumentParser(description='Measure the dynamic costs of memoization -- the space and time costs of memoizing this set of regexes, as determined using the prototype engine.')
parser.add_argument('--regex-file', type=str, help='In: NDJSON file of objects containing libMemo.SimpleRegex objects (at least the key "pattern", and "evilInput" if you want an SL-specific analysis)', required=True,
  dest='regexFile')

# Parse args
args = parser.parse_args()

# Here we go!
main(args.regexFile)
