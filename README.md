# memoized-regex-engine

Prototype of memoized regex engine.

## Table of contents

| Item | Description | Location |
|------|-------------|----------|
| Simple regex engine | Source code for Cox's simple Thompson regex engine (baseline and variants)       | src-simple/ |
| WIP (Full-featured regex engine) | WIP (Source code for the Perl regex engine (baseline and variants)) | src-perl/ |
| Evaluation (semantics / performance) | Prototype evaluation. Test suite, benchmark suite               | eval/ |

## Statement of origin

The simple and full regex engines are extensions of existing engines.
  - The original source for `src-simple/` is from Cox's re1 project, available from Google Code Archive [here](https://code.google.com/archive/p/re1/).
  - The original source for `src-perl/` is a fork of Perl, from branch blead commit 34667d08d3bf4da83ed39a692fb83467dc30a4a6 ([Link to GitHub](https://github.com/Perl/perl5/commit/34667d08d3bf4da83ed39a692fb83467dc30a4a6)).
  - The hash table used for "negative entries" is [uthash](https://github.com/troydhanson/uthash), by T. Hanson and A. O'Dwyer (and their collaborators)
  - The JSON library used for the CLI is [jsmn](https://github.com/zserge/jsmn), by S. Zaitsev (and their collaborators).

The memoization extensions, test suite, and benchmark suite are my own.