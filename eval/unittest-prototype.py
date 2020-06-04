#!/usr/bin/env python3
# Unit tests for the prototype

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
import itertools

# Shell dependencies

DEFAULT_SEMANTIC_TEST_SUITE = os.path.join(os.environ['MEMOIZATION_PROJECT_ROOT'], 'src-simple', 'test', 'semantic-behav.txt')
DEFAULT_PERF_TEST_SUITE = os.path.join(os.environ['MEMOIZATION_PROJECT_ROOT'], 'src-simple', 'test', 'perf-behav.txt')

shellDeps = [ libMemo.ProtoRegexEngine.CLI ]

# Other globals
SAVE_TMP_FILES = True

##########
class TestResult:
  """Result of a test
  
  success: Boolean
  failureMsg:  Str
  """
  def __init__(self, success, failureMsg):
    self.success = success
    self.failureMsg = failureMsg

class TestCase:
  """Test case"""
  def __init__(self):
    assert(False) # Use factory method
  
  @staticmethod
  def Factory(pieces, testType):
    if testType == TestSuite.SEMANTIC_TEST:
      return SemanticTestCase(pieces)
    elif testType == TestSuite.PERF_TEST:
      return PerformanceTestCase(pieces)
    assert(False)
  
  def run(self):
    """Returns TestResult[], one per attempted configuration"""
    # Subclass and overload
    assert(False)
  
  def _queryEngine(self, ss, es, regex, input):
    """Returns rawCmd, validSyntax, EngineMeasurements"""
    try:
      queryFile = libMemo.ProtoRegexEngine.buildQueryFile(regex, input)
      rawCmd = "{} {} {} '{}' {}".format(libMemo.ProtoRegexEngine.CLI,
            libMemo.ProtoRegexEngine.SELECTION_SCHEME.scheme2cox[ss],
            libMemo.ProtoRegexEngine.ENCODING_SCHEME.scheme2cox[es],
          regex, input
      )
      libLF.log("  Test case: {}".format(rawCmd))
      em = libMemo.ProtoRegexEngine.query(ss, es, queryFile)
      validSyntax = True
    except SyntaxError as err:
      validSyntax = False
      em = None
    
    if not SAVE_TMP_FILES:
      os.file.unlink(queryFile)
    
    return rawCmd, validSyntax, em

class SemanticTestCase(TestCase):
  def __init__(self, pieces):
    self.regex, self.input, self.result = pieces
    self.expectSyntaxError = (self.result == "SYNTAX")
    self.shouldMatch = (self.result == "MATCH")
    self.type = TestSuite.SEMANTIC_TEST
  
  def run(self):
    testResults = []

    # Semantics should be identical across all memoization treatments
    for selectionScheme, encodingScheme in itertools.product(
      libMemo.ProtoRegexEngine.SELECTION_SCHEME.scheme2cox.keys(),
      libMemo.ProtoRegexEngine.ENCODING_SCHEME.scheme2cox.keys()
    ):
      # Skip nonsense requests
      if libMemo.ProtoRegexEngine.SELECTION_SCHEME.scheme2cox[selectionScheme] == "none" and \
         libMemo.ProtoRegexEngine.ENCODING_SCHEME.scheme2cox[encodingScheme] != "none":
         continue

      rawCmd, validRegex, em = self._queryEngine(selectionScheme, encodingScheme, self.regex, self.input)

      # Calculate the TestResult
      if validRegex:
        if self.expectSyntaxError:
          tr = TestResult(False, "Incorrect, expected syntax error for /{}/".format(self.regex))
        else:
          if (em.matched and self.shouldMatch) or (not em.matched and not self.shouldMatch):
            tr = TestResult(True, "Correct, match(/{}/, {})={} under selection '{}' encoding '{}'".format(self.regex, self.input, em.matched, selectionScheme, encodingScheme))
          else:
            tr = TestResult(False, "Incorrect, match(/{}/, {})={} under selection {} encoding {} -- try {}".format(self.regex, self.input, em.matched, selectionScheme, encodingScheme, rawCmd))
      else: # Invalid regex
        if self.expectSyntaxError:
          tr = TestResult(True, "Correct, syntax error for /{}/".format(self.regex))
        else:
          tr = TestResult(False, "Incorrect, syntax error for /{}/".format(self.regex))
      
      testResults.append(tr)
    return testResults

class PerformanceTestCase(TestCase):
  CURVE_EXP = "exponential"
  CURVE_POLY = "polynomial"
  CURVE_LIN = "linear"

  def __init__(self, pieces):
    regex, evilInput, memo, curve = pieces
    self.regex = regex
    ei_pref, ei_pump, ei_suff = [p.strip() for p in evilInput.split(":")]
    self.evilInput = libLF.EvilInput().initFromRaw(
      True,
      [ libLF.PumpPair().initFromRaw(ei_pref, ei_pump) ],
      ei_suff
    )

    if memo == "NONE":
      self.memoSS = libMemo.ProtoRegexEngine.SELECTION_SCHEME.SS_None
    elif memo == "FULL":
      self.memoSS = libMemo.ProtoRegexEngine.SELECTION_SCHEME.SS_Full
    elif memo == "INDEG":
      self.memoSS = libMemo.ProtoRegexEngine.SELECTION_SCHEME.SS_InDeg
    elif memo == "ANCESTOR":
      self.memoSS = libMemo.ProtoRegexEngine.SELECTION_SCHEME.SS_Loop
    else:
      raise SyntaxError("Unexpected memo " + memo)

    if curve == "EXP":
      self.curve = PerformanceTestCase.CURVE_EXP
    elif curve == "POLY":
      self.curve = PerformanceTestCase.CURVE_POLY
    elif curve == "LIN":
      self.curve = PerformanceTestCase.CURVE_LIN
    else:
      raise SyntaxError("Unexpected curve " + curve)

  def run(self):
    testResults = []

    baselineVisits = None
    nAdjustedVisits = []
    maxPumpsExp = 10
    maxPumpsElse = 50
    maxPumps = maxPumpsExp if self.curve == PerformanceTestCase.CURVE_EXP else maxPumpsElse

    # Collect visit counts as we increase pump
    for nPumps in range(1, maxPumps):
      input = self.evilInput.build(nPumps)
      rawCmd, validRegex, em = self._queryEngine(self.memoSS, libMemo.ProtoRegexEngine.ENCODING_SCHEME.ES_None, self.regex, input)
      assert(validRegex)

      if not baselineVisits:
        assert(nPumps == 1)
        baselineVisits = em.si_nTotalVisits

      # Subtract off the "one pump" term so we can see the growth rate
      nAdjustedVisits.append(em.si_nTotalVisits - baselineVisits)
    
    # Confirm the visit counts indicate the expected curve
    matchedCurve = self._checkCurve(nAdjustedVisits, self.curve)
    return [
      TestResult(matchedCurve, "Error, expected curve {} but visits {}".format(self.curve, nAdjustedVisits))
    ]
  
  def _firstRatios(self, visitCounts):
    return [
        b / a if a > 0 else 2
        for a, b in zip(visitCounts, visitCounts[1:])
      ]

  def _firstDifferences(self, visitCounts):
    return [
        b - a
        for a, b in zip(visitCounts, visitCounts[1:])
      ]

  def _checkCurve(self, visitCounts, expectedCurve):
    """Returns True if curve fits, else False"""
    libLF.log("Expected {} curve -- visits {}".format(expectedCurve, visitCounts))
    firstRatios = self._firstRatios(visitCounts)
    firstDifferences  = self._firstDifferences(visitCounts)
    print("rats: " + str(firstRatios))
    print("diffs: " + str(firstDifferences) + " ({} unique, {} visitCounts)".format(len(set(firstDifferences)), len(visitCounts)))
    if expectedCurve == PerformanceTestCase.CURVE_EXP: 
      return min(firstRatios) >= 2
    elif expectedCurve == PerformanceTestCase.CURVE_POLY: 
      # Super-linear means that the number of visits grows more-than-linearly with the input
      # The behavior gets weird once REWBR are introduced, so be generous here
      return len(set(firstDifferences)) >= len(visitCounts)/2
    elif expectedCurve == PerformanceTestCase.CURVE_LIN:
      return len(set(firstDifferences)) == 1

class TestSuite:
  """A collection of test cases"""

  SEMANTIC_TEST = "semantics"
  PERF_TEST = "performance"

  def __init__(self, testSuiteFile, testsType):
    self.testSuiteFile = testSuiteFile
    self.type = testsType

    self.tests = self._loadTests()
  
  def _loadTests(self):
    libLF.log("Loading {} test suite from {}".format(self.type, self.testSuiteFile))

    tests = []
    with open(self.testSuiteFile, 'r') as inStream:
      for line in inStream:
        pieces = self._parse(line)
        if not pieces:
          continue
        tests.append(TestCase.Factory(pieces, self.type))
    libLF.log("Loaded {} {} test cases from {}".format(self.type, len(tests), self.testSuiteFile))
    return tests
  
  def _parse(self, line):
    """Parse line in test suite file
    
    Returns None or a list of the stripped, comma-separated pieces"""
    # Skip empty lines or comment lines
    if re.match(r'^\s*$', line) or re.match(r'^\s*#', line):
      return None
    preComment = line.split("#")[0]
    return [
      piece.strip()
      for piece in preComment.split(",")
    ]
  
  def run(self):
    """Run test cases and log results

    returns nFailures"""
    nTestCases = len(self.tests)
    testFailures = []
    nFailures = 0

    for i, testCase in enumerate(self.tests):
      libLF.log("Case {}/{}".format(i, nTestCases))

      anyFailures = False
      for testResult in testCase.run():
        if not testResult.success:
          testFailures.append(testResult.failureMsg)
          anyFailures = True

      if anyFailures:
        nFailures += 1

    if testFailures:
      libLF.log("{}/{} {} tests failed in some way:".format(nFailures, nTestCases, self.type))
      for failDescr in testFailures:
        libLF.log("  {}".format(failDescr))
    else:
      libLF.log("All {} {} tests passed".format(nTestCases, self.type))

    return nFailures

def main(semanticsTestsFile, semanticOnly, performanceTestsFile, perfOnly):
  libLF.log('semanticsTestsFile {} semanticOnly, performanceTestsFile {} perfOnly {}' \
    .format(semanticsTestsFile, semanticOnly, performanceTestsFile, perfOnly))

  #### Check dependencies
  libLF.checkShellDependencies(shellDeps)

  #### Load and run each test
  summary = [] 
  
  for testType, testsFile in [
    (TestSuite.SEMANTIC_TEST, semanticsTestsFile),
    (TestSuite.PERF_TEST, performanceTestsFile)
  ]:
    if perfOnly and testType != TestSuite.PERF_TEST:
      continue
    if semanticOnly and testType != TestSuite.SEMANTIC_TEST:
      continue

    libLF.log("Loading {} tests from {}".format(testType, testsFile))
    ts = TestSuite(testsFile, testType)

    libLF.log("Running {} tests".format(testType))
    nFailures = ts.run()
    summary.append("{} tests from {}: {} failures".format(testType, testsFile, nFailures))
  
  libLF.log("****************************************")
  for line in summary:
    libLF.log("  " + line)

#####################################################

# Parse args
parser = argparse.ArgumentParser(description='Unit test driver for the prototype')
parser.add_argument('--semanticsTestsFile', type=str, default=DEFAULT_SEMANTIC_TEST_SUITE, help='In: Test suite file of inputs and outputs. Format is described in the default file, {}'.format(DEFAULT_SEMANTIC_TEST_SUITE), required=False,
  dest='semanticsTestsFile')
parser.add_argument('--semanticOnly', default=False, action='store_true', help='Skip perf tests')
parser.add_argument('--performanceTestsFile', type=str, default=DEFAULT_PERF_TEST_SUITE, help='In: Test suite file of inputs and outputs. Format is described in the default file, {}'.format(DEFAULT_PERF_TEST_SUITE), required=False,
  dest='performanceTestsFile')
parser.add_argument('--perfOnly', default=False, action='store_true', help='Skip semantic tests')

# Parse args
args = parser.parse_args()

# Here we go!
main(args.semanticsTestsFile, args.semanticOnly, args.performanceTestsFile, args.perfOnly)
