
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <mpr/mpr.h>

#include "mpr_AbstractObject.h"
#include "mpr_AbstractObject_Properties.h"
#include "mpr_Device.h"
#include "mpr_Graph.h"
#include "mpr_List.h"
#include "mpr_Map.h"
#include "mpr_Signal_Instance.h"
#include "mpr_Signal.h"
#include "mpr_Time.h"

#include "config.h"

#ifdef HAVE_INTTYPES_H
#include <inttypes.h>
#define PR_MPR_ID PRIu64
#else
#define PR_MPR_ID "llu"
#endif

#define jlong_ptr(a) ((jlong)(uintptr_t)(a))
#define ptr_jlong(a) ((void *)(uintptr_t)(a))

JNIEnv *genv=0;
int bailing=0;

const char *graph_evt_strings[] = {
    "NEW",
    "MODIFIED",
    "REMOVED",
    "EXPIRED"
};

const char *signal_evt_strings[] = {
    "NEW_INSTANCE",
    "UPSTREAM_RELEASE",
    "DOWNSTREAM_RELEASE",
    "OVERFLOW",
    "UPDATE",
    "ALL"
};

enum {
    SIG_CB_UNKNOWN = -1,
    SIG_CB_SCAL_INT = 0,
    SIG_CB_VECT_INT,
    SIG_CB_SCAL_FLT,
    SIG_CB_VECT_FLT,
    SIG_CB_SCAL_DBL,
    SIG_CB_VECT_DBL,
    SIG_CB_SCAL_INT_INST,
    SIG_CB_VECT_INT_INST,
    SIG_CB_SCAL_FLT_INST,
    SIG_CB_VECT_FLT_INST,
    SIG_CB_SCAL_DBL_INST,
    SIG_CB_VECT_DBL_INST,
    NUM_SIG_CB_TYPES
};

const char *signal_update_method_strings[] = {
    "(Lmpr/Signal;Lmpr/signal/Event;ILmpr/Time;)V",
    "(Lmpr/Signal;Lmpr/signal/Event;[ILmpr/Time;)V",
    "(Lmpr/Signal;Lmpr/signal/Event;FLmpr/Time;)V",
    "(Lmpr/Signal;Lmpr/signal/Event;[FLmpr/Time;)V",
    "(Lmpr/Signal;Lmpr/signal/Event;DLmpr/Time;)V",
    "(Lmpr/Signal;Lmpr/signal/Event;[DLmpr/Time;)V",
    "(Lmpr/Signal$Instance;Lmpr/signal/Event;ILmpr/Time;)V",
    "(Lmpr/Signal$Instance;Lmpr/signal/Event;[ILmpr/Time;)V",
    "(Lmpr/Signal$Instance;Lmpr/signal/Event;FLmpr/Time;)V",
    "(Lmpr/Signal$Instance;Lmpr/signal/Event;[FLmpr/Time;)V",
    "(Lmpr/Signal$Instance;Lmpr/signal/Event;DLmpr/Time;)V",
    "(Lmpr/Signal$Instance;Lmpr/signal/Event;[DLmpr/Time;)V",
};

typedef struct {
    jobject signal;
    jobject listener;
    int listener_type;
} signal_jni_context_t, *signal_jni_context;

typedef struct {
    jobject inst;
    jobject listener;
    jobject user_ref;
} inst_jni_context_t, *inst_jni_context;

const char *signal_event_method_strings[] = {

};

/**** Helpers ****/

static inline int is_local(mpr_obj obj)
{
    return obj ? mpr_obj_get_prop_as_int32(obj, MPR_PROP_IS_LOCAL, 0) : 0;
}

static inline int signal_length(mpr_sig sig)
{
    return sig ? mpr_obj_get_prop_as_int32((mpr_obj)sig, MPR_PROP_LEN, 0) : 0;
}

static inline mpr_type signal_type(mpr_sig sig)
{
    return sig ? mpr_obj_get_prop_as_int32((mpr_obj)sig, MPR_PROP_TYPE, 0) : 0;
}

static inline const void* signal_user_data(mpr_sig sig)
{
    return sig ? mpr_obj_get_prop_as_ptr((mpr_obj)sig, MPR_PROP_DATA, 0) : NULL;
}

static void throwIllegalArgumentSignal(JNIEnv *env)
{
    jclass newExcCls =
        (*env)->FindClass(env, "java/lang/IllegalArgumentException");
    if (newExcCls) {
        (*env)->ThrowNew(env, newExcCls,
                         "Signal object is not associated with a mpr_sig.");
    }
}

static void throwIllegalArgument(JNIEnv *env, const char *message)
{
    jclass newExcCls =
    (*env)->FindClass(env, "java/lang/IllegalArgumentException");
    if (newExcCls) {
        (*env)->ThrowNew(env, newExcCls, message);
    }
}

static void throwOutOfMemory(JNIEnv *env)
{
    jclass newExcCls =
        (*env)->FindClass(env, "java/lang/OutOfMemoryException");
    if (newExcCls) {
        char msg[] = "Out of memory";
        (*env)->ThrowNew(env, newExcCls, msg);
    }
}

static mpr_dev get_mpr_obj_from_jobject(JNIEnv *env, jobject jobj)
{
    // TODO check object here
    jclass cls = (*env)->GetObjectClass(env, jobj);
    if (cls) {
        jfieldID val = (*env)->GetFieldID(env, cls, "_obj", "J");
        if (val) {
            jlong s = (*env)->GetLongField(env, jobj, val);
            return (mpr_obj)ptr_jlong(s);
        }
    }
    return 0;
}

static mpr_sig get_inst_from_jobject(JNIEnv *env, jobject obj, mpr_id *id)
{
    // TODO check signal here
    jclass cls = (*env)->GetObjectClass(env, obj);
    if (cls) {
        mpr_sig sig = 0;
        jfieldID val = (*env)->GetFieldID(env, cls, "_sigptr", "J");
        if (val) {
            jlong s = (*env)->GetLongField(env, obj, val);
            sig = (mpr_sig)ptr_jlong(s);
        }
        if (id) {
            val = (*env)->GetFieldID(env, cls, "_id", "J");
            if (val) {
                jlong s = (*env)->GetLongField(env, obj, val);
                *id = (mpr_id)ptr_jlong(s);
            }
        }
        return sig;
    }
    throwIllegalArgumentSignal(env);
    return 0;
}

static mpr_graph get_graph_from_jobject(JNIEnv *env, jobject obj)
{
    // TODO check graph here
    jclass cls = (*env)->GetObjectClass(env, obj);
    if (cls) {
        jfieldID val = (*env)->GetFieldID(env, cls, "_graph", "J");
        if (val) {
            jlong s = (*env)->GetLongField(env, obj, val);
            return (mpr_graph)ptr_jlong(s);
        }
    }
    throwIllegalArgument(env, "Couldn't retrieve graph pointer.");
    return 0;
}

static mpr_time *get_time_from_jobject(JNIEnv *env, jobject obj, mpr_time *time)
{
    if (!obj) return 0;
    jclass cls = (*env)->GetObjectClass(env, obj);
    if (cls) {
        jfieldID sec = (*env)->GetFieldID(env, cls, "sec", "I");
        jfieldID frac = (*env)->GetFieldID(env, cls, "frac", "I");
        if (sec && frac) {
            time->sec = (*env)->GetLongField(env, obj, sec);
            time->frac = (*env)->GetLongField(env, obj, frac);
            return time;
        }
    }
    return 0;
}

static jobject get_jobject_from_time(JNIEnv *env, mpr_time *time)
{
    jobject jtime = 0;
    if (time) {
        jclass cls = (*env)->FindClass(env, "mpr/Time");
        if (cls) {
            jmethodID mid = (*env)->GetMethodID(env, cls, "<init>", "(II)V");
            if (mid) {
                jtime = (*env)->NewObject(env, cls, mid, time->sec, time->frac);
            }
            else {
                printf("Error looking up Time constructor.\n");
                exit(1);
            }
        }
    }
//    else
    // TODO: return MPR_NOW ?
    return jtime;
}

static jobject get_jobject_from_graph_evt(JNIEnv *env, mpr_graph_evt evt)
{
    jobject obj = 0;
    jclass cls = (*env)->FindClass(env, "mpr/graph/Event");
    if (cls) {
        jfieldID fid = (*env)->GetStaticFieldID(env, cls, graph_evt_strings[evt],
                                                "Lmpr/graph/Event;");
        if (fid) {
            obj = (*env)->GetStaticObjectField(env, cls, fid);
        }
        else {
            printf("Error looking up graph/Event field '%s'.\n",
                   graph_evt_strings[evt]);
            exit(1);
        }
    }
    return obj;
}

static jobject get_jobject_from_signal_evt(JNIEnv *env, mpr_sig_evt evt)
{
    jobject obj = 0;
    jclass cls = (*env)->FindClass(env, "mpr/signal/Event");
    const char *evt_str;
    switch (evt) {
        case MPR_SIG_INST_NEW:      evt_str = signal_evt_strings[0];    break;
        case MPR_SIG_REL_UPSTRM:    evt_str = signal_evt_strings[1];    break;
        case MPR_SIG_REL_DNSTRM:    evt_str = signal_evt_strings[2];    break;
        case MPR_SIG_INST_OFLW:     evt_str = signal_evt_strings[3];    break;
        case MPR_SIG_UPDATE:        evt_str = signal_evt_strings[4];    break;
        default:
            printf("Error looking up signal event %d.\n", evt);
            exit(1);
    }
    if (cls) {
        jfieldID fid = (*env)->GetStaticFieldID(env, cls, evt_str,
                                                "Lmpr/signal/Event;");
        if (fid) {
            obj = (*env)->GetStaticObjectField(env, cls, fid);
        }
        else {
            printf("Error looking up signal/Event field '%s'.\n", evt_str);
            exit(1);
        }
    }
    return obj;
}
static jobject build_Value(JNIEnv *env, const int len, mpr_type type,
                           const void *val, mpr_time *time)
{
    jmethodID mid;
    jclass cls = 0;
    jobject ret = 0;

    if (len <= 0 || !val) {
        // return empty Value
        return NULL;
    }

    switch (type) {
        case MPR_BOOL:
        case MPR_INT32: {
            if (1 == len) {
                cls = (*env)->FindClass(env, "java/lang/Integer");
                mid = (*env)->GetMethodID(env, cls, "<init>", "(I)V");
                if (mid)
                    ret = (*env)->NewObject(env, cls, mid, *((int *)val));
            }
            else {
                mid = (*env)->GetMethodID(env, cls, "<init>", "([I)V");
                if (mid) {
                    jintArray arr = (*env)->NewIntArray(env, len);
                    (*env)->SetIntArrayRegion(env, arr, 0, len, val);
                    // TODO: test
                    ret = arr;//(*env)->NewObject(env, cls, mid, arr);
                }
            }
            break;
        }
        case MPR_INT64: {
            if (1 == len) {
                cls = (*env)->FindClass(env, "java/lang/Long");
                mid = (*env)->GetMethodID(env, cls, "<init>", "(J)V");
                if (mid)
                    ret = (*env)->NewObject(env, cls, mid, *((long *)val));
            }
            else {
                mid = (*env)->GetMethodID(env, cls, "<init>", "([J)V");
                if (mid) {
                    jintArray arr = (*env)->NewLongArray(env, len);
                    (*env)->SetLongArrayRegion(env, arr, 0, len, val);
                    // TODO: test
                    ret = arr;//(*env)->NewObject(env, cls, mid, arr);
                }
            }
            break;
        }
        case MPR_FLT: {
            if (1 == len) {
                cls = (*env)->FindClass(env, "java/lang/Float");
                mid = (*env)->GetMethodID(env, cls, "<init>", "(F)V");
                if (mid)
                    ret = (*env)->NewObject(env, cls, mid, *((float *)val));
            }
            else {
                mid = (*env)->GetMethodID(env, cls, "<init>", "([F)V");
                if (mid) {
                    jfloatArray arr = (*env)->NewFloatArray(env, len);
                    (*env)->SetFloatArrayRegion(env, arr, 0, len, val);
                    // TODO: test
                    ret = (*env)->NewObject(env, cls, mid, arr);
                }
            }
            break;
        }
        case MPR_DBL: {
            if (1 == len) {
                cls = (*env)->FindClass(env, "java/lang/Double");
                mid = (*env)->GetMethodID(env, cls, "<init>", "(D)V");
                if (mid)
                    ret = (*env)->NewObject(env, cls, mid, *((double *)val));
            }
            else {
                mid = (*env)->GetMethodID(env, cls, "<init>", "([D)V");
                if (mid) {
                    jdoubleArray arr = (*env)->NewDoubleArray(env, len);
                    (*env)->SetDoubleArrayRegion(env, arr, 0, len, val);
                    ret = (*env)->NewObject(env, cls, mid, arr);
                }
            }
            break;
        }
        case MPR_STR: {
            if (1 == len) {
                ret = (*env)->NewStringUTF(env, (char *)val);
            }
            else {
                ret = (*env)->NewObjectArray(env, len,
                                             (*env)->FindClass(env, "java/lang/String"),
                                             (*env)->NewStringUTF(env, ""));
                int i;
                char **strings = (char**)val;
                for (i = 0; i < len; i++) {
                    (*env)->SetObjectArrayElement(env, ret, i,
                                                  (*env)->NewStringUTF(env, strings[i]));
                }
            }
            break;
        }
        case MPR_TIME: {
            if (1 == len) {
                cls = (*env)->FindClass(env, "mpr/Time");
                mid = (*env)->GetMethodID(env, cls, "<init>", "(II)V");
                mpr_time *t = (mpr_time*)val;
                if (mid)
                    ret = (*env)->NewObject(env, cls, mid, (*t).sec, (*t).frac);
            }
            break;
        }
        case MPR_LIST: {
            cls = (*env)->FindClass(env, "mpr/List");
            if (!cls) {
                printf("failed to find class mpr/List\n");
                break;
            }
            mid = (*env)->GetMethodID(env, cls, "<init>", "(J)V");
            if (!mid) {
                printf("failed to find methodID for mpr/List constructor\n");
                break;
            }
            if (mid)
                ret = (*env)->NewObject(env, cls, mid, val);
            break;
        }
        default:
            printf("ERROR: unhandled value type: %d ('%c')\n", type, type);
            break;
    }
    return ret;
}

static void java_signal_update_cb(mpr_sig sig, mpr_sig_evt evt, mpr_id id,
                                  int len, mpr_type type, const void *val,
                                  mpr_time time)
{
    if (bailing)
        return;

    jobject jtime = get_jobject_from_time(genv, &time);

    signal_jni_context ctx = (signal_jni_context)signal_user_data(sig);
    if (!ctx || !ctx->signal || !ctx->listener) {
        printf("Error: missing signal ctx in callback\n");
        return;
    }

    jobject eventobj = get_jobject_from_signal_evt(genv, evt);
    if (!eventobj) {
        printf("Error looking up signal event\n");
        return;
    }
    jobject update_cb = ctx->listener;
    jclass listener_cls = (*genv)->GetObjectClass(genv, update_cb);
    jmethodID mid = 0;
    inst_jni_context ictx;

    // TODO: handler val==NULL
    // prep values
    if (ctx->listener_type <= SIG_CB_UNKNOWN || ctx->listener_type >= NUM_SIG_CB_TYPES)
        return;
    void *sig_ptr = ctx->signal;
    if (ctx->listener_type >= SIG_CB_SCAL_INT_INST) {
        ictx = ((inst_jni_context) mpr_sig_get_inst_data(sig, id));
        if (!ictx || !ictx->inst) {
            printf("error: no instance user data\n");
            return;
        }
        sig_ptr = ictx->inst;
    }
    mid = (*genv)->GetMethodID(genv, listener_cls, "onEvent",
                               signal_update_method_strings[ctx->listener_type]);
    if (!mid) {
        printf("error: problem finding method id '%s'\n",
               signal_update_method_strings[ctx->listener_type]);
        return;
    }
    switch (ctx->listener_type % SIG_CB_SCAL_INT_INST) {
        case SIG_CB_SCAL_INT: {
            int ival;
            switch (type) {
                case MPR_INT32: ival = *(int*)val;              break;
                case MPR_FLT:   ival = (int)*(float*)val;       break;
                case MPR_DBL:   ival = (int)*(double*)val;      break;
            }
            (*genv)->CallVoidMethod(genv, update_cb, mid, sig_ptr, eventobj,
                                    ival, jtime);
            break;
        }
        case SIG_CB_SCAL_FLT: {
            float fval;
            switch (type) {
                case MPR_INT32: fval = (float)*(int*)val;       break;
                case MPR_FLT:   fval = *(float*)val;            break;
                case MPR_DBL:   fval = (float)*(double*)val;    break;
            }
            (*genv)->CallVoidMethod(genv, update_cb, mid, sig_ptr, eventobj,
                                    fval, jtime);
            break;
        }
        case SIG_CB_SCAL_DBL: {
            double dval;
            switch (type) {
                case MPR_INT32: dval = (double)*(int*)val;      break;
                case MPR_FLT:   dval = (double)*(float*)val;    break;
                case MPR_DBL:   dval = *(double*)val;           break;
            }
            (*genv)->CallVoidMethod(genv, update_cb, mid, sig_ptr, eventobj,
                                    dval, jtime);
            break;
        }
        case SIG_CB_VECT_INT: {
            jintArray arr = (*genv)->NewIntArray(genv, len);
            if (!arr)
                return;
            switch (type) {
                case MPR_INT32:
                    (*genv)->SetIntArrayRegion(genv, arr, 0, len, val);
                    break;
                case MPR_FLT: {
                    jint iarr[len];
                    for (int i = 0; i < len; i++)
                        iarr[i] = (int)((float*)val)[i];
                    (*genv)->SetIntArrayRegion(genv, arr, 0, len, iarr);
                    break;
                }
                case MPR_DBL: {
                    jint iarr[len];
                    for (int i = 0; i < len; i++)
                        iarr[i] = (int)((double*)val)[i];
                    (*genv)->SetIntArrayRegion(genv, arr, 0, len, iarr);
                    break;
                }
            }
            (*genv)->CallVoidMethod(genv, update_cb, mid, sig_ptr, eventobj,
                                    arr, jtime);
            (*genv)->DeleteLocalRef(genv, arr);
            break;
        }
        case SIG_CB_VECT_FLT: {
            jfloatArray arr = (*genv)->NewFloatArray(genv, len);
            if (!arr)
                return;
            switch (type) {
                case MPR_INT32: {
                    jfloat farr[len];
                    for (int i = 0; i < len; i++)
                        farr[i] = (float)((int*)val)[i];
                    (*genv)->SetFloatArrayRegion(genv, arr, 0, len, farr);
                    break;
                }
                case MPR_FLT:
                    (*genv)->SetFloatArrayRegion(genv, arr, 0, len, val);
                    break;
                case MPR_DBL: {
                    jfloat farr[len];
                    for (int i = 0; i < len; i++)
                        farr[i] = (float)((double*)val)[i];
                    (*genv)->SetFloatArrayRegion(genv, arr, 0, len, farr);
                    break;
                }
            }
            (*genv)->CallVoidMethod(genv, update_cb, mid, sig_ptr, eventobj,
                                    arr, jtime);
            (*genv)->DeleteLocalRef(genv, arr);
            break;
        }
        case SIG_CB_VECT_DBL: {
            jdoubleArray arr = (*genv)->NewDoubleArray(genv, len);
            if (!arr)
                return;
            switch (type) {
                case MPR_INT32: {
                    jdouble darr[len];
                    for (int i = 0; i < len; i++)
                        darr[i] = (double)((int*)val)[i];
                    (*genv)->SetDoubleArrayRegion(genv, arr, 0, len, darr);
                    break;
                }
                case MPR_FLT: {
                    jdouble darr[len];
                    for (int i = 0; i < len; i++)
                        darr[i] = (double)((float*)val)[i];
                    (*genv)->SetDoubleArrayRegion(genv, arr, 0, len, darr);
                    break;
                }
                case MPR_DBL:
                    (*genv)->SetDoubleArrayRegion(genv, arr, 0, len, val);
                    break;
            }
            (*genv)->CallVoidMethod(genv, update_cb, mid, sig_ptr, eventobj,
                                    arr, jtime);
            (*genv)->DeleteLocalRef(genv, arr);
            break;
        }
        default:
            printf("error: unhandled callback type\n");
    }
    if (jtime)
        (*genv)->DeleteLocalRef(genv, jtime);
}

static int signal_listener_type(const char *cMethodSig)
{
    int type = SIG_CB_UNKNOWN;
    if (strncmp(cMethodSig, "public void ", 12)) {
        printf("error parsing method signature (1)\n");
        return type;
    }
    cMethodSig += 12; // advance to method name
    char *dot = strchr(cMethodSig, '.');
    if (!dot) {
        printf("error parsing method signature (2)\n");
        return type;
    }
    cMethodSig = dot + 1;
    if (strncmp(cMethodSig, "onEvent(", 8)) {
        printf("error parsing method signature (3)\n");
        return type;
    }
    cMethodSig += 8;
    // check if is singleton or instanced listener
    int instanced = 0;
    if (strncmp(cMethodSig, "mpr.Signal$Instance,mpr.signal.Event,", 37) == 0) {
        instanced = 1;
        cMethodSig += 37;
    }
    else if (strncmp(cMethodSig, "mpr.Signal,mpr.signal.Event,", 28) == 0) {
        instanced = 0;
        cMethodSig += 28;
    }
    else {
        printf("error parsing method signature (4)\n");
        return type;
    }
    // check data type
    if (strncmp(cMethodSig, "int", 3) == 0) {
        cMethodSig += 3;
        if (strncmp(cMethodSig, "[]", 2) == 0)
            type = SIG_CB_VECT_INT;
        else
            type = SIG_CB_SCAL_INT;
    }
    else if (strncmp(cMethodSig, "float", 5) == 0) {
        cMethodSig += 5;
        if (strncmp(cMethodSig, "[]", 2) == 0)
            type = SIG_CB_VECT_FLT;
        else
            type = SIG_CB_SCAL_FLT;
    }
    else if (strncmp(cMethodSig, "double", 3) == 0) {
        cMethodSig += 3;
        if (strncmp(cMethodSig, "[]", 2) == 0)
            type = SIG_CB_VECT_DBL;
        else
            type = SIG_CB_SCAL_DBL;
    }
    else {
        printf("error parsing method signature (5)\n");
        return type;
    }
    if (instanced)
        type += 6;
    printf("returning handler type %d\n", type);
    return type;
}

static jobject create_signal_object(JNIEnv *env, jobject devobj,
                                    signal_jni_context ctx, jobject listener,
                                    jstring methodSig, mpr_sig sig)
{
    jobject sigobj = 0;
    jmethodID mid;
    // Create a wrapper class for this signal
    jclass cls = (*env)->FindClass(env, "mpr/Signal");
    if (cls) {
        mid = (*env)->GetMethodID(env, cls, "<init>", "(J)V");
        sigobj = (*env)->NewObject(env, cls, mid, jlong_ptr(sig));
    }

    if (!sigobj) {
        printf("Error creating signal wrapper class.\n");
        exit(1);
    }

    mpr_obj_set_prop((mpr_obj)sig, MPR_PROP_DATA, NULL, 1, MPR_PTR, ctx, 0);
    ctx->signal = (*env)->NewGlobalRef(env, sigobj);
    ctx->listener_type = SIG_CB_UNKNOWN;

    if (!listener || !methodSig ||
        !(*env)->IsInstanceOf(env, listener,
                              (*env)->FindClass(env, "mpr/signal/Listener"))) {
        ctx->listener = 0;
        return sigobj;
    }

    const char *cMethodSig = (*env)->GetStringUTFChars(env, methodSig, 0);
    ctx->listener_type = signal_listener_type(cMethodSig);
    (*env)->ReleaseStringUTFChars(env, methodSig, cMethodSig);
    if (ctx->listener_type > SIG_CB_UNKNOWN)
        ctx->listener = (*env)->NewGlobalRef(env, listener);

    return sigobj;
}

/**** mpr_AbstractObject.h ****/

JNIEXPORT void JNICALL Java_mpr_AbstractObject_mprObjectFree
  (JNIEnv *env, jobject obj, jlong abstr)
{
    mpr_obj mobj = (mpr_obj)ptr_jlong(abstr);
    if (!mobj || MPR_DEV != mpr_obj_get_type(mobj) || !is_local(mobj))
        return;

    mpr_dev dev = (mpr_dev)mobj;
    /* Free all references to Java objects. */
    mpr_obj *sigs = mpr_dev_get_sigs(dev, MPR_DIR_ANY);
    while (sigs) {
        mpr_sig temp = (mpr_sig)*sigs;
        sigs = mpr_list_get_next(sigs);
        // do not call instance event callbacks
        mpr_sig_set_cb(temp, 0, 0);
        // check if we have active instances
        int i;
        for (i = 0; i < mpr_sig_get_num_inst(temp, MPR_STATUS_ACTIVE); i++) {
            mpr_id id = mpr_sig_get_inst_id(temp, i, MPR_STATUS_ACTIVE);
            inst_jni_context ictx;
            ictx = mpr_sig_get_inst_data(temp, id);
            if (!ictx)
                continue;
            if (ictx->inst)
                (*env)->DeleteGlobalRef(env, ictx->inst);
            if (ictx->listener)
                (*env)->DeleteGlobalRef(env, ictx->listener);
            if (ictx->user_ref)
                (*env)->DeleteGlobalRef(env, ictx->user_ref);
            free(ictx);
        }
        signal_jni_context ctx = (signal_jni_context)signal_user_data(temp);
        if (ctx->signal)
            (*env)->DeleteGlobalRef(env, ctx->signal);
        if (ctx->listener)
            (*env)->DeleteGlobalRef(env, ctx->listener);
        free(ctx);
    }
    mpr_dev_free(dev);
}

JNIEXPORT jlong JNICALL Java_mpr_AbstractObject_graph
  (JNIEnv *env, jobject jobj, jlong ptr)
{
    mpr_obj mobj = (mpr_obj) ptr_jlong(ptr);
    return mobj ? jlong_ptr(mpr_obj_get_graph(mobj)) : 0;
}

JNIEXPORT void JNICALL Java_mpr_AbstractObject__1push
  (JNIEnv *env, jobject jobj, jlong ptr)
{
    mpr_obj obj = (mpr_obj)ptr_jlong(ptr);
    if (obj)
        mpr_obj_push(obj);
}

/**** mpr_AbstractObject_Properties.h ****/

JNIEXPORT jint JNICALL Java_mpr_AbstractObject_00024Properties__1count
  (JNIEnv *env, jobject jobj, jlong ptr)
{
    mpr_obj mobj = (mpr_obj) ptr_jlong(ptr);
    return mpr_obj_get_num_props(mobj, 0);
}

/*
 * Class:     mpr_AbstractObject_Properties
 * Method:    _get
 * Signature: (JLjava/lang/Object;)Ljava/util/Map/Entry;
 */
JNIEXPORT jobject JNICALL Java_mpr_AbstractObject_00024Properties__1get
  (JNIEnv *env, jobject jobj, jlong ptr, jint id, jobject jkey)
{
    mpr_obj mobj = (mpr_obj) ptr_jlong(ptr);
    const char *ckey = 0;
    mpr_prop prop;
    mpr_type type;
    int len;
    const void *val;

    // key Object must be either string, mpr_prop or index

    if (jkey) {
        ckey = (*env)->GetStringUTFChars(env, jkey, 0);
        prop = mpr_obj_get_prop_by_key(mobj, ckey, &len, &type, &val, 0);
    }
    else
        prop = mpr_obj_get_prop_by_idx(mobj, id, &ckey, &len, &type, &val, 0);

    jobject o = (MPR_PROP_UNKNOWN != prop)
                ? build_Value(env, len, type, val, 0)
                : NULL;

    jclass cls = (*env)->FindClass(env, "java/util/AbstractMap$SimpleEntry");
    if (NULL == cls) {
        return NULL;
    }
    jmethodID init = (*env)->GetMethodID(env, cls, "<init>", "(Ljava/lang/Object;Ljava/lang/Object;)V");
    if (jkey) {
        jobject mapEntry = (*env)->NewObject(env, cls, init, jkey, o);
        (*env)->ReleaseStringUTFChars(env, jkey, ckey);
        return mapEntry;
    }
    else {
        jkey = (*env)->NewStringUTF(env, ckey);
        jobject mapEntry = (*env)->NewObject(env, cls, init, jkey, o);
        return mapEntry;
    }
}

JNIEXPORT jobject JNICALL Java_mpr_AbstractObject_00024Properties__1keySet
  (JNIEnv *env, jobject jobj, jlong ptr)
{
    mpr_obj mobj = (mpr_obj) ptr_jlong(ptr);
    jclass cls = (*env)->FindClass(env, "java/util/Set");
    if (NULL == cls) {
        return NULL;
    }
    jmethodID init = (*env)->GetMethodID(env, cls, "<init>", "()V");
    if (NULL == init)
        printf("problem with init MID\n");
    jobject set = (*env)->NewObject(env, cls, init, 1);
    jmethodID add = (*env)->GetMethodID(env, cls, "add", "Ljava/lang/String;");
    if (NULL == add)
        printf("problem with add MID\n");
    int i, num = mpr_obj_get_num_props(mobj, 0);
    const char *key;
    for (i = 0; i < num; i++) {
        mpr_obj_get_prop_by_idx(mobj, i, &key, NULL, NULL, NULL, NULL);
        if (!key) {
            printf("problem retrieving key for object property %d\n", i);
            continue;
        }
        jobject jkey = (*env)->NewStringUTF(env, key);
        (*env)->CallObjectMethod(env, set, add, jkey);
    }
    return set;
}

JNIEXPORT jobject JNICALL Java_mpr_AbstractObject_00024Properties__1entrySet
  (JNIEnv *env, jobject jobj, jlong ptr)
{
    mpr_obj mobj = (mpr_obj) ptr_jlong(ptr);
    jclass cls = (*env)->FindClass(env, "java/util/Set");
    if (NULL == cls) {
        return NULL;
    }
    jmethodID init = (*env)->GetMethodID(env, cls, "<init>", "()V");
    if (NULL == init)
        printf("problem with init MID\n");
    jobject set = (*env)->NewObject(env, cls, init, 1);
    jmethodID add = (*env)->GetMethodID(env, cls, "add", "Ljava/lang/String;");
    if (NULL == add)
        printf("problem with add MID\n");
    int i, len, num = mpr_obj_get_num_props(mobj, 0);
    mpr_type type;
    const void *val;
    for (i = 0; i < num; i++) {
        mpr_obj_get_prop_by_idx(mobj, i, NULL, &len, &type, &val, NULL);
        jobject o = build_Value(env, len, type, val, 0);
        (*env)->CallObjectMethod(env, set, add, o);
    }
    return set;
}

JNIEXPORT void JNICALL Java_mpr_AbstractObject_00024Properties__1put
  (JNIEnv *env, jobject jobj, jlong ptr, jint id, jstring jkey, jobject jval)
{
    mpr_obj mobj = (mpr_obj) ptr_jlong(ptr);

    jclass cls = (*env)->GetObjectClass(env, jval);
    if (!cls) {
        printf("couldn't find class for %p\n", jval);
        return;
    }
    jmethodID mid;

    const char *ckey = jkey ? (*env)->GetStringUTFChars(env, jkey, 0) : 0;

    if ((*env)->IsInstanceOf(env, jval, (*env)->FindClass(env, "java/lang/Boolean"))) {
        mid = (*env)->GetMethodID(env, cls, "booleanValue", "()Z");
        int val = (JNI_TRUE == (*env)->CallBooleanMethod(env, jval, mid));
        mpr_obj_set_prop(mobj, id, ckey, 1, MPR_BOOL, &val, 1);
    }
    else if ((*env)->IsInstanceOf(env, jval, (*env)->FindClass(env, "java/lang/Byte"))) {
        mid = (*env)->GetMethodID(env, cls, "byteValue", "()B");
        int val = (*env)->CallByteMethod(env, jval, mid);
        mpr_obj_set_prop(mobj, id, ckey, 1, MPR_INT32, &val, 1);
    }
    else if ((*env)->IsInstanceOf(env, jval, (*env)->FindClass(env, "java/lang/Character"))) {
        mid = (*env)->GetMethodID(env, cls, "charValue", "()C");
        int val = (*env)->CallCharMethod(env, jval, mid);
        mpr_obj_set_prop(mobj, id, ckey, 1, MPR_INT32, &val, 1);
    }
    else if ((*env)->IsInstanceOf(env, jval, (*env)->FindClass(env, "java/lang/Double"))) {
        mid = (*env)->GetMethodID(env, cls, "doubleValue", "()D");
        double val = (*env)->CallDoubleMethod(env, jval, mid);
        mpr_obj_set_prop(mobj, id, ckey, 1, MPR_DBL, &val, 1);
    }
    else if ((*env)->IsInstanceOf(env, jval, (*env)->FindClass(env, "java/lang/Float"))) {
        mid = (*env)->GetMethodID(env, cls, "floatValue", "()F");
        float val = (*env)->CallFloatMethod(env, jval, mid);
        mpr_obj_set_prop(mobj, id, ckey, 1, MPR_FLT, &val, 1);
    }
    else if ((*env)->IsInstanceOf(env, jval, (*env)->FindClass(env, "java/lang/Integer"))) {
        mid = (*env)->GetMethodID(env, cls, "intValue", "()I");
        int val = (*env)->CallIntMethod(env, jval, mid);
        mpr_obj_set_prop(mobj, id, ckey, 1, MPR_INT32, &val, 1);
    }
    else if ((*env)->IsInstanceOf(env, jval, (*env)->FindClass(env, "java/lang/Long"))) {
        mid = (*env)->GetMethodID(env, cls, "longValue", "()L");
        long val = (*env)->CallLongMethod(env, jval, mid);
        mpr_obj_set_prop(mobj, id, ckey, 1, MPR_INT64, &val, 1);
    }
    else if ((*env)->IsInstanceOf(env, jval, (*env)->FindClass(env, "java/lang/Short"))) {
        mid = (*env)->GetMethodID(env, cls, "shortValue", "()D");
        int val = (*env)->CallShortMethod(env, jval, mid);
        mpr_obj_set_prop(mobj, id, ckey, 1, MPR_INT32, &val, 1);
    }
    else if ((*env)->IsInstanceOf(env, jval, (*env)->FindClass(env, "java/lang/String"))) {
        const char *val = (*env)->GetStringUTFChars(env, jval, 0);
        mpr_obj_set_prop(mobj, id, ckey, 1, MPR_STR, val, 1);
    }
    // TODO: handle arrays, etc

    if (jkey && ckey)
        (*env)->ReleaseStringUTFChars(env, jkey, ckey);
}

JNIEXPORT void JNICALL Java_mpr_AbstractObject_00024Properties__1remove
  (JNIEnv *env, jobject jobj, jlong ptr, jint id, jstring jkey)
{
    mpr_obj mobj = (mpr_obj) ptr_jlong(ptr);
    const char *ckey = jkey ? (*env)->GetStringUTFChars(env, jkey, 0) : 0;
    mpr_obj_remove_prop(mobj, id, ckey);
    if (jkey && ckey)
        (*env)->ReleaseStringUTFChars(env, jkey, ckey);
}

/**** mpr_Graph.h ****/

JNIEXPORT jlong JNICALL Java_mpr_Graph_mprGraphNew
  (JNIEnv *env, jobject obj, jint flags)
{
    return jlong_ptr(mpr_graph_new(flags));
}

JNIEXPORT void JNICALL Java_mpr_Graph_mprGraphFree
  (JNIEnv *env, jobject obj, jlong jgraph)
{
    mpr_graph g = (mpr_graph)ptr_jlong(jgraph);
    mpr_graph_free(g);
}

JNIEXPORT jstring JNICALL Java_mpr_Graph_getInterface
  (JNIEnv *env, jobject obj)
{
    mpr_graph g = get_graph_from_jobject(env, obj);
    if (!g)
        return NULL;
    const char *iface = mpr_graph_get_interface(g);
    return build_Value(env, 1, MPR_STR, iface, 0);
}

JNIEXPORT jobject JNICALL Java_mpr_Graph_setInterface
  (JNIEnv *env, jobject obj, jstring jiface)
{
    mpr_graph g = get_graph_from_jobject(env, obj);
    if (g) {
        const char *ciface = (*env)->GetStringUTFChars(env, jiface, 0);
        mpr_graph_set_interface(g, ciface);
        (*env)->ReleaseStringUTFChars(env, jiface, ciface);
    }
    return obj;
}

JNIEXPORT jstring JNICALL Java_mpr_Graph_getAddress
  (JNIEnv *env, jobject obj)
{
    mpr_graph g = get_graph_from_jobject(env, obj);
    if (!g)
        return NULL;
    const char *address = mpr_graph_get_address(g);
    return build_Value(env, 1, MPR_STR, address, 0);
}

JNIEXPORT jobject JNICALL Java_mpr_Graph_setAddress
  (JNIEnv *env, jobject obj, jstring jgroup, jint port)
{
    mpr_graph g = get_graph_from_jobject(env, obj);
    if (g) {
        const char *cgroup = (*env)->GetStringUTFChars(env, jgroup, 0);
        mpr_graph_set_address(g, cgroup, port);
        (*env)->ReleaseStringUTFChars(env, jgroup, cgroup);
    }
    return obj;
}

JNIEXPORT jobject JNICALL Java_mpr_Graph_poll
  (JNIEnv *env, jobject obj, jint block_ms)
{
    genv = env;
    bailing = 0;
    mpr_graph g = get_graph_from_jobject(env, obj);
    if (g)
        mpr_graph_poll(g, block_ms);
    return obj;
}

JNIEXPORT void JNICALL Java_mpr_Graph_mprGraphSubscribe
  (JNIEnv *env, jobject obj, jlong jgraph, jobject jdev, jint flags, jint lease)
{
    mpr_graph g = (mpr_graph)ptr_jlong(jgraph);
    mpr_dev dev = (mpr_dev)get_mpr_obj_from_jobject(env, jdev);
    if (g && dev)
        mpr_graph_subscribe(g, dev, flags, lease);
}

JNIEXPORT jobject JNICALL Java_mpr_Graph_unsubscribe
  (JNIEnv *env, jobject obj, jobject jdev)
{
    mpr_graph g = get_graph_from_jobject(env, obj);
    mpr_dev dev = (mpr_dev)get_mpr_obj_from_jobject(env, jdev);
    if (g && dev)
        mpr_graph_unsubscribe(g, dev);
    return obj;
}

static jobject get_jobject_from_mpr_obj(mpr_obj mobj)
{
    if (!mobj)
        return NULL;
    jclass cls;
    mpr_type type = mpr_obj_get_type(mobj);
    switch (type) {
        case MPR_DEV:
            cls = (*genv)->FindClass(genv, "mpr/Device");
            break;
        case MPR_SIG:
            cls = (*genv)->FindClass(genv, "mpr/Signal");
            break;
        case MPR_MAP:
            cls = (*genv)->FindClass(genv, "mpr/Map");
            break;
        default:
            printf("error: unknown type %d in get_jobject_from_mpr_obj\n", type);
            return NULL;
    }
    if (!cls)
        return NULL;
    jmethodID mid = (*genv)->GetMethodID(genv, cls, "<init>", "(J)V");
    return mid ? (*genv)->NewObject(genv, cls, mid, jlong_ptr(mobj)) : NULL;
}

static void java_graph_cb(mpr_graph g, mpr_obj mobj, mpr_graph_evt evt,
                          const void *user_data)
{
    if (bailing || !user_data)
        return;

    jobject jobj = get_jobject_from_mpr_obj(mobj);
    jobject eventobj = get_jobject_from_graph_evt(genv, evt);
    if (!eventobj) {
        printf("Error looking up graph event\n");
        return;
    }

    jobject listener = (jobject)user_data;
    jclass cls = (*genv)->GetObjectClass(genv, listener);
    jmethodID mid;
    if (cls) {
        switch (mpr_obj_get_type(mobj)) {
            case MPR_DEV:
                mid = (*genv)->GetMethodID(genv, cls, "onEvent",
                                           "(Lmpr/Device;Lmpr/graph/Event;)V");
                break;
            case MPR_SIG:
                mid = (*genv)->GetMethodID(genv, cls, "onEvent",
                                           "(Lmpr/Signal;Lmpr/graph/Event;)V");
                break;
            case MPR_MAP:
                mid = (*genv)->GetMethodID(genv, cls, "onEvent",
                                           "(Lmpr/Map;Lmpr/graph/Event;)V");
                break;
            default:
                return;
        }
        if (mid) {
            (*genv)->CallVoidMethod(genv, listener, mid, jobj, eventobj);
            if ((*genv)->ExceptionOccurred(genv))
                bailing = 1;
        }
        else {
            printf("Error looking up onEvent method.\n");
        }
    }
}

JNIEXPORT void JNICALL Java_mpr_Graph_addCallback
  (JNIEnv *env, jobject obj, jlong jgraph, jobject listener)
{
    mpr_graph g = (mpr_graph)ptr_jlong(jgraph);
    if (!g || !listener)
        return;
    jobject o = (*env)->NewGlobalRef(env, listener);
    mpr_graph_add_cb(g, java_graph_cb, MPR_DEV, o);
}

JNIEXPORT void JNICALL Java_mpr_Graph_removeCallback
  (JNIEnv *env, jobject obj, jlong jgraph, jobject listener)
{
    mpr_graph g = (mpr_graph)ptr_jlong(jgraph);
    if (!g || !listener)
        return;
    // TODO: fix mismatch in user_data
    mpr_graph_remove_cb(g, java_graph_cb, listener);
    (*env)->DeleteGlobalRef(env, listener);
}

JNIEXPORT jlong JNICALL Java_mpr_Graph_devices
  (JNIEnv *env, jobject obj, jlong jgraph)
{
    mpr_graph g = (mpr_graph) ptr_jlong(jgraph);
    return g ? jlong_ptr(mpr_graph_get_objs(g, MPR_DEV)) : 0;
}

JNIEXPORT jlong JNICALL Java_mpr_Graph_signals
  (JNIEnv *env, jobject obj, jlong jgraph)
{
    mpr_graph g = (mpr_graph) ptr_jlong(jgraph);
    return g ? jlong_ptr(mpr_graph_get_objs(g, MPR_SIG)) : 0;
}

JNIEXPORT jlong JNICALL Java_mpr_Graph_maps
  (JNIEnv *env, jobject obj, jlong jgraph)
{
    mpr_graph g = (mpr_graph) ptr_jlong(jgraph);
    return g ? jlong_ptr(mpr_graph_get_objs(g, MPR_MAP)) : 0;
}

/**** mpr_Device.h ****/

JNIEXPORT jlong JNICALL Java_mpr_Device_mprDeviceNew
  (JNIEnv *env, jobject obj, jstring name, jobject jgraph)
{
    mpr_graph g = jgraph ? get_graph_from_jobject(env, jgraph) : 0;
    const char *cname = (*env)->GetStringUTFChars(env, name, 0);
    mpr_dev dev = mpr_dev_new(cname, g);
    (*env)->ReleaseStringUTFChars(env, name, cname);
    return jlong_ptr(dev);
}

JNIEXPORT jint JNICALL Java_mpr_Device_poll
  (JNIEnv *env, jobject obj, jint block_ms)
{
    genv = env;
    bailing = 0;
    mpr_dev dev = (mpr_dev)get_mpr_obj_from_jobject(env, obj);
    return dev ? mpr_dev_poll(dev, block_ms) : 0;
}

JNIEXPORT jobject JNICALL Java_mpr_Device_add_1signal
  (JNIEnv *env, jobject obj, jlong jdev, jint dir, jstring name, jint len,
   jint type, jstring unit, jobject min, jobject max, jobject numInst,
   jobject listener, jstring methodSig)
{
    if (!name || (len <= 0) || (type != MPR_FLT && type != MPR_INT32 && type != MPR_DBL))
        return 0;

    mpr_dev dev = (mpr_dev)ptr_jlong(jdev);
    if (!dev)
        return 0;

    const char *cname = (*env)->GetStringUTFChars(env, name, 0);
    const char *cunit = unit ? (*env)->GetStringUTFChars(env, unit, 0) : 0;
    int num_inst = 0;
    if (numInst) {
        jclass cls = (*env)->FindClass(env, "java/lang/Integer");
        jmethodID getVal = (*env)->GetMethodID(env, cls, "intValue", "()I");
        num_inst = (*env)->CallIntMethod(env, numInst, getVal);
    }
    mpr_sig sig = mpr_sig_new(dev, dir, cname, len, type, cunit,
                              0, 0, num_inst ? &num_inst : NULL, 0, 0);
    if (listener) {
        mpr_sig_set_cb(sig, java_signal_update_cb, MPR_SIG_UPDATE);
    }

    (*env)->ReleaseStringUTFChars(env, name, cname);
    if (unit)
        (*env)->ReleaseStringUTFChars(env, unit, cunit);

    if (!sig)
        return 0;

    signal_jni_context ctx = ((signal_jni_context)
                              calloc(1, sizeof(signal_jni_context_t)));
    if (!ctx) {
        throwOutOfMemory(env);
        return 0;
    }
    jobject sigobj = create_signal_object(env, obj, ctx, listener, methodSig, sig);

    if (!sigobj) {
        printf("Could not create signal object.\n");
        return 0;
    }
    if (min) {
        Java_mpr_AbstractObject_00024Properties__1put(env, sigobj, jlong_ptr(sig),
                                                      MPR_PROP_MIN, NULL, min);
    }
    if (max) {
        Java_mpr_AbstractObject_00024Properties__1put(env, sigobj, jlong_ptr(sig),
                                                      MPR_PROP_MAX, NULL, max);
    }
    return sigobj;
}

JNIEXPORT void JNICALL Java_mpr_Device_mprDeviceRemoveSignal
  (JNIEnv *env, jobject obj, jlong jdev, jobject jsig)
{
    mpr_dev dev = (mpr_dev) ptr_jlong(jdev);
    if (!dev || !is_local((mpr_obj)dev))
        return;
    mpr_sig sig = (mpr_sig)get_mpr_obj_from_jobject(env, jsig);
    if (sig) {
        // do not call instance event callbacks
        mpr_sig_set_cb(sig, 0, 0);

        // check if we have active instances
        while (mpr_sig_get_num_inst(sig, MPR_STATUS_ACTIVE)) {
            mpr_id id = mpr_sig_get_inst_id(sig, 0, MPR_STATUS_ACTIVE);
            if (!id)
                continue;
            inst_jni_context ictx;
            ictx = mpr_sig_get_inst_data(sig, id);
            if (!ictx)
                continue;
            if (ictx->inst)
                (*env)->DeleteGlobalRef(env, ictx->inst);
            if (ictx->listener)
                (*env)->DeleteGlobalRef(env, ictx->listener);
            if (ictx->user_ref)
                (*env)->DeleteGlobalRef(env, ictx->user_ref);
            free(ictx);
        }

        signal_jni_context ctx = (signal_jni_context)signal_user_data(sig);
        if (ctx->signal)
            (*env)->DeleteGlobalRef(env, ctx->signal);
        if (ctx->listener)
            (*env)->DeleteGlobalRef(env, ctx->listener);
        free(ctx);

        mpr_sig_free(sig);
    }
}

JNIEXPORT jboolean JNICALL Java_mpr_Device_ready
  (JNIEnv *env, jobject obj)
{
    mpr_dev dev = (mpr_dev)get_mpr_obj_from_jobject(env, obj);
    return dev ? mpr_dev_get_is_ready(dev) : 0;
}

JNIEXPORT jobject JNICALL Java_mpr_Device_startQueue
  (JNIEnv *env, jobject obj, jobject jtime)
{
    mpr_dev dev = (mpr_dev)get_mpr_obj_from_jobject(env, obj);
    if (!dev)
        return 0;
    mpr_time time, *ptime = 0;
    ptime = get_time_from_jobject(env, jtime, &time);
    if (!ptime) {
        mpr_time_set(&time, MPR_NOW);
        jtime = get_jobject_from_time(env, &time);
    }
    mpr_dev_start_queue(dev, time);
    return jtime;
}

JNIEXPORT jobject JNICALL Java_mpr_Device_sendQueue
  (JNIEnv *env, jobject obj, jobject jtime)
{
    mpr_dev dev = (mpr_dev)get_mpr_obj_from_jobject(env, obj);
    if (!dev || !is_local((mpr_obj)dev))
        return 0;
    mpr_time time, *ptime = 0;
    ptime = get_time_from_jobject(env, jtime, &time);
    if (ptime)
        mpr_dev_send_queue(dev, time);
    return obj;
}

JNIEXPORT jlong JNICALL Java_mpr_Device_signals
  (JNIEnv *env, jobject obj, jlong jdev, jint dir)
{
    mpr_dev dev = (mpr_dev) ptr_jlong(jdev);
    return dev ? jlong_ptr(mpr_dev_get_sigs(dev, dir)) : 0;
}

/**** mpr_List.h ****/

JNIEXPORT jobject JNICALL Java_mpr_List__1newObject
  (JNIEnv *env, jobject jobj, jlong listptr)
{
    mpr_obj mobj = (mpr_obj)ptr_jlong(listptr);
    if (!mobj)
        return NULL;
    return get_jobject_from_mpr_obj(mobj);
}

JNIEXPORT jlong JNICALL Java_mpr_List__1copy
  (JNIEnv *env, jobject obj, jlong listptr)
{
    mpr_list list = (mpr_list) ptr_jlong(listptr);
    if (!list)
        return 0;
    return jlong_ptr(mpr_list_get_cpy(list));
}

JNIEXPORT jlong JNICALL Java_mpr_List__1deref
  (JNIEnv *env, jobject obj, jlong list)
{
    void **ptr = (void**)ptr_jlong(list);
    return jlong_ptr(*ptr);
}

JNIEXPORT void JNICALL Java_mpr_List__1filter
  (JNIEnv *env, jobject obj, jlong list, jint id, jstring name, jobject val,
   jint op)
{
// TODO
}

JNIEXPORT void JNICALL Java_mpr_List__1free
  (JNIEnv *env, jobject obj, jlong list)
{
    mpr_obj *objs = (mpr_obj*) ptr_jlong(list);
    if (objs)
        mpr_list_free(objs);
}

JNIEXPORT jlong JNICALL Java_mpr_List__1isect
  (JNIEnv *env, jobject obj, jlong lhs, jlong rhs)
{
    mpr_obj *objs_lhs = (mpr_obj*) ptr_jlong(lhs);
    mpr_obj *objs_rhs = (mpr_obj*) ptr_jlong(rhs);

    if (!objs_lhs || !objs_rhs)
        return 0;

    // use a copy of rhs
    mpr_obj *objs_rhs_cpy = mpr_list_get_cpy(objs_rhs);
    return jlong_ptr(mpr_list_get_isect(objs_lhs, objs_rhs_cpy));
}

JNIEXPORT jlong JNICALL Java_mpr_List__1union
  (JNIEnv *env, jobject obj, jlong lhs, jlong rhs)
{
    mpr_obj *objs_lhs = (mpr_obj*) ptr_jlong(lhs);
    mpr_obj *objs_rhs = (mpr_obj*) ptr_jlong(rhs);

    if (!objs_rhs)
        return lhs;

    // use a copy of rhs
    mpr_dev *objs_rhs_cpy = mpr_list_get_cpy(objs_rhs);
    return jlong_ptr(mpr_list_get_union(objs_lhs, objs_rhs_cpy));
}

JNIEXPORT jint JNICALL Java_mpr_List__1size
  (JNIEnv *env, jobject obj, jlong list)
{
    mpr_obj *objs = (mpr_obj*) ptr_jlong(list);
    return objs ? mpr_list_get_size(objs) : 0;
}

JNIEXPORT jlong JNICALL Java_mpr_List__1next
  (JNIEnv *env, jobject obj, jlong list)
{
    mpr_obj *objs = (mpr_obj*) ptr_jlong(list);
    return objs ? jlong_ptr(mpr_list_get_next(objs)) : 0;
}

JNIEXPORT jlong JNICALL Java_mpr_List__1diff
  (JNIEnv *env, jobject obj, jlong lhs, jlong rhs)
{
    mpr_obj *objs_lhs = (mpr_obj*) ptr_jlong(lhs);
    mpr_obj *objs_rhs = (mpr_obj*) ptr_jlong(rhs);

    if (!objs_lhs || !objs_rhs)
        return lhs;

    // use a copy of rhs
    mpr_obj *objs_rhs_cpy = mpr_list_get_cpy(objs_rhs);
    return jlong_ptr(mpr_list_get_diff(objs_lhs, objs_rhs_cpy));
}

JNIEXPORT jboolean JNICALL Java_mpr_List__1contains
  (JNIEnv *env, jobject jobj, jlong list, jobject item);

/*
 * Class:     mpr_List
 * Method:    _containsAll
 * Signature: (JJ)Z
 */
JNIEXPORT jboolean JNICALL Java_mpr_List__1containsAll
  (JNIEnv *env, jobject jobs, jlong lhs, jlong rhs);

/*
 * Class:     mpr_List
 * Method:    toArray
 * Signature: ()[Lmpr/AbstractObject;
 */
JNIEXPORT jobjectArray JNICALL Java_mpr_List_toArray
  (JNIEnv *env, jobject obj);

/**** mpr_Map.h ****/

JNIEXPORT jlong JNICALL Java_mpr_Map_signals
  (JNIEnv *env, jobject obj, jlong jmap, jint loc)
{
    mpr_map map = (mpr_map) ptr_jlong(jmap);
    return map ? jlong_ptr(mpr_map_get_sigs(map, loc)) : 0;
}

JNIEXPORT jlong JNICALL Java_mpr_Map_signal
  (JNIEnv *env, jobject obj, jlong jmap, jint loc, jint idx)
{
    mpr_map map = (mpr_map) ptr_jlong(jmap);
    if (!map)
        return 0;
    mpr_list l = mpr_map_get_sigs(map, loc & MPR_LOC_ANY);
    if (l) {
        mpr_sig s = (mpr_sig)mpr_list_get_idx(l, idx);
        mpr_list_free(l);
        return jlong_ptr(s);
    }
    return 0;
}

JNIEXPORT jlong JNICALL Java_mpr_Map_mprMapNew
  (JNIEnv *env, jobject obj, jobjectArray jsrc, jobject jdst)
{
    int i, num_src = (*env)->GetArrayLength(env, jsrc);
    if (num_src <= 0 || !jdst)
        return 0;
    mpr_sig src[num_src];
    for (i = 0; i < num_src; i++) {
        jobject o = (*env)->GetObjectArrayElement(env, jsrc, i);
        src[i] = (mpr_sig)get_mpr_obj_from_jobject(env, o);
    }
    mpr_sig dst = (mpr_sig)get_mpr_obj_from_jobject(env, jdst);

    mpr_map map = mpr_map_new(num_src, src, 1, &dst);
    return map ? jlong_ptr(map) : 0;
}

JNIEXPORT jobject JNICALL Java_mpr_Map_refresh
  (JNIEnv *env, jobject obj)
{
    mpr_map map = (mpr_map)get_mpr_obj_from_jobject(env, obj);
    if (map)
        mpr_map_refresh(map);
    return obj;
}

JNIEXPORT jobject JNICALL Java_mpr_Map_addScope
  (JNIEnv *env, jobject obj, jobject jdev)
{
    mpr_map map = (mpr_map)get_mpr_obj_from_jobject(env, obj);
    if (map) {
        mpr_dev dev = (mpr_dev)get_mpr_obj_from_jobject(env, jdev);
        if (dev)
            mpr_map_add_scope(map, dev);
    }
    return obj;
}

JNIEXPORT jobject JNICALL Java_mpr_Map_removeScope
  (JNIEnv *env, jobject obj, jobject jdev)
{
    mpr_map map = (mpr_map)get_mpr_obj_from_jobject(env, obj);
    if (map) {
        mpr_dev dev = (mpr_dev)get_mpr_obj_from_jobject(env, jdev);
        if (dev)
            mpr_map_remove_scope(map, dev);
    }
    return obj;
}

JNIEXPORT jboolean JNICALL Java_mpr_Map_ready
  (JNIEnv *env, jobject obj)
{
    mpr_map map = (mpr_map)get_mpr_obj_from_jobject(env, obj);
    return map ? (mpr_map_get_is_ready(map)) : 0;
}

/**** mpr_Signal_Instances.h ****/

JNIEXPORT jlong JNICALL Java_mpr_Signal_00024Instance_mprInstance
  (JNIEnv *env, jobject obj, jboolean has_id, jlong jid, jobject ref)
{
    mpr_id id = 0;
    mpr_sig sig = get_inst_from_jobject(env, obj, 0);
    if (!sig)
        return 0;

    if (has_id)
        id = jid;
    else if (mpr_sig_get_num_inst(sig, MPR_STATUS_RESERVED)) {
        // retrieve id from a reserved signal instance
        id = mpr_sig_get_inst_id(sig, 0, MPR_STATUS_RESERVED);
    }
    else {
        // try to steal an active instance
        mpr_dev dev = mpr_sig_get_dev(sig);
        id = mpr_dev_generate_unique_id(dev);
        if (!mpr_sig_activate_inst(sig, id)) {
            printf("Could not activate instance with id %"PR_MPR_ID"\n", id);
            return 0;
        }
    }

    inst_jni_context ctx = ((inst_jni_context) mpr_sig_get_inst_data(sig, id));
    if (!ctx) {
        printf("No context found for instance %"PR_MPR_ID"\n", id);
        return 0;
    }

    ctx->inst = (*env)->NewGlobalRef(env, obj);
    if (ref)
        ctx->user_ref = (*env)->NewGlobalRef(env, ref);
    return id;
}

JNIEXPORT void JNICALL Java_mpr_Signal_00024Instance_release
  (JNIEnv *env, jobject obj, jobject jtime)
{
    mpr_id id;
    mpr_sig sig = get_inst_from_jobject(env, obj, &id);
    if (sig) {
        mpr_time time, *ptime = 0;
        if (jtime)
            ptime = get_time_from_jobject(env, jtime, &time);
        mpr_sig_release_inst(sig, id, ptime ? *ptime : MPR_NOW);
    }
}

JNIEXPORT void JNICALL Java_mpr_Signal_00024Instance_mprFreeInstance
  (JNIEnv *env, jobject obj, jlong jsig, jlong id, jobject jtime)
{
    mpr_sig sig = (mpr_sig) ptr_jlong(jsig);
    if (!sig)
        return;
    inst_jni_context ctx = mpr_sig_get_inst_data(sig, id);
    if (ctx) {
        if (ctx->listener)
            (*env)->DeleteGlobalRef(env, ctx->listener);
        if (ctx->inst)
            (*env)->DeleteGlobalRef(env, ctx->inst);
        if (ctx->user_ref)
            (*env)->DeleteGlobalRef(env, ctx->user_ref);
    }
    mpr_time time, *ptime = 0;
    ptime = get_time_from_jobject(env, jtime, &time);
    mpr_sig_release_inst(sig, id, ptime ? *ptime : MPR_NOW);
}

JNIEXPORT jint JNICALL Java_mpr_Signal_00024Instance_isActive
  (JNIEnv *env, jobject obj)
{
    mpr_id id;
    mpr_sig sig = get_inst_from_jobject(env, obj, &id);
    return sig ? mpr_sig_get_inst_is_active(sig, id) : 0;
}

JNIEXPORT jobject JNICALL Java_mpr_Signal_00024Instance_userReference
  (JNIEnv *env, jobject obj)
{
    mpr_id id;
    mpr_sig sig = get_inst_from_jobject(env, obj, &id);
    if (!sig)
        return 0;
    inst_jni_context ctx = ((inst_jni_context) mpr_sig_get_inst_data(sig, id));
    return ctx ? ctx->user_ref : 0;
}

JNIEXPORT jobject JNICALL Java_mpr_Signal_00024Instance_setUserReference
  (JNIEnv *env, jobject obj, jobject userObj)
{
    mpr_id id;
    mpr_sig sig = get_inst_from_jobject(env, obj, &id);
    if (!sig)
        return obj;
    inst_jni_context ctx = ((inst_jni_context) mpr_sig_get_inst_data(sig, id));
    if (ctx) {
        if (ctx->user_ref)
            (*env)->DeleteGlobalRef(env, ctx->user_ref);
        ctx->user_ref = userObj ? (*env)->NewGlobalRef(env, userObj) : 0;
    }
    return obj;
}

/**** mpr_Signal.h ****/

JNIEXPORT jobject JNICALL Java_mpr_Signal_device
  (JNIEnv *env, jobject obj)
{
    jclass cls = (*env)->FindClass(env, "mpr/Device");
    if (!cls)
        return 0;

    mpr_dev dev = 0;
    mpr_sig sig = (mpr_sig)get_mpr_obj_from_jobject(env, obj);
    if (sig)
        dev = mpr_sig_get_dev(sig);

    jmethodID mid = (*env)->GetMethodID(env, cls, "<init>", "(J)V");
    return (*env)->NewObject(env, cls, mid, jlong_ptr(dev));
}

JNIEXPORT void JNICALL Java_mpr_Signal_mprSignalSetCB
  (JNIEnv *env, jobject obj, jlong jsig, jobject listener, jstring methodSig,
    jint flags)
{
    mpr_sig sig = (mpr_sig) ptr_jlong(jsig);
    // signal_jni_context ctx = mpr_obj_get_prop_as_ptr((mpr_obj)sig, MPR_PROP_DATA, 0);
    signal_jni_context ctx = (signal_jni_context)signal_user_data(sig);
    if (!ctx) {
        return;
    }
    if (ctx->listener == listener) {
        mpr_sig_set_cb(sig, java_signal_update_cb, flags);
        return;
    }
    if (ctx->listener) {
        (*env)->DeleteGlobalRef(env, ctx->listener);
    }
    if (listener) {
        const char *cMethodSig = (*env)->GetStringUTFChars(env, methodSig, 0);
        ctx->listener_type = signal_listener_type(cMethodSig);
        (*env)->ReleaseStringUTFChars(env, methodSig, cMethodSig);
        ctx->listener = (*env)->NewGlobalRef(env, listener);
        mpr_sig_set_cb(sig, java_signal_update_cb, flags);
    }
    else {
        ctx->listener = 0;
        mpr_sig_set_cb(sig, NULL, 0);
    }
    return;
}

JNIEXPORT jobject JNICALL Java_mpr_Signal_listener
  (JNIEnv *env, jobject obj)
{
    // TODO
    return NULL;
}

JNIEXPORT void JNICALL Java_mpr_Signal_mprSignalReserveInstances
  (JNIEnv *env, jobject obj, jlong jsig, jint num, jlongArray jids)
{
    mpr_sig sig = (mpr_sig) ptr_jlong(jsig);
    mpr_dev dev;
    if (!sig || !(dev = mpr_sig_get_dev(sig)))
        return;

    mpr_id *ids = 0;
    if (jids) {
        num = (*env)->GetArrayLength(env, jids);
        ids = (mpr_id*)(*env)->GetLongArrayElements(env, jids, NULL);
    }

    inst_jni_context ctx = 0;
    int i;
    for (i = 0; i < num; i++) {
        mpr_id id = ids ? (ids[i]) : mpr_dev_generate_unique_id(dev);

        if (!ctx) {
            ctx = ((inst_jni_context)
                   calloc(1, sizeof(inst_jni_context_t)));
            if (!ctx) {
                throwOutOfMemory(env);
                return;
            }
        }

        if (mpr_sig_reserve_inst(sig, 1, &id, (void**)&ctx))
            ctx = 0;
    }

    if (ctx)
        free(ctx);

    if (ids)
        (*env)->ReleaseLongArrayElements(env, jids, (jlong*)ids, JNI_ABORT);
}

JNIEXPORT jobject JNICALL Java_mpr_Signal_oldestActiveInstance
  (JNIEnv *env, jobject obj)
{
    mpr_sig sig = (mpr_sig)get_mpr_obj_from_jobject(env, obj);
    if (!sig) {
        printf("couldn't retrieve signal in "
               " Java_mpr_Signal_oldestActiveInstance()\n");
        return 0;
    }
    mpr_id id = mpr_sig_get_oldest_inst_id(sig);
    inst_jni_context ctx = ((inst_jni_context) mpr_sig_get_inst_data(sig, id));
    return ctx ? ctx->inst : 0;
}

JNIEXPORT jobject JNICALL Java_mpr_Signal_newestActiveInstance
  (JNIEnv *env, jobject obj)
{
    mpr_sig sig = (mpr_sig)get_mpr_obj_from_jobject(env, obj);
    if (!sig)
        return 0;
    mpr_id id = mpr_sig_get_newest_inst_id(sig);
    inst_jni_context ctx = ((inst_jni_context) mpr_sig_get_inst_data(sig, id));
    return ctx ? ctx->inst : 0;
}

JNIEXPORT jint JNICALL Java_mpr_Signal_numActiveInstances
  (JNIEnv *env, jobject obj)
{
    mpr_sig sig = (mpr_sig)get_mpr_obj_from_jobject(env, obj);
    return sig ? mpr_sig_get_num_inst(sig, MPR_STATUS_ACTIVE) : 0;
}

JNIEXPORT jint JNICALL Java_mpr_Signal_numReservedInstances
  (JNIEnv *env, jobject obj)
{
    mpr_sig sig = (mpr_sig)get_mpr_obj_from_jobject(env, obj);
    return sig ? mpr_sig_get_num_inst(sig, MPR_STATUS_RESERVED) : 0;
}

JNIEXPORT jobject JNICALL Java_mpr_Signal_setValue
  (JNIEnv *env, jobject obj, jlong jid, jobject jval, jobject jtime)
{
    jclass cls = (*env)->GetObjectClass(env, jval);
    if (!cls) {
        printf("couldn't find class for %p\n", jval);
        return obj;
    }
    mpr_sig sig = (mpr_sig)get_mpr_obj_from_jobject(env, obj);
    if (!sig)
        return obj;

    mpr_id id = (mpr_id)ptr_jlong(jid);

    mpr_time time, *ptime = 0;
    ptime = get_time_from_jobject(env, jtime, &time);

    jmethodID mid;

    if ((*env)->IsInstanceOf(env, jval, (*env)->FindClass(env, "java/lang/Integer"))) {
        mid = (*env)->GetMethodID(env, cls, "intValue", "()I");
        int val = (*env)->CallIntMethod(env, jval, mid);
        mpr_sig_set_value(sig, id, 1, MPR_INT32, &val, ptime ? *ptime : MPR_NOW);
    }
    else if ((*env)->IsInstanceOf(env, jval, (*env)->FindClass(env, "java/lang/Float"))) {
        mid = (*env)->GetMethodID(env, cls, "floatValue", "()F");
        float val = (*env)->CallFloatMethod(env, jval, mid);
        mpr_sig_set_value(sig, id, 1, MPR_FLT, &val, ptime ? *ptime : MPR_NOW);
    }
    else if ((*env)->IsInstanceOf(env, jval, (*env)->FindClass(env, "java/lang/Double"))) {
        mid = (*env)->GetMethodID(env, cls, "doubleValue", "()D");
        double val = (*env)->CallDoubleMethod(env, jval, mid);
        mpr_sig_set_value(sig, id, 1, MPR_DBL, &val, ptime ? *ptime : MPR_NOW);
    }
    else if ((*env)->IsInstanceOf(env, jval, (*env)->FindClass(env, "[I"))) {
        int len = (*env)->GetArrayLength(env, jval);
        jint* vals = (*env)->GetIntArrayElements(env, jval, NULL);
        if (vals) {
            mpr_sig_set_value(sig, id, len, MPR_INT32, vals, ptime ? *ptime : MPR_NOW);
            (*env)->ReleaseIntArrayElements(env, jval, vals, JNI_ABORT);
        }
    }
    else if ((*env)->IsInstanceOf(env, jval, (*env)->FindClass(env, "[F"))) {
        int len = (*env)->GetArrayLength(env, jval);
        jfloat* vals = (*env)->GetFloatArrayElements(env, jval, NULL);
        if (vals) {
            mpr_sig_set_value(sig, id, len, MPR_FLT, vals, ptime ? *ptime : MPR_NOW);
            (*env)->ReleaseFloatArrayElements(env, jval, vals, JNI_ABORT);
        }
    }
    else if ((*env)->IsInstanceOf(env, jval, (*env)->FindClass(env, "[D"))) {
        int len = (*env)->GetArrayLength(env, jval);
        jdouble* vals = (*env)->GetDoubleArrayElements(env, jval, NULL);
        if (vals) {
            mpr_sig_set_value(sig, id, len, MPR_DBL, vals, ptime ? *ptime : MPR_NOW);
            (*env)->ReleaseDoubleArrayElements(env, jval, vals, JNI_ABORT);
        }
    }
    else {
        printf("Object type not supported!\n");
    }

    return obj;
}

JNIEXPORT jboolean JNICALL Java_mpr_Signal_hasValue
  (JNIEnv *env, jobject obj, jlong id)
{
    mpr_sig sig = (mpr_sig)get_mpr_obj_from_jobject(env, obj);
    if (sig && mpr_sig_get_value(sig, id, NULL))
        return JNI_TRUE;
    return JNI_FALSE;
}

JNIEXPORT jobject JNICALL Java_mpr_Signal_getValue
  (JNIEnv *env, jobject obj, jlong jid)
{
    mpr_sig sig = (mpr_sig)get_mpr_obj_from_jobject(env, obj);
    mpr_type type = MPR_INT32;
    int len = 0;
    const void *val = 0;
    mpr_time time = {0,1};

    mpr_id id = (mpr_id)ptr_jlong(jid);
    if (sig) {
        type = signal_type(sig);
        len = signal_length(sig);
        val = mpr_sig_get_value(sig, id, &time);
    }
    return build_Value(env, len, type, val, &time);
}

JNIEXPORT jlong JNICALL Java_mpr_Signal_maps
  (JNIEnv *env, jobject obj, jlong jsig, jint dir)
{
    mpr_sig sig = (mpr_sig) ptr_jlong(jsig);
    return sig ? jlong_ptr(mpr_sig_get_maps(sig, dir)) : 0;
}

/**** mpr_Time.h ****/

JNIEXPORT void JNICALL Java_mpr_Time_mprNow
  (JNIEnv *env, jobject obj)
{
    jclass cls = (*env)->GetObjectClass(env, obj);
    if (cls) {
        jfieldID sec = (*env)->GetFieldID(env, cls, "sec", "I");
        jfieldID frac = (*env)->GetFieldID(env, cls, "frac", "I");
        if (sec && frac) {
            mpr_time time;
            mpr_time_set(&time, MPR_NOW);
            (*env)->SetLongField(env, obj, sec, time.sec);
            (*env)->SetLongField(env, obj, frac, time.frac);
        }
    }
}
