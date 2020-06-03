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
MAX_MATCH_MS = 5*1000
NTRIALS = 3 # Take the median of this many trials

class CaseStudy:
  expPumps = list(range(1,35,1))
  polyPumps = [1,2,3,5,8] + list(range(25,1000,25))
  linPumps = [1,2,3,5,8] + list(range(100,1000,100))

  def __init__(self, line):
    obj = json.loads(line)
    self.nick = obj['nick']
    self.title = obj['title']
    self.unmemoBehav = obj['unmemoBehav']
    self.memoBehav = obj['memoBehav']
    self.regex = libMemo.SimpleRegex()
    self.regex.initFromNDJSON(line)
  
  def _collectData(self, pumps, doMemo):
    matchTimes = []

    if doMemo:
      ss = libMemo.ProtoRegexEngine.SELECTION_SCHEME.SS_Full
    else:
      ss = libMemo.ProtoRegexEngine.SELECTION_SCHEME.SS_None
    es = libMemo.ProtoRegexEngine.ENCODING_SCHEME.ES_None

    matchTimeMS = 0
    for pump in pumps:
      if matchTimeMS > MAX_MATCH_MS:
        break

      times = []
      # Query
      libLF.log("nick {} nPumps {}".format(self.nick, pump))
      queryFile = libMemo.ProtoRegexEngine.buildQueryFile(self.regex.pattern, self.regex.evilInputs[0].build(pump))
      for i in range(NTRIALS):
        em = libMemo.ProtoRegexEngine.query(ss, es, queryFile)
        matchTimeMS = max(int(em.si_simTimeUS/1000), 10)
        times.append(matchTimeMS)
      med = statistics.median(times)
      os.unlink(queryFile)

      # Record
      matchTimes.append(med)
      libLF.log("nPumps {} matchTimeMS {}".format(pump, matchTimes[-1]))
    return matchTimes

  def run(self):
    """Plot the "growth curve" for this case study, with and without memoization

    Returns: name of file
    """
    # Track these for the second pass

    nonMemoMatchTimes = []
  
    if self.unmemoBehav == "exp":
      unmemoPumps = CaseStudy.expPumps
    elif self.unmemoBehav == "poly":
      unmemoPumps = CaseStudy.polyPumps
    elif self.unmemoBehav == "lin":
      unmemoPumps = CaseStudy.linPumps

    if self.memoBehav == "exp":
      memoPumps = CaseStudy.expPumps
    elif self.memoBehav == "poly":
      memoPumps = CaseStudy.polyPumps
    elif self.memoBehav == "lin":
      memoPumps = CaseStudy.linPumps

    unmemoMatchTimes = self._collectData(unmemoPumps, False)
    memoMatchTimes = self._collectData(memoPumps, True)

    ret = []
    for np, mt in zip(unmemoPumps, unmemoMatchTimes):
      ret.append({"Memoized": False, "nPumps": np, "matchTimeS": mt/1000})
    for np, mt in zip(memoPumps, memoMatchTimes):
      ret.append({"Memoized": True, "nPumps": np, "matchTimeS": mt/1000})
    
    #### DataFrame
    df = pd.DataFrame(data=ret)
    print(df.groupby("Memoized").describe())

    #### Emit results
    caseStudy_fname = os.path.join('figs', 'caseStudy-{}.pdf'.format(self.nick))
    plt.figure()
    ax = sns.lineplot(x="nPumps", y="matchTimeS", hue="Memoized", hue_order=[False, True], marker='o', data=df)
    ax.set_ylim(bottom=0, top=MAX_MATCH_MS/1000)
    if self.memoBehav == "lin":
      ax.set_xlim(left=0, right=250)
    elif self.memoBehav == "poly":
      pass

    plt.ylabel('Match time (s)', fontsize=22)
    plt.yticks(fontsize=20)
    plt.xlabel('Number of pumps (string length)', fontsize=22)
    plt.xticks(fontsize=20)
    plt.title(self.title, fontsize=24)
    ax.legend(loc="lower right", fontsize=20)
    print("Saving to {}".format(caseStudy_fname))
    plt.savefig(fname=caseStudy_fname, bbox_inches='tight')

    return caseStudy_fname

def loadCaseStudies(caseStudyFile):
  """Return CaseStudy[]"""
  caseStudies = []
  libLF.log('Loading caseStudies from {}'.format(caseStudyFile))
  with open(caseStudyFile, 'r') as inStream:
    for line in inStream:
      line = line.strip()
      if len(line) == 0:
        continue
      
      try:
        # Build a Regex
        caseStudies.append(CaseStudy(line))
      except KeyboardInterrupt:
        raise
      except BaseException as err:
        libLF.log('Exception parsing line:\n  {}\n  {}'.format(line, err))
        traceback.print_exc()

    libLF.log('Loaded {} caseStudies from {}'.format(len(caseStudies), caseStudyFile))
    return caseStudies

################

def main(caseStudyFile):
  libLF.log('caseStudyFile {}' \
    .format(caseStudyFile))

  #### Check dependencies
  libLF.checkShellDependencies(shellDeps)

  #### Load data
  caseStudies = loadCaseStudies(caseStudyFile)

  #### Run
  studiesToRun = ["REWBR-2", "Microsoft", "Cloudflare"]
  nick2fname = {}
  for caseStudy in caseStudies:
    if caseStudy.nick in studiesToRun:
      nick2fname[caseStudy.nick] = caseStudy.run()
  for nick, fname in nick2fname.items():
    libLF.log("Case study {} -- See {}".format(nick, fname))


#####################################################

# Parse args
parser = argparse.ArgumentParser(description='Measure the dynamic costs of memoization -- the space and time costs of memoizing this set of regexes, as determined using the prototype engine.')
parser.add_argument('--caseStudy-file', type=str, help='In: NDJSON file of objects containing CaseStudy objects (keys: nick, title, unmemoBehav, memoBehav, pattern, evilInputs)', required=True,
  dest='caseStudyFile')

# Parse args
args = parser.parse_args()

# Here we go!
main(args.caseStudyFile)
