// Copyright 2007-2009 Russ Cox.  All Rights Reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

%{
#include <stdlib.h>
#include <ctype.h>
#include "regexp.h"
#include "log.h"

static int yylex(void);
static void yyerror(char*);
static Regexp *parsed_regexp;
static int nparen;

static char _curlyString[64];
static int _curlyStringIx = 0;
typedef struct _curlyNumbers {
	int min;
	int max;
} curlyNumbers;

static void _handleCurlyChar(int chr) {
	if (isdigit(chr) || chr == ',') {
		_curlyString[_curlyStringIx++] = chr;
	}
	else {
		printf("curlyString invalid char: %c\n", chr);
		yyerror("curlyString: invalid char");
	}
}

// Parse {1}, {1,2}, {,1}, and {1,} into a curlyNumbers
static curlyNumbers parseCurlies(char *str) {
	curlyNumbers cn;
	int low;
	int high;
	char *comma;

	comma = strchr(str, ',');
	if (comma == NULL) { // {1}
		low = high = atoi(str);
	} else if (comma == str) { // {,1}
		low = -1;
		high = atoi(str+1);
	} else if (comma == str + strlen(str) - 1) { // {1,}
		low = atoi(str);
		high = -1;
	} else {
		low = atoi(str);
		high = atoi(comma+1);
	}

	cn.min = low;
	cn.max = high;
	if (cn.min >= 0 && cn.max >= 0 && cn.min > cn.max)
		yyerror("A{M,N}: M must be <= N");

	return cn;
}

static int DISABLE_CAPTURES = 0; // We ignore captures during parsing of (?=lookahead)

%}

%union {
	Regexp *re;
	int c;
	int nparen;
	char *curlyString;
}

%token	<c> CHAR EOL
%type	<re>	alt concat repeat curly single escape line ccc charRanges charRange charRangeChar
%type	<nparen> count
%type   <curlyString> curlyString

%%

line: 
	alt EOL
	{
		parsed_regexp = $1;
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
|	single curly
	{
		$$ = $2;
		$$->left = $1;
	}
|	single curly '?'
	{
		$$ = $2;
		$$->left = $1;
		$$->n = 1;
	}
;

curly:
	'{'
	{
		_curlyStringIx = 0;
	}
	curlyString '}'
	{
		_curlyString[_curlyStringIx] = '\0';
		curlyNumbers cn = parseCurlies(_curlyString);
		$$ = reg(Curly, nil, nil);
		$$->curlyMin = cn.min;
		$$->curlyMax = cn.max;
	}
;

curlyString:
	CHAR
	{
		_handleCurlyChar($1);
		$$ = _curlyString;
	}
|	curlyString CHAR
	{
		_handleCurlyChar($2);
		$$ = _curlyString;
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
		// Is it a ZWA (\b \A etc.) or a normal char escape?
		if ($2 == 'b' || $2 == 'B' || $2 == 'A' || $2 == 'Z' || $2 == 'z') {
			$$ = reg(InlineZWA, nil, nil);
		} else {
			$$ = reg(CharEscape, nil, nil);
		}
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
|	'\\' '{'
	{
		$$ = reg(CharEscape, nil, nil);
		$$->ch = '{';
	}
|	'\\' '}'
	{
		$$ = reg(CharEscape, nil, nil);
		$$->ch = '}';
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
		//printf("Lookahead a\n");
		DISABLE_CAPTURES = 1;
	}
	alt ')'
	{
		//printf("Lookahead b\n");
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
|   '^'
	{
		$$ = reg(InlineZWA, nil, nil);
		$$->ch = '^';
	}
|   '$'
	{
		$$ = reg(InlineZWA, nil, nil);
		$$->ch = '$';
	}
;

ccc:
	'[' '-' ']'
	{
		$$ = reg(CustomCharClass, nil, nil);
		$$->plusDash = 1;
	}
|	'[' charRanges ']'
	{
		$$ = reg(CustomCharClass, $2, nil);
		$$->plusDash = 1;
		$$->ccInvert = 0;
	}
	// Variant with dash -- unambiguous. Cannot do "charRanges '-'" because yacc is an LR(1) -- ambiguous?
	// (Hence we have a rewriting pass to convert [a-] into [-a])
|	'[' '-' charRanges ']'
	{
		$$ = reg(CustomCharClass, $3, nil);
		$$->plusDash = 1;
		$$->ccInvert = 0;
	}
	// Inverted
|	'[' '^' '-' ']'
	{
		$$ = reg(CustomCharClass, nil, nil);
		$$->plusDash = 1;
		$$->ccInvert = 1;
	}
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

// Structure is a Reg linked list: (CharRange 1 with details) L (CharRange 2 with details) L ...
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
|   escape /* TODO For perfect accuracy, in many regex engines, eg [\b] denotes a backspace character. We ignore context so it means a boundary. */
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
|   '{'
	{
		$$ = reg(Lit, nil, nil);
		$$->ch = '{';
	}
|   '}'
	{
		$$ = reg(Lit, nil, nil);
		$$->ch = '}';
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

	if (input == NULL || *input == 0)
		return EOL;
	c = *input++;
	if (strchr("^|*+?(){}:=.\\[^-]$", c))
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

static int
startsWithAnchor(Regexp *r)
{
	Regexp *curr = r;
	while (curr != NULL) {
		switch (curr->type) {
		case InlineZWA:
			if (curr->ch == '^' || curr->ch == 'A')
				return 1;
			else
				return 0;
		case Alt:
		case Cat:
				curr = curr->left;
				break;
		default:
			return 0;
		}
	}

	return 0;
}

static int
endsWithAnchor(Regexp *r)
{
	Regexp *curr = r;
	while (curr != NULL) {
		switch (curr->type) {
		case InlineZWA:
			if (curr->ch == '$' || curr->ch == 'z' || curr->ch == 'Z')
				return 1;
			else
				return 0;
		case Alt:
		case Cat:
			curr = curr->right;
			break;
		default:
			return 0;
		}
	}

	return 0;
}

static void
_moveDashToBeginningOfCCC(char *s)
{
	int len = strlen(s);
	int i, j;

	int start = 0;
	int esc = 0;
	int inCCC = 0;
	int negated = 0;
	for (i = 0; i < len; i++) {
		if (esc) {
			esc = 0;
			continue;
		}

		if (s[i] == '\\') {
			esc = 1;
			continue;
		}

		if (inCCC && s[i] == '-' && s[i+1] == ']') {
			// Switcheroo
			int newLoc = negated ? start + 2 : start + 1;

			// Copy rest right
			for (j = i; j >= newLoc; j--) {
				s[j] = s[j-1];
			}

			// Move - left
			s[newLoc] = '-';
		}

		if (s[i] == '[') {
			inCCC = 1;
			negated = 0;
			start = i;
		}
		if (s[i] == '^' && inCCC && start == i-1) {
			negated = 1;
		}
		if (s[i] == ']') {
			inCCC = 0;
		}
	}

	return;
}

static void
rewriteSyntax(char *s)
{
	logMsg(LOG_DEBUG, "rewriteSyntax: Before /%s/\n", s); 
	_moveDashToBeginningOfCCC(s);
	logMsg(LOG_DEBUG, "rewriteSyntax: After /%s/\n", s); 
}


Regexp*
parse(char *s)
{
	Regexp *r, *combine;

	rewriteSyntax(s);

	input = s;
	parsed_regexp = nil;
	nparen = 0;
	if(yyparse() != 1)
		yyerror("did not parse");
	if(parsed_regexp == nil)
		yyerror("parser nil");
	
	logMsg(LOG_INFO, "parsed_regexp\n"); 
	printre(parsed_regexp);
	printf("\n");
		
	r = reg(Paren, parsed_regexp, nil);	// $0 parens

	/* Tack on the dotstars */
	combine = r;
	/* This is imperfect, e.g. (^...), but it should cover most regexes. */
	logMsg(LOG_INFO, "parsed_regexp type %d\n", parsed_regexp->type);
	int startAnchor = startsWithAnchor(parsed_regexp);
	if (startAnchor) {
		logMsg(LOG_INFO, "Starts with anchor\n");
	} else {
		logMsg(LOG_INFO, "No ^, tacking on leading .*");
		Regexp *bolDotstar = reg(Star, reg(Dot, nil, nil), nil);
		bolDotstar->n = 1;	// non-greedy
		combine = reg(Cat, bolDotstar, combine);
	}

	int endAnchor = endsWithAnchor(parsed_regexp);
	if (endAnchor) {
		logMsg(LOG_INFO, "Ends with anchor\n");
	} else {
		logMsg(LOG_INFO, "No $, tacking on trailing .*");
		Regexp *eolDotstar = reg(Star, reg(Dot, nil, nil), nil);
		eolDotstar->n = 1;	// non-greedy
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
