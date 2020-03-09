# memoized-regex-engine

Prototype of memoized regex engine.

## Table of contents

| Item | Description | Location |
|------|-------------|----------|
| Regex engine and variants   | Source code for regex engine (baseline and variants)      | src/ |
| Semantic test suite         | Test suite of input-output pairs                          | test/ |
| Performance benchmark suite | Test suite of super-linear regexes with worst-case inputs | benchmark/ |

## Statement of origin

As the copyright information in the engine files indicates, these engines are extensions of Russ Cox's prototype regex engines.
- Cox's engines are available [here](https://swtch.com/~rsc/regexp/).
- The memoization extensions, test suite, and benchmark suite are my own.