#include <math.h>

#include "operations.h"
#include "expression.h"

float Sum(float a, float b)
{
	float y;
	y = a + b;
	return y;
}

float Diff(float a, float b)
{
	float y;
	y = a - b;
	return y;
}

float Mult(float a, float b)
{
	float y;
	y = a * b;
	return y;
}

float Div(float a, float b)
{
	float y;
	y = a / b;
	return y;
}

float Pow(float a, float b)
{
	float y;
	y = pow(a, b);
	return y;
}

float Opp(float x)
{
	float y;
	y = -x;
	return y;
}

float Sin(float x)
{
	float y;
	y = sin(x);
	return y;
}

float Cos(float x)
{
	float y;
	y = cos(x);
	return y;
}

float Tan(float x)
{
	float y;
	y = tan(x);
	return y;
}

float Abs(float x)
{
	float y;
	y = fabs(x);
	return y;
}

float Sqrt(float x)
{
	float y;
	y = sqrt(x);
	return y;
}

float Log(float x)
{
	float y;
	y = log(x);
	return y;
}

float Log10(float x)
{
	float y;
	y = log10(x);
	return y;
}

float Exp(float x)
{
	float y;
	y = exp(x);
	return y;
}

float Floor(float x)
{
	float y;
	  y = (float)floor(x);
	return y;
}


float Round(float x)
{
	float y;
	  y = (float)round(x);
	return y;
}


float Ceil(float x)
{
	float y;
	  y = (float)ceil(x);
	return y;
}



Operator VARX={-1,eval, PRIOR_VAR, NULL, NULL};
Operator VARX_HISTORY={-11,eval, PRIOR_VAR, NULL, NULL};

Operator VARY={-2,eval, PRIOR_VAR, NULL, NULL};
Operator VARY_HISTORY={-22,eval, PRIOR_VAR, NULL, NULL};

Operator EVAL={0,eval, PRIOR_NUMBER, NULL, NULL};

Operator PLUS={1,arity_2, PRIOR_PLUS_MINUS, NULL, Sum};
Operator MINUS={2,arity_2, PRIOR_PLUS_MINUS, NULL, Diff};
Operator TIMES={3,arity_2, PRIOR_MULT_DIV, NULL,Mult};
Operator DIV={4,arity_2, PRIOR_MULT_DIV, NULL, Div};
Operator POW={5,arity_2, PRIOR_POW, NULL, Pow};
Operator EXP={6,arity_1, PRIOR_FUNCTION, Exp, NULL};
Operator LOG={7,arity_1, PRIOR_FUNCTION, Log, NULL};
Operator LOG10={8,arity_1, PRIOR_FUNCTION, Log10, NULL};
Operator SQUARE_ROOT={9,arity_1, PRIOR_FUNCTION, Sqrt, NULL};
Operator ABS={10,arity_1, PRIOR_FUNCTION, Abs, NULL};
Operator SIN={11,arity_1, PRIOR_FUNCTION, Sin, NULL};
Operator COS={12,arity_1, PRIOR_FUNCTION, Cos, NULL};
Operator TAN={13,arity_1, PRIOR_FUNCTION, Tan, NULL};
Operator OPP={14,arity_2, PRIOR_OPP, Opp, NULL};
Operator FLOOR={15,arity_1, PRIOR_FUNCTION, Floor, NULL};
Operator ROUND={16,arity_1, PRIOR_FUNCTION, Round, NULL};
Operator CEIL={17,arity_1, PRIOR_FUNCTION, Ceil, NULL};



float EvalTree(Tree *T, float *history_x, float *history_y, int hist_pos, error *err) {

float valueL, valueR;

Operator operat=T->oper;

/* The node is a number*/
if (operat.id==EVAL.id) return T->num;

/* The node is a variable*/
else if (operat.id==VARX.id) return history_x[hist_pos%MAX_HISTORY_ORDER];
else if (operat.id==VARY.id) return history_y[hist_pos%MAX_HISTORY_ORDER];

/* The node is a history value */
else if (operat.id==VARX_HISTORY.id)
    {
        if ( ((int)(fabs(T->num))) > MAX_HISTORY_ORDER-1 )
            {
                 perror("History computation error\n");
                *err=ERR_HISTORY_COMPUTATION;
                return (float)0;
            }

        else return (history_x[((int)fabs(T->num)+hist_pos)%MAX_HISTORY_ORDER]);

    }

else if (operat.id==VARY_HISTORY.id)
    {
        if ( ((int)(fabs(T->num))) > MAX_HISTORY_ORDER-1)
            {
                 perror("History computation error\n");
                *err=ERR_HISTORY_COMPUTATION;
                return (float)0;
            }

        else return (history_y[((int)fabs(T->num)+hist_pos)%MAX_HISTORY_ORDER]);

    }


/* The node is a function or an operator*/
else
    {
        if (T->left!=NULL)
            {
	      valueL=EvalTree(T->left, history_x, history_y, hist_pos, err);
                if (operat.type==arity_1) return (*operat.function1)(valueL);
            }

        else
            {
                perror("Computation error : empty left branch\n");
                *err=ERR_COMPUTATION;
                return (float)0;
            }

        if (T->right!=NULL)
            {
	      valueR=EvalTree(T->right, history_x, history_y, hist_pos, err);

                if (operat.type==arity_2) return (*operat.function2)(valueL, valueR);

                else
                    {
                        perror("Computation error : unknown term\n");
                        *err=ERR_COMPUTATION;
                        return (float)0;
                    }
            }

        else
            {
                perror("Computation error : empty right branch\n");
                *err=ERR_COMPUTATION;
                return (float)0;
            }
    }
}
