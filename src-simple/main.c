// Copyright 2007-2009 Russ Cox.  All Rights Reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "regexp.h"
#include "vendor/cJSON.h"
#include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct Query Query;

struct Query
{
	char *regex;
	char *input;
};

struct {
	char *name;
	int (*fn)(Prog*, char*, char**, int);
} tab[] = {
	{"recursive", recursiveprog},
	{"recursiveloop", recursiveloopprog},
	{"backtrack", backtrack},
	{"thompson", thompsonvm},
	{"pike", pikevm},
};

void
usage(void)
{
	/* TODO: Diagnose cases where rle-tuned doesn't help */
	fprintf(stderr, "usage: re {none|full|indeg|loop} {none|neg|rle|rle-tuned} { regexp string | -f patternAndStr.json }\n");
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
	assert(fread(string, 1, fsize, f) == fsize);
	fclose(f);

	string[fsize] = 0;
	return string;
}

Query
loadQuery(char *inFile)
{
	Query q;
	char *rawJson;
	cJSON *parsedJson, *key;

	if (access(inFile, F_OK) != 0) {
		assert(!"No such file\n");
	}

	// Read file
	printf("HELLO\n");
	logMsg(LOG_INFO, "Reading %s", inFile);
	rawJson = loadFile(inFile);
	logMsg(LOG_INFO, "Contents: <%s>", rawJson);

	// Parse contents
	logMsg(LOG_INFO, "json parse");
	parsedJson = cJSON_Parse(rawJson);
	logMsg(LOG_INFO, "%d keys", cJSON_GetArraySize(parsedJson));
	assert(cJSON_GetArraySize(parsedJson) >= 2);
	
	key = cJSON_GetObjectItem(parsedJson, "pattern");
	assert(key != NULL);
	q.regex = strdup(key->valuestring);
	logMsg(LOG_INFO, "regex: <%s>", q.regex);

	key = cJSON_GetObjectItem(parsedJson, "input");
	assert(key != NULL);
	q.input = strdup(key->valuestring);
	logMsg(LOG_INFO, "input: <%s>", q.input);

	cJSON_Delete(parsedJson);
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
		return -1; // Compiler warning
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
	else if (strcmp(arg, "rle-tuned") == 0)
		return ENCODING_RLE_TUNED;
    else {
		fprintf(stderr, "Error, unknown encoding %s\n", arg);
		usage();
		return -1; // Compiler warning
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

	logMsg(LOG_INFO, "Candidate string: %s", q.input);
	for(j=0; j<nelem(tab); j++) { /* Go through all matchers */
		if (strcmp(tab[j].name, "backtrack") != 0) { /* We just care about backtrack */
			continue;
		}
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
