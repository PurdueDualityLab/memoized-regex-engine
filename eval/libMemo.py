"""Memoization: utils associated with memoization experiments
"""

# Import libLF
import os
import sys
sys.path.append(os.path.join(os.environ['MEMOIZATION_PROJECT_ROOT'], 'eval', 'lib'))
import libLF

# Other imports
import json
import re
import tempfile
import pandas as pd

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

        all = scheme2cox.keys()
        allMemo = [ SS_Full, SS_InDeg, SS_Loop ]

    class ENCODING_SCHEME:
        ES_None = "no encoding"
        ES_Negative = "negative encoding"
        ES_RLE = "RLE"
        ES_RLE_TUNED = "RLE-tuned"

        scheme2cox = {
            ES_None: "none",
            ES_Negative: "neg",
            ES_RLE: "rle",
            # ES_RLE_TUNED: "rle-tuned", # TODO Work out the right math here
        }

        all = scheme2cox.keys()

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
    def query(selectionScheme, encodingScheme, queryFile, timeout=None):
        """Query the engine

        selectionScheme: SELECTION_SCHEME
        encodingScheme: ENCODING_SCHEME
        queryFile: file path
        timeout: integer seconds before raising subprocess.TimeoutExpired

        returns: EngineMeasurements
        raises: on rc != 0, or on timeout
        """
        rc, stdout, stderr = libLF.runcmd_OutAndErr(
            args= [ ProtoRegexEngine.CLI,
              ProtoRegexEngine.SELECTION_SCHEME.scheme2cox[selectionScheme],
              ProtoRegexEngine.ENCODING_SCHEME.scheme2cox[encodingScheme],
              '-f', queryFile ],
            timeout=timeout
        )
        if rc != 0:
            if "syntax error" in stderr:
                raise SyntaxError("Engine raised syntax error\n  rc: {}\nstdout:\n{}\n\nstderr:\n{}".format(rc, stdout, stderr))
            else:
                raise BaseException('Invocation failed; rc {} stdout\n  {}\n\nstderr\n  {}'.format(rc, stdout, stderr))

        res = re.search(r"Need (\d+) bits", stdout)
        if res:
          libLF.log("Wished for {} bits".format(res.group(1)))

        # libLF.log("stderr: <" + stderr + ">")
        return ProtoRegexEngine.EngineMeasurements(stderr.strip(), "-no match-" in stdout)
    
    class EngineMeasurements:
        """Engine measurements
        
        This is a Python-native version of the JSON object 
        emitted by the regex engine.
        It offers some assurance of type safety.
        """
        def __init__(self, measAsJSON, misMatched):
            obj = json.loads(measAsJSON)
            self._unpackInputInfo(obj['inputInfo'])
            self._unpackMemoizationInfo(obj['memoizationInfo'])
            self._unpackSimulationInfo(obj['simulationInfo'])
            self.matched = not misMatched
        
        def _unpackInputInfo(self, dict):
            self.ii_lenW = int(dict['lenW'])
            self.ii_nStates = int(dict['nStates'])

        def _unpackMemoizationInfo(self, dict):
            self.mi_config_encoding = dict['config']['encoding']
            self.mi_config_vertexSelection = dict['config']['vertexSelection']

            self.mi_results_maxObservedAsymptoticCostsPerVertex = [
              int(cost) for cost in dict['results']['maxObservedAsymptoticCostsPerMemoizedVertex']
            ]
            self.mi_results_maxObservedMemoryBytesPerVertex = [
              int(cost) for cost in dict['results']['maxObservedMemoryBytesPerMemoizedVertex']
            ]
            self.mi_results_nSelectedVertices = int(dict['results']['nSelectedVertices'])
            self.mi_results_lenW = int(dict['results']['lenW'])

        def _unpackSimulationInfo(self, dict):
            self.si_nTotalVisits = int(dict['nTotalVisits'])
            self.si_simTimeUS = int(dict['simTimeUS'])
            self.si_visitsToMostVisitedSimPos = int(dict['visitsToMostVisitedSimPos'])
            self.si_nPossibleTotalVisitsWithMemoization = int(dict['nPossibleTotalVisitsWithMemoization'])
            self.si_visitsToMostVisitedSimPos = int(dict['visitsToMostVisitedSimPos'])

###
# Input classes
###

class SimpleRegex:
  """Simple regex for use with a memoized regex engine.
     Can be pattern ("all") or pattern+evilInput ("SL")
  """
  def __init__(self):
    self.pattern = None
    self.evilInputs = []
    return
  
  def initFromNDJSON(self, line):
    obj = json.loads(line)
    self.pattern = obj['pattern']
    self.evilInputs = []
    if 'evilInputs' in obj:
      for _ei in obj['evilInputs']:
        _ei['couldParse'] = True # Hack
        ei = libLF.EvilInput()
        ei.initFromDict(_ei)
        self.evilInputs.append(ei)

    return self

###
# Output classes
###

class MemoizationStaticAnalysis:
  """Represents the result of regex pattern static analysis for memoization purposes"""
  def __init__(self):
    self.pattern = None
    self.policy2nSelectedVertices = {}
  
  def initFromRaw(self, pattern, policy2nSelectedVertices):
    self.pattern = pattern

    self.policy2nSelectedVertices = policy2nSelectedVertices
    # All memoization policies measured?
    s1 = set(policy2nSelectedVertices.keys())
    s2 = set(policy2nSelectedVertices.keys())
    assert s1 <= s2 <= s1
    
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

class MemoizationDynamicAnalysis:
  """Represents the result of regex pattern dynamic analysis for memoization purposes"""
  def __init__(self):
    self.pattern = None
    self.automatonSize = -1
    self.phiInDeg = -1
    self.phiQuantifier = -1
    self.inputLength = -1

    self.evilInput = None # If an SL regex
    self.nPumps = -1 # If an SL regex

    # Set these if you run a production regex analysis
    self.productionEnginePumps = -1
    self.perlBehavior = ""
    self.phpBehavior = ""
    self.csharpBehavior = ""

    self.selectionPolicy_to_enc2space = {} # Numeric space cost
    self.selectionPolicy_to_enc2time = {} # Numeric time cost

    for scheme in ProtoRegexEngine.SELECTION_SCHEME.scheme2cox.keys():
      if scheme != ProtoRegexEngine.SELECTION_SCHEME.SS_None:
        self.selectionPolicy_to_enc2space[scheme] = {}
        self.selectionPolicy_to_enc2time[scheme] = {}
  
  def initFromRaw(self, pattern, automatonSize, phiInDeg, phiQuantifier, inputLength, evilInput, nPumps, selectionPolicy_to_enc2space, selectionPolicy_to_enc2time):
    self.pattern = pattern
    self.automatonSize = automatonSize
    self.phiInDeg = phiInDeg
    self.phiQuantifier = phiQuantifier
    self.inputLength = inputLength
    self.evilInput = evilInput
    self.nPumps = nPumps
    self.selectionPolicy_to_enc2space = selectionPolicy_to_enc2space
    self.selectionPolicy_to_enc2time = selectionPolicy_to_enc2time
    return self
  
  def initFromNDJSON(self, jsonStr):
    obj = libLF.fromNDJSON(jsonStr)
    return self.initFromDict(obj)

  def initFromDict(self, obj):
    self.pattern = obj['pattern']
    self.automatonSize = obj['automatonSize']
    self.phiInDeg = obj['phiInDeg']
    self.phiQuantifier = obj['phiQuantifier']
    self.inputLength = obj['inputLength']

    if obj['evilInput'] is not None:
      ei = libLF.EvilInput()
      ei.initFromNDJSON(obj['evilInput'])
      self.evilInput = ei
    else:
      self.evilInput = None

    self.nPumps = obj['nPumps']

    self.productionEnginePumps = obj['productionEnginePumps']
    self.perlBehavior = obj['perlBehavior']
    self.phpBehavior = obj['phpBehavior']
    self.csharpBehavior = obj['csharpBehavior']

    self.selectionPolicy_to_enc2space = obj['selectionPolicy_to_enc2space']
    self.selectionPolicy_to_enc2time = obj['selectionPolicy_to_enc2time']
    return self
  
  def toNDJSON(self):
    _dict = {
      'pattern': self.pattern,
      'automatonSize': self.automatonSize,
      'phiInDeg': self.phiInDeg,
      'phiQuantifier': self.phiQuantifier,
      'inputLength': self.inputLength,
      'evilInput': self.evilInput.toNDJSON() if self.evilInput else None,
      'nPumps': self.nPumps,
      'perlBehavior': self.perlBehavior,
      'productionEnginePumps': self.productionEnginePumps,
      'selectionPolicy_to_enc2space': self.selectionPolicy_to_enc2space,
      'selectionPolicy_to_enc2time': self.selectionPolicy_to_enc2time,
    }
    return json.dumps(_dict)
  
  def validate(self):
    """Returns True if everything looks OK, else raises an error"""
    assert self.automatonSize >= 0, "No automaton"
    assert self.phiInDeg >= 0, "Negative |Phi_in-deg|?"
    assert self.phiQuantifier >= 0, "Negative |Phi_quantifier|?"
    assert self.inputLength > 0, "no input"
    # Full space cost for Phi=Q should be |Q| * |w|
    fullSpaceCost = self.selectionPolicy_to_enc2space[
      ProtoRegexEngine.SELECTION_SCHEME.SS_Full
    ][
      ProtoRegexEngine.ENCODING_SCHEME.ES_None
    ]
    assert fullSpaceCost == self.automatonSize * (self.inputLength+1), \
      "fullSpaceCost {} != {} * {}".format(fullSpaceCost, self.automatonSize, self.inputLength)

    # Full table should have the most space complexity
    for selectionScheme, enc2space in self.selectionPolicy_to_enc2space.items():
      for encodingScheme, spaceCost in enc2space.items():
        assert spaceCost <= fullSpaceCost, \
          "General fullSpaceCost < cost for {}-{}".format(selectionScheme, encodingScheme)
        assert spaceCost <= enc2space[ProtoRegexEngine.ENCODING_SCHEME.ES_None], \
          "Phi-specific fullSpaceCost < cost for {}-{}".format(selectionScheme, encodingScheme)

    return True
  
  def toDataFrame(self):
    """Return a pandas DataFrame
    
    This expands the selection-encoding dictionaries
    """
    rows = []
    for selectionPolicy, d in self.selectionPolicy_to_enc2space.items():
      for encodingPolicy, space in d.items():
        rows.append( {
          "pattern": self.pattern,
          "|Q|": self.automatonSize,
          "|Phi_{in-deg > 1}|": self.phiInDeg,
          "|Phi_{quantifier}|": self.phiQuantifier,
          "|w|": self.inputLength + 1, # Count the null byte
          "SL": True,
          "nPumps": self.nPumps,
          "perlBehavior": self.perlBehavior,
          "phpBehavior": self.phpBehavior,
          "csharpBehavior": self.csharpBehavior,
          "productionEnginePumps": self.productionEnginePumps,
          "selectionPolicy": selectionPolicy,
          "encodingPolicy": encodingPolicy,
          "spaceCost": space,
          "timeCost": self.selectionPolicy_to_enc2time[selectionPolicy][encodingPolicy],
        })
    return pd.DataFrame(data=rows)
