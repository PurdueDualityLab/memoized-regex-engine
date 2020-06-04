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
  def __init__(self, success, description):
    self.success = success
    self.description = description

class TestCase:
  """Test case"""
  def __init__(self):
    assert(False) # Use factory method
  
  @staticmethod
  def Factory(line, testType):
    if testType == TestSuite.SEMANTIC_TEST:
      return SemanticTestCase(line)
    elif testType == TestSuite.PERF_TEST:
      return PerformanceTestCase(line)
    assert(False)
  
  def run(self):
    """Returns [SUCCESS, DESCRIPTION] for each configuration to try"""
    # Subclass and overload
    assert(False)
  
  def _queryEngine(self, ss, es, regex, input):
    """Returns rawCmd, validSyntax, EngineMeasurements"""
    try:
      queryFile = libMemo.ProtoRegexEngine.buildQueryFile(self.regex, self.input)
      rawCmd = "{} {} {} '{}' {}".format(libMemo.ProtoRegexEngine.CLI,
            libMemo.ProtoRegexEngine.SELECTION_SCHEME.scheme2cox[ss],
            libMemo.ProtoRegexEngine.ENCODING_SCHEME.scheme2cox[es],
          self.regex, self.input
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
  def __init__(self, line):
    self.regex, self.input, self.result = [piece.strip() for piece in line.split(",")]
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
  def __init__(self, line):
    reg, evilInput, memo, curve = [piece.strip() for piece in line.split(",")]

  def run(self):
    return [TestResult(False, "TODO")]

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
        line = self._removeComments(line)
        if not line:
          continue
        tests.append(TestCase.Factory(line, self.type))
    libLF.log("Loaded {} {} test cases from {}".format(self.type, len(tests), self.testSuiteFile))
    return tests
  
  def _removeComments(self, line):
    """Remove comments from line in test suite file
    
    Returns None or a stripped line"""
    # Skip empty lines or comment lines
    if re.match(r'^\s*$', line) or re.match(r'^\s*#', line):
      return None
    return line.split("#")[0]
  
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
          testFailures.append(testResult.description)
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

def main(semanticsTestsFile, performanceTestsFile):
  libLF.log('semanticsTestsFile {} performanceTestsFile {}' \
    .format(semanticsTestsFile, performanceTestsFile))

  #### Check dependencies
  libLF.checkShellDependencies(shellDeps)

  #### Load and run each test
  summary = [] 
  
  for testType, testsFile in [
    (TestSuite.SEMANTIC_TEST, semanticsTestsFile), (TestSuite.PERF_TEST, performanceTestsFile)
  ]:
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
parser.add_argument('--performanceTestsFile', type=str, default=DEFAULT_PERF_TEST_SUITE, help='In: Test suite file of inputs and outputs. Format is described in the default file, {}'.format(DEFAULT_PERF_TEST_SUITE), required=False,
  dest='performanceTestsFile')

# Parse args
args = parser.parse_args()

# Here we go!
main(args.semanticsTestsFile, args.performanceTestsFile)
