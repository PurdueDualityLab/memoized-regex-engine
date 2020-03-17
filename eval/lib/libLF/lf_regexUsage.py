"""Lingua Franca: Regexes used in source code
"""

import os
import tempfile
import json

import libLF

import re

#####
# SimpleRegexUsage
#####

class SimpleFileWithRegexes:
  """Represents "simple" file with regexes

  This is the format that the per-language "extract-regexps" tools emit.

  Members: fileName language couldParse regexes[]
    Each regex has keys: pattern flags
  """

  def __init__(self):
    """Declare an object and then initialize using JSON or "Raw" input."""
    self.initialized = False
    self.type = 'SimpleFileWithRegexes'
  
  def initFromRaw(self, fileName, language, couldParse, regexes=[]):
    """file: str
       language: str
       couldParse: bool
       regexes: []
    """

    self.initialized = True

    self.fileName = fileName
    self.language = language
    self.couldParse = couldParse
    self.regexes = regexes
    return self

  def initFromNDJSON(self, jsonStr):
    self.initialized = True

    obj = libLF.fromNDJSON(jsonStr)
    self.fileName = obj['fileName']
    self.language = obj['language']
    self.couldParse = obj['couldParse']
    self.regexes = obj['regexes']
    return self
  
  def toNDJSON(self):
    assert(self.initialized)
    # Consistent and in ndjson format
    return libLF.toNDJSON(self._toDict())
  
  def _toDict(self):
    obj = { "fileName": self.fileName,
            "language": self.language,
            "couldParse": self.couldParse,
            "regexes": self.regexes,
            "type": self.type
    }
    return obj

#####
# RegexUsage
#####

class RegexUsage:
  """Represents the use of a regex in source code.

  Members: pattern flags[] inputs[] regexes relPath basename
  """

  def __init__(self):
    """Declare an object and then initialize using JSON or "Raw" input."""
    self.initialized = False
    self.type = 'RegexUsage'
  
  def initFromRaw(self, pattern, flags, inputs, regexes, relPath, basename):
    """pattern: string
       flags: list of flags like ['re.DOTALL', 're.UNICODE'] or ['i', 'g'] depending on the language
       inputs: list of strings evaluated against this regex
       regexes: unique name of regexes from which this was derived
       relPath: file containing pattern, relative to regexes root, e.g. lib/jquery.js
       basename: basename of file containing pattern, e.g. jquery.js
    """

    self.initialized = True

    self.pattern = pattern
    self.flags = flags
    self.inputs = inputs
    self.regexes = regexes
    self.relPath = relPath
    self.basename = basename
    return self

  def initFromNDJSON(self, jsonStr):
    self.initialized = True

    obj = libLF.fromNDJSON(jsonStr)
    self.pattern = obj['pattern']
    self.flags = obj['flags']
    self.inputs = obj['inputs']
    self.regexes = obj['regexes']
    self.relPath = obj['relPath']
    self.basename = obj['basename']
    return self
  
  def toNDJSON(self):
    assert(self.initialized)
    # Consistent and in ndjson format
    return libLF.toNDJSON(self._toDict())
  
  def addInput(self, _input):
    self.inputs.append(_input)

  def _toDict(self):
    obj = { "pattern": self.pattern,
            "flags": self.flags,
            "inputs": self.inputs,
            "regexes": self.regexes,
            "relPath": self.relPath,
            "basename": self.basename
    }
    return obj

def sfwrToRegexUsageList(sfwr):
  """Expand a SimpleFileWithRegexes to a RegexUsage[]
  
  Handy if a regex extractor produces SFWR and analyses want a RegexUsage
  """
  ruList = []
  for regex in sfwr.regexes:
      ru = libLF.RegexUsage()
      basePath = os.path.basename(sfwr.fileName)
      ru.initFromRaw(regex['pattern'], regex['flags'], None, None, sfwr.fileName, basePath)
      ruList.append(ru)
  libLF.log('sfwrToRegexUsageList: Got {} regexes from {}'.format(len(ruList), sfwr.fileName))
  return ruList

#####
# RegexUsageWithHistory
#####

class RegexUsageWithHistory:
  """Represents the use of a regex in source code.

  Members: pattern flags[] inputs[] regexes relPath basename
  """

  def __init__(self):
    """Declare an object and then initialize using JSON or "Raw" input."""
    self.initialized = False
    self.type = 'RegexUsage'
  
  def initFromRaw(self, pattern, flags, inputs, regexes, relPath, basename, lineNumber, logHistory=None):
    """pattern: string
       flags: list of flags like ['re.DOTALL', 're.UNICODE'] or ['i', 'g'] depending on the language
       inputs: list of strings evaluated against this regex
       regexes: unique name of regexes from which this was derived
       relPath: file containing pattern, relative to regexes root, e.g. lib/jquery.js
       basename: basename of file containing pattern, e.g. jquery.js
       lineNumber: the line number the regex was found on
       logHistory: the results of running log on the regex line number
    """

    self.initialized = True

    self.pattern = pattern
    self.flags = flags
    self.inputs = inputs
    self.regexes = regexes
    self.relPath = relPath
    self.basename = basename
    self.lineNumber = lineNumber
    if logHistory is None:
      self.logHistory = ''
    else:
      self.logHistory = logHistory
    return self

  def initFromNDJSON(self, jsonStr):
    self.initialized = True

    obj = libLF.fromNDJSON(jsonStr)
    self.pattern = obj['pattern']
    self.flags = obj['flags']
    self.inputs = obj['inputs']
    self.regexes = obj['regexes']
    self.relPath = obj['relPath']
    self.basename = obj['basename']
    self.lineNumber = obj['lineNumber']
    if 'logHistory' in obj:
      self.logHistory = obj['logHistory']
    else:
      self.logHistory = ''
    return self
  
  def toNDJSON(self):
    assert(self.initialized)
    # Consistent and in ndjson format
    return libLF.toNDJSON(self._toDict())
  
  def addInput(self, _input):
    self.inputs.append(_input)
  def newLogHistory(self, _logHistory):
    self.logHistory = _logHistory

  def _toDict(self):
    obj = { "pattern": self.pattern,
            "flags": self.flags,
            "inputs": self.inputs,
            "regexes": self.regexes,
            "relPath": self.relPath,
            "basename": self.basename,
            "lineNumber": self.lineNumber,
            "logHistory": self.logHistory
    }
    return obj

#####
# Regex
#####

class Regex:
  """Represents a unique pattern found somewhere in our datasets

  Members:
    pattern: str
    useCount_registry_to_nModules: {}
       # unique modules used in STATICALLY
       keyed by registry
       'CPAN' -> 4, 'npm' -> 5, etc.
    useCount_registry_to_nModules_static: {}
    useCount_registry_to_nModules_dynamic: {}
      Like useCount_registry_to_nModules, but tracks by static and dynamic appearances.
      All static regexes may appear in dynamic (if there is a test suite with full coverage).
      Dynamic may include other regexes too.
    useCount_IStype_to_nPosts: {}
       # times used in different InternetSource's
       'SO' -> 3, 'REL' -> 2
    supportedLangs: str[]
      For syntax portability.
      This starts empty, and is populated by calls to isSupportedInLanguage.
    nUniqueInputsTested: int
    semanticDifferenceWitnesses: SemanticDifferenceWitness[]
      These are populated by test-for-semantic-portability.py
  """

  DEFAULT_VULN_REGEX_DETECTOR_ROOT = \
    os.path.join(os.environ['ECOSYSTEM_REGEXP_PROJECT_ROOT'], 'vuln-regex-detector')
  
  USE_TYPE_STATIC = "static"
  USE_TYPE_DYNAMIC = "dynamic"

  reg2lang = {
    'npm': 'JavaScript', # TypeScript is evaluated on a JS engine
    'crates.io': 'Rust',
    'packagist': 'PHP',
    'pypi': 'Python',
    'rubygems': 'Ruby',
    'cpan': 'Perl',
    'maven': 'Java',
    'godoc': 'Go',
  } 

  def __init__(self):
    """Declare an object and then initialize using JSON or "Raw" input."""
    self.initialized = False
    self.type = 'Regex'
    self.supportedLangs = []
    self.nUniqueInputsTested = -1
    self.semanticDifferenceWitnesses = []

  def initFromRaw(self, pattern, 
      useCount_registry_to_nModules, 
      useCount_IStype_to_nPosts,
      useCount_registry_to_nModules_static=None, 
      useCount_registry_to_nModules_dynamic=None,
      supportedLangs=None,
      nUniqueInputsTested=None,
      semanticDifferenceWitnesses=None):
    self.initialized = True

    self.pattern = pattern
    self.useCount_registry_to_nModules = useCount_registry_to_nModules
    self.useCount_IStype_to_nPosts = useCount_IStype_to_nPosts

    if useCount_registry_to_nModules_static is None:
      self.useCount_registry_to_nModules_static = {}
    else:
      self.useCount_registry_to_nModules_static = useCount_registry_to_nModules_static

    if useCount_registry_to_nModules_dynamic is None:
      self.useCount_registry_to_nModules_dynamic = {}
    else:
      self.useCount_registry_to_nModules_dynamic = useCount_registry_to_nModules_dynamic

    if supportedLangs is None:
      self.supportedLangs = []
    else:
      self.supportedLangs = supportedLangs

    if nUniqueInputsTested is None:
      self.nUniqueInputsTested = -1
    else:
      self.nUniqueInputsTested = nUniqueInputsTested

    if semanticDifferenceWitnesses is None:
      self.semanticDifferenceWitnesses = []
    else:
      self.semanticDifferenceWitnesses = semanticDifferenceWitnesses

    return self
  
  def initFromDict(self, obj):
    self.initialized = True
    self.pattern = obj['pattern']
    self.useCount_registry_to_nModules = obj['useCount_registry_to_nModules']
    self.useCount_IStype_to_nPosts = obj['useCount_IStype_to_nPosts']

    if 'useCount_registry_to_nModules_static' in obj:
      self.useCount_registry_to_nModules_static = obj['useCount_registry_to_nModules_static']
    else:
      self.useCount_registry_to_nModules_static = {}

    if 'useCount_registry_to_nModules_dynamic' in obj:
      self.useCount_registry_to_nModules_dynamic = obj['useCount_registry_to_nModules_dynamic']
    else:
      self.useCount_registry_to_nModules_dynamic = {}

    if 'supportedLangs' in obj:
      self.supportedLangs = obj['supportedLangs']
    else:
      self.supportedLangs = []

    if 'nUniqueInputsTested' in obj:
      self.nUniqueInputsTested = obj['nUniqueInputsTested']
    else:
      self.nUniqueInputsTested = []

    if 'semanticDifferenceWitnesses' in obj:
      self.semanticDifferenceWitnesses = [
        SemanticDifferenceWitness().initFromNDJSON(sdwJson)
        for sdwJson in obj['semanticDifferenceWitnesses']
      ]
    else:
      self.semanticDifferenceWitnesses = []

    return self

  def initFromNDJSON(self, jsonStr):
    self.initialized = True
    obj = libLF.fromNDJSON(jsonStr)
    return self.initFromDict(obj)
  
  def toNDJSON(self):
    assert(self.initialized)
    # Consistent and in ndjson format
    return libLF.toNDJSON(self._toDict())

  def _toDict(self):
    obj = { "pattern": self.pattern,
            "useCount_registry_to_nModules": self.useCount_registry_to_nModules,
            "useCount_IStype_to_nPosts": self.useCount_IStype_to_nPosts,
            "useCount_registry_to_nModules_static": self.useCount_registry_to_nModules_static,
            "useCount_registry_to_nModules_dynamic": self.useCount_registry_to_nModules_dynamic,
            "supportedLangs": self.supportedLangs,
            "type": self.type,
            "nUniqueInputsTested": self.nUniqueInputsTested,
            "semanticDifferenceWitnesses": [
              sdw.toNDJSON()
              for sdw in self.semanticDifferenceWitnesses
            ]
    }
    return obj

  def usedInRegistry(self, registryName, useType):
    """Mark a new use in this registry (e.g. CPAN)."""
    # Initialize registry
    for d in [self.useCount_registry_to_nModules, self.useCount_registry_to_nModules_static, self.useCount_registry_to_nModules_dynamic]:
      if registryName not in d:
        d[registryName] = 0

    self.useCount_registry_to_nModules[registryName] += 1
    if useType == Regex.USE_TYPE_DYNAMIC:
      self.useCount_registry_to_nModules_dynamic[registryName] += 1
    elif useType == Regex.USE_TYPE_STATIC:
      self.useCount_registry_to_nModules_static[registryName] += 1
    else:
      raise ValueError("Error, unexpected useType {}".format(useType))
  
  def usedInInternetSource(self, internetSourceType):
    """Mark a new use in this InternetSource type (e.g. SO)."""
    if internetSourceType not in self.useCount_IStype_to_nPosts:
      self.useCount_IStype_to_nPosts[internetSourceType] = 0
    self.useCount_IStype_to_nPosts[internetSourceType] += 1
  
  def registriesUsedIn(self):
    """Returns registries this regex was used in. [reg1, ...]"""
    return self.useCount_registry_to_nModules.keys()
  
  def langsUsedIn(self):
    """Returns lower-case languages used in, based on reverse mapping from registry
    
    Includes static and dynamic"""
    return [
      Regex.reg2lang[reg].lower()
      for reg in self.useCount_registry_to_nModules.keys()
      if self.useCount_registry_to_nModules[reg]
    ]

  def langsUsedInStatic(self):
    return [
      Regex.reg2lang[reg].lower()
      for reg in self.useCount_registry_to_nModules_static.keys()
      if self.useCount_registry_to_nModules_static[reg]
    ]

  def langsUsedInDynamic(self):
    return [
      Regex.reg2lang[reg].lower()
      for reg in self.useCount_registry_to_nModules_dynamic.keys()
      if self.useCount_registry_to_nModules_dynamic[reg]
    ]
  
  def internetSourcesAppearedIn(self):
    """Returns types of internet sources this regexes appeared in. [source1, ...]"""
    return self.useCount_IStype_to_nPosts.keys()
  
  def isSupportedInLanguage(self, lang, vrdPath=DEFAULT_VULN_REGEX_DETECTOR_ROOT):
    """Returns True if regex can be used in lang
    
    Also updates internal member."""
    checkRegexSupportScript = os.path.join(vrdPath, 'src', 'validate', 'check-regex-support.pl')

    # Build query
    query = {
      "language": lang,
      "pattern": self.pattern
    }
    libLF.log('Query: {}'.format(json.dumps(query)))

    # Query from tempfile
    with tempfile.NamedTemporaryFile(prefix='SyntaxAnalysis-queryLangs-', suffix='.json', delete=True) as ntf:
      libLF.writeToFile(ntf.name, json.dumps(query))
      rc, out = libLF.runcmd("VULN_REGEX_DETECTOR_ROOT={} '{}' '{}'" \
        .format(vrdPath, checkRegexSupportScript, ntf.name))
      out = out.strip()

    libLF.log('Got rc {} out\n{}'.format(rc, out))
    # TODO Not sure if this can go wrong.
    assert(rc == 0)

    obj = json.loads(out)
    if bool(obj['validPattern']):
      if lang not in self.supportedLangs:
        self.supportedLangs.append(lang)
      return True
    else:
      return False

#####
# RegexPatternAndInputs
#####

class RegexPatternAndInputs:
  """Represents a regex pattern and inputs to test it with

  For use in equivalence checking.

  Members:
    pattern: str
    stringsByProducer: { producerName: [str, str, ...] }
  """

  def __init__(self):
    """Declare an object and then initialize using JSON or "Raw" input."""
    self.initialized = False
    self.type = 'RegexPatternAndInputs'

  def initFromRaw(self, pattern, stringsByProducer):
    self.initialized = True

    self.pattern = pattern
    self.stringsByProducer = stringsByProducer

    return self
  
  def initFromDict(self, obj):
    self.initialized = True
    self.pattern = obj['pattern']
    self.stringsByProducer = obj['stringsByProducer']

    return self

  def initFromNDJSON(self, jsonStr):
    self.initialized = True
    obj = libLF.fromNDJSON(jsonStr)
    return self.initFromDict(obj)
  
  def toNDJSON(self):
    assert(self.initialized)
    # Consistent and in ndjson format
    return libLF.toNDJSON(self._toDict())

  def _toDict(self):
    obj = { "pattern": self.pattern,
            "stringsByProducer": self.stringsByProducer,
            "type": self.type
    }
    return obj

  def getNTotalInputs(self):
    """Includes duplicates"""
    sum = 0
    for prod in self.stringsByProducer:
      sum += len(self.stringsByProducer[prod])
    return sum

  def getUniqueInputs(self):
    uniqInputs = set()
    for prod in self.stringsByProducer:
      uniqInputs.update(self.stringsByProducer[prod])
    return uniqInputs

#####
# Various helpers for semantic difference tracking
#####

class SemanticDifferenceWitness:
  """Track the MatchResult(s) for this input, associated with language(s)
  
  pattern: a string
  input: the input that was attempted
  matchResultToLangs: {
       <No match>   -> ["JavaScript"],
       <Match: 'a'> -> ["Ruby"],
       <Match: 'b'> -> ["..."],
   }
    (the keys are MatchResult objects)
  """
  def __init__(self):
    self.pattern = None
    self.input = None
    self.matchResultToLangs = None

  def initFromRaw(self, pattern, _input, matchResultToLangs=None):
    self.pattern = pattern
    self.input = _input

    if matchResultToLangs is None:
      self.matchResultToLangs = {}
    else:
      self.matchResultToLangs = matchResultToLangs
    return self

  def initFromNDJSON(self, jsonStr):
    obj = libLF.fromNDJSON(jsonStr)
    self.pattern = obj['pattern']
    self.input = obj['input']

    self.matchResultToLangs = {}
    for mrJSON, langs in obj['matchResultToLangs'].items():
      mr = MatchResult().initFromNDJSON(mrJSON)
      self.matchResultToLangs[mr] = langs

    return self

  def toNDJSON(self):
    # Consistent and in ndjson format
    return libLF.toNDJSON(self._toDict())
  
  def _toDict(self):
    mrtl = {}
    for mr, langs in self.matchResultToLangs.items():
      mrtl[mr.toNDJSON()] = langs
    obj = { "pattern": self.pattern,
            "input": self.input,
            "matchResultToLangs": mrtl
    }
    return obj

  
  def addRER(self, rer):
    """Note how X on Y behaved in Z

    rer: RegexEvaluationResult
    """
    if rer.matchResult not in self.matchResultToLangs:
      self.matchResultToLangs[rer.matchResult] = []

    if rer.language not in self.matchResultToLangs[rer.matchResult]:
      self.matchResultToLangs[rer.matchResult].append(rer.language)
  
  def isTrueWitness(self):
    """If multiple distinct match results, this is a true witness of a semantic difference"""
    nUniqueResults = len(self.matchResultToLangs) 

    if nUniqueResults > 1:
      libLF.log('True witness: For /{}/, the (JSON) input {} is a true witness'.format(self.pattern, json.dumps(self.input)))
      for mr in self.matchResultToLangs:
        libLF.log('  %-100s -> %s' % (str(mr), self.matchResultToLangs[mr]))
    return nUniqueResults > 1

  def lang2mr(self):
    """Invert the matchResultToLangs member""" 
    _lang2mr = {}
    for mr, langs in self.matchResultToLangs.items():
      for l in langs:
        _lang2mr[l] = mr
    return _lang2mr

class MatchContents:
  """Contents of a regex match

  matchedString: str
  captureGroups: str[] containing the strings captured by any capture groups
  """
  def __init__(self):
    self.matchedString = None
    self.captureGroups = []

  def initFromRaw(self, matchedString, captureGroups):
    self.matchedString = matchedString # Full string that matched
    self.captureGroups = captureGroups # Group 1, group 2, ... (substrings of matchedString)
    return self

  def initFromNDJSON(self, jsonStr):
    obj = libLF.fromNDJSON(jsonStr)
    self.matchedString = obj['matchedString']
    self.captureGroups = obj['captureGroups']
    return self

  def toNDJSON(self):
    # Consistent and in ndjson format
    return libLF.toNDJSON(self._toDict())
  
  def _toDict(self):
    obj = { "matchedString": self.matchedString,
            "captureGroups": self.captureGroups
    }
    return obj
  
  def __eq__(self, other):
    return self.matchedString == other.matchedString \
      and self.captureGroups == other.captureGroups
  
  def __hash__(self):
    return hash((self.matchedString, tuple(self.captureGroups)))
  
  def __str__(self):
    return "matchedString: <{}> captureGroups: {}" \
      .format(self.matchedString, self.captureGroups)
  
class MatchResult:
  """(1) Did it match? (2) If so, what string matched, and capture groups?

  matched: bool
  matchContents: MatchContents
  
  Hashable -- can use for set, dict, etc.
  """
  def __init__(self):
    self.matched = None
    self.matchContents = None

  def initFromRaw(self, matched, matchContents):
    self.matched = matched
    self.matchContents = matchContents
    return self

  def initFromNDJSON(self, jsonStr):
    obj = libLF.fromNDJSON(jsonStr)
    self.matched = obj['matched']
    self.matchContents = MatchContents().initFromNDJSON(obj['matchContents'])
    return self

  def toNDJSON(self):
    # Consistent and in ndjson format
    return libLF.toNDJSON(self._toDict())
  
  def _toDict(self):
    obj = { "matched": self.matched,
            "matchContents": self.matchContents.toNDJSON()
    }
    return obj
  
  def __eq__(self, other):
    return self.matched == other.matched \
      and self.matchContents == other.matchContents
  
  def __hash__(self):
    return hash((self.matched, self.matchContents))
  
  def __str__(self):
    return "matched: {}, contents: {}".format(self.matched, str(self.matchContents))

  def terseStr(self):
    return "{} {} {}".format(
      "T" if self.matched else "F",
      json.dumps(self.matchContents.matchedString) if self.matched else "",
      self.matchContents.captureGroups if self.matched else "",
    )
  
class RegexEvaluationResult:
  """RER: Result of regex X evaluated on input Y in language Z"""
  def __init__(self, pattern, _input, language, matchResult):
    self.pattern = pattern
    self.input = _input
    self.language = language
    self.matchResult = matchResult

class RegexTranslator:
  """Translate regexes into Java/C# via ad hoc syntactic tweaks.

  Targets the most common features.
  Converts about 95% of real Python and JavaScript regexes
  to something Java/C# compatible.
  """
  # TODO Deal with nesting.

  # If altUnicodeFlag is specified, replace the u flag with it in capture
  # groups to preserve the presence/absence of flags.
  @staticmethod
  def translateRegex(pattern, sourceLang, destLang, altUnicodeFlag=''):
    assert(destLang == "C#")
    return RegexTranslator.translateToCSharp(pattern, sourceLang, altUnicodeFlag=altUnicodeFlag)
  
  @staticmethod
  def translateToCSharp(pattern, sourceLang, altUnicodeFlag=''):
    libLF.log("translateToCSharp: Orig /{}/".format(pattern))
    for transFunc in [
                      RegexTranslator.translateQEQuote,
                      RegexTranslator.translateCaptureGroups,
                      RegexTranslator.translateCurlies,
                      RegexTranslator.removeUFlag,
                     ]:
      if transFunc == RegexTranslator.removeUFlag:
        pattern = transFunc(pattern, altUnicodeFlag=altUnicodeFlag)
      else:
        pattern = transFunc(pattern)
      libLF.log(" -> /{}/".format(pattern))
    libLF.log("translateToCSharp: Final /{}/".format(pattern))
    return pattern

  @staticmethod
  def translateCaptureGroups(pattern):
    # NB This won't work with nesting
    # Convert the named capture groups to Java/C# notation
    pattern = re.sub(r'\(\?P<([^>]+)>', r'(?<\1>', pattern)
    # Convert references to named groups to Java/C# notation
    pattern = re.sub(r'\(\?P=([^)]+)\)', r'\\k<\1>', pattern)
    return pattern

  @staticmethod
  def translateCurlies(pattern):
    # NB This won't work with arbitrary nesting, e.g. templating a la '{{ UUID }}'

    # Convert {not-digits} into \{not-digits\}
    # Lookarounds:
    #   - Don't replace special escape sequences like \u{...} or \x{...}
    #   - Don't replace already-escaped curlies like \{...\}
    pattern = re.sub(r'(?<!\\(?:u|x|[pP]|g|k|N))(?<!\\)\{([^}]*?[^\d,}][^}]*?)(?<!\\)\}', r'\{\1\}', pattern)
    return pattern

  @staticmethod
  def translateQEQuote(pattern):
    # Convert /\Qescaped()string\E/ into /escaped\(\)string/.

    def repl(match):
      quote = match.group(1)
      return re.escape(quote)

    pattern = re.sub(r'\\Q(.*?)\\E', repl, pattern)
    pattern = re.sub(r'\\Q(.*?)$', repl, pattern)
    return pattern

  @staticmethod
  def removeUFlag(pattern, altUnicodeFlag=''):
    # Removes the u (Unicode support) flag from capture groups because C#
    # doesn't support it. E.g. convert /(?u:group)/ to /(?:group)/.
    # If altUnicodeFlag is specified, replace the u with it to preserve the
    # presence/absence of flags.

    escape = False
    flags = False
    result = ''

    # Scan for the (? sequence that starts a capture group with flags
    for i in range(len(pattern)):
      c = pattern[i]
      if not escape and c == '(' and i + 1 < len(pattern) and pattern[i+1] == '?':
        flags = True
      elif c not in '?-imsUuxpadln':
        flags = False

      if flags and c == 'u':
        result += altUnicodeFlag
      else:
        result += c

      if c == '\\' and not escape:
        escape = True
      else:
        escape = False

    return result
