
#ifndef __MPR_THREAD_DATA_H__
#define __MPR_THREAD_DATA_H__

#include "config.h"

#ifdef HAVE_ARPA_INET_H
 #include <arpa/inet.h>
#else
 #ifdef HAVE_WINSOCK2_H
  #include <winsock2.h>
 #endif
#endif

typedef struct _mpr_thread_data {
    void *object;
#ifdef HAVE_LIBPTHREAD
    pthread_t thread;
#else
#ifdef HAVE_WIN32_THREADS
    HANDLE thread;
#endif
#endif
    int block_ms;
    volatile int is_active;
    volatile int is_done;
} mpr_thread_data_t, *mpr_thread_data;

#endif /* __MPR_THREAD_DATA_H__ */
