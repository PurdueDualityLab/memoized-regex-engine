// Copyright 2007-2009 Russ Cox.  All Rights Reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

%{
#include "regexp.h"

static int yylex(void);
static void yyerror(char*);
static Regexp *parsed_regexp;
static int nparen;

%}

%union {
	Regexp *re;
	int c;
	int nparen;
}

%token	<c> BOL_ANCHOR CHAR EOL_ANCHOR EOL
%type	<re>	alt concat repeat single escape line 
%type	<nparen> count

%%

line: 
	BOL_ANCHOR alt EOL_ANCHOR EOL
	{
		printf("double anchors\n");
		parsed_regexp = $2;
		parsed_regexp->bolAnchor = 1;
		parsed_regexp->eolAnchor = 1;
		return 1;
	}
|	BOL_ANCHOR alt EOL
	{
		printf("bol anchor\n");
		parsed_regexp = $2;
		parsed_regexp->bolAnchor = 1;
		parsed_regexp->eolAnchor = 0;
		return 1;
	}
|	alt EOL_ANCHOR EOL
	{
		printf("eol anchor\n");
		parsed_regexp = $1;
		parsed_regexp->bolAnchor = 0;
		parsed_regexp->eolAnchor = 1;
		return 1;
	}
|	alt EOL
	{
		printf("no anchors\n");
		parsed_regexp = $1;
		parsed_regexp->bolAnchor = 0;
		parsed_regexp->eolAnchor = 0;
		return 1;
	}

alt:
	concat
|	alt '|' concat
	{
		$$ = reg(Alt, $1, $3);
	}
;

concat:
	repeat
|	concat repeat
	{
		$$ = reg(Cat, $1, $2);
	}
;

repeat:
	single
|	single '*'
	{
		$$ = reg(Star, $1, nil);
	}
|	single '*' '?'
	{
		$$ = reg(Star, $1, nil);
		$$->n = 1;
	}
|	single '+'
	{
		$$ = reg(Plus, $1, nil);
	}
|	single '+' '?'
	{
		$$ = reg(Plus, $1, nil);
		$$->n = 1;
	}
|	single '?'
	{
		$$ = reg(Quest, $1, nil);
	}
|	single '?' '?'
	{
		$$ = reg(Quest, $1, nil);
		$$->n = 1;
	}
;

count:
	{
		$$ = ++nparen;
	}
;

// Anything can be escaped -- breakdown is from yylex()
escape:
	'\\' CHAR
	{
		$$ = reg(CharEscape, nil, nil);
		$$->ch = $2;
	}
|	'\\' '|'
	{
		$$ = reg(CharEscape, nil, nil);
		$$->ch = '|';
	}
|	'\\' '*'
	{
		$$ = reg(CharEscape, nil, nil);
		$$->ch = '*';
	}
|	'\\' '+'
	{
		$$ = reg(CharEscape, nil, nil);
		$$->ch = '+';
	}
|	'\\' '?'
	{
		$$ = reg(CharEscape, nil, nil);
		$$->ch = '?';
	}
|	'\\' '('
	{
		$$ = reg(CharEscape, nil, nil);
		$$->ch = '(';
	}
|	'\\' ')'
	{
		$$ = reg(CharEscape, nil, nil);
		$$->ch = ')';
	}
|	'\\' ':'
	{
		$$ = reg(CharEscape, nil, nil);
		$$->ch = ':';
	}
|	'\\' '.'
	{
		$$ = reg(CharEscape, nil, nil);
		$$->ch = '.';
	}
|	'\\' '\\'
	{
		$$ = reg(CharEscape, nil, nil);
		$$->ch = '\\';
	}
;

single:
	'(' count alt ')'
	{
		$$ = reg(Paren, $3, nil);
		$$->n = $2;
	}
|	'(' '?' ':' alt ')'
	{
		$$ = $4;
	}
|	escape
	{
		$$ = $1;
	}
|	CHAR
	{
		printf("CHAR: %d\n", $1);
		$$ = reg(Lit, nil, nil);
		$$->ch = $1;
	}
|	'.'
	{
		$$ = reg(Dot, nil, nil);
	}
;

%%

static char *input;
static Regexp *parsed_regexp;
static int nparen;
int gen;

static int
yylex(void)
{
	int c;

	if(input == NULL || *input == 0)
		return EOL;
	c = *input++;
	if(strchr("|*+?():.\\", c))
		return c;
	if(c == '^')
		return BOL_ANCHOR;
	if(c == '$')
		return EOL_ANCHOR;
	yylval.c = c;
	return CHAR;
}

void
fatal(char *fmt, ...)
{
	va_list arg;
	
	va_start(arg, fmt);
	fprintf(stderr, "fatal error: ");
	vfprintf(stderr, fmt, arg);
	fprintf(stderr, "\n");
	va_end(arg);
	exit(2);
}

static void
yyerror(char *s)
{
	fatal("%s", s);
}


Regexp*
parse(char *s)
{
	Regexp *r, *bolDotstar, *eolDotstar, *combine;

	input = s;
	parsed_regexp = nil;
	nparen = 0;
	if(yyparse() != 1)
		yyerror("did not parse");
	if(parsed_regexp == nil)
		yyerror("parser nil");
		
	r = reg(Paren, parsed_regexp, nil);	// $0 parens
	bolDotstar = reg(Star, reg(Dot, nil, nil), nil);
	bolDotstar->n = 1;	// non-greedy
	eolDotstar = reg(Star, reg(Dot, nil, nil), nil);
	eolDotstar->n = 1;	// non-greedy

	/* Tack on the dotstars */
	combine = r;
	if (!parsed_regexp->bolAnchor) {
		printf("No ^, tacking on leading .*\n");
		combine = reg(Cat, bolDotstar, combine);
	}
	if (!parsed_regexp->eolAnchor) {
		printf("No $, tacking on trailing .*\n");
		combine = reg(Cat, combine, eolDotstar);
	}
	combine->bolAnchor = parsed_regexp->bolAnchor;
	combine->eolAnchor = parsed_regexp->eolAnchor;
	return combine;
}

void*
mal(int n)
{
	void *v;
	
	v = malloc(n);
	if(v == nil)
		fatal("out of memory");
	memset(v, 0, n);
	return v;
}	

Regexp*
reg(int type, Regexp *left, Regexp *right)
{
	Regexp *r;
	
	r = mal(sizeof *r);
	r->type = type;
	r->left = left;
	r->right = right;
	return r;
}

void
printre(Regexp *r)
{
	switch(r->type) {
	default:
		printf("???");
		break;
	
	case Alt:
		printf("Alt(");
		printre(r->left);
		printf(", ");
		printre(r->right);
		printf(")");
		break;

	case Cat:
		printf("Cat(");
		printre(r->left);
		printf(", ");
		printre(r->right);
		printf(")");
		break;
	
	case Lit:
		printf("Lit(%c)", r->ch);
		break;
	
	case Dot:
		printf("Dot");
		break;

	case CharEscape:
		printf("Esc(%c)", r->ch);
		break;

	case Paren:
		printf("Paren(%d, ", r->n);
		printre(r->left);
		printf(")");
		break;
	
	case Star:
		if(r->n)
			printf("Ng");
		printf("Star(");
		printre(r->left);
		printf(")");
		break;
	
	case Plus:
		if(r->n)
			printf("Ng");
		printf("Plus(");
		printre(r->left);
		printf(")");
		break;
	
	case Quest:
		if(r->n)
			printf("Ng");
		printf("Quest(");
		printre(r->left);
		printf(")");
		break;
	}
}