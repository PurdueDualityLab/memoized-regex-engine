"""Memoization: utils associated with memoization experiments
"""

# Import libLF
import os
import sys
sys.path.append(os.path.join(os.environ['MEMOIZATION_PROJECT_ROOT'], 'eval', 'lib'))
import libLF

# Other imports
import json

###
# Constants
###

PROTOTYPE_REGEX_ENGINE_CLI = os.path.join(os.environ['MEMOIZATION_PROJECT_ROOT'], "src-simple", "re")

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