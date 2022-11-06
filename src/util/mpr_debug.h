
#ifndef __MPR_DEBUG_H__
#define __MPR_DEBUG_H__

/**** Subscriptions ****/
#ifdef DEBUG
void print_subscription_flags(int flags);
#endif

#define RETURN_UNLESS(condition) { if (!(condition)) { return; }}
#define RETURN_ARG_UNLESS(condition, arg) { if (!(condition)) { return arg; }}
#define DONE_UNLESS(condition) { if (!(condition)) { goto done; }}
#define FUNC_IF(func, arg) { if (arg) { func(arg); }}
#define PROP(NAME) MPR_PROP_##NAME

#if DEBUG
#define TRACE_RETURN_UNLESS(a, ret, ...) \
if (!(a)) { trace(__VA_ARGS__); return ret; }
#define TRACE_DEV_RETURN_UNLESS(a, ret, ...) \
if (!(a)) { trace_dev(dev, __VA_ARGS__); return ret; }
#define TRACE_NET_RETURN_UNLESS(a, ret, ...) \
if (!(a)) { trace_net(__VA_ARGS__); return ret; }
#else
#define TRACE_RETURN_UNLESS(a, ret, ...) if (!(a)) { return ret; }
#define TRACE_DEV_RETURN_UNLESS(a, ret, ...) if (!(a)) { return ret; }
#define TRACE_NET_RETURN_UNLESS(a, ret, ...) if (!(a)) { return ret; }
#endif

/*! Debug tracer */
#if defined(__GNUC__) || defined(WIN32)
#ifdef DEBUG
#include <stdio.h>
#include <assert.h>
#define trace(...) { printf("-- " __VA_ARGS__); }
#define trace_graph(...)  { printf("\x1B[31m-- <graph>\x1B[0m " __VA_ARGS__);}
#define trace_net(...)  { printf("\x1B[33m-- <network>\x1B[0m  " __VA_ARGS__);}
#define die_unless(a, ...) { if (!(a)) { printf("-- " __VA_ARGS__); assert(a); } }
#else /* !DEBUG */
#define trace(...) {}
#define trace_graph(...) {}
#define trace_net(...) {}
#define die_unless(...) {}
#endif /* DEBUG */
#else /* !__GNUC__ */
#define trace(...) {};
#define trace_graph(...) {};
#define trace_net(...) {};
#define die_unless(...) {};
#endif /* __GNUC__ */

#endif /* __MPR_DEBUG_H__ */
