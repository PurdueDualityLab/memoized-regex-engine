#!/usr/bin/env python3
# Test a libLF.SimpleRegex for SL behavior under various memoization schemes.
# This analysis includes time measurements for performance.
# Run it alone, or use core pinning (e.g. taskset) to reduce interference

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

# Shell dependencies
shellDeps = [ libMemo.ProtoRegexEngine.CLI ]

# Other globals
MATCH_TIMEOUT = 2 # Seconds
GROWTH_RATE_INF = "INF"

SAVE_TMP_FILES = True

EXPAND_EVIL_INPUT = True

##########

class MyTask(libLF.parallel.ParallelTask): # Not actually parallel, but keep the API
  def __init__(self, regex, powerPumps):
    self.regex = regex
    self.powerPumps = powerPumps
  
  def run(self):
    try:
      libLF.log('Working on regex: /{}/'.format(self.regex.pattern))

      # Sanity check: how many have the same semantics as they do in PCRE?
      ei, growthRate = self._findMostSLInput(self.regex)
      return ei is not None
      # TODO: Now measure costs under various modes using the highest-cost EI
      # First, as a guide, let's whiteboard the figures we might show

      # Run the analysis
      self.slra = self._measureCosts(self.regex)
      # Return
      libLF.log('Completed regex /{}/'.format(self.regex.pattern))
      return self.slra
    except KeyboardInterrupt:
      raise
    except BaseException as err:
      libLF.log('Exception while testing regex /{}/'.format(self.regex.pattern))
      return err
  
  def _findMostSLInput(self, regex):
    """Of a regex's evil inputs, identify the one that yields the MOST SL behavior.

    returns: libLF.EvilInput
             None if all are linear-time (i.e. RE1's semantics differ from PCRE)
    """
    libLF.log('Testing whether this regex exhibits SL behavior: /{}/'.format(regex.pattern))
    assert(regex.evilInputs)

    if EXPAND_EVIL_INPUT:
      # Expand the evil inputs.
      # The longer expansions may have higher growth rates (larger polynomials),
      #   but may be buggy and not trigger mismatches properly.
      expandedEIs = []
      for ei in regex.evilInputs:
        expandedEIs += ei.expand()
      libLF.log("Considering {} EvilInput's, expanded from {}".format(len(expandedEIs), len(regex.evilInputs)))
      evilInputs = expandedEIs
    else:
      evilInputs = regex.evilInputs

    eiWithLargestGrowthRate = None
    eiLargestGrowthRate = -1

    for i, ei in enumerate(evilInputs):
      libLF.log("Checking evil input {}/{}:\n  {}".format(i, len(evilInputs), ei.toNDJSON()))
      # Take 5 samples per evil input
      # Need at least 3 to compute second derivative
      pumps = [i*3 for i in range(1,5)]
      pump2meas = {}
      for nPumps in pumps:
        queryFile = libMemo.ProtoRegexEngine.buildQueryFile(regex.pattern, ei.build(nPumps))
        try:
          pump2meas[nPumps] = libMemo.ProtoRegexEngine.query(
            libMemo.ProtoRegexEngine.SELECTION_SCHEME.SS_None, libMemo.ProtoRegexEngine.ENCODING_SCHEME.ES_None, queryFile,
            timeout=MATCH_TIMEOUT 
          )
        except subprocess.TimeoutExpired:
          # If we timed out, that's about as significant a growth rate as can be expected
          if not SAVE_TMP_FILES:
            os.unlink(queryFile)
          libLF.log("SL regex: /{}/".format(regex.pattern))
          return ei, GROWTH_RATE_INF

        # libLF.log("{} pumps, {} visits".format(nPumps, pump2meas[nPumps].si_nTotalVisits))
        if not SAVE_TMP_FILES:
          os.unlink(queryFile)
    
      # Compute growth rates -- first derivative
      growthRates = []
      for i_pump, j_pump in zip(pumps[1:], pumps[2:]):
        growthRates.append( pump2meas[j_pump].si_nTotalVisits - pump2meas[i_pump].si_nTotalVisits )
      
      # If the growth rates are strictly monotonically increasing, then we have super-linear growth
      # (i.e. we're looking for a positive second derivative -- acceleration!)
      growthIsSuperLinear = True
      for g1, g2 in zip(growthRates, growthRates[1:]):
        if g1 >= g2:
          libLF.log('Not super-linear growth. Successive rates were {}, {}'.format(g1, g2))
          growthIsSuperLinear = False
          break
      
      # Is this the largest of the observed growth rates?
      if growthIsSuperLinear:
        largestGrowth = growthRates[-1] - growthRates[-2]
        if largestGrowth > eiLargestGrowthRate:
          eiWithLargestGrowthRate = ei
          eiLargestGrowthRate = largestGrowth

    if eiWithLargestGrowthRate:
      libLF.log("SL regex: /{}/".format(regex.pattern))
    else:
      libLF.log("False SL regex: /{}/".format(regex.pattern))

    return eiWithLargestGrowthRate, eiLargestGrowthRate

  def _measureCosts(self, regex):
    """Returns MemoizationDynamicAnalysis or raises an exception"""
    raise BaseException("TODO")
    try:
      libLF.log('Measuring for regex: <{}>'.format(regex.pattern))
      # Create query file
    except:
      raise
    return None

################

def getTasks(regexFile, powerPumps):
  regexes = loadRegexFile(regexFile)
  tasks = [MyTask(regex, powerPumps) for regex in regexes]
  libLF.log('Prepared {} tasks'.format(len(tasks)))
  return tasks

def loadRegexFile(regexFile):
  """Return a list of libMemo.SimpleRegex's"""
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

def main(regexFile, outFile, powerPumps):
  libLF.log('regexFile {} outFile {} powerPumps {}' \
    .format(regexFile, outFile, powerPumps))

  #### Check dependencies
  libLF.checkShellDependencies(shellDeps)

  #### Load data
  tasks = getTasks(regexFile, powerPumps)
  nRegexes = len(tasks)

  #### Process data

  results = []
  for t in tasks:
    try:
      res = t.run()
      if type(res) is not type(True):
        libLF.log("Exception on /{}/: {}".format(t.regex.pattern, res))

      results.append(res)
    except BaseException as err:
      libLF.log("Exception on /{}/: {}".format(t.regex.pattern, err))
      results.append(err)

  #### Emit results

  libLF.log('Writing results to {}'.format(outFile))
  nSL = 0
  nNonSL = 0
  nExceptions = 0
  with open(outFile, 'w') as outStream:
    for res in results:
        # Emit
        if type(res) is type(True):
          if res:
            nSL += 1
          else:
            nNonSL += 1
        else:
          nExceptions += 1

  libLF.log('{} regexes were SL, {} were linear, {} exceptions'.format(nSL, nNonSL, nExceptions))

#####################################################

# Parse args
parser = argparse.ArgumentParser(description='Test a set of libLF.Regex\'s for SL behavior. Regexes are tested in every supported language.')
parser.add_argument('--regex-file', type=str, help='In: File of libLF.Regex objects', required=True,
  dest='regexFile')
parser.add_argument('--out-file', type=str, help='Out: File of libLF.SLRegexAnalysis objects', required=True,
  dest='outFile')
parser.add_argument('--power-pumps', type=int, help='Number of pumps to trigger power-law SL behavior (e.g. quadratic)', required=False, default=500000,
  dest='powerPumps')
args = parser.parse_args()

# Here we go!
main(args.regexFile, args.outFile, args.powerPumps)
