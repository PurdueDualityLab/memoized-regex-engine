"""Classes for the Memoized Regex Engine project
"""

import libLF
import json

import os
import tempfile

class MemoizationStaticAnalysis:
  """Represents the result of regex pattern static analysis for memoization purposes"""
  MEMO_POLICIES_TO_COXRE_NICKNAMES = {
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
  