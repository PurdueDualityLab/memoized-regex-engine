// Copyright 2007-2009 Russ Cox.  All Rights Reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "regexp.h"

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
	fprintf(stderr, "usage: re {none|full|indeg|loop} {none|neg|rle} regexp [string ...]\n");
	fprintf(stderr, "  The first argument is the memoization strategy\n");
	fprintf(stderr, "  The second argument is the memo table encoding scheme\n");
	fprintf(stderr, "  Engine follows partial-match semantics");
	exit(2);
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
	int i, j, k, l, memoMode, memoEncoding;
	Regexp *re;
	Prog *prog;
	char *sub[MAXSUB];

	if(argc < 4)
		usage();
	
	memoMode = getMemoMode(argv[1]);
	memoEncoding = getEncoding(argv[2]);
	re = parse(argv[3]);
	printre(re);
	printf("\n");

	prog = compile(re, memoMode);
	printprog(prog);
	prog->memoMode = memoMode;
	prog->memoEncoding = memoEncoding;

	for(i=4; i<argc; i++) { /* Try each of the strings against the regex */
		printf("#%d %s\n", i, argv[i]);
		for(j=0; j<nelem(tab); j++) { /* Go through all matchers */
			if (strcmp(tab[j].name, "backtrack") != 0) { /* We just care about backtrack */
				continue;
			}
			printf("%s ", tab[j].name);
			memset(sub, 0, sizeof sub);
			if(!tab[j].fn(prog, argv[i], sub, nelem(sub))) {
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
					printf("%d", (int)(sub[l] - argv[i]));
				printf(",");
				if(sub[l+1] == nil)
					printf("?");
				else
					printf("%d", (int)(sub[l+1] - argv[i]));
				printf(")");
			}
			printf("\n");
		}
	}
	return 0;
}
