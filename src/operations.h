#ifndef OPERATIONS_H
#define OPERATIONS_H

#include <stdio.h>
#include <stdlib.h>


/*! Type of an operator*/
typedef enum {

delimiter,   //!< Parenthesis and end of string or end of line symbols
arity_1,	 //!< Operator or function with one argument
arity_2,	 //!< Operator or function with two arguments
eval		 //!< Variable, history value, or numeric value

} operator_type;

/*! Priority of an operator, used to construct the expression tree */
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

/*! Type of the operator nodes in the expression tree*/
typedef struct Operator{
short id;                         //!< Identifier
operator_type type;				  //!< Operator type : delimiter, arity 1, arity 2 or evaluation
operator_priority prior;		  //!< Priority
float (*function1)(float);		  //!< Pointer to the evaluation function (if type arity_1)
float (*function2)(float, float); //!< Pointer to the evaluation function (if type arity_2)
} Operator;


/*! Operator used for the evaluation of a numeric value */
extern Operator EVAL;

/*! Operator used for the evaluation of the variable x */
extern Operator VARX;

/*! Operator used for the evaluation of the variable y */
extern Operator VARY;

/*! Operator used for the evaluation of a previous value of the variable x */
extern Operator VARX_HISTORY;

/*! Operator used for the evaluation of a previous value of the variable y */
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
