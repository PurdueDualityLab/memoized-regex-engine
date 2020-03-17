"""Lingua Franca: GitHub project
"""

import libLF.lf_ndjson as lf_ndjson
import libLF
import json
import re

import os
import tempfile
import subprocess
import time

class SimpleGitHubProjectNameAndStars:
  """GitHub project name with # stars
  
  name: projectOwner/repoName 
  nStars: integer
  """
  Type = 'SimpleGitHubProjectNameAndStars'
  def __init__(self):
    self.type = SimpleGitHubProjectNameAndStars.Type
    self.initialized = False

  def initFromRaw(self, name, nStars):
    """Initialize from individual fields"""
    self.initialized = True

    self.name = name
    self.nStars = nStars
    return self

  def initFromJSON(self, jsonStr):
    """Initialize from NDJSON string"""
    self.initialized = True

    obj = lf_ndjson.fromNDJSON(jsonStr)
    assert(obj['type'] == SimpleGitHubProjectNameAndStars.Type)
    self.type = obj['type']
    self.name = obj['name']
    self.nStars = obj['nStars']
    
    return self
  
  def toNDJSON(self):
    assert(self.initialized)
    # Consistent and in ndjson format
    return lf_ndjson.toNDJSON(self._toDict())
  
  def getOwner(self):
    return self.name.split('/')[0]

  def getName(self):
    return self.name.split('/')[1]

  def _toDict(self):
    obj = { "type": self.type,
            "name": self.name,
            "nStars": self.nStars
    }
    return obj
  
  def toGitHubProject(self):
    return libLF.GitHubProject() \
           .initFromRaw(self.getOwner(),
                        self.getName(),
                        "maven",
                        ["UNKNOWN"],
                        nStars=self.nStars)


class GitHubProject:
    """Represents a GitHub project.
    
    owner: The project owner: github.com/OWNER/name, e.g. 'facebook'
    name: The project name: github.com/owner/NAME, e.g. 'react'
    registry: What registry did we get this project from, e.g. 'crates.io'
    modules: The registry modules that pointed to this project, e.g. ['rustModule1', 'rustModule2']
    nStars: The number of GitHub accounts that starred this project, e.g. 1
      Default is GitHubProject.UnknownStars
    tarballPath: The path to a tarball of a clone of this project on sushi, e.g. '/home/davisjam/lf/clones/crates.io/1/facebook-react.tgz'
      Default is GitHubProject.NoTarballPath
    regexPath: The path to a ndjson file of RegexUsage objects found in this project on sushi, e.g. '/home/davisjam/lf/clones/crates.io/1/facebook-react.json'
      Default is GitHubProject.NoRegexPath
      These are *STATICALLY* extracted regexes.
    dynRegexPath: Like regexPath, but for dynamically-extracted regexes.
      Default is GitHubProject.NoRegexPath
    """

    UnknownStars = -1
    NoTarballPath = 'NoTarballPath'
    NoRegexPath = 'NoRegexPath'
    Type = 'GitHubProject'

    def __init__(self):
        """Declare an object and then initialize using JSON or "Raw" input."""
        self.initialized = False
        self.type = GitHubProject.Type
  
    def initFromRaw(self, owner, name, registry, modules, nStars=UnknownStars, tarballPath=NoTarballPath, regexPath=NoRegexPath, dynRegexPath=NoRegexPath, projectHistoryFile=None):
        """Initialize from individual fields"""
        self.initialized = True

        self.owner = owner
        self.name = name
        self.registry = registry
        self.modules = modules

        if nStars is not None:
          self.nStars = nStars
        else:
          self.nStars = GitHubProject.UnknownStars

        if tarballPath is not None:
          self.tarballPath = tarballPath
        else:
          self.tarballPath = GitHubProject.NoTarballPath

        if regexPath is not None:
          self.regexPath = regexPath
        else:
          self.regexPath = GitHubProject.NoRegexPath

        if dynRegexPath is not None:
          self.dynRegexPath = dynRegexPath
        else:
          self.dynRegexPath = GitHubProject.NoRegexPath

        if projectHistoryFile is not None:
          self.projectHistoryFile = tarballPath
        else:
          self.projectHistoryFile = ""
        
        return self

    def initFromJSON(self, jsonStr):
        """Initialize from ndjson string"""
        self.initialized = True

        obj = lf_ndjson.fromNDJSON(jsonStr)
        assert(obj['type'] == GitHubProject.Type)
        self.type = obj['type']
        self.owner = obj['owner']
        self.name = obj['name']
        self.registry = obj['registry']
        self.modules = obj['modules']
        self.nStars = obj['nStars']
        self.tarballPath = obj['tarballPath']

        if 'regexPath' in obj:
            self.regexPath = obj['regexPath']
        else:
            self.regexPath = GitHubProject.NoRegexPath

        if 'dynRegexPath' in obj:
            self.dynRegexPath = obj['dynRegexPath']
        else:
            self.dynRegexPath = GitHubProject.NoRegexPath

        if 'projectHistoryFile' in obj:
            self.projectHistoryFile = obj['projectHistoryFile']
        else:
            self.projectHistoryFile = ""
        
        return self

    def toNDJSON(self):
        assert(self.initialized)
        # Consistent and in ndjson format
        return lf_ndjson.toNDJSON(self._toDict())

    def _toDict(self):
        obj = { "type": self.type,
                "registry": self.registry,
                "owner": self.owner,
                "name": self.name,
                "modules": self.modules,
                "nStars": self.nStars,
                "tarballPath": self.tarballPath,
                "regexPath": self.regexPath,
                "dynRegexPath": self.dynRegexPath,
                "projectHistoryFile": self.projectHistoryFile
        }
        return obj

    def getNModules(self):
      """Return the number of modules that point to this project."""
      return len(self.modules)

###############
# Module file analysis
###############

def getAllSourceFiles(dirName, registry):
  """Get all source files for a project from this module registry

  Args:
    dirName: source root
    registry: lower-case string, e.g. 'npm' or 'maven'
  Returns:
    lang2sourceFiles: { 'JavaScript': [{obj1}, ...] , ...
      Each sourceFile object is as described by getFileSummaryFromCLOC
      The languages are those languages used in the given registry
      This does not yet have filtering for vendored code applied
  """
  assert(registry.lower() in libLF.registryToPrimaryLanguages)

  # Get source files for all languages present in dir
  _language2files = getFileSummaryFromCLOC(dirName)

  # Copy out the records of interest
  lang2sourceFiles = {}
  for lang in libLF.registryToPrimaryLanguages[registry]:
    lang2sourceFiles[lang] = _language2files[lang]
  return lang2sourceFiles

def _looksVendored(filePath):
  # cf. https://github.com/github/linguist/blob/master/lib/linguist/vendor.yml
  lowerCaseThirdPartyDirs = ["third-party", "third_party", "thirdparty",
                    "3rd-party", "3rd_party", "3rdparty",
                    "vendor", "vendors",
                    "extern", "external",
                    "node_modules" # npm
                   ]

  # Check if in a vendor or build dir
  splitPath = libLF.pathSplitAll(filePath)
  splitPath.pop() # Basename
  for d in splitPath:
    if d.lower() in lowerCaseThirdPartyDirs:
        return True
  return False

def removeVendoredSourceFiles(lang2sourceFiles):
  """Remove the vendored (third-party) source files

  Args:
    lang2sourceFiles: see getSourceFiles()
      NB: Modified in-place!
  Returns:
    unvendoredSourceFiles: sourceFiles with third-party files removed

  Identifies third-party files using heuristics taken from GitHub.
  """
  for lang, sourceFiles in lang2sourceFiles.items():
    lang2sourceFiles[lang] = [
      fileObj
      for fileObj in sourceFiles
      if not _looksVendored(fileObj["name"])
    ]
  return sourceFiles

def getFileSummaryFromCLOC(dirName):
  """Simple analyses on files in a module

  Returns language2files{}
  Keys are from the values of libLF.registryToPrimaryLanguages
    { "javascript": [
        { "name": XYZ,
          "LOC": XYZ
        },
        ...
      ],
      "ruby": [
        {...}, ...
      ],
      ...
    }

  LOC: lines of non-comment non-whitespace

  Requires that 'cloc' be in your PATH
  """

  language2files = {}
  for reg in libLF.registryToPrimaryLanguages:
    for lang in libLF.registryToPrimaryLanguages[reg]:
      language2files[lang] = []

  # Get a tmp file for CLOC output
  fd, clocDataFileName = tempfile.mkstemp(suffix='.json', prefix='cloc-')
  os.close(fd)

  try:
    # Run CLOC
    cmd = ["cloc", dirName, "--json", "--by-file", "--report-file", clocDataFileName]
    libLF.log("CMD: " + " ".join(cmd))
    subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(0.1) # I see intermittent parse failures. Maybe this will help?

    # Grab output
    libLF.log("Loading report from {}".format(clocDataFileName))
    with open(clocDataFileName, 'r') as clocOut:
      res = json.load(clocOut)
      # The keys of res are "header", "SUM", and file names
      for key in res:
        if os.path.isfile(key) and not os.path.islink(key):
          sourceFile = key
          loc = res[key]["code"]
          lang = res[key]["language"]
          # Keep if it's in a language of interest
          if lang.lower() in language2files:
            fileObj = {
              "name": sourceFile,
              "LOC": loc
            }
            language2files[lang.lower()].append(fileObj)
  except subprocess.CalledProcessError:
    libLF.log("Command yielded non-zero rc")
  os.unlink(clocDataFileName)

  # All done
  return language2files

def logLang2SourceFiles(lang2sourceFiles):
  """Log a lang2sourceFiles from CLOC"""
  fmt = "%20s %10s %15s"
  libLF.log(fmt % ("Language", "Num files", "Net LOC"))
  libLF.log(fmt % ("-----", "-----", "-----"))
  for lang in lang2sourceFiles:
    loc = 0
    for f in lang2sourceFiles[lang]:
      loc += f["LOC"]
    libLF.log(fmt % (lang, len(lang2sourceFiles[lang]), loc))

def getUnvendoredSourceFiles(projDir, registry):
  """Return lang2sourceFiles for the languages in this registry
  
  Filters out vendored files
  cf. getSourceFiles
  """
  lang2sourceFiles = getAllSourceFiles(projDir, registry)

  libLF.log("All source files")
  logLang2SourceFiles(lang2sourceFiles)

  libLF.log("Removing vendored source files")
  removeVendoredSourceFiles(lang2sourceFiles)

  libLF.log("Non-vendored lang2sourceFiles:")
  libLF.logLang2SourceFiles(lang2sourceFiles)

  return lang2sourceFiles