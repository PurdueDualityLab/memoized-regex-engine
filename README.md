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
| Prototype size measurements          | For reporting in paper                                          | measure-prototype-size/ |

## Configuration

Set the following environment variables:
- `MEMOIZATION_PROJECT_ROOT`: The directory to which you cloned this
- `ECOSYSTEM_REGEXP_PROJECT_ROOT`: Set this to anything. It's referenced in `eval/`, but not actually used in this project. Needs to be cleaned up...

The file `.sample_config` has examples. You might run `. .sample_config`.

## Running evaluation

### Security study

This is Figure 4 plus prose.

- For Figure 4, run `eval/case-study-timeCost.py`.
- For the security experiment described in the text, run `eval/measure-memoization-behavior.py --regex-file X --runSecurityAnalysis --trials 1 --out-file /tmp/out`. It prints a summary at the end.

### Effectiveness of selective memoization

This is Figure 5 plus prose.

- Run `eval/measure-phi-sizes.py --regex-file X --out-file /tmp/phiSizes.json` to collect data.
- Set the globals in `eval/analye_phi_measurements.py` to analyze the data and generate the figure.

### Practical space costs

This is Figure 6 plus prose.

- Run `eval/measure-phi-sizes.py --regex-file X --queryPrototype --trials 1 --perf-pumps 20480 --max-attack-stringLen 20480 --out-file /tmp/SOSpaceCost.json` to collect data.
- Set the globals in `eval/analye_dynamic_measurements.py` to analyze the data and generate the figure.

## Statement of origin

The simple and full regex engines are extensions of existing engines.
  - The original source for `src-simple/` is from Cox's re1 project, available from Google Code Archive [here](https://code.google.com/archive/p/re1/).
  - The original source for `src-perl/` is a fork of Perl, from branch blead commit 34667d08d3bf4da83ed39a692fb83467dc30a4a6 ([Link to GitHub](https://github.com/Perl/perl5/commit/34667d08d3bf4da83ed39a692fb83467dc30a4a6)).
  - The hash table used for "negative entries" is [uthash](https://github.com/troydhanson/uthash), by T. Hanson and A. O'Dwyer (and their collaborators)
  - The JSON library used for the CLI is [cJSON](https://github.com/DaveGamble/cJSON), by D. Gamble (and their collaborators).

The memoization extensions, test suite, and benchmark suite are my own.

FYI: Most data in the paper was generated on a 10-node compute cluster. On a standard desktop it would take several days/weeks(?) to replicate.
