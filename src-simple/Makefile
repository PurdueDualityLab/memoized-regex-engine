# Copyright 2007-2009 Russ Cox.  All Rights Reserved.
# Extensions are Copyright 2020 James Davis. All rights Reserved.
# Use of this source code is governed by a BSD-style
# license that can be found in the LICENSE file.

CC=gcc
CFLAGS=-ggdb -Wall -Werror -O2

TARG=re
OFILES=\
	regexp.o\
	memoize.o\
	backtrack.o\
	compile.o\
	main.o\
	pike.o\
	recursive.o\
	sub.o\
	thompson.o\
	y.tab.o\
	vendor/avl_tree.o\
	vendor/cJSON.o\
	rle.o\
	log.o\

RLE_TEST_OFILES=\
	vendor/avl_tree.o\
	rle.o\
	log.o\

HFILES=\
	regexp.h\
	y.tab.h\
	vendor/avl_tree.h\
	vendor/cJSON.h\
	rle.h\
	log.h\

re: $(OFILES)
	$(CC) -o re $(OFILES)

vendor/avl_tree.o:
	cd vendor; make; cd -;

vendor/cJSON.o: vendor/avl_tree.o
	# Do nothing, vendor/Makefile handles this
	
%.o: %.c $(HFILES)
	$(CC) -c $(CFLAGS) $*.c

y.tab.h y.tab.c: parse.y
	${BISONPATH}bison -v -y parse.y

clean:
	rm -f *.o core re y.tab.[ch] y.output
	cd vendor; make clean; cd -

_testhelper:
	make re;
	$(CC) -o rle-test rle-test.c $(RLE_TEST_OFILES)

semtests: _testhelper
	MEMOIZATION_LOGLVL=debug ./rle-test && cd ../eval; MEMOIZATION_LOGLVL=silent ./unittest-prototype.py --semanticOnly

perftests: _testhelper
	MEMOIZATION_LOGLVL=debug ./rle-test && cd ../eval; MEMOIZATION_LOGLVL=silent ./unittest-prototype.py --perfOnly

tests: _testhelper
	MEMOIZATION_LOGLVL=debug ./rle-test && cd ../eval; MEMOIZATION_LOGLVL=silent ./unittest-prototype.py