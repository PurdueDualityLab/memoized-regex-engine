#include "regexp.h"

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
freereg(Regexp *r)
{
	if (r->left != NULL) {
		freereg(r->left);
	}

	if (r->right != NULL) {
		freereg(r->right);
	}
	
	if (r->children != NULL) {
		int i;
		for (i = 0; i < r->arity; i++) {
			freereg(r->children[i]);
		}
		free(r->children);
	}

	if (r->ccLow != NULL) {
		freereg(r->ccLow);
	}
	if (r->ccHigh != NULL) {
		freereg(r->ccHigh);
	}

	free(r);
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

	case Curly:
		if(r->n)
			printf("Ng");
		printf("Curly:<%d,%d>(", r->curlyMin, r->curlyMax);
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
