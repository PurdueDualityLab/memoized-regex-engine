// Copyright 2007-2009 Russ Cox.  All Rights Reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "regexp.h"
#include "jsmn.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct Query Query;

struct Query
{
	char *regex;
	char *input;
};

// https://github.com/zserge/jsmn/blob/master/example/simple.c
static int jsonStrEq(char *jsonStr, jsmntok_t *tok, const char *s) {
  if (tok->type == JSMN_STRING && (int) strlen(s) == tok->end - tok->start &&
      strncmp(jsonStr + tok->start, s, tok->end - tok->start) == 0) {
    return 0;
  }
  return -1;
}

struct {
	char *name;
	int (*fn)(Prog*, char*, char**, int);
} tab[] = {
	"recursive", recursiveprog,
	"recursiveloop", recursiveloopprog,
	"backtrack", backtrack,
	"thompson", thompsonvm,
	"pike", pikevm,
};

void
usage(void)
{
	fprintf(stderr, "usage: re {none|full|indeg|loop} {none|neg|rle} { regexp string | -f patternAndStr.json }\n");
	fprintf(stderr, "  The first argument is the memoization strategy\n");
	fprintf(stderr, "  The second argument is the memo table encoding scheme\n");
	exit(2);
}

char *loadFile(char *fileName)
{
	FILE *f;
	long fsize;
	char *string;

	// https://stackoverflow.com/a/14002993
	f = fopen(fileName, "r");
	fseek(f, 0, SEEK_END);
	fsize = ftell(f);
	fseek(f, 0, SEEK_SET);

	string = mal(fsize + 1);
	fread(string, 1, fsize, f);
	fclose(f);

	string[fsize] = 0;
	return string;
}

Query
loadQuery(char *inFile)
{
	int i, nKeys = 0;
	Query q;
	char *rawJson;
	jsmn_parser p;
	jsmntok_t t[128]; /* We expect no more than 128 JSON tokens */

	// Read file
	printf("Reading %s\n", inFile);
	rawJson = loadFile(inFile);
	printf("Contents: <%s>\n", rawJson);

	// Parse contents
	printf("json parse\n");
	jsmn_init(&p);
	nKeys = jsmn_parse(&p, rawJson, strlen(rawJson), t, 128);
	printf("%d keys\n", nKeys);
	if (nKeys < 0)
		assert(!"json parse failed\n");

	// Extract regex and input
	printf("extracting pattern and input\n");
	if (nKeys < 1 || t[0].type != JSMN_OBJECT)
		assert(!"Object expected\n");

	/* Loop over all keys of the root object */
	for (i = 1; i < nKeys; i++) {
		printf("i: %d\n", i);
		printf("key: %.*s\n", t[i].end - t[i].start, rawJson + t[i].start);
		if (jsonStrEq(rawJson, &t[i], "pattern") == 0) {
			//printf("pattern: %s\n", rawJson + t[i + 1].start);
			q.regex = strndup(rawJson + t[i + 1].start, t[i+1].end - t[i+1].start);
			printf("regex: <%s>\n", q.regex);
			i++;
		} else if (jsonStrEq(rawJson, &t[i], "input") == 0) {
			//printf("input: %s\n", JSMN_STRING + t[i + 1].start);
			q.input = strndup(rawJson + t[i + 1].start, t[i+1].end - t[i+1].start);
			printf("input: <%s>\n", q.input);
			i++;
		} else {
			assert(!"Unexpected key\n");
		}
	}

	free(rawJson);
	return q;
}

int
getMemoMode(char *arg)
{
	if (strcmp(arg, "none") == 0)
		return MEMO_NONE;
	else if (strcmp(arg, "full") == 0)
		return MEMO_FULL;
	else if (strcmp(arg, "indeg") == 0)
		return MEMO_IN_DEGREE_GT1;
	else if (strcmp(arg, "loop") == 0)
		return MEMO_LOOP_DEST;
    else {
		fprintf(stderr, "Error, unknown memostrategy %s\n", arg);
		usage();
	}
}

int
getEncoding(char *arg)
{
	if (strcmp(arg, "none") == 0)
		return ENCODING_NONE;
	else if (strcmp(arg, "neg") == 0)
		return ENCODING_NEGATIVE;
	else if (strcmp(arg, "rle") == 0)
		return ENCODING_RLE;
    else {
		fprintf(stderr, "Error, unknown encoding %s\n", arg);
		usage();
	}
}

int
main(int argc, char **argv)
{
	int j, k, l, memoMode, memoEncoding;
	Query q;
	Regexp *re;
	Prog *prog;
	char *sub[MAXSUB];

	if(argc < 4)
		usage();
	
	memoMode = getMemoMode(argv[1]);
	memoEncoding = getEncoding(argv[2]);

	if (strcmp(argv[3], "-f") == 0) {
		q = loadQuery(argv[4]);
	} else {
		q.regex = argv[3];
		q.input = argv[4];
	}

	re = parse(q.regex);
	printre(re);
	printf("\n");

	prog = compile(re, memoMode);
	printprog(prog);
	prog->memoMode = memoMode;
	prog->memoEncoding = memoEncoding;

	printf("Candidate string: %s\n", q.input);
	for(j=0; j<nelem(tab); j++) { /* Go through all matchers */
		if (strcmp(tab[j].name, "backtrack") != 0) { /* We just care about backtrack */
			continue;
		}
		printf("%s ", tab[j].name);
		memset(sub, 0, sizeof sub);
		if(!tab[j].fn(prog, q.input, sub, nelem(sub))) {
			printf("-no match-\n");
			continue;
		}
		printf("match");
		for(k=MAXSUB; k>0; k--)
			if(sub[k-1])
				break;
		for(l=0; l<k; l+=2) {
			printf(" (");
			if(sub[l] == nil)
				printf("?");
			else
				printf("%d", (int)(sub[l] - q.input));
			printf(",");
			if(sub[l+1] == nil)
				printf("?");
			else
				printf("%d", (int)(sub[l+1] - q.input));
			printf(")");
		}
		printf("\n");
	}
	return 0;
}
