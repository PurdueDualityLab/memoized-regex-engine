# Copyright 2007-2009 Russ Cox.  All Rights Reserved.
# Use of this source code is governed by a BSD-style
# license that can be found in the LICENSE file.

CC=gcc
CFLAGS=-ggdb -Wall -O2

TARG=re
OFILES=\
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

HFILES=\
	regexp.h\
	y.tab.h\
	vendor/avl_tree.h\
	vendor/cJSON.h\
	rle.h\

re: $(OFILES)
	$(CC) -o re $(OFILES)

vendor/avl_tree.o:
	cd vendor; make; cd -;

vendor/cJSON.o: vendor/avl_tree.o
	# Do nothing, vendor/Makefile handles this
	
%.o: %.c $(HFILES)
	$(CC) -c $(CFLAGS) $*.c

y.tab.h y.tab.c: parse.y
	bison -v -y parse.y

clean:
	rm -f *.o core re y.tab.[ch] y.output
	cd vendor; make clean; cd -