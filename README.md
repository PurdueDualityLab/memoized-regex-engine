# memoized-regex-engine

Prototype of memoized backtracking regex engine. Novelty:
- Demonstrates selective memoization and various encoding schemes for the memo table.
- Includes benchmarking for large-scale evaluation on real-world regexes.

Research paper is available [here](https://davisjam.github.io/publications/).

## Table of contents

| Item | Description | Location |
|------|-------------|----------|
| Simple regex engine | Source code for Cox's simple Thompson regex engine (baseline and variants)       | src-simple/ |
| Evaluation (semantics / performance) | Prototype evaluation. Test suite, benchmark suite               | eval/ |
| Prototype size measurements                                                                            | measure-prototype-size/ |

## Configuration

Set the following environment variables:
- `MEMOIZATION_PROJECT_ROOT`: The directory to which you cloned this
- `ECOSYSTEM_REGEXP_PROJECT_ROOT`: Set this to anything. It's referenced in `eval/`, but not actually used in this project. Needs to be cleaned up...

The file `.sample_config` has examples. You might run `. .sample_config`.

## Statement of origin

The simple and full regex engines are extensions of existing engines.
  - The original source for `src-simple/` is from Cox's re1 project, available from Google Code Archive [here](https://code.google.com/archive/p/re1/).
  - The original source for `src-perl/` is a fork of Perl, from branch blead commit 34667d08d3bf4da83ed39a692fb83467dc30a4a6 ([Link to GitHub](https://github.com/Perl/perl5/commit/34667d08d3bf4da83ed39a692fb83467dc30a4a6)).
  - The hash table used for "negative entries" is [uthash](https://github.com/troydhanson/uthash), by T. Hanson and A. O'Dwyer (and their collaborators)
  - The JSON library used for the CLI is [cJSON](https://github.com/DaveGamble/cJSON), by D. Gamble (and their collaborators).

The memoization extensions, test suite, and benchmark suite are my own.

FYI: Most data in the paper was generated on a 10-node compute cluster. On a standard desktop it would take several days/weeks(?) to replicate.
