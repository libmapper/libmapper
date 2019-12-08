
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <mpr/mpr.h>

#include "mpr_AbstractObject.h"
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

typedef struct {
    jobject signal;
    jobject listener;
    jobject instUpdateListener;
    jobject instEventListener;
} signal_jni_context_t, *signal_jni_context;

typedef struct {
    jobject inst;
    jobject listener;
    jobject user_ref;
} inst_jni_context_t, *inst_jni_context;

const char *graph_evt_strings[] = {
    "ADDED",
    "MODIFIED",
    "REMOVED",
    "EXPIRED"
};

const char *inst_evt_strings[] = {
    "NEW",
    "UPSTREAM_RELEASE",
    "DOWNSTREAM_RELEASE",
    "OVERFLOW",
    "ALL"
};

/**** Helpers ****/

static int is_local(mpr_obj o)
{
    int len;
    mpr_type type;
    const void *val;
    mpr_obj_get_prop_by_idx(o, MPR_PROP_IS_LOCAL, o, &len, &type, &val, 0);
    return (1 == len && MPR_BOOL == type) ? *(int*)val : 0;
}

static const char* signal_name(mpr_sig sig)
{
    if (!sig)
        return NULL;
    int len;
    mpr_type type;
    const void *val;
    mpr_obj_get_prop_by_idx((mpr_obj)sig, MPR_PROP_NAME, 0, &len, &type, &val, 0);
    return (1 == len && MPR_STR == type && val) ? (const char*)val : NULL;
}

static int signal_length(mpr_sig sig)
{
    if (!sig)
        return 0;
    int len;
    mpr_type type;
    const void *val;
    mpr_obj_get_prop_by_idx((mpr_obj)sig, MPR_PROP_LEN, 0, &len, &type, &val, 0);
    return (1 == len && MPR_INT32 == type && val) ? *(int*)val : 0;
}

static mpr_type signal_type(mpr_sig sig)
{
    if (!sig)
        return 0;
    int len;
    mpr_type type;
    const void *val;
    mpr_obj_get_prop_by_idx((mpr_obj)sig, MPR_PROP_TYPE, 0, &len, &type, &val, 0);
    return (1 == len && val) ? *(mpr_type*)val : 0;
}

static const void* signal_user_data(mpr_sig sig)
{
    return sig ? mpr_obj_get_prop_ptr((mpr_obj)sig, MPR_PROP_DATA, NULL) : NULL;
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

static mpr_time_t *get_time_from_jobject(JNIEnv *env, jobject obj, mpr_time time)
{
    if (!obj) return 0;
    jclass cls = (*env)->GetObjectClass(env, obj);
    if (cls) {
        jfieldID sec = (*env)->GetFieldID(env, cls, "sec", "J");
        jfieldID frac = (*env)->GetFieldID(env, cls, "frac", "J");
        if (sec && frac) {
            time->sec = (*env)->GetLongField(env, obj, sec);
            time->frac = (*env)->GetLongField(env, obj, frac);
            return time;
        }
    }
    return 0;
}

static jobject get_jobject_from_time(JNIEnv *env, mpr_time time)
{
    jobject jtime = 0;
    if (time) {
        jclass cls = (*env)->FindClass(env, "mpr/Time");
        if (cls) {
            jmethodID mid = (*env)->GetMethodID(env, cls, "<init>", "(JJ)V");
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

static jobject get_jobject_from_inst_event(JNIEnv *env, int evt)
{
    jobject obj = 0;
    jclass cls = (*env)->FindClass(env, "mpr/signal/InstanceEvent");
    if (cls) {
        jfieldID fid = (*env)->GetStaticFieldID(env, cls, inst_evt_strings[evt],
                                                "Lmpr/signal/InstanceEvent;");
        if (fid)
            obj = (*env)->GetStaticObjectField(env, cls, fid);
        else {
            printf("Error looking up InstanceEvent field.\n");
            exit(1);
        }
    }
    return obj;
}

static jobject build_Value(JNIEnv *env, const int len, mpr_type type,
                           const void *val, mpr_time_t *time)
{
    jmethodID mid;
    jclass cls = (*env)->FindClass(env, "mpr/Value");
    if (!cls)
        return 0;
    jobject ret = 0;

    if (len <= 0 || !val) {
        // return empty Value
        mid = (*env)->GetMethodID(env, cls, "<init>", "()V");
        if (mid)
            ret = (*env)->NewObject(env, cls, mid);
    }
    else {
        switch (type) {
            case MPR_BOOL:
            case MPR_INT32: {
                if (len == 1) {
                    mid = (*env)->GetMethodID(env, cls, "<init>", "(I)V");
                    if (mid)
                        ret = (*env)->NewObject(env, cls, mid, *((int *)val));
                }
                else {
                    mid = (*env)->GetMethodID(env, cls, "<init>", "([I)V");
                    if (mid) {
                        jintArray arr = (*env)->NewIntArray(env, len);
                        (*env)->SetIntArrayRegion(env, arr, 0, len, val);
                        ret = (*env)->NewObject(env, cls, mid, arr);
                    }
                }
                break;
            }
            case MPR_FLT: {
                if (len == 1) {
                    mid = (*env)->GetMethodID(env, cls, "<init>", "(F)V");
                    if (mid)
                        ret = (*env)->NewObject(env, cls, mid, *((float *)val));
                }
                else {
                    mid = (*env)->GetMethodID(env, cls, "<init>", "([F)V");
                    if (mid) {
                        jfloatArray arr = (*env)->NewFloatArray(env, len);
                        (*env)->SetFloatArrayRegion(env, arr, 0, len, val);
                        ret = (*env)->NewObject(env, cls, mid, arr);
                    }
                }
                break;
            }
            case MPR_DBL: {
                if (len == 1) {
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
                if (len == 1) {
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
            default:
                break;
        }
    }
    if (ret && time) {
        jfieldID fid = (*env)->GetFieldID(env, cls, "time", "Lmpr/Time;");
        if (!fid) {
            printf("error: couldn't find value.time fieldId\n");
            return ret;
        }
        jobject jtime = (*env)->GetObjectField(env, ret, fid);
        if (!jtime) {
            printf("error: couldn't find value.time\n");
            return ret;
        }
        cls = (*env)->GetObjectClass(env, jtime);
        if (!cls) {
            printf("error: could find time class.\n");
            return ret;
        }
        fid = (*env)->GetFieldID(env, cls, "sec", "J");
        if (fid)
            (*env)->SetLongField(env, jtime, fid, time->sec);
        fid = (*env)->GetFieldID(env, cls, "frac", "J");
        if (fid)
            (*env)->SetLongField(env, jtime, fid, time->frac);
    }
    return ret;
}

static int get_Value_elements(JNIEnv *env, jobject jprop, void **val,
                              mpr_type *type)
{
    jclass cls = (*env)->GetObjectClass(env, jprop);
    if (!cls)
        return 0;

    jfieldID typeid = (*env)->GetFieldID(env, cls, "_type", "C");
    jfieldID lenid = (*env)->GetFieldID(env, cls, "length", "I");
    if (!typeid || !lenid)
        return 0;

    *type = (*env)->GetCharField(env, jprop, typeid);
    int len = (*env)->GetIntField(env, jprop, lenid);
    if (!len)
        return 0;

    jfieldID valf = 0;
    jobject o = 0;

    switch (*type) {
        case MPR_BOOL: {
            valf = (*env)->GetFieldID(env, cls, "_b", "[Z");
            o = (*env)->GetObjectField(env, jprop, valf);
            int *ints = malloc(sizeof(int) * len);
            for (int i = 0; i < len; i++) {
                ints[i] = (*env)->GetObjectArrayElement(env, o, i) ? 1 : 0;
            }
            *val = ints;
            break;
        }
        case MPR_INT32:
            valf = (*env)->GetFieldID(env, cls, "_i", "[I");
            o = (*env)->GetObjectField(env, jprop, valf);
            *val = (*env)->GetIntArrayElements(env, o, NULL);
            break;
        case MPR_FLT:
            valf = (*env)->GetFieldID(env, cls, "_f", "[F");
            o = (*env)->GetObjectField(env, jprop, valf);
            *val = (*env)->GetFloatArrayElements(env, o, NULL);
            break;
        case MPR_DBL:
            valf = (*env)->GetFieldID(env, cls, "_d", "[D");
            o = (*env)->GetObjectField(env, jprop, valf);
            *val = (*env)->GetDoubleArrayElements(env, o, NULL);
            break;
        case MPR_STR: {
            valf = (*env)->GetFieldID(env, cls, "_s", "[Ljava/lang/String;");
            o = (*env)->GetObjectField(env, jprop, valf);
            // need to unpack string array and rebuild
            if (len == 1) {
                jstring jstr = (jstring) (*env)->GetObjectArrayElement(env, o, 0);
                *val = (void*) (*env)->GetStringUTFChars(env, jstr, 0);
            }
            else {
                jstring jstrs[len];
                const char **cstrs = malloc(sizeof(char*) * len);
                int i;
                for (i = 0; i < len; i++) {
                    jstrs[i] = (jstring) (*env)->GetObjectArrayElement(env, o, i);
                    cstrs[i] = (*env)->GetStringUTFChars(env, jstrs[i], 0);
                }
                *val = cstrs;
            }
            break;
        }
        default:
            return 0;
    }
    return len;
}

static void release_Value_elements(JNIEnv *env, jobject jprop, void *val)
{
    jclass cls = (*env)->GetObjectClass(env, jprop);
    if (!cls)
        return;

    jfieldID typeid = (*env)->GetFieldID(env, cls, "_type", "C");
    jfieldID lenid = (*env)->GetFieldID(env, cls, "length", "I");
    if (!typeid || !lenid)
        return;

    mpr_type type = (*env)->GetCharField(env, jprop, typeid);
    int len = (*env)->GetIntField(env, jprop, lenid);
    if (!len)
        return;

    jfieldID valf = 0;
    jobject o = 0;

    switch (type) {
        case MPR_BOOL: {
            int *ints = (int*)val;
            if (ints)
                free(ints);
            break;
        }
        case MPR_INT32:
            valf = (*env)->GetFieldID(env, cls, "_i", "[I");
            o = (*env)->GetObjectField(env, jprop, valf);
            (*env)->ReleaseIntArrayElements(env, o, val, JNI_ABORT);
            break;
        case MPR_FLT:
            valf = (*env)->GetFieldID(env, cls, "_f", "[F");
            o = (*env)->GetObjectField(env, jprop, valf);
            (*env)->ReleaseFloatArrayElements(env, o, val, JNI_ABORT);
            break;
        case MPR_DBL:
            valf = (*env)->GetFieldID(env, cls, "_d", "[D");
            o = (*env)->GetObjectField(env, jprop, valf);
            (*env)->ReleaseDoubleArrayElements(env, o, val, JNI_ABORT);
            break;
        case MPR_STR: {
            valf = (*env)->GetFieldID(env, cls, "_s", "[Ljava/lang/String;");
            o = (*env)->GetObjectField(env, jprop, valf);

            jstring jstr;
            if (len == 1) {
                const char *cstring = (const char*)val;
                jstr = (jstring) (*env)->GetObjectArrayElement(env, o, 0);
                (*env)->ReleaseStringUTFChars(env, jstr, cstring);
            }
            else {
                const char **cstrings = (const char**)val;
                int i;
                for (i = 0; i < len; i++) {
                    jstr = (jstring) (*env)->GetObjectArrayElement(env, o, i);
                    (*env)->ReleaseStringUTFChars(env, jstr, cstrings[i]);
                }
                free(cstrings);
            }
            break;
        }
    }
}

static void java_signal_update_cb(mpr_sig sig, mpr_sig_evt evt, mpr_id id,
                                  int len, mpr_type type, const void *val,
                                  mpr_time_t *time)
{
    if (bailing)
        return;

    jobject vobj = 0;
    if (MPR_FLT == type && val) {
        jfloatArray varr = (*genv)->NewFloatArray(genv, len);
        if (varr)
            (*genv)->SetFloatArrayRegion(genv, varr, 0, len, val);
        vobj = (jobject) varr;
    }
    else if (MPR_INT32 == type && val) {
        jintArray varr = (*genv)->NewIntArray(genv, len);
        if (varr)
            (*genv)->SetIntArrayRegion(genv, varr, 0, len, val);
        vobj = (jobject) varr;
    }
    else if (MPR_DBL == type && val) {
        jdoubleArray varr = (*genv)->NewDoubleArray(genv, len);
        if (varr)
            (*genv)->SetDoubleArrayRegion(genv, varr, 0, len, val);
        vobj = (jobject) varr;
    }

    if (!vobj && val) {
        char msg[1024];
        snprintf(msg, 1024,
                 "Unknown signal type for %s in callback handler (%c,%d).",
                 signal_name(sig), type, len);
        jclass newExcCls =
            (*genv)->FindClass(genv, "java/lang/IllegalArgumentException");
        if (newExcCls)
            (*genv)->ThrowNew(genv, newExcCls, msg);
        bailing = 1;
        return;
    }

    jobject jtime = get_jobject_from_time(genv, time);

    jobject update_cb;
    signal_jni_context ctx = (signal_jni_context)signal_user_data(sig);
    if (!ctx)
        return;

    if (ctx->instUpdateListener) {
        inst_jni_context ictx;
        ictx = ((inst_jni_context) mpr_sig_get_inst_data(sig, id));
        if (!ictx)
            return;
        update_cb = ictx->listener;
        if (update_cb && ictx->inst) {
            jclass cls = (*genv)->GetObjectClass(genv, update_cb);
            if (cls) {
                jmethodID mid=0;
                if (type == MPR_INT32) {
                    mid = (*genv)->GetMethodID(genv, cls, "onUpdate",
                                               "(Lmpr/Signal$Instance;"
                                               "[I"
                                               "Lmpr/Time;)V");
                }
                else if (type == MPR_FLT) {
                    mid = (*genv)->GetMethodID(genv, cls, "onUpdate",
                                               "(Lmpr/Signal$Instance;"
                                               "[F"
                                               "Lmpr/Time;)V");
                }
                else if (type == MPR_DBL) {
                    mid = (*genv)->GetMethodID(genv, cls, "onUpdate",
                                               "(Lmpr/Signal$Instance;"
                                               "[D"
                                               "Lmpr/Time;)V");
                }
                if (mid) {
                    (*genv)->CallVoidMethod(genv, update_cb, mid, ictx->inst,
                                            vobj, jtime);
                    if ((*genv)->ExceptionOccurred(genv))
                        bailing = 1;
                }
                else {
                    printf("Error looking up onUpdate method.\n");
                    exit(1);
                }
                if (vobj)
                    (*genv)->DeleteLocalRef(genv, vobj);
                if (jtime)
                    (*genv)->DeleteLocalRef(genv, jtime);
                return;
            }
        }

    }

    update_cb = ctx->listener;
    if (update_cb && ctx->signal) {
        jclass cls = (*genv)->GetObjectClass(genv, update_cb);
        if (cls) {
            jmethodID mid=0;
            if (type == MPR_INT32) {
                mid = (*genv)->GetMethodID(genv, cls, "onUpdate",
                                           "(Lmpr/Signal;"
                                           "[I"
                                           "Lmpr/Time;)V");
            }
            else if (type == MPR_FLT) {
                mid = (*genv)->GetMethodID(genv, cls, "onUpdate",
                                           "(Lmpr/Signal;"
                                           "[F"
                                           "Lmpr/Time;)V");
            }
            else if (type == MPR_DBL) {
                mid = (*genv)->GetMethodID(genv, cls, "onUpdate",
                                           "(Lmpr/Signal;"
                                           "[D"
                                           "Lmpr/Time;)V");
            }
            if (mid) {
                (*genv)->CallVoidMethod(genv, update_cb, mid, ctx->signal,
                                        vobj, jtime);
                if ((*genv)->ExceptionOccurred(genv))
                    bailing = 1;
            }
            else {
                printf("Error looking up onUpdate method.\n");
                exit(1);
            }
        }
    }

    if (vobj)
        (*genv)->DeleteLocalRef(genv, vobj);
    if (jtime)
        (*genv)->DeleteLocalRef(genv, jtime);
}

static jobject create_signal_object(JNIEnv *env, jobject devobj,
                                    signal_jni_context ctx,
                                    jobject listener,
                                    mpr_sig sig)
{
    jobject sigobj = 0;
    // Create a wrapper class for this signal
    jclass cls = (*env)->FindClass(env, "mpr/Signal");
    if (cls) {
        jmethodID mid = (*env)->GetMethodID(env, cls, "<init>", "(J)V");
        sigobj = (*env)->NewObject(env, cls, mid, jlong_ptr(sig));
    }

    if (sigobj) {
        mpr_obj_set_prop((mpr_obj)sig, MPR_PROP_DATA, NULL, 1, MPR_PTR, &ctx, 0);
        ctx->signal = (*env)->NewGlobalRef(env, sigobj);
        ctx->listener = listener ? (*env)->NewGlobalRef(env, listener) : 0;
    }
    else {
        printf("Error creating signal wrapper class.\n");
        exit(1);
    }
    return sigobj;
}

/**** mpr_AbstractObject.h ****/

JNIEXPORT void JNICALL Java_mpr_AbstractObject_mprObjectFree
  (JNIEnv *env, jobject obj, jlong abstr)
{
    mpr_obj mobj = (mpr_obj)ptr_jlong(abstr);
    if (!mobj || MPR_DEV != mpr_obj_get_type(mobj)
        || !is_local(mobj))
        return;

    mpr_dev dev = (mpr_dev)mobj;
    /* Free all references to Java objects. */
    mpr_obj *sigs = mpr_dev_get_sigs(dev, MPR_DIR_ANY);
    while (sigs) {
        mpr_sig temp = (mpr_sig)*sigs;
        sigs = mpr_list_next(sigs);

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
        if (ctx->instEventListener)
            (*env)->DeleteGlobalRef(env, ctx->instEventListener);
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

JNIEXPORT jint JNICALL Java_mpr_AbstractObject_numProperties
  (JNIEnv *env, jobject jobj)
{
    mpr_obj mobj = get_mpr_obj_from_jobject(env, jobj);
    return mobj ? mpr_obj_get_num_props(mobj, 0) : 0;
}

JNIEXPORT jobject JNICALL Java_mpr_AbstractObject_getProperty__Ljava_lang_String_2
  (JNIEnv *env, jobject jobj, jstring name)
{
    mpr_obj mobj = get_mpr_obj_from_jobject(env, jobj);
    const char *cname = (*env)->GetStringUTFChars(env, name, 0);
    mpr_type type;
    int len;
    const void *val;
    mpr_prop propID = mpr_obj_get_prop_by_key(mobj, cname, &len, &type, &val, 0);
    jobject o = 0;
    if (MPR_PROP_UNKNOWN != propID)
        o = build_Value(env, len, type, val, 0);

    (*env)->ReleaseStringUTFChars(env, name, cname);
    return o;
}

JNIEXPORT jobject JNICALL Java_mpr_AbstractObject_getProperty__I
  (JNIEnv *env, jobject jobj, jint propID)
{
    mpr_obj mobj = get_mpr_obj_from_jobject(env, jobj);
    const char *name;
    mpr_type type;
    int len;
    const void *val;
    propID = mpr_obj_get_prop_by_idx(mobj, propID, &name, &len, &type, &val, 0);
    jobject o = 0;
    if (MPR_PROP_UNKNOWN != propID)
        o = build_Value(env, len, type, val, 0);

    return o;
}

JNIEXPORT void JNICALL Java_mpr_AbstractObject_mprSetProperty
  (JNIEnv *env, jobject jobj, jlong ptr, jint id, jstring name, jobject jval)
{
    mpr_obj mobj = (mpr_obj) ptr_jlong(ptr);
    if (!mobj)
        return;
    const char *cname = name ? (*env)->GetStringUTFChars(env, name, 0) : 0;
    mpr_type type;
    void *val;
    int len = get_Value_elements(env, jval, &val, &type);
    if (len) {
        mpr_obj_set_prop(mobj, id, cname, len, type, val, 1);
        release_Value_elements(env, jval, val);
    }
}

JNIEXPORT void JNICALL Java_mpr_AbstractObject_removeProperty
  (JNIEnv *env, jobject jobj, jint id, jstring name)
{
    mpr_obj mobj = get_mpr_obj_from_jobject(env, jobj);
    if (!mobj)
        return;
    const char *cname = name ? (*env)->GetStringUTFChars(env, name, 0) : 0;
    mpr_obj_remove_prop(mobj, id, cname);
    if (cname)
        (*env)->ReleaseStringUTFChars(env, name, cname);
}

JNIEXPORT void JNICALL Java_mpr_AbstractObject_mprPush
  (JNIEnv *env, jobject jobj, jlong ptr)
{
    mpr_obj obj = (mpr_obj)ptr_jlong(ptr);
    if (obj)
        mpr_obj_push(obj);
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

static void java_graph_cb(mpr_graph g, mpr_obj mobj, mpr_graph_evt evt,
                          const void *user_data)
{
    if (bailing || !user_data)
        return;

    // Create a wrapper class for the device
    jclass cls;
    mpr_data_type type = mpr_obj_get_type(mobj);
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
            return;
    }
    if (!cls)
        return;
    jmethodID mid = (*genv)->GetMethodID(genv, cls, "<init>", "(J)V");
    jobject jobj;
    if (mid)
        jobj = (*genv)->NewObject(genv, cls, mid, jlong_ptr(mobj));
    else {
        printf("Error looking up Object init method\n");
        return;
    }
    jobject eventobj = get_jobject_from_graph_evt(genv, evt);
    if (!eventobj) {
        printf("Error looking up graph event\n");
        return;
    }

    jobject listener = (jobject)user_data;
    cls = (*genv)->GetObjectClass(genv, listener);
    if (cls) {
        switch (type) {
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

JNIEXPORT jobject JNICALL Java_mpr_Graph_devices
  (JNIEnv *env, jobject obj)
{
    jclass cls = (*env)->FindClass(env, "mpr/List");
    if (!cls)
        return 0;

    mpr_dev *devs = 0;
    mpr_graph g = get_graph_from_jobject(env, obj);
    if (g)
        devs = mpr_graph_get_list(g, MPR_DEV);

    jmethodID mid = (*env)->GetMethodID(env, cls, "<init>", "(J)V");
    return (*env)->NewObject(env, cls, mid, jlong_ptr(devs));
}

JNIEXPORT jobject JNICALL Java_mpr_Graph_signals
  (JNIEnv *env, jobject obj)
{
    jclass cls = (*env)->FindClass(env, "mpr/List");
    if (!cls)
        return 0;

    mpr_obj *signals = 0;
    mpr_graph g = get_graph_from_jobject(env, obj);
    if (g)
        signals = mpr_graph_get_list(g, MPR_SIG);

    jmethodID mid = (*env)->GetMethodID(env, cls, "<init>", "(J)V");
    return (*env)->NewObject(env, cls, mid, jlong_ptr(signals));
}

JNIEXPORT jobject JNICALL Java_mpr_Graph_maps
  (JNIEnv *env, jobject obj)
{
    jclass cls = (*env)->FindClass(env, "mpr/List");
    if (!cls)
        return 0;

    mpr_obj *maps = 0;
    mpr_graph g = get_graph_from_jobject(env, obj);
    if (g)
        maps = mpr_graph_get_list(g, MPR_MAP);

    jmethodID mid = (*env)->GetMethodID(env, cls, "<init>", "(J)V");
    return (*env)->NewObject(env, cls, mid, jlong_ptr(maps));
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

JNIEXPORT jint JNICALL Java_mpr_Device_mprDevicePoll
  (JNIEnv *env, jobject obj, jlong jdev, jint block_ms)
{
    genv = env;
    bailing = 0;
    mpr_dev dev = (mpr_dev)ptr_jlong(jdev);
    return dev ? mpr_dev_poll(dev, block_ms) : 0;
}

JNIEXPORT jobject JNICALL Java_mpr_Device_mprAddSignal
  (JNIEnv *env, jobject obj, jint dir, jint numInst, jstring name, jint len,
   jchar type, jstring unit, jobject min, jobject max, jobject listener)
{
    if (!name || (len <= 0) || (type != MPR_FLT && type != MPR_INT32 && type != MPR_DBL))
        return 0;

    mpr_dev dev = (mpr_dev)get_mpr_obj_from_jobject(env, obj);
    if (!dev)
        return 0;

    const char *cname = (*env)->GetStringUTFChars(env, name, 0);
    const char *cunit = unit ? (*env)->GetStringUTFChars(env, unit, 0) : 0;

    mpr_sig sig = mpr_sig_new(dev, dir, numInst, cname, len, (char)type, cunit,
                              0, 0, 0, 0);
    if (listener)
        mpr_sig_set_cb(sig, java_signal_update_cb, MPR_SIG_UPDATE);

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
    jobject sigobj = create_signal_object(env, obj, ctx, listener, sig);

    if (sigobj) {
        if (min) {
            Java_mpr_AbstractObject_mprSetProperty(env, sigobj, jlong_ptr(sig),
                                                   MPR_PROP_MIN, NULL, min);
        }
        if (max) {
            Java_mpr_AbstractObject_mprSetProperty(env, sigobj, jlong_ptr(sig),
                                                   MPR_PROP_MAX, NULL, min);
        }
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
        if (ctx->instEventListener)
            (*env)->DeleteGlobalRef(env, ctx->instEventListener);
        free(ctx);

        mpr_sig_free(sig);
    }
}

JNIEXPORT jboolean JNICALL Java_mpr_Device_ready
  (JNIEnv *env, jobject obj)
{
    mpr_dev dev = (mpr_dev)get_mpr_obj_from_jobject(env, obj);
    return dev ? mpr_dev_ready(dev) : 0;
}

JNIEXPORT jobject JNICALL Java_mpr_Device_startQueue
  (JNIEnv *env, jobject obj, jobject jtime)
{
    mpr_dev dev = (mpr_dev)get_mpr_obj_from_jobject(env, obj);
    if (!dev)
        return 0;
    mpr_time_t time, *ptime = 0;
    ptime = get_time_from_jobject(env, jtime, &time);
    if (!ptime) {
        mpr_time_now(&time);
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
    mpr_time_t time, *ptime = 0;
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

JNIEXPORT jobject JNICALL Java_mpr_List_newObject
  (JNIEnv *env, jobject obj, jlong);

JNIEXPORT jlong JNICALL Java_mpr_List_mprListCopy
  (JNIEnv *env, jobject obj, jlong list)
{
    mpr_obj *objs = (mpr_obj*) ptr_jlong(list);
    if (!objs)
        return 0;
    return jlong_ptr(mpr_list_cpy(objs));
}

JNIEXPORT jlong JNICALL Java_mpr_List_mprListDeref
  (JNIEnv *env, jobject obj, jlong list)
{
    void **ptr = (void**)ptr_jlong(list);
    return jlong_ptr(*ptr);
}

JNIEXPORT void JNICALL Java_mpr_List_mprListFilter
  (JNIEnv *env, jobject obj, jlong list, jint id, jstring name, jobject val)
{
// TODO
}

JNIEXPORT void JNICALL Java_mpr_List_mprListFree
  (JNIEnv *env, jobject obj, jlong list)
{
    mpr_obj *objs = (mpr_obj*) ptr_jlong(list);
    if (objs)
        mpr_list_free(objs);
}

JNIEXPORT jlong JNICALL Java_mpr_List_mprListIntersect
  (JNIEnv *env, jobject obj, jlong lhs, jlong rhs)
{
    mpr_obj *objs_lhs = (mpr_obj*) ptr_jlong(lhs);
    mpr_obj *objs_rhs = (mpr_obj*) ptr_jlong(rhs);

    if (!objs_lhs || !objs_rhs)
        return 0;

    // use a copy of rhs
    mpr_obj *objs_rhs_cpy = mpr_list_cpy(objs_rhs);
    return jlong_ptr(mpr_list_isect(objs_lhs, objs_rhs_cpy));
}

JNIEXPORT jlong JNICALL Java_mpr_List_mprListJoin
  (JNIEnv *env, jobject obj, jlong lhs, jlong rhs)
{
    mpr_obj *objs_lhs = (mpr_obj*) ptr_jlong(lhs);
    mpr_obj *objs_rhs = (mpr_obj*) ptr_jlong(rhs);

    if (!objs_rhs)
        return lhs;

    // use a copy of rhs
    mpr_dev *objs_rhs_cpy = mpr_list_cpy(objs_rhs);
    return jlong_ptr(mpr_list_union(objs_lhs, objs_rhs_cpy));
}

JNIEXPORT jint JNICALL Java_mpr_List_mprListLength
  (JNIEnv *env, jobject obj, jlong list)
{
    mpr_obj *objs = (mpr_obj*) ptr_jlong(list);
    return objs ? mpr_list_get_count(objs) : 0;
}

JNIEXPORT jlong JNICALL Java_mpr_List_mprListNext
  (JNIEnv *env, jobject obj, jlong list)
{
    mpr_obj *objs = (mpr_obj*) ptr_jlong(list);
    return objs ? jlong_ptr(mpr_list_next(objs)) : 0;
}

JNIEXPORT jlong JNICALL Java_mpr_List_mprListSubtract
  (JNIEnv *env, jobject obj, jlong lhs, jlong rhs)
{
    mpr_obj *objs_lhs = (mpr_obj*) ptr_jlong(lhs);
    mpr_obj *objs_rhs = (mpr_obj*) ptr_jlong(rhs);

    if (!objs_lhs || !objs_rhs)
        return lhs;

    // use a copy of rhs
    mpr_obj *objs_rhs_cpy = mpr_list_cpy(objs_rhs);
    return jlong_ptr(mpr_list_diff(objs_lhs, objs_rhs_cpy));
}

/**** mpr_Map.h ****/

JNIEXPORT jlong JNICALL Java_mpr_Map_mprMapSrcSignalPtr
  (JNIEnv *env, jobject obj, jlong jmap, jint index)
{
    mpr_map map = (mpr_map) ptr_jlong(jmap);
    return map ? jlong_ptr(mpr_map_get_sig(map, MPR_LOC_SRC, index)) : 0;
}

JNIEXPORT jlong JNICALL Java_mpr_Map_mprMapDstSignalPtr
  (JNIEnv *env, jobject obj, jlong jmap)
{
    mpr_map map = (mpr_map) ptr_jlong(jmap);
    return map ? jlong_ptr(mpr_map_get_sig(map, MPR_LOC_DST, 0)) : 0;
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

JNIEXPORT jint JNICALL Java_mpr_Map_mprMapNumSignals
  (JNIEnv *env, jobject obj, jlong jmap, jint loc)
{
    mpr_map map = (mpr_map) ptr_jlong(jmap);
    return map ? mpr_map_get_num_sigs(map, loc) : 0;
}

JNIEXPORT jboolean JNICALL Java_mpr_Map_ready
  (JNIEnv *env, jobject obj)
{
    mpr_map map = (mpr_map)get_mpr_obj_from_jobject(env, obj);
    return map ? (mpr_map_ready(map)) : 0;
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
        mpr_time_t time, *ptime = 0;
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
    mpr_time_t time, *ptime = 0;
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

JNIEXPORT jobject JNICALL Java_mpr_Signal_setUpdateListener
  (JNIEnv *env, jobject obj, jobject listener)
{
    mpr_sig sig = (mpr_sig)get_mpr_obj_from_jobject(env, obj);
    if (!sig)
        return obj;

    signal_jni_context ctx = (signal_jni_context)signal_user_data(sig);
    if (ctx->listener)
        (*env)->DeleteGlobalRef(env, ctx->listener);
    if (listener) {
        ctx->listener = (*env)->NewGlobalRef(env, listener);
        mpr_sig_set_cb(sig, java_signal_update_cb, MPR_SIG_UPDATE);
    }
    else {
        ctx->listener = 0;
        mpr_sig_set_cb(sig, 0, 0);
    }
    return obj;
}

JNIEXPORT jobject JNICALL Java_mpr_Signal_setInstanceUpdateListener
  (JNIEnv *env, jobject obj, jobject listener)
{
    mpr_sig sig = (mpr_sig)get_mpr_obj_from_jobject(env, obj);
    if (!sig)
        return obj;

    int i, idx, status = MPR_STATUS_ACTIVE | MPR_STATUS_RESERVED;
    for (i = 0; i < mpr_sig_get_num_inst(sig, status); i++) {
        idx = mpr_sig_get_inst_id(sig, i, status);
        inst_jni_context ctx = ((inst_jni_context) mpr_sig_get_inst_data(sig, idx));
        if (!ctx)
            continue;
        if (ctx->listener)
            (*env)->DeleteGlobalRef(env, ctx->listener);
        ctx->listener = listener ? (*env)->NewGlobalRef(env, listener) : 0;
    }

    // also set it for the signal
    signal_jni_context ctx = (signal_jni_context)signal_user_data(sig);
    if (ctx) {
        if (ctx->instUpdateListener)
            (*env)->DeleteGlobalRef(env, ctx->instUpdateListener);
        if (listener)
            ctx->instUpdateListener = (*env)->NewGlobalRef(env, listener);
        else
            ctx->instUpdateListener = 0;
    }
    return obj;
}

JNIEXPORT jobject JNICALL Java_mpr_Signal_instUpdateListener
  (JNIEnv *env, jobject obj)
{
    mpr_sig sig = (mpr_sig)get_mpr_obj_from_jobject(env, obj);
    if (sig) {
        signal_jni_context ctx = (signal_jni_context)signal_user_data(sig);
        return ctx ? ctx->listener : 0;
    }
    return 0;
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

JNIEXPORT void JNICALL Java_mpr_Signal_mprSetStealingMode
  (JNIEnv *env, jobject obj, jint mode)
{
    mpr_sig sig = (mpr_sig)get_mpr_obj_from_jobject(env, obj);
    if (sig)
        mpr_obj_set_prop((mpr_obj)sig, MPR_PROP_STEAL_MODE, NULL, 1, MPR_INT32,
                         &mode, 1);
}

JNIEXPORT jint JNICALL Java_mpr_Signal_mprStealingMode
  (JNIEnv *env, jobject obj, jlong jsig)
{
    mpr_sig sig = (mpr_sig) ptr_jlong(jsig);
    return sig ? mpr_obj_get_prop_i32((mpr_obj)sig, MPR_PROP_STEAL_MODE, NULL) : 0;
}

JNIEXPORT jobject JNICALL Java_mpr_Signal_updateInstance__JILmpr_Time_2
  (JNIEnv *env, jobject obj, jlong jid, jint val, jobject jtime)
{
    mpr_sig sig = (mpr_sig)get_mpr_obj_from_jobject(env, obj);
    if (!sig)
        return obj;

    mpr_id id = (mpr_id)ptr_jlong(jid);

    mpr_time_t time, *ptime = 0;
    ptime = get_time_from_jobject(env, jtime, &time);

    mpr_sig_set_value(sig, id, 1, MPR_INT32, &val, ptime ? *ptime : MPR_NOW);

    return obj;
}

JNIEXPORT jobject JNICALL Java_mpr_Signal_updateInstance__JFLmpr_Time_2
  (JNIEnv *env, jobject obj, jlong jid, jfloat val, jobject jtime)
{
    mpr_sig sig = (mpr_sig)get_mpr_obj_from_jobject(env, obj);
    if (!sig)
        return obj;

    mpr_id id = (mpr_id)ptr_jlong(jid);

    mpr_time_t time, *ptime = 0;
    ptime = get_time_from_jobject(env, jtime, &time);

    mpr_sig_set_value(sig, id, 1, MPR_FLT, &val, ptime ? *ptime : MPR_NOW);

    return obj;
}

JNIEXPORT jobject JNICALL Java_mpr_Signal_updateInstance__JDLmpr_Time_2
  (JNIEnv *env, jobject obj, jlong jid, jdouble val, jobject jtime)
{
    mpr_sig sig = (mpr_sig)get_mpr_obj_from_jobject(env, obj);
    if (!sig)
        return obj;

    mpr_id id = (mpr_id)ptr_jlong(jid);

    mpr_time_t time, *ptime = 0;
    ptime = get_time_from_jobject(env, jtime, &time);

    mpr_sig_set_value(sig, id, 1, MPR_DBL, &val, ptime ? *ptime : MPR_NOW);

    return obj;
}

JNIEXPORT jobject JNICALL Java_mpr_Signal_updateInstance__J_3ILmpr_Time_2
  (JNIEnv *env, jobject obj, jlong jid, jintArray vals, jobject jtime)
{
    mpr_sig sig = (mpr_sig)get_mpr_obj_from_jobject(env, obj);
    if (!sig)
        return obj;

    int len = (*env)->GetArrayLength(env, vals);

    mpr_id id = (mpr_id)ptr_jlong(jid);

    mpr_time_t time, *ptime = 0;
    ptime = get_time_from_jobject(env, jtime, &time);

    jint *ivals = (*env)->GetIntArrayElements(env, vals, 0);
    if (!ivals)
        return obj;

    mpr_sig_set_value(sig, id, len, MPR_INT32, ivals, ptime ? *ptime : MPR_NOW);

    (*env)->ReleaseIntArrayElements(env, vals, ivals, JNI_ABORT);
    return obj;
}

JNIEXPORT jobject JNICALL Java_mpr_Signal_updateInstance__J_3FLmpr_Time_2
  (JNIEnv *env, jobject obj, jlong jid, jfloatArray vals, jobject jtime)
{
    mpr_sig sig = (mpr_sig)get_mpr_obj_from_jobject(env, obj);
    if (!sig)
        return obj;

    int len = (*env)->GetArrayLength(env, vals);

    mpr_id id = (mpr_id)ptr_jlong(jid);

    mpr_time_t time, *ptime = 0;
    ptime = get_time_from_jobject(env, jtime, &time);

    jfloat *fvals = (*env)->GetFloatArrayElements(env, vals, 0);
    if (!fvals)
        return obj;

    mpr_sig_set_value(sig, id, len, MPR_FLT, fvals, ptime ? *ptime : MPR_NOW);

    (*env)->ReleaseFloatArrayElements(env, vals, fvals, JNI_ABORT);
    return obj;
}

JNIEXPORT jobject JNICALL Java_mpr_Signal_updateInstance__J_3DLmpr_Time_2
  (JNIEnv *env, jobject obj, jlong jid, jdoubleArray vals, jobject jtime)
{
    mpr_sig sig = (mpr_sig)get_mpr_obj_from_jobject(env, obj);
    if (!sig)
        return obj;

    int len = (*env)->GetArrayLength(env, vals);

    mpr_id id = (mpr_id)ptr_jlong(jid);

    mpr_time_t time, *ptime = 0;
    ptime = get_time_from_jobject(env, jtime, &time);

    jdouble *dvals = (*env)->GetDoubleArrayElements(env, vals, 0);
    if (!dvals)
        return obj;

    mpr_sig_set_value(sig, id, len, MPR_DBL, dvals, ptime ? *ptime : MPR_NOW);

    (*env)->ReleaseDoubleArrayElements(env, vals, dvals, JNI_ABORT);
    return obj;
}

JNIEXPORT jobject JNICALL Java_mpr_Signal_instanceValue
  (JNIEnv *env, jobject obj, jlong jid)
{
    mpr_sig sig = (mpr_sig)get_mpr_obj_from_jobject(env, obj);
    mpr_type type = MPR_INT32;
    int len = 0;
    const void *val = 0;
    mpr_time_t time = {0,1};

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
        jfieldID sec = (*env)->GetFieldID(env, cls, "sec", "J");
        jfieldID frac = (*env)->GetFieldID(env, cls, "frac", "J");
        if (sec && frac) {
            mpr_time_t time;
            mpr_time_now(&time);
            (*env)->SetLongField(env, obj, sec, time.sec);
            (*env)->SetLongField(env, obj, frac, time.frac);
        }
    }
}
