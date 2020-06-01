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

DEFAULT_TEST_SUITE = os.path.join(os.environ['MEMOIZATION_PROJECT_ROOT'], 'src-simple', 'test', 'query-response.txt')

shellDeps = [ libMemo.ProtoRegexEngine.CLI ]

# Other globals
SAVE_TMP_FILES = True

##########

class TestCase:
  """Test case"""
  def __init__(self, line):
    self.regex, self.input, self.result = [piece.strip() for piece in line.split(",")]
    self.shouldMatch = (self.result == "MATCH")
  
  def run(self):
    """Returns SUCCESS, DESCRIPTION"""
    # Under all configurations
    for selectionScheme, encodingScheme in itertools.product(
      libMemo.ProtoRegexEngine.SELECTION_SCHEME.scheme2cox.keys(),
      libMemo.ProtoRegexEngine.ENCODING_SCHEME.scheme2cox.keys()
    ):
      queryFile = libMemo.ProtoRegexEngine.buildQueryFile(self.regex, self.input)
      em = libMemo.ProtoRegexEngine.query(selectionScheme, encodingScheme, queryFile)

      if (em.matched and self.shouldMatch) or (not em.matched and not self.shouldMatch):
        return True, "Correct, match(/{}/, {})={} under selection '{}' encoding '{}'".format(self.regex, self.input, em.matched, selectionScheme, encodingScheme)
      else:
        return False, "Incorrect, match(/{}/, {})={} under selection {} encoding {} -- try {} {} {} {} {}".format(
          self.regex, self.input, em.matched, selectionScheme, encodingScheme,
          libMemo.ProtoRegexEngine.CLI,
            libMemo.ProtoRegexEngine.SELECTION_SCHEME.scheme2cox[selectionScheme],
            libMemo.ProtoRegexEngine.ENCODING_SCHEME.scheme2cox[encodingScheme],
          self.regex, self.input,
          )

class TestSuite:
  """Test cases from the test suite"""
  def __init__(self, testSuiteFile):
    self.testCases = []
    self._loadTestSuite(testSuiteFile)
  
  def getTestCases(self):
    return self.testCases
  
  def _loadTestSuite(self, testSuiteFile):
    libLF.log("Loading test suite from {}".format(testSuiteFile))
    with open(testSuiteFile, 'r') as inStream:
      for line in inStream:
        # Skip empty lines or comment lines
        if re.match(r'^\s*$', line) or re.match(r'^\s*#', line):
          continue
        # Remove trailing comments
        line = line.split("#")[0]
        tc = TestCase(line)
        self.testCases.append(tc)
    libLF.log("Loaded {} test cases".format(len(self.testCases)))

def main(queryFile):
  libLF.log('queryFile {}' \
    .format(queryFile))

  #### Check dependencies
  libLF.checkShellDependencies(shellDeps)

  testSuite = TestSuite(queryFile)

  testFailures = []

  # Try all test cases
  for testCase in testSuite.getTestCases():
    success, descr = testCase.run()
    if not success:
      testFailures.append(descr)
  
  #### Emit results
  if testFailures:
    libLF.log("{}/{} tests failed in some way:".format(len(testFailures), len(testSuite.getTestCases())))
    for failDescr in testFailures:
      libLF.log("  {}".format(failDescr))
  else:
    libLF.log("All {} tests passed".format(len(testSuite.getTestCases())))

#####################################################

# Parse args
parser = argparse.ArgumentParser(description='Unit test driver for the prototype')
parser.add_argument('--queryFile', type=str, default=DEFAULT_TEST_SUITE, help='In: Test suite file of inputs and outputs. Format is described in the default file, {}'.format(DEFAULT_TEST_SUITE), required=False,
  dest='queryFile')

# Parse args
args = parser.parse_args()

# Here we go!
main(args.queryFile)
