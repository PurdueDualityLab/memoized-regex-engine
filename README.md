# memoized-regex-engine

Prototype of memoized regex engine.

## Table of contents

| Item | Description | Location |
|------|-------------|----------|
| Simple regex engine | Source code for Cox's simple Thompson regex engine (baseline and variants)      | src-simple/ |
| Full-featured regex engine | Source code for the Perl regex engine (baseline and variants)      | src-perl/ |
| Semantic test suite         | Test suite of input-output pairs                          | test/ |
| Performance benchmark suite | Test suite of super-linear regexes with worst-case inputs | benchmark/ |

## Statement of origin

The simple and full regex engines are extensions of existing engines.
  - The original source for `src-simple/` is from Cox's re1 project, available from Google Code Archive [here](https://code.google.com/archive/p/re1/).
  - The original source for `src-perl/` is a fork of Perl, from branch blead commit 34667d08d3bf4da83ed39a692fb83467dc30a4a6 ([Link to GitHub](https://github.com/Perl/perl5/commit/34667d08d3bf4da83ed39a692fb83467dc30a4a6)).

The memoization extensions, test suite, and benchmark suite are my own.