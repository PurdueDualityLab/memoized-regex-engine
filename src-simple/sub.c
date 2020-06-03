// Copyright 2007-2009 Russ Cox.  All Rights Reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "regexp.h"

Sub *freesub;

Sub*
newsub(int n, char *start)
{
	Sub *s;
	
	s = freesub;
	if(s != nil)
		freesub = (Sub*)s->sub[0];
	else
		s = mal(sizeof *s);
	s->nsub = n;
	s->start = start;
	s->ref = 1;
	return s;
}

Sub*
incref(Sub *s)
{
	s->ref++;
	return s;
}

Sub*
update(Sub *s, int i, char *p)
{
	Sub *s1;
	int j;

	if(s->ref > 1) {
		/* Fork */
		s1 = newsub(s->nsub, s->start);
		for(j=0; j<s->nsub; j++)
			s1->sub[j] = s->sub[j];
		s->ref--;
		s = s1;
	}
	s->sub[i] = p;
	return s;
}

void
decref(Sub *s)
{
	if(--s->ref == 0) {
		s->sub[0] = (char*)freesub;
		freesub = s;
	}
}

int
isgroupset(Sub *s, int g)
{
	assert(g*2 <= MAXSUB);
	return s->sub[2*g] != nil && s->sub[2*g + 1] != nil;
}