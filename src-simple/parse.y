// Copyright 2007-2009 Russ Cox.  All Rights Reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

%{
#include "regexp.h"
#include "log.h"

static int yylex(void);
static void yyerror(char*);
static Regexp *parsed_regexp;
static int nparen;

static int DISABLE_CAPTURES = 0; // We ignore captures during parsing of (?=lookahead)

%}

%union {
	Regexp *re;
	int c;
	int nparen;
}

%token	<c> CHAR EOL
%type	<re>	alt concat repeat single escape line ccc charRanges charRange charRangeChar
%type	<nparen> count

%%

line: 
	'^' alt '$' EOL
	{
		parsed_regexp = $2;
		parsed_regexp->bolAnchor = 1;
		parsed_regexp->eolAnchor = 1;
		return 1;
	}
|	'^' alt EOL
	{
		parsed_regexp = $2;
		parsed_regexp->bolAnchor = 1;
		parsed_regexp->eolAnchor = 0;
		return 1;
	}
|	alt '$' EOL
	{
		parsed_regexp = $1;
		parsed_regexp->bolAnchor = 0;
		parsed_regexp->eolAnchor = 1;
		return 1;
	}
|	alt EOL
	{
		parsed_regexp = $1;
		parsed_regexp->bolAnchor = 0;
		parsed_regexp->eolAnchor = 0;
		return 1;
	}
;

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
|	'\\' '='
	{
		$$ = reg(CharEscape, nil, nil);
		$$->ch = '=';
	}
|	'\\' '.'
	{
		$$ = reg(CharEscape, nil, nil);
		$$->ch = '.';
	}
|	'\\' '['
	{
		$$ = reg(CharEscape, nil, nil);
		$$->ch = '[';
	}
|	'\\' ']'
	{
		$$ = reg(CharEscape, nil, nil);
		$$->ch = ']';
	}
|	'\\' '-'
	{
		$$ = reg(CharEscape, nil, nil);
		$$->ch = '-';
	}
|	'\\' '\\'
	{
		$$ = reg(CharEscape, nil, nil);
		$$->ch = '\\';
	}
|	'\\' '^'
	{
		$$ = reg(CharEscape, nil, nil);
		$$->ch = '^';
	}
|	'\\' '$'
	{
		$$ = reg(CharEscape, nil, nil);
		$$->ch = '$';
	}
;

single:
	'(' count alt ')'
	{
		if (DISABLE_CAPTURES) {
			$$ = $3;
		}
		else {
			$$ = reg(Paren, $3, nil);
			$$->n = $2;
		}
	}
|	'(' '?' ':' alt ')'
	{
		$$ = $4;
	}
|	'(' '?' '='
	{
		printf("Lookahead a\n");
		DISABLE_CAPTURES = 1;
	}
	alt ')'
	{
		printf("Lookahead b\n");
		$$ = reg(Lookahead, $5, nil);
		DISABLE_CAPTURES = 0;
	}
|	escape
	{
		$$ = $1;
	}
|	':'
	{
		$$ = reg(Lit, nil, nil);
		$$->ch = ':';
	}
|	'='
	{
		$$ = reg(Lit, nil, nil);
		$$->ch = '=';
	}
|	ccc
	{
		$$ = $1;
	}
|	CHAR
	{
		$$ = reg(Lit, nil, nil);
		$$->ch = $1;
	}
|	'.'
	{
		$$ = reg(Dot, nil, nil);
	}
|	'-'
	{
		$$ = reg(Lit, nil, nil);
		$$->ch = '-';
	}
;

ccc:
	'[' charRanges ']'
	{
		//printf("[ charRanges ]\n");
		$$ = reg(CustomCharClass, $2, nil);
		$$->plusDash = 1;
		$$->ccInvert = 0;
	}
	// Variant with dash -- unambiguous. Cannot do "charRanges '-'" because yacc is an LR(1) -- ambiguous?
|	'[' '-' charRanges ']'
	{
		//printf("[ - charRanges ]\n");
		$$ = reg(CustomCharClass, $3, nil);
		$$->plusDash = 1;
		$$->ccInvert = 0;
	}
	// Inverted
|   '[' '^' charRanges ']'
	{
		$$ = reg(CustomCharClass, $3, nil);
		$$->ccInvert = 1;
	}
|   '[' '^' '-' charRanges ']'
	{
		$$ = reg(CustomCharClass, $4, nil);
		$$->plusDash = 1;
		$$->ccInvert = 1;
	}
;

// Structure is a linked list: (CharRange 1 with details) L (CharRange 2 with details) L ...
charRanges:
	charRange
|   charRanges charRange
	{
		//printf("charRanges\n");
		$$ = $2;
		$$->left = $1;
	}
;

charRange:
	charRangeChar '-' charRangeChar
	{
		//printf("charRangeChar - charRangeChar\n");
		$$ = reg(CharRange, nil, nil);
		$$->ccLow = $1;
		$$->ccHigh = $3;
	}
|	charRangeChar
	{
		//printf("charRangeChar\n");
		$$ = reg(CharRange, nil, nil);
		$$->ccLow = $1;
		$$->ccHigh = $1;
	}
	;

// Most metachars are overridden inside a CCC
// '-' is either a metachar (in the middle) or a leading literal (['-'...])
// This syntax is limited.
// In Python, for example, '-' works between charRanges as well, or as a suffix, e.g. [a-c-x-y] which is a-c + - + x-y
charRangeChar:
	CHAR
	{
		$$ = reg(Lit, nil, nil);
		$$->ch = $1;
	}
|   escape
|   '.'
	{
		$$ = reg(Lit, nil, nil);
		$$->ch = '.';
	}
|   ':'
	{
		$$ = reg(Lit, nil, nil);
		$$->ch = ':';
	}
|   '='
	{
		$$ = reg(Lit, nil, nil);
		$$->ch = '=';
	}
|   '*'
	{
		$$ = reg(Lit, nil, nil);
		$$->ch = '*';
	}
|   '+'
	{
		$$ = reg(Lit, nil, nil);
		$$->ch = '+';
	}
|   '?'
	{
		$$ = reg(Lit, nil, nil);
		$$->ch = '?';
	}
|   '('
	{
		$$ = reg(Lit, nil, nil);
		$$->ch = '(';
	}
|   ')'
	{
		$$ = reg(Lit, nil, nil);
		$$->ch = ')';
	}
|   '|'
	{
		$$ = reg(Lit, nil, nil);
		$$->ch = '|';
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
	if(strchr("^|*+?():=.\\[^-]$", c))
		return c;
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
		logMsg(LOG_INFO, "No ^, tacking on leading .*");
		combine = reg(Cat, bolDotstar, combine);
	}
	if (!parsed_regexp->eolAnchor) {
		logMsg(LOG_INFO, "No $, tacking on trailing .*");
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
	int i;

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

	case AltList:
		printf("AltList(");
		for (i = 0; i < r->arity; i++) {
			printre(r->children[i]);
			if (i+1 < r->arity) {
				printf(", ");
			}
		}
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

	case CustomCharClass:
		if (r->ccInvert)
			printf("Neg");
		printf("CCC(");
		if (r->mergedRanges) {
			for (i = 0; i < r->arity; i++) {
				printre(r->children[i]);
				if (i + 1 < r->arity)
					printf(",");
			}
		}
		else {
			printre(r->left);
		}
		printf(")");
		break;

	case CharRange:
		if (r->left != NULL) {
			printre(r->left);
			printf(",");
		}
		printf("CharRange(");

		if (r->ccLow == r->ccHigh)
			printre(r->ccLow);
		else {
			printre(r->ccLow);
			printf("-");
			printre(r->ccHigh);
		}
		printf(")");
		break;

	case Backref:
		printf("Backref(%d)", r->cgNum);
		break;

	case Lookahead:
		printf("Lookahead(");
		printre(r->left);
		printf(")");
		break;
	}

}
