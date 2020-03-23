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
import statistics

# Shell dependencies
shellDeps = [ libMemo.ProtoRegexEngine.CLI ]

# Other globals
MATCH_TIMEOUT = 2 # Seconds
GROWTH_RATE_INF = "INF"

SAVE_TMP_FILES = True

EXPAND_EVIL_INPUT = True

##########

class MyTask(libLF.parallel.ParallelTask): # Not actually parallel, but keep the API
  NOT_SL = "NOT_SL"

  # Take 5 samples per evil input
  # Need at least 3 to compute second derivative
  # But keep values low to avoid too-long backtracking stacks
  PUMPS_TO_TRY = [ i*3 for i in range(1,5) ]

  # Can be much longer because memoization prevents geometric growth of the backtracking stack
  PERF_PUMPS_TO_TRY = [ i*500 for i in range(1,5) ]

  def __init__(self, regex, nTrialsPerCondition):
    self.regex = regex
    self.nTrialsPerCondition = nTrialsPerCondition
  
  def run(self):
    try:
      libLF.log('Working on regex: /{}/'.format(self.regex.pattern))

      # Filter out non-SL regexes
      libLF.log("  1. Confirming that regex is SL")
      ei, growthRate = self._findMostSLInput(self.regex)
      if ei is None:
        return MyTask.NOT_SL
      
      # Run the analysis
      # Use this EI to obtain per-pump SL MDAs
      libLF.log("  2. Running analysis on SL regex")
      self.pump_to_mdas = self._runSLDynamicAnalysis(self.regex, ei, self.nTrialsPerCondition)

      # TODO Also need something for non-SL regexes

      # Return
      libLF.log('Completed regex /{}/'.format(self.regex.pattern))
      return self.pump_to_mdas
    except KeyboardInterrupt:
      raise
    except BaseException as err:
      libLF.log('Exception while testing regex /{}/'.format(self.regex.pattern))
      return err
  
  def _measureCondition(self, regex, mostEI, nPumps, selectionScheme, encodingScheme, nTrialsPerCondition):
    """Obtain the average time and space costs for this condition
    
    Returns: time (numeric), space (numeric)
    """
    queryFile = libMemo.ProtoRegexEngine.buildQueryFile(regex.pattern, mostEI.build(nPumps))

    measures = []
    for i in range(0, nTrialsPerCondition):
      try:
        meas = libMemo.ProtoRegexEngine.query(selectionScheme, encodingScheme, queryFile, timeout=30)
        measures.append(meas)
      except BaseException:
        libLF.log("Error, a timeout should not have occurred with memoization in place")
        raise

    indivTimeCosts = [ meas.si_simTimeUS for meas in measures ]
    indivSpaceCosts = [
      sum(meas.mi_results_maxObservedCostPerVertex)
      for meas in measures
    ]

    # Space costs should be constant
    assert(min(indivSpaceCosts) == max(indivSpaceCosts))

    # Let's check that time costs do not vary too much, warn if it's too high
    time_coefficientOfVariance = statistics.stdev(indivTimeCosts) / statistics.mean(indivTimeCosts)
    libLF.log("Time coefficient of variance: {}".format(round(time_coefficientOfVariance, 2)))
    if 0.5 < time_coefficientOfVariance:
      libLF.log("Warning, time CV {} was >= 0.5".format(time_coefficientOfVariance))

    # Condense
    time = statistics.median_low(indivTimeCosts)
    space = statistics.median_low(indivSpaceCosts)
    
    return time, space
  
  def _runSLDynamicAnalysis(self, regex, mostEI, nTrialsPerCondition):
    """Obtain MDAs for this <regex, EI> pair
    
    returns: pump_to_libMemo.MemoizationDynamicAnalysis
    """

    selectionScheme_to_encodingScheme2engineMeasurement = {}

    nonMemoizationSelectionSchemes = [ libMemo.ProtoRegexEngine.SELECTION_SCHEME.SS_None ]

    selectionSchemes = [
      scheme
      for scheme in libMemo.ProtoRegexEngine.SELECTION_SCHEME.scheme2cox.keys()
      if scheme not in nonMemoizationSelectionSchemes
    ]
    encodingSchemes = libMemo.ProtoRegexEngine.ENCODING_SCHEME.scheme2cox.keys()

    # How many experimental conditions?
    nConditions = len(selectionSchemes) * len(encodingSchemes)
    libLF.log("    {} experimental conditions".format(nConditions))

    # Obtain engine measurements for each combination of the
    # memoization selection and encoding schemes
    pump_to_mda = {}
    for nPumps in MyTask.PERF_PUMPS_TO_TRY:
      conditionIx = 1

      # Prep an MDA
      mda = libMemo.MemoizationDynamicAnalysis()
      mda.pattern = regex.pattern
      mda.inputLength = len(mostEI.build(nPumps))
      mda.evilInput = mostEI
      mda.nPumps = nPumps

      for selectionScheme in selectionSchemes:
        selectionScheme_to_encodingScheme2engineMeasurement[selectionScheme] = {}
        for encodingScheme in encodingSchemes:
          libLF.log("    Trying selection/encoding combination {}/{}".format(conditionIx, nConditions))
          conditionIx += 1

          timeCost, spaceCost = self._measureCondition(regex, mostEI, nPumps, selectionScheme, encodingScheme, nTrialsPerCondition)

          mda.selectionPolicy_to_enc2time[selectionScheme][encodingScheme] = timeCost
          mda.selectionPolicy_to_enc2space[selectionScheme][encodingScheme] = spaceCost

          # Did we screw up?
          assert(mda.validate())

          pump_to_mda[nPumps] = mda
    return pump_to_mda
  
  def _findMostSLInput(self, regex):
    """Of a regex's evil inputs, identify the one that yields the MOST SL behavior.

    returns: libLF.EvilInput, itsGrowthRate
             None, -1 if all are linear-time (e.g. RE1's semantics differ from PCRE)
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
      pump2meas = {}
      for nPumps in MyTask.PUMPS_TO_TRY:
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
      for i_pump, j_pump in zip(MyTask.PUMPS_TO_TRY[1:], MyTask.PUMPS_TO_TRY[2:]):
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

################

def getTasks(regexFile, nTrialsPerCondition):
  regexes = loadRegexFile(regexFile)
  tasks = [MyTask(regex, nTrialsPerCondition) for regex in regexes]
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

def main(regexFile, nTrialsPerCondition, outFile):
  libLF.log('regexFile {} nTrialsPerCondition {} outFile {}' \
    .format(regexFile, nTrialsPerCondition, outFile))

  #### Check dependencies
  libLF.checkShellDependencies(shellDeps)

  #### Load data
  tasks = getTasks(regexFile, nTrialsPerCondition)
  nRegexes = len(tasks)

  #### Process data

  results = [] # List of results -- libMemo.MemoizationDynamicAnalysis | False | BaseException
  for t in tasks:
    try:
      res = t.run()
      if type(res) is not type({}) and \
         type(res) != MyTask.NOT_SL:
        libLF.log("Exception on /{}/: {}".format(t.regex.pattern, res))

      results.append(res)
    except BaseException as err:
      libLF.log("Exception on /{}/: {}".format(t.regex.pattern, err))
      results.append(err)

  #### Emit results

  # TODO It will be *way* cheaper to turn this into a Pandas dataframe
  # Let's do this during the RunTask() to minimize space costs
  # Do this conversion tomorrow.

  # TODO I find the negative space cost estimate hard to believe for the 1.json file.
  # Double-check these.

  libLF.log('Writing results to {}'.format(outFile))
  nSL = 0
  nNonSL = 0
  nExceptions = 0
  with open(outFile, 'w') as outStream:
    for pump2mda in results:
        # Emit
        if type(pump2mda) is type({}):
          nSL += 1
          for mda in pump2mda.values():
            outStream.write(mda.toNDJSON() + '\n')
        elif pump2mda == MyTask.NOT_SL:
          nNonSL += 1
        else:
          nExceptions += 1
          libLF.log("Error message: " + str(pump2mda))

  libLF.log('{} regexes were SL, {} were linear, {} exceptions'.format(nSL, nNonSL, nExceptions))

#####################################################

# Parse args
parser = argparse.ArgumentParser(description='Measure the dynamic costs of memoization -- the space and time costs of memoizing this set of regexes, as determined using the prototype engine.')
parser.add_argument('--regex-file', type=str, help='In: NDJSON file of objects containing libMemo.SimpleRegex objects (at least the key "pattern", and "evilInput" if you want an SL-specific analysis)', required=True,
  dest='regexFile')
parser.add_argument('--trials', type=str, help='In: Number of trials per experimental condition (only affects time complexity)', required=False, default=20,
  dest='nTrialsPerCondition')
parser.add_argument('--out-file', type=str, help='Out: File of libMemo.MemoizationDynamicAnalysis objects', required=True,
  dest='outFile')

# Parse args
args = parser.parse_args()

# Here we go!
main(args.regexFile, args.nTrialsPerCondition, args.outFile)
