#ifndef __MPR_EXPR_TRACE_H__
#define __MPR_EXPR_TRACE_H__

#ifdef DEBUG
    #define TRACE_PARSE 0 /* Set non-zero to see trace during parse. */
    #define TRACE_EVAL 0 /* Set non-zero to see trace during evaluation. */
#else
    #define TRACE_PARSE 0 /* Set non-zero to see trace during parse. */
    #define TRACE_EVAL 0 /* Set non-zero to see trace during evaluation. */
#endif

#endif /* __MPR_EXPR_TRACE_H__ */
