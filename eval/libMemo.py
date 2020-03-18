"""Memoization: utils associated with memoization experiments
"""

# Import libLF
import os
import sys
sys.path.append(os.path.join(os.environ['MEMOIZATION_PROJECT_ROOT'], 'eval', 'lib'))
import libLF

# Other imports
import json
import tempfile

###
# Constants
###

class ProtoRegexEngine:
    """One stop shop for interacting with the Prototype Regex Engine
    
    Don't instantiate this. Everything is static.
    """
    CLI = os.path.join(os.environ['MEMOIZATION_PROJECT_ROOT'], "src-simple", "re")

    class SELECTION_SCHEME:
        SS_None = "no memoization"
        SS_Full = "full memoization"
        SS_InDeg = "selective: indeg>1"
        SS_Loop = "selective: loop"

        scheme2cox = {
            SS_None: "none",
            SS_Full: "full",
            SS_InDeg: "indeg",
            SS_Loop: "loop",
        }

    class ENCODING_SCHEME:
        ES_None = "no encoding"
        ES_Negative = "negative encoding"
        ES_RLE = "RLE"

        scheme2cox = {
            ES_None: "none",
            ES_Negative: "neg",
            ES_RLE: "rle",
        }

    @staticmethod
    def buildQueryFile(pattern, input, filePrefix="protoRegexEngineQueryFile-"):
        """Build a query file
        
        pattern: string
        input: string
        [filePrefix]: string
        
        returns: tmp fileName. Caller should unlink.
        """
        fd, name = tempfile.mkstemp(suffix=".json", prefix=filePrefix)
        os.close(fd)
        with open(name, 'w') as outStream:
            json.dump({
                "pattern": pattern,
                "input": input,
            }, outStream)
        return name

    @staticmethod
    def query(selectionScheme, encodingScheme, queryFile):
        """Query the engine

        selectionScheme: SELECTION_SCHEME
        encodingScheme: ENCODING_SCHEME
        queryFile: file path

        returns: EngineMeasurements
        throws: on rc != 0
        """
        rc, stdout, stderr = libLF.runcmd_OutAndErr(' '.join(
            [ProtoRegexEngine.CLI,
            ProtoRegexEngine.SELECTION_SCHEME.scheme2cox[selectionScheme],
            ProtoRegexEngine.ENCODING_SCHEME.scheme2cox[encodingScheme],
            '-f', queryFile]
        ))
        # libLF.log("Queried engine (rc {})".format(rc))
        # libLF.log(stdout)
        # libLF.log(stderr)
        if rc != 0:
            raise BaseException('Invocation failed; rc {} stdout\n  {}\n\nstderr\n  {}'.format(rc, stdout, stderr))
        return ProtoRegexEngine.EngineMeasurements(stderr)
    
    class EngineMeasurements:
        """Engine measurements
        
        This is a Python-native version of the JSON object 
        emitted by the regex engine.
        It offers some assurance of type safety.
        """
        def __init__(self, measAsJSON):
            obj = json.loads(measAsJSON)
            self._unpackInputInfo(obj['inputInfo'])
            self._unpackMemoizationInfo(obj['memoizationInfo'])
            self._unpackSimulationInfo(obj['simulationInfo'])
        
        def _unpackInputInfo(self, dict):
            self.ii_lenW = dict['lenW']
            self.ii_nStates = dict['nStates']

        def _unpackMemoizationInfo(self, dict):
            self.mi_config_encoding = dict['config']['encoding']
            self.mi_config_vertexSelection = dict['config']['vertexSelection']

            self.mi_results = dict['results']['maxObservedCostPerMemoizedVertex']
            self.mi_results_nSelectedVertices = dict['results']['nSelectedVertices']
            self.mi_results_lenW = dict['results']['lenW']

        def _unpackSimulationInfo(self, dict):
            self.si_nTotalVisits = dict['nTotalVisits']
            self.si_simTimeUS = dict['simTimeUS']
            self.si_visitsToMostVisitedSearchState = dict['visitsToMostVisitedSearchState']
            self.si_nPossibleTotalVisitsWithMemoization = dict['nPossibleTotalVisitsWithMemoization']
            self.si_visitsToMostVisitedSearchState = dict['visitsToMostVisitedSearchState']

###
# Classes
###

class SimpleRegex:
  """Simple regex for use with a memoized regex engine.
     Can be pattern ("all") or pattern+evilInput ("SL")
  """
  def __init__(self):
    return
  
  def initFromNDJSON(self, line):
    obj = json.loads(line)
    self.pattern = obj['pattern']
    if 'evilInput' in obj:
      ei = libLF.evilInput()
      ei.initFromDict(obj['evilInput'])
      self.evilInput = ei

    return self

class MemoizationStaticAnalysis:
  """Represents the result of regex pattern static analysis for memoization purposes"""
  MEMO_POLICIES_TO_COXRE_NICKNAMES = {
    "memoNone": "none",
    "memoAll": "full",
    "memoInDegGT1": "indeg",
    "memoLoop": "loop",
  }

  def __init__(self):
    self.pattern = None
    self.policy2nSelectedVertices = {}
  
  def initFromRaw(self, pattern, policy2nSelectedVertices):
    self.pattern = pattern

    self.policy2nSelectedVertices = policy2nSelectedVertices
    # All memoization policies measured?
    s1 = set(policy2nSelectedVertices.keys())
    s2 = set(policy2nSelectedVertices.keys())
    assert(s1 <= s2 and s2 <= s1)
    
    return self
  
  def initFromNDJSON(self, jsonStr):
    obj = libLF.fromNDJSON(jsonStr)
    return self.initFromDict(obj)

  def initFromDict(self, obj):
    self.pattern = obj['pattern']
    self.policy2nSelectedVertices = obj['policy2nSelectedVertices']
    return self
  
  def toNDJSON(self):
    _dict = {
      'pattern': self.pattern,
      'policy2nSelectedVertices': self.policy2nSelectedVertices
    }
    return json.dumps(_dict)
  