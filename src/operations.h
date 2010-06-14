#ifndef OPERATIONS_H
#define OPERATIONS_H

#include <stdio.h>
#include <stdlib.h>


typedef enum {

delimiter,
arity_1,
arity_2,
eval

} operator_type;

typedef enum {

PRIOR_PLUS_MINUS=1,
PRIOR_MULT_DIV=2,
PRIOR_POW=3,
PRIOR_FUNCTION=4,
PRIOR_OPP=4,
PRIOR_NUMBER=6,
PRIOR_VAR=7

} operator_priority;


float Sum(float a, float b);
float Diff(float a, float b);
float Mult(float a, float b);
float Div(float a, float b);
float Pow(float a, float b);
float Opp(float x);
float Sin(float x);
float Cos(float x);
float Tan(float x);
float Abs(float x);
float Sqrt(float x);
float Log(float x);
float Log10(float x);
float Exp(float x);
float Floor(float x);
float Round(float x);
float Ceil(float x);
float Max(float x, float y);
float Min(float x, float y);


typedef float Operand;

typedef struct Operator{
short id;
operator_type type;
operator_priority prior;
float (*function1)(float);
float (*function2)(float, float);
} Operator;


extern Operator EVAL;
extern Operator VARX;
extern Operator VARY;
extern Operator VARX_HISTORY;
extern Operator VARY_HISTORY;
extern Operator TIMES;
extern Operator DIV;
extern Operator POW;
extern Operator PLUS;
extern Operator MINUS;
extern Operator SIN;
extern Operator COS;
extern Operator TAN;
extern Operator EXP;
extern Operator LOG;
extern Operator LOG10;
extern Operator ABS;
extern Operator OPP;
extern Operator SQUARE_ROOT;
extern Operator FLOOR;
extern Operator ROUND;
extern Operator CEIL;

#endif
