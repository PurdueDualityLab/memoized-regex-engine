#!/usr/bin/env python3
# Measure the size of the various "phi" vertex subsets for selective memoization.
# This analysis can run single-node many-core.

# Import libMemo
import libMemo

# Import libLF
import os
import sys
sys.path.append(os.path.join(os.environ['MEMOIZATION_PROJECT_ROOT'], 'eval', 'lib'))
import libLF

# Other dependencies
import re
import json
import tempfile
import argparse
import traceback
import time

TMP_FILE_PREFIX = 'measure-phi-sizes-{}-{}'.format(time.time(), os.getpid())

shellDeps = [ libMemo.ProtoRegexEngine.CLI ]

##########

class MyTask(libLF.parallel.ParallelTask):
  def __init__(self, simpleRegex):
    self.simpleRegex = simpleRegex
  
  def run(self):
    try:
      libLF.log('Working on regex: /{}/'.format(self.simpleRegex.pattern))
      # Run the analysis
      self.msa = self._measurePhis(self.simpleRegex)
      # Return
      libLF.log('Completed regex /{}/'.format(self.simpleRegex.pattern))
      return self.msa
    except KeyboardInterrupt:
      raise
    except BaseException as err:
      libLF.log('Exception while testing regex /{}/: '.format(self.simpleRegex.pattern) + err)
      return err
    
  def _measurePhis(self, simpleRegex):
    """Returns MemoizationStaticAnalysis or raises an exception"""
    try:
      libLF.log('Measuring for regex: <{}>'.format(simpleRegex.pattern))

      # Create query file
      queryFile = libMemo.ProtoRegexEngine.buildQueryFile(simpleRegex.pattern, "a")

      # Collect information for each |phi|
      phi2size = {}
      
      for phi in libMemo.ProtoRegexEngine.SELECTION_SCHEME.scheme2cox.keys():
        if phi == libMemo.ProtoRegexEngine.SELECTION_SCHEME.SS_None:
          continue

        engMeas = libMemo.ProtoRegexEngine.query(
          phi, libMemo.ProtoRegexEngine.ENCODING_SCHEME.ES_None, queryFile
        )
        phi2size[phi] = engMeas.mi_results_nSelectedVertices
        libLF.log('Regex /{}/ had |phi={}| = {}'.format(simpleRegex.pattern, phi, phi2size[phi]))
      
      msa = libMemo.MemoizationStaticAnalysis()
      msa.initFromRaw(simpleRegex.pattern, phi2size)
      os.unlink(queryFile)
      return msa

    except BaseException as err:
      libLF.log('Exception while analyzing: err <{}> pattern /{}/'.format(err, simpleRegex.pattern))
      raise

################

def getTasks(regexFile):
  regexes = loadRegexFile(regexFile)
  tasks = [MyTask(regex) for regex in regexes]
  libLF.log('Prepared {} tasks'.format(len(tasks)))
  return tasks

def loadRegexFile(regexFile):
  """Return a list of SimpleRegex's"""
  regexes = []
  libLF.log('Loading regexes from {}'.format(regexFile))
  with open(regexFile, 'r') as inStream:
    for line in inStream:
      line = line.strip()
      if len(line) == 0:
        continue
      
      try:
        # Build a Regex
        regex = libMemo.SimpleRegex()
        regex.initFromNDJSON(line)
        regexes.append(regex)
      except KeyboardInterrupt:
        raise
      except BaseException as err:
        libLF.log('Exception parsing line:\n  {}\n  {}'.format(line, err))
        traceback.print_exc()

    libLF.log('Loaded {} regexes from {}'.format(len(regexes), regexFile))
    return regexes

################

def main(regexFile, outFile, parallelism):
  libLF.log('regexFile {} outFile {} parallelism {}' \
    .format(regexFile, outFile, parallelism))

  #### Check dependencies
  libLF.checkShellDependencies(shellDeps)

  #### Load data
  tasks = getTasks(regexFile)
  libLF.log('{} regexes'.format(len(tasks)))

  #### Process data

  # CPU-bound, no limits
  libLF.log('Submitting to map')
  results = libLF.parallel.map(tasks, parallelism,
    libLF.parallel.RateLimitEnums.NO_RATE_LIMIT, libLF.parallel.RateLimitEnums.NO_RATE_LIMIT,
    jitter=False)

  #### Emit results

  libLF.log('Writing results to {}'.format(outFile))
  nSuccesses = 0
  nExceptions = 0
  with open(outFile, 'w') as outStream:
    for msa in results:
        # Emit
        if type(msa) is libMemo.MemoizationStaticAnalysis:
          nSuccesses += 1
          outStream.write(msa.toNDJSON() + '\n')
        else:
          nExceptions += 1
          libLF.log("Error message: " + str(msa))
  libLF.log('Successfully performed MemoizationStaticAnalysis on {} regexes, {} exceptions'.format(nSuccesses, nExceptions))

  #### Analysis
  # TODO Any preliminary analysis

  return

#####################################################

# Parse args
parser = argparse.ArgumentParser(description='Measure |Q|, |Phi_indeg>1| and |Phi_loop| for a set of regexes, as determined using the prototype engine.')
parser.add_argument('--regex-file', type=str, help='In: NDJSON file of objects containing at least the key "pattern"', required=True,
  dest='regexFile')
parser.add_argument('--out-file', type=str, help='Out: File of libMemo.MemoizationStaticAnalysis objects', required=True,
  dest='outFile')
parser.add_argument('--parallelism', type=int, help='Maximum cores to use', required=False, default=libLF.parallel.CPUCount.CPU_BOUND,
  dest='parallelism')
args = parser.parse_args()

# Here we go!
main(args.regexFile, args.outFile, args.parallelism)
