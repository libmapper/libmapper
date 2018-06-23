
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <mapper/mapper.h>

#include "mapper_AbstractObject.h"
#include "mapper_Device.h"
#include "mapper_Graph.h"
#include "mapper_List.h"
#include "mapper_Map.h"
#include "mapper_Signal_Instance.h"
#include "mapper_Signal.h"
#include "mapper_Time.h"

#include "config.h"

#ifdef HAVE_INTTYPES_H
#include <inttypes.h>
#define PR_MAPPER_ID PRIu64
#else
#define PR_MAPPER_ID "llu"
#endif

#define jlong_ptr(a) ((jlong)(uintptr_t)(a))
#define ptr_jlong(a) ((void *)(uintptr_t)(a))

JNIEnv *genv=0;
int bailing=0;

typedef struct {
    jobject signal;
    jobject listener;
    jobject instanceUpdateListener;
    jobject instanceEventListener;
} signal_jni_context_t, *signal_jni_context;

typedef struct {
    jobject instance;
    jobject listener;
    jobject user_ref;
} instance_jni_context_t, *instance_jni_context;

const char *event_strings[] = {
    "ADDED",
    "MODIFIED",
    "REMOVED",
    "EXPIRED"
};

const char *instance_event_strings[] = {
    "NEW",
    "UPSTREAM_RELEASE",
    "DOWNSTREAM_RELEASE",
    "OVERFLOW",
    "ALL"
};

/**** Helpers ****/

static int is_local(mapper_object o)
{
    int len;
    mapper_type type;
    const void *val;
    mapper_object_get_prop_by_index(o, MAPPER_PROP_IS_LOCAL, NULL, &len, &type,
                                    &val);
    return (1 == len && MAPPER_BOOL == type) ? *(int*)val : 0;
}

static const char* signal_name(mapper_signal sig)
{
    if (!sig)
        return NULL;
    int len;
    mapper_type type;
    const void *val;
    mapper_object_get_prop_by_index((mapper_object)sig, MAPPER_PROP_NAME, NULL,
                                    &len, &type, &val);
    return (1 == len && MAPPER_STRING == type && val) ? (const char*)val : NULL;
}

static int signal_length(mapper_signal sig)
{
    if (!sig)
        return 0;
    int len;
    mapper_type type;
    const void *val;
    mapper_object_get_prop_by_index((mapper_object)sig, MAPPER_PROP_LENGTH, NULL,
                                    &len, &type, &val);
    return (1 == len && MAPPER_INT32 == type && val) ? *(int*)val : 0;
}

static mapper_type signal_type(mapper_signal sig)
{
    if (!sig)
        return 0;
    int len;
    mapper_type type;
    const void *val;
    mapper_object_get_prop_by_index((mapper_object)sig, MAPPER_PROP_TYPE, NULL,
                                    &len, &type, &val);
    return (1 == len && val) ? *(mapper_type*)val : 0;
}

static const void* signal_user_data(mapper_signal sig)
{
    if (!sig)
        return NULL;
    int len;
    mapper_type type;
    const void *val;
    mapper_object_get_prop_by_index((mapper_object)sig, MAPPER_PROP_USER_DATA,
                                    NULL, &len, &type, &val);
    return (1 == len && MAPPER_PTR == type) ? val : NULL;
}

static void throwIllegalArgumentSignal(JNIEnv *env)
{
    jclass newExcCls =
        (*env)->FindClass(env, "java/lang/IllegalArgumentException");
    if (newExcCls) {
        (*env)->ThrowNew(env, newExcCls,
                         "Signal object is not associated with a mapper_signal.");
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

static mapper_device get_mapper_object_from_jobject(JNIEnv *env, jobject jobj)
{
    // TODO check object here
    jclass cls = (*env)->GetObjectClass(env, jobj);
    if (cls) {
        jfieldID val = (*env)->GetFieldID(env, cls, "_obj", "J");
        if (val) {
            jlong s = (*env)->GetLongField(env, jobj, val);
            return (mapper_object)ptr_jlong(s);
        }
    }
    return 0;
}

static mapper_signal get_instance_from_jobject(JNIEnv *env, jobject obj,
                                               mapper_id *id)
{
    // TODO check signal here
    jclass cls = (*env)->GetObjectClass(env, obj);
    if (cls) {
        mapper_signal sig = 0;
        jfieldID val = (*env)->GetFieldID(env, cls, "_sigptr", "J");
        if (val) {
            jlong s = (*env)->GetLongField(env, obj, val);
            sig = (mapper_signal)ptr_jlong(s);
        }
        if (id) {
            val = (*env)->GetFieldID(env, cls, "_id", "J");
            if (val) {
                jlong s = (*env)->GetLongField(env, obj, val);
                *id = (mapper_id)ptr_jlong(s);
            }
        }
        return sig;
    }
    throwIllegalArgumentSignal(env);
    return 0;
}

static mapper_graph get_graph_from_jobject(JNIEnv *env, jobject obj)
{
    // TODO check graph here
    jclass cls = (*env)->GetObjectClass(env, obj);
    if (cls) {
        jfieldID val = (*env)->GetFieldID(env, cls, "_graph", "J");
        if (val) {
            jlong s = (*env)->GetLongField(env, obj, val);
            return (mapper_graph)ptr_jlong(s);
        }
    }
    throwIllegalArgument(env, "Couldn't retrieve graph pointer.");
    return 0;
}

static mapper_time_t *get_time_from_jobject(JNIEnv *env, jobject obj,
                                            mapper_time time)
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

static jobject get_jobject_from_time(JNIEnv *env, mapper_time time)
{
    jobject jtime = 0;
    if (time) {
        jclass cls = (*env)->FindClass(env, "mapper/Time");
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
    // TODO: return MAPPER_NOW ?
    return jtime;
}

static jobject get_jobject_from_graph_record_event(JNIEnv *env,
                                                   mapper_record_event event)
{
    jobject obj = 0;
    jclass cls = (*env)->FindClass(env, "mapper/graph/Event");
    if (cls) {
        jfieldID fid = (*env)->GetStaticFieldID(env, cls, event_strings[event],
                                                "Lmapper/graph/Event;");
        if (fid) {
            obj = (*env)->GetStaticObjectField(env, cls, fid);
        }
        else {
            printf("Error looking up graph/Event field '%s'.\n",
                   event_strings[event]);
            exit(1);
        }
    }
    return obj;
}

static jobject get_jobject_from_instance_event(JNIEnv *env, int event)
{
    jobject obj = 0;
    jclass cls = (*env)->FindClass(env, "mapper/signal/InstanceEvent");
    if (cls) {
        jfieldID fid = (*env)->GetStaticFieldID(env, cls,
                                                instance_event_strings[event],
                                                "Lmapper/signal/InstanceEvent;");
        if (fid)
            obj = (*env)->GetStaticObjectField(env, cls, fid);
        else {
            printf("Error looking up InstanceEvent field.\n");
            exit(1);
        }
    }
    return obj;
}

static jobject build_Value(JNIEnv *env, const int len, mapper_type type,
                           const void *val, mapper_time_t *time)
{
    jmethodID mid;
    jclass cls = (*env)->FindClass(env, "mapper/Value");
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
            case MAPPER_BOOL:
            case MAPPER_INT32: {
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
            case MAPPER_FLOAT: {
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
            case MAPPER_DOUBLE: {
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
            case MAPPER_STRING: {
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
        jfieldID fid = (*env)->GetFieldID(env, cls, "time", "Lmapper/Time;");
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
                              mapper_type *type)
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
        case MAPPER_BOOL: {
            valf = (*env)->GetFieldID(env, cls, "_b", "[Z");
            o = (*env)->GetObjectField(env, jprop, valf);
            int *ints = malloc(sizeof(int) * len);
            for (int i = 0; i < len; i++) {
                ints[i] = (*env)->GetObjectArrayElement(env, o, i) ? 1 : 0;
            }
            *val = ints;
            break;
        }
        case MAPPER_INT32:
            valf = (*env)->GetFieldID(env, cls, "_i", "[I");
            o = (*env)->GetObjectField(env, jprop, valf);
            *val = (*env)->GetIntArrayElements(env, o, NULL);
            break;
        case MAPPER_FLOAT:
            valf = (*env)->GetFieldID(env, cls, "_f", "[F");
            o = (*env)->GetObjectField(env, jprop, valf);
            *val = (*env)->GetFloatArrayElements(env, o, NULL);
            break;
        case MAPPER_DOUBLE:
            valf = (*env)->GetFieldID(env, cls, "_d", "[D");
            o = (*env)->GetObjectField(env, jprop, valf);
            *val = (*env)->GetDoubleArrayElements(env, o, NULL);
            break;
        case MAPPER_STRING: {
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

    mapper_type type = (*env)->GetCharField(env, jprop, typeid);
    int len = (*env)->GetIntField(env, jprop, lenid);
    if (!len)
        return;

    jfieldID valf = 0;
    jobject o = 0;

    switch (type) {
        case MAPPER_BOOL: {
            int *ints = (int*)val;
            if (ints)
                free(ints);
            break;
        }
        case MAPPER_INT32:
            valf = (*env)->GetFieldID(env, cls, "_i", "[I");
            o = (*env)->GetObjectField(env, jprop, valf);
            (*env)->ReleaseIntArrayElements(env, o, val, JNI_ABORT);
            break;
        case MAPPER_FLOAT:
            valf = (*env)->GetFieldID(env, cls, "_f", "[F");
            o = (*env)->GetObjectField(env, jprop, valf);
            (*env)->ReleaseFloatArrayElements(env, o, val, JNI_ABORT);
            break;
        case MAPPER_DOUBLE:
            valf = (*env)->GetFieldID(env, cls, "_d", "[D");
            o = (*env)->GetObjectField(env, jprop, valf);
            (*env)->ReleaseDoubleArrayElements(env, o, val, JNI_ABORT);
            break;
        case MAPPER_STRING: {
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

static void java_signal_update_cb(mapper_signal sig, mapper_id id, int len,
                                  mapper_type type, const void *val,
                                  mapper_time_t *time)
{
    if (bailing)
        return;

    jobject vobj = 0;
    if (MAPPER_FLOAT == type && val) {
        jfloatArray varr = (*genv)->NewFloatArray(genv, len);
        if (varr)
            (*genv)->SetFloatArrayRegion(genv, varr, 0, len, val);
        vobj = (jobject) varr;
    }
    else if (MAPPER_INT32 == type && val) {
        jintArray varr = (*genv)->NewIntArray(genv, len);
        if (varr)
            (*genv)->SetIntArrayRegion(genv, varr, 0, len, val);
        vobj = (jobject) varr;
    }
    else if (MAPPER_DOUBLE == type && val) {
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

    if (ctx->instanceUpdateListener) {
        instance_jni_context ictx;
        ictx = ((instance_jni_context)
                mapper_signal_get_instance_user_data(sig, id));
        if (!ictx)
            return;
        update_cb = ictx->listener;
        if (update_cb && ictx->instance) {
            jclass cls = (*genv)->GetObjectClass(genv, update_cb);
            if (cls) {
                jmethodID mid=0;
                if (type == MAPPER_INT32) {
                    mid = (*genv)->GetMethodID(genv, cls, "onUpdate",
                                               "(Lmapper/Signal$Instance;"
                                               "[I"
                                               "Lmapper/Time;)V");
                }
                else if (type == MAPPER_FLOAT) {
                    mid = (*genv)->GetMethodID(genv, cls, "onUpdate",
                                               "(Lmapper/Signal$Instance;"
                                               "[F"
                                               "Lmapper/Time;)V");
                }
                else if (type == MAPPER_DOUBLE) {
                    mid = (*genv)->GetMethodID(genv, cls, "onUpdate",
                                               "(Lmapper/Signal$Instance;"
                                               "[D"
                                               "Lmapper/Time;)V");
                }
                if (mid) {
                    (*genv)->CallVoidMethod(genv, update_cb, mid,
                                            ictx->instance, vobj, jtime);
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
            if (type == MAPPER_INT32) {
                mid = (*genv)->GetMethodID(genv, cls, "onUpdate",
                                           "(Lmapper/Signal;"
                                           "[I"
                                           "Lmapper/Time;)V");
            }
            else if (type == MAPPER_FLOAT) {
                mid = (*genv)->GetMethodID(genv, cls, "onUpdate",
                                           "(Lmapper/Signal;"
                                           "[F"
                                           "Lmapper/Time;)V");
            }
            else if (type == MAPPER_DOUBLE) {
                mid = (*genv)->GetMethodID(genv, cls, "onUpdate",
                                           "(Lmapper/Signal;"
                                           "[D"
                                           "Lmapper/Time;)V");
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

static void java_signal_instance_event_cb(mapper_signal sig, mapper_id id,
                                          mapper_instance_event event,
                                          mapper_time_t *time)
{
    if (bailing)
        return;

    jobject jtime = get_jobject_from_time(genv, time);

    signal_jni_context ctx = (signal_jni_context)signal_user_data(sig);
    if (!ctx->instanceEventListener || !ctx->signal)
        return;
    jclass cls = (*genv)->GetObjectClass(genv, ctx->instanceEventListener);
    if (!cls)
        return;
    instance_jni_context ictx;
    ictx = (instance_jni_context)mapper_signal_get_instance_user_data(sig, id);
    if (!ictx || !ictx->instance)
        return;
    jmethodID mid;
    mid = (*genv)->GetMethodID(genv, cls, "onEvent", "(Lmapper/Signal$Instance;"
                               "Lmapper/signal/InstanceEvent;Lmapper/Time;)V");
    if (mid) {
        jobject eventobj = get_jobject_from_instance_event(genv, event);
        (*genv)->CallVoidMethod(genv, ctx->instanceEventListener, mid,
                                ictx->instance, eventobj, jtime);
        if ((*genv)->ExceptionOccurred(genv))
            bailing = 1;
    }
    else {
        printf("Error looking up onEvent method.\n");
        exit(1);
    }

    if (jtime)
        (*genv)->DeleteLocalRef(genv, jtime);
}

static jobject create_signal_object(JNIEnv *env, jobject devobj,
                                    signal_jni_context ctx,
                                    jobject listener,
                                    mapper_signal sig)
{
    jobject sigobj = 0;
    // Create a wrapper class for this signal
    jclass cls = (*env)->FindClass(env, "mapper/Signal");
    if (cls) {
        jmethodID mid = (*env)->GetMethodID(env, cls, "<init>", "(J)V");
        sigobj = (*env)->NewObject(env, cls, mid, jlong_ptr(sig));
    }

    if (sigobj) {
        mapper_object_set_prop((mapper_object)sig, MAPPER_PROP_USER_DATA, NULL,
                               1, MAPPER_PTR, &ctx, 0);
        ctx->signal = (*env)->NewGlobalRef(env, sigobj);
        ctx->listener = listener ? (*env)->NewGlobalRef(env, listener) : 0;
    }
    else {
        printf("Error creating signal wrapper class.\n");
        exit(1);
    }
    return sigobj;
}

/**** mapper_AbstractObject.h ****/

JNIEXPORT void JNICALL Java_mapper_AbstractObject_mapperObjectFree
  (JNIEnv *env, jobject obj, jlong abstr)
{
    mapper_object mobj = (mapper_object)ptr_jlong(abstr);
    if (!mobj || MAPPER_OBJ_DEVICE != mapper_object_get_type(mobj)
        || !is_local(mobj))
        return;

    mapper_device dev = (mapper_device)mobj;
    /* Free all references to Java objects. */
    mapper_object *sigs = mapper_device_get_signals(dev, MAPPER_DIR_ANY);
    while (sigs) {
        mapper_signal temp = (mapper_signal)*sigs;
        sigs = mapper_object_list_next(sigs);

        // do not call instance event callback
        mapper_signal_set_instance_event_callback(temp, 0, 0);

        // check if we have active instances
        int i;
        for (i = 0; i < mapper_signal_get_num_active_instances(temp); i++) {
            mapper_id id = mapper_signal_get_instance_id(temp, i);
            instance_jni_context ictx;
            ictx = mapper_signal_get_instance_user_data(temp, id);
            if (!ictx)
                continue;
            if (ictx->instance)
                (*env)->DeleteGlobalRef(env, ictx->instance);
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
        if (ctx->instanceEventListener)
            (*env)->DeleteGlobalRef(env, ctx->instanceEventListener);
        free(ctx);
    }
    mapper_device_free(dev);
}

JNIEXPORT jlong JNICALL Java_mapper_AbstractObject_graph
  (JNIEnv *env, jobject jobj, jlong ptr)
{
    mapper_object mobj = (mapper_object) ptr_jlong(ptr);
    return mobj ? jlong_ptr(mapper_object_get_graph(mobj)) : 0;
}

JNIEXPORT jint JNICALL Java_mapper_AbstractObject_numProperties
  (JNIEnv *env, jobject jobj)
{
    mapper_object mobj = get_mapper_object_from_jobject(env, jobj);
    return mobj ? mapper_object_get_num_props(mobj, 0) : 0;
}

JNIEXPORT jobject JNICALL Java_mapper_AbstractObject_getProperty__Ljava_lang_String_2
  (JNIEnv *env, jobject jobj, jstring name)
{
    mapper_object mobj = get_mapper_object_from_jobject(env, jobj);
    const char *cname = (*env)->GetStringUTFChars(env, name, 0);
    mapper_type type;
    int len;
    const void *val;
    mapper_property propID = mapper_object_get_prop_by_name(mobj, cname, &len,
                                                            &type, &val);
    jobject o = 0;
    if (MAPPER_PROP_UNKNOWN != propID)
        o = build_Value(env, len, type, val, 0);

    (*env)->ReleaseStringUTFChars(env, name, cname);
    return o;
}

JNIEXPORT jobject JNICALL Java_mapper_AbstractObject_getProperty__I
  (JNIEnv *env, jobject jobj, jint propID)
{
    mapper_object mobj = get_mapper_object_from_jobject(env, jobj);
    const char *name;
    mapper_type type;
    int len;
    const void *val;
    propID = mapper_object_get_prop_by_index(mobj, propID, &name, &len, &type,
                                             &val);
    jobject o = 0;
    if (MAPPER_PROP_UNKNOWN != propID)
        o = build_Value(env, len, type, val, 0);

    return o;
}

JNIEXPORT void JNICALL Java_mapper_AbstractObject_mapperSetProperty
  (JNIEnv *env, jobject jobj, jlong ptr, jint id, jstring name, jobject jval)
{
    mapper_object mobj = (mapper_object) ptr_jlong(ptr);
    if (!mobj)
        return;
    const char *cname = name ? (*env)->GetStringUTFChars(env, name, 0) : 0;
    mapper_type type;
    void *val;
    int len = get_Value_elements(env, jval, &val, &type);
    if (len) {
        mapper_object_set_prop(mobj, id, cname, len, type, val, 1);
        release_Value_elements(env, jval, val);
    }
}

JNIEXPORT void JNICALL Java_mapper_AbstractObject_removeProperty
  (JNIEnv *env, jobject jobj, jint id, jstring name)
{
    mapper_object mobj = get_mapper_object_from_jobject(env, jobj);
    if (!mobj)
        return;
    const char *cname = name ? (*env)->GetStringUTFChars(env, name, 0) : 0;
    mapper_object_remove_prop(mobj, id, cname);
    if (cname)
        (*env)->ReleaseStringUTFChars(env, name, cname);
}

JNIEXPORT void JNICALL Java_mapper_AbstractObject_mapperPush
  (JNIEnv *env, jobject jobj, jlong ptr)
{
    mapper_object obj = (mapper_object)ptr_jlong(ptr);
    if (obj)
        mapper_object_push(obj);
}

/**** mapper_Graph.h ****/

JNIEXPORT jlong JNICALL Java_mapper_Graph_mapperGraphNew
  (JNIEnv *env, jobject obj, jint flags)
{
    return jlong_ptr(mapper_graph_new(flags));
}

JNIEXPORT void JNICALL Java_mapper_Graph_mapperGraphFree
  (JNIEnv *env, jobject obj, jlong jgraph)
{
    mapper_graph g = (mapper_graph)ptr_jlong(jgraph);
    mapper_graph_free(g);
}

JNIEXPORT jobject JNICALL Java_mapper_Graph_poll
  (JNIEnv *env, jobject obj, jint block_ms)
{
    genv = env;
    bailing = 0;
    mapper_graph g = get_graph_from_jobject(env, obj);
    if (g)
        mapper_graph_poll(g, block_ms);
    return obj;
}

JNIEXPORT void JNICALL Java_mapper_Graph_mapperGraphSubscribe
  (JNIEnv *env, jobject obj, jlong jgraph, jobject jdev, jint flags, jint lease)
{
    mapper_graph g = (mapper_graph)ptr_jlong(jgraph);
    mapper_device dev = (mapper_device)get_mapper_object_from_jobject(env, jdev);
    if (g && dev)
        mapper_graph_subscribe(g, dev, flags, lease);
}

JNIEXPORT jobject JNICALL Java_mapper_Graph_unsubscribe
  (JNIEnv *env, jobject obj, jobject jdev)
{
    mapper_graph g = get_graph_from_jobject(env, obj);
    mapper_device dev = (mapper_device)get_mapper_object_from_jobject(env, jdev);
    if (g && dev)
        mapper_graph_unsubscribe(g, dev);
    return obj;
}

JNIEXPORT jobject JNICALL Java_mapper_Graph_requestDevices
  (JNIEnv *env, jobject obj)
{
    mapper_graph g = get_graph_from_jobject(env, obj);
    if (g)
        mapper_graph_request_devices(g);
    return obj;
}

static void java_graph_cb(mapper_graph g, mapper_object mobj,
                          mapper_record_event event, const void *user_data)
{
    if (bailing || !user_data)
        return;

    // Create a wrapper class for the device
    jclass cls;
    mapper_object_type type = mapper_object_get_type(mobj);
    switch (type) {
        case MAPPER_OBJ_DEVICE:
            cls = (*genv)->FindClass(genv, "mapper/Device");
            break;
        case MAPPER_OBJ_SIGNAL:
            cls = (*genv)->FindClass(genv, "mapper/Signal");
            break;
        case MAPPER_OBJ_MAP:
            cls = (*genv)->FindClass(genv, "mapper/Map");
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
    jobject eventobj = get_jobject_from_graph_record_event(genv, event);
    if (!eventobj) {
        printf("Error looking up graph event\n");
        return;
    }

    jobject listener = (jobject)user_data;
    cls = (*genv)->GetObjectClass(genv, listener);
    if (cls) {
        switch (type) {
            case MAPPER_OBJ_DEVICE:
                mid = (*genv)->GetMethodID(genv, cls, "onEvent",
                                           "(Lmapper/Device;Lmapper/graph/Event;)V");
                break;
            case MAPPER_OBJ_SIGNAL:
                mid = (*genv)->GetMethodID(genv, cls, "onEvent",
                                           "(Lmapper/Signal;Lmapper/graph/Event;)V");
                break;
            case MAPPER_OBJ_MAP:
                mid = (*genv)->GetMethodID(genv, cls, "onEvent",
                                           "(Lmapper/Map;Lmapper/graph/Event;)V");
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

JNIEXPORT void JNICALL Java_mapper_Graph_addCallback
  (JNIEnv *env, jobject obj, jlong jgraph, jobject listener)
{
    mapper_graph g = (mapper_graph)ptr_jlong(jgraph);
    if (!g || !listener)
        return;
    jobject o = (*env)->NewGlobalRef(env, listener);
    mapper_graph_add_callback(g, java_graph_cb, MAPPER_OBJ_DEVICE, o);
}

JNIEXPORT void JNICALL Java_mapper_Graph_removeCallback
  (JNIEnv *env, jobject obj, jlong jgraph, jobject listener)
{
    mapper_graph g = (mapper_graph)ptr_jlong(jgraph);
    if (!g || !listener)
        return;
    // TODO: fix mismatch in user_data
    mapper_graph_remove_callback(g, java_graph_cb, listener);
    (*env)->DeleteGlobalRef(env, listener);
}

JNIEXPORT jobject JNICALL Java_mapper_Graph_devices
  (JNIEnv *env, jobject obj)
{
    jclass cls = (*env)->FindClass(env, "mapper/List");
    if (!cls)
        return 0;

    mapper_device *devs = 0;
    mapper_graph g = get_graph_from_jobject(env, obj);
    if (g)
        devs = mapper_graph_get_objects(g, MAPPER_OBJ_DEVICE);

    jmethodID mid = (*env)->GetMethodID(env, cls, "<init>", "(J)V");
    return (*env)->NewObject(env, cls, mid, jlong_ptr(devs));
}

JNIEXPORT jobject JNICALL Java_mapper_Graph_signals
  (JNIEnv *env, jobject obj)
{
    jclass cls = (*env)->FindClass(env, "mapper/List");
    if (!cls)
        return 0;

    mapper_object *signals = 0;
    mapper_graph g = get_graph_from_jobject(env, obj);
    if (g)
        signals = mapper_graph_get_objects(g, MAPPER_OBJ_SIGNAL);

    jmethodID mid = (*env)->GetMethodID(env, cls, "<init>", "(J)V");
    return (*env)->NewObject(env, cls, mid, jlong_ptr(signals));
}

JNIEXPORT jobject JNICALL Java_mapper_Graph_maps
  (JNIEnv *env, jobject obj)
{
    jclass cls = (*env)->FindClass(env, "mapper/List");
    if (!cls)
        return 0;

    mapper_object *maps = 0;
    mapper_graph g = get_graph_from_jobject(env, obj);
    if (g)
        maps = mapper_graph_get_objects(g, MAPPER_OBJ_MAP);

    jmethodID mid = (*env)->GetMethodID(env, cls, "<init>", "(J)V");
    return (*env)->NewObject(env, cls, mid, jlong_ptr(maps));
}

/**** mapper_Device.h ****/

JNIEXPORT jlong JNICALL Java_mapper_Device_mapperDeviceNew
  (JNIEnv *env, jobject obj, jstring name, jobject jgraph)
{
    mapper_graph g = jgraph ? get_graph_from_jobject(env, jgraph) : 0;
    const char *cname = (*env)->GetStringUTFChars(env, name, 0);
    mapper_device dev = mapper_device_new(cname, g);
    (*env)->ReleaseStringUTFChars(env, name, cname);
    return jlong_ptr(dev);
}

JNIEXPORT jint JNICALL Java_mapper_Device_mapperDevicePoll
  (JNIEnv *env, jobject obj, jlong jdev, jint block_ms)
{
    genv = env;
    bailing = 0;
    mapper_device dev = (mapper_device)ptr_jlong(jdev);
    return dev ? mapper_device_poll(dev, block_ms) : 0;
}

JNIEXPORT jobject JNICALL Java_mapper_Device_mapperAddSignal
  (JNIEnv *env, jobject obj, jint dir, jint numInst, jstring name, jint len,
   jchar type, jstring unit, jobject min, jobject max, jobject listener)
{
    if (!name || (len <= 0) || (   type != MAPPER_FLOAT
                                && type != MAPPER_INT32
                                && type != MAPPER_DOUBLE))
        return 0;

    mapper_device dev = (mapper_device)get_mapper_object_from_jobject(env, obj);
    if (!dev)
        return 0;

    const char *cname = (*env)->GetStringUTFChars(env, name, 0);
    const char *cunit = unit ? (*env)->GetStringUTFChars(env, unit, 0) : 0;

    mapper_signal sig = mapper_device_add_signal(dev, dir, numInst, cname,
                                                 len, (char)type, cunit, 0, 0,
                                                 listener ? java_signal_update_cb : 0);

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
            Java_mapper_AbstractObject_mapperSetProperty(env, sigobj, jlong_ptr(sig),
                                                         MAPPER_PROP_MIN, NULL,
                                                         min);
        }
        if (max) {
            Java_mapper_AbstractObject_mapperSetProperty(env, sigobj, jlong_ptr(sig),
                                                         MAPPER_PROP_MAX, NULL,
                                                         min);
        }
    }
    return sigobj;
}

JNIEXPORT void JNICALL Java_mapper_Device_mapperDeviceRemoveSignal
  (JNIEnv *env, jobject obj, jlong jdev, jobject jsig)
{
    mapper_device dev = (mapper_device) ptr_jlong(jdev);
    if (!dev || !is_local((mapper_object)dev))
        return;
    mapper_signal sig = (mapper_signal)get_mapper_object_from_jobject(env, jsig);
    if (sig) {
        // do not call instance event callback
        mapper_signal_set_instance_event_callback(sig, 0, 0);

        // check if we have active instances
        while (mapper_signal_get_num_active_instances(sig)) {
            mapper_id id = mapper_signal_get_active_instance_id(sig, 0);
            if (!id)
                continue;
            instance_jni_context ictx;
            ictx = mapper_signal_get_instance_user_data(sig, id);
            if (!ictx)
                continue;
            if (ictx->instance)
                (*env)->DeleteGlobalRef(env, ictx->instance);
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
        if (ctx->instanceEventListener)
            (*env)->DeleteGlobalRef(env, ctx->instanceEventListener);
        free(ctx);

        mapper_device_remove_signal(dev, sig);
    }
}

JNIEXPORT jlong JNICALL Java_mapper_Device_addSignalGroup
  (JNIEnv *env, jobject obj)
{
    mapper_device dev = (mapper_device)get_mapper_object_from_jobject(env, obj);
    return dev ? mapper_device_add_signal_group(dev) : 0;
}

JNIEXPORT jobject JNICALL Java_mapper_Device_removeSignalGroup
  (JNIEnv *env, jobject obj, jlong grp)
{
    mapper_device dev = (mapper_device)get_mapper_object_from_jobject(env, obj);
    if (dev)
        mapper_device_remove_signal_group(dev, grp);
    return obj;
}

JNIEXPORT jboolean JNICALL Java_mapper_Device_ready
  (JNIEnv *env, jobject obj)
{
    mapper_device dev = (mapper_device)get_mapper_object_from_jobject(env, obj);
    return dev ? mapper_device_ready(dev) : 0;
}

JNIEXPORT jobject JNICALL Java_mapper_Device_startQueue
  (JNIEnv *env, jobject obj, jobject jtime)
{
    mapper_device dev = (mapper_device)get_mapper_object_from_jobject(env, obj);
    if (!dev)
        return 0;
    mapper_time_t time, *ptime = 0;
    ptime = get_time_from_jobject(env, jtime, &time);
    if (!ptime) {
        mapper_time_now(&time);
        jtime = get_jobject_from_time(env, &time);
    }
    mapper_device_start_queue(dev, time);
    return jtime;
}

JNIEXPORT jobject JNICALL Java_mapper_Device_sendQueue
  (JNIEnv *env, jobject obj, jobject jtime)
{
    mapper_device dev = (mapper_device)get_mapper_object_from_jobject(env, obj);
    if (!dev || !is_local((mapper_object)dev))
        return 0;
    mapper_time_t time, *ptime = 0;
    ptime = get_time_from_jobject(env, jtime, &time);
    if (ptime)
        mapper_device_send_queue(dev, time);
    return obj;
}

JNIEXPORT jlong JNICALL Java_mapper_Device_signals
  (JNIEnv *env, jobject obj, jlong jdev, jint dir)
{
    mapper_device dev = (mapper_device) ptr_jlong(jdev);
    return dev ? jlong_ptr(mapper_device_get_signals(dev, dir)) : 0;
}

JNIEXPORT jlong JNICALL Java_mapper_Device_maps
  (JNIEnv *env, jobject obj, jlong jdev, jint dir)
{
    mapper_device dev = (mapper_device) ptr_jlong(jdev);
    return dev ? jlong_ptr(mapper_device_get_maps(dev, dir)) : 0;
}

/**** mapper_List.h ****/

JNIEXPORT jobject JNICALL Java_mapper_List_newObject
  (JNIEnv *env, jobject obj, jlong);

JNIEXPORT jlong JNICALL Java_mapper_List_mapperListCopy
  (JNIEnv *env, jobject obj, jlong list)
{
    mapper_object *objs = (mapper_object*) ptr_jlong(list);
    if (!objs)
        return 0;
    return jlong_ptr(mapper_object_list_copy(objs));
}

JNIEXPORT jlong JNICALL Java_mapper_List_mapperListDeref
  (JNIEnv *env, jobject obj, jlong list)
{
    void **ptr = (void**)ptr_jlong(list);
    return jlong_ptr(*ptr);
}

JNIEXPORT void JNICALL Java_mapper_List_mapperListFilter
  (JNIEnv *env, jobject obj, jlong list, jint id, jstring name, jobject val)
{
// TODO
}

JNIEXPORT void JNICALL Java_mapper_List_mapperListFree
  (JNIEnv *env, jobject obj, jlong list)
{
    mapper_object *objs = (mapper_object*) ptr_jlong(list);
    if (objs)
        mapper_object_list_free(objs);
}

JNIEXPORT jlong JNICALL Java_mapper_List_mapperListIntersect
  (JNIEnv *env, jobject obj, jlong lhs, jlong rhs)
{
    mapper_object *objs_lhs = (mapper_object*) ptr_jlong(lhs);
    mapper_object *objs_rhs = (mapper_object*) ptr_jlong(rhs);

    if (!objs_lhs || !objs_rhs)
        return 0;

    // use a copy of rhs
    mapper_object *objs_rhs_cpy = mapper_object_list_copy(objs_rhs);
    return jlong_ptr(mapper_object_list_intersection(objs_lhs, objs_rhs_cpy));
}

JNIEXPORT jlong JNICALL Java_mapper_List_mapperListJoin
  (JNIEnv *env, jobject obj, jlong lhs, jlong rhs)
{
    mapper_object *objs_lhs = (mapper_object*) ptr_jlong(lhs);
    mapper_object *objs_rhs = (mapper_object*) ptr_jlong(rhs);

    if (!objs_rhs)
        return lhs;

    // use a copy of rhs
    mapper_device *objs_rhs_cpy = mapper_object_list_copy(objs_rhs);
    return jlong_ptr(mapper_object_list_union(objs_lhs, objs_rhs_cpy));
}

JNIEXPORT jint JNICALL Java_mapper_List_mapperListLength
  (JNIEnv *env, jobject obj, jlong list)
{
    mapper_object *objs = (mapper_object*) ptr_jlong(list);
    return objs ? mapper_object_list_get_length(objs) : 0;
}

JNIEXPORT jlong JNICALL Java_mapper_List_mapperListNext
  (JNIEnv *env, jobject obj, jlong list)
{
    mapper_object *objs = (mapper_object*) ptr_jlong(list);
    return objs ? jlong_ptr(mapper_object_list_next(objs)) : 0;
}

JNIEXPORT jlong JNICALL Java_mapper_List_mapperListSubtract
  (JNIEnv *env, jobject obj, jlong lhs, jlong rhs)
{
    mapper_object *objs_lhs = (mapper_object*) ptr_jlong(lhs);
    mapper_object *objs_rhs = (mapper_object*) ptr_jlong(rhs);

    if (!objs_lhs || !objs_rhs)
        return lhs;

    // use a copy of rhs
    mapper_object *objs_rhs_cpy = mapper_object_list_copy(objs_rhs);
    return jlong_ptr(mapper_object_list_difference(objs_lhs, objs_rhs_cpy));
}

/**** mapper_Map.h ****/

JNIEXPORT jlong JNICALL Java_mapper_Map_mapperMapSrcSignalPtr
  (JNIEnv *env, jobject obj, jlong jmap, jint index)
{
    mapper_map map = (mapper_map) ptr_jlong(jmap);
    return map ? jlong_ptr(mapper_map_get_signal(map, MAPPER_LOC_SRC, index)) : 0;
}

JNIEXPORT jlong JNICALL Java_mapper_Map_mapperMapDstSignalPtr
  (JNIEnv *env, jobject obj, jlong jmap)
{
    mapper_map map = (mapper_map) ptr_jlong(jmap);
    return map ? jlong_ptr(mapper_map_get_signal(map, MAPPER_LOC_DST, 0)) : 0;
}

JNIEXPORT jlong JNICALL Java_mapper_Map_mapperMapNew
  (JNIEnv *env, jobject obj, jobjectArray jsrc, jobject jdst)
{
    int i, num_src = (*env)->GetArrayLength(env, jsrc);
    if (num_src <= 0 || !jdst)
        return 0;
    mapper_signal src[num_src];
    for (i = 0; i < num_src; i++) {
        jobject o = (*env)->GetObjectArrayElement(env, jsrc, i);
        src[i] = (mapper_signal)get_mapper_object_from_jobject(env, o);
    }
    mapper_signal dst = (mapper_signal)get_mapper_object_from_jobject(env, jdst);

    mapper_map map = mapper_map_new(num_src, src, 1, &dst);
    return map ? jlong_ptr(map) : 0;
}

JNIEXPORT jobject JNICALL Java_mapper_Map_refresh
  (JNIEnv *env, jobject obj)
{
    mapper_map map = (mapper_map)get_mapper_object_from_jobject(env, obj);
    if (map)
        mapper_map_refresh(map);
    return obj;
}

JNIEXPORT jobject JNICALL Java_mapper_Map_scopes
  (JNIEnv *env, jobject obj)
{
    jclass cls = (*env)->FindClass(env, "mapper/List");
    if (!cls)
        return 0;
    mapper_map map = (mapper_map)get_mapper_object_from_jobject(env, obj);
    if (!map)
        return 0;
    mapper_device *scopes = mapper_map_get_scopes(map);
    jmethodID mid = (*env)->GetMethodID(env, cls, "<init>", "(J)V");
    return (*env)->NewObject(env, cls, mid, jlong_ptr(scopes));
}

JNIEXPORT jobject JNICALL Java_mapper_Map_addScope
  (JNIEnv *env, jobject obj, jobject jdev)
{
    mapper_map map = (mapper_map)get_mapper_object_from_jobject(env, obj);
    if (map) {
        mapper_device dev = (mapper_device)get_mapper_object_from_jobject(env, jdev);
        if (dev)
            mapper_map_add_scope(map, dev);
    }
    return obj;
}

JNIEXPORT jobject JNICALL Java_mapper_Map_removeScope
  (JNIEnv *env, jobject obj, jobject jdev)
{
    mapper_map map = (mapper_map)get_mapper_object_from_jobject(env, obj);
    if (map) {
        mapper_device dev = (mapper_device)get_mapper_object_from_jobject(env, jdev);
        if (dev)
            mapper_map_remove_scope(map, dev);
    }
    return obj;
}

JNIEXPORT jint JNICALL Java_mapper_Map_mapperMapNumSignals
  (JNIEnv *env, jobject obj, jlong jmap, jint loc)
{
    mapper_map map = (mapper_map) ptr_jlong(jmap);
    return map ? mapper_map_get_num_signals(map, loc) : 0;
}

JNIEXPORT jboolean JNICALL Java_mapper_Map_ready
  (JNIEnv *env, jobject obj)
{
    mapper_map map = (mapper_map)get_mapper_object_from_jobject(env, obj);
    return map ? (mapper_map_ready(map)) : 0;
}

/**** mapper_Signal_Instances.h ****/

JNIEXPORT jlong JNICALL Java_mapper_Signal_00024Instance_mapperInstance
  (JNIEnv *env, jobject obj, jboolean has_id, jlong jid, jobject ref)
{
    mapper_id id = 0;
    mapper_signal sig = get_instance_from_jobject(env, obj, 0);
    if (!sig)
        return 0;

    if (has_id)
        id = jid;
    else if (mapper_signal_get_num_reserved_instances(sig)) {
        // retrieve id from a reserved signal instance
        id = mapper_signal_get_reserved_instance_id(sig, 0);
    }
    else {
        // try to steal an active instance
        mapper_device dev = mapper_signal_get_device(sig);
        id = mapper_device_generate_unique_id(dev);
        if (!mapper_signal_activate_instance(sig, id)) {
            printf("Could not activate instance with id %"PR_MAPPER_ID"\n", id);
            return 0;
        }
    }

    instance_jni_context ctx = ((instance_jni_context)
                                mapper_signal_get_instance_user_data(sig, id));
    if (!ctx) {
        printf("No context found for instance %"PR_MAPPER_ID"\n", id);
        return 0;
    }

    ctx->instance = (*env)->NewGlobalRef(env, obj);
    if (ref)
        ctx->user_ref = (*env)->NewGlobalRef(env, ref);
    return id;
}

JNIEXPORT void JNICALL Java_mapper_Signal_00024Instance_release
  (JNIEnv *env, jobject obj, jobject jtime)
{
    mapper_id id;
    mapper_signal sig = get_instance_from_jobject(env, obj, &id);
    if (sig) {
        mapper_time_t time, *ptime = 0;
        if (jtime)
            ptime = get_time_from_jobject(env, jtime, &time);
        mapper_signal_release_instance(sig, id, ptime ? *ptime : MAPPER_NOW);
    }
}

JNIEXPORT void JNICALL Java_mapper_Signal_00024Instance_mapperFreeInstance
  (JNIEnv *env, jobject obj, jlong jsig, jlong id, jobject jtime)
{
    mapper_signal sig = (mapper_signal) ptr_jlong(jsig);
    if (!sig)
        return;
    instance_jni_context ctx = mapper_signal_get_instance_user_data(sig, id);
    if (ctx) {
        if (ctx->listener)
            (*env)->DeleteGlobalRef(env, ctx->listener);
        if (ctx->instance)
            (*env)->DeleteGlobalRef(env, ctx->instance);
        if (ctx->user_ref)
            (*env)->DeleteGlobalRef(env, ctx->user_ref);
    }
    mapper_time_t time, *ptime = 0;
    ptime = get_time_from_jobject(env, jtime, &time);
    mapper_signal_release_instance(sig, id, ptime ? *ptime : MAPPER_NOW);
}

JNIEXPORT jint JNICALL Java_mapper_Signal_00024Instance_isActive
  (JNIEnv *env, jobject obj)
{
    mapper_id id;
    mapper_signal sig = get_instance_from_jobject(env, obj, &id);
    return sig ? mapper_signal_get_instance_is_active(sig, id) : 0;
}

JNIEXPORT jobject JNICALL Java_mapper_Signal_00024Instance_userReference
  (JNIEnv *env, jobject obj)
{
    mapper_id id;
    mapper_signal sig = get_instance_from_jobject(env, obj, &id);
    if (!sig)
        return 0;
    instance_jni_context ctx = ((instance_jni_context)
                                mapper_signal_get_instance_user_data(sig, id));
    return ctx ? ctx->user_ref : 0;
}

JNIEXPORT jobject JNICALL Java_mapper_Signal_00024Instance_setUserReference
  (JNIEnv *env, jobject obj, jobject userObj)
{
    mapper_id id;
    mapper_signal sig = get_instance_from_jobject(env, obj, &id);
    if (!sig)
        return obj;
    instance_jni_context ctx = ((instance_jni_context)
                                mapper_signal_get_instance_user_data(sig, id));
    if (ctx) {
        if (ctx->user_ref)
            (*env)->DeleteGlobalRef(env, ctx->user_ref);
        ctx->user_ref = userObj ? (*env)->NewGlobalRef(env, userObj) : 0;
    }
    return obj;
}

/**** mapper_Signal.h ****/

JNIEXPORT jobject JNICALL Java_mapper_Signal_device
  (JNIEnv *env, jobject obj)
{
    jclass cls = (*env)->FindClass(env, "mapper/Device");
    if (!cls)
        return 0;

    mapper_device dev = 0;
    mapper_signal sig = (mapper_signal)get_mapper_object_from_jobject(env, obj);
    if (sig)
        dev = mapper_signal_get_device(sig);

    jmethodID mid = (*env)->GetMethodID(env, cls, "<init>", "(J)V");
    return (*env)->NewObject(env, cls, mid, jlong_ptr(dev));
}

JNIEXPORT void JNICALL Java_mapper_Signal_mapperSignalSetInstanceEventCB
  (JNIEnv *env, jobject obj, jobject listener, jint flags)
{
    mapper_signal sig = (mapper_signal)get_mapper_object_from_jobject(env, obj);
    if (!sig)
        return;

    signal_jni_context ctx = (signal_jni_context)signal_user_data(sig);

    if (ctx->instanceEventListener)
        (*env)->DeleteGlobalRef(env, ctx->instanceEventListener);
    if (listener) {
        ctx->instanceEventListener = (*env)->NewGlobalRef(env, listener);
        mapper_signal_set_instance_event_callback(sig,
                                                  java_signal_instance_event_cb,
                                                  flags);
    }
    else {
        ctx->instanceEventListener = 0;
        mapper_signal_set_instance_event_callback(sig, 0, flags);
    }
}

JNIEXPORT jobject JNICALL Java_mapper_Signal_setUpdateListener
  (JNIEnv *env, jobject obj, jobject listener)
{
    mapper_signal sig = (mapper_signal)get_mapper_object_from_jobject(env, obj);
    if (!sig)
        return obj;

    signal_jni_context ctx = (signal_jni_context)signal_user_data(sig);
    if (ctx->listener)
        (*env)->DeleteGlobalRef(env, ctx->listener);
    if (listener) {
        ctx->listener = (*env)->NewGlobalRef(env, listener);
        mapper_signal_set_callback(sig, java_signal_update_cb);
    }
    else {
        ctx->listener = 0;
        mapper_signal_set_callback(sig, 0);
    }
    return obj;
}

JNIEXPORT jobject JNICALL Java_mapper_Signal_setInstanceUpdateListener
  (JNIEnv *env, jobject obj, jobject listener)
{
    mapper_signal sig = (mapper_signal)get_mapper_object_from_jobject(env, obj);
    if (!sig)
        return obj;

    int i, idx;
    for (i = 0; i < mapper_signal_get_num_instances(sig); i++) {
        idx = mapper_signal_get_instance_id(sig, i);
        instance_jni_context ctx = ((instance_jni_context)
                                    mapper_signal_get_instance_user_data(sig, idx));
        if (!ctx)
            continue;
        if (ctx->listener)
            (*env)->DeleteGlobalRef(env, ctx->listener);
        ctx->listener = listener ? (*env)->NewGlobalRef(env, listener) : 0;
    }

    // also set it for the signal
    signal_jni_context ctx = (signal_jni_context)signal_user_data(sig);
    if (ctx) {
        if (ctx->instanceUpdateListener)
            (*env)->DeleteGlobalRef(env, ctx->instanceUpdateListener);
        if (listener)
            ctx->instanceUpdateListener = (*env)->NewGlobalRef(env, listener);
        else
            ctx->instanceUpdateListener = 0;
    }
    return obj;
}

JNIEXPORT jobject JNICALL Java_mapper_Signal_instanceUpdateListener
  (JNIEnv *env, jobject obj)
{
    mapper_signal sig = (mapper_signal)get_mapper_object_from_jobject(env, obj);
    if (sig) {
        signal_jni_context ctx = (signal_jni_context)signal_user_data(sig);
        return ctx ? ctx->listener : 0;
    }
    return 0;
}

JNIEXPORT jobject JNICALL Java_mapper_Signal_setGroup
  (JNIEnv *env, jobject obj, jlong grp)
{
    mapper_signal sig = (mapper_signal)get_mapper_object_from_jobject(env, obj);
    if (sig)
        mapper_signal_set_group(sig, grp);
    return obj;
}

JNIEXPORT jlong JNICALL Java_mapper_Signal_group
  (JNIEnv *env, jobject obj)
{
    mapper_signal sig = (mapper_signal)get_mapper_object_from_jobject(env, obj);
    return sig ? mapper_signal_get_group(sig) : 0;
}

JNIEXPORT void JNICALL Java_mapper_Signal_mapperSignalReserveInstances
  (JNIEnv *env, jobject obj, jlong jsig, jint num, jlongArray jids)
{
    mapper_signal sig = (mapper_signal) ptr_jlong(jsig);
    mapper_device dev;
    if (!sig || !(dev = mapper_signal_get_device(sig)))
        return;

    mapper_id *ids = 0;
    if (jids) {
        num = (*env)->GetArrayLength(env, jids);
        ids = (mapper_id*)(*env)->GetLongArrayElements(env, jids, NULL);
    }

    instance_jni_context ctx = 0;
    int i;
    for (i = 0; i < num; i++) {
        mapper_id id = ids ? (ids[i]) : mapper_device_generate_unique_id(dev);

        if (!ctx) {
            ctx = ((instance_jni_context)
                   calloc(1, sizeof(instance_jni_context_t)));
            if (!ctx) {
                throwOutOfMemory(env);
                return;
            }
        }

        if (mapper_signal_reserve_instances(sig, 1, &id, (void**)&ctx))
            ctx = 0;
    }

    if (ctx)
        free(ctx);

    if (ids)
        (*env)->ReleaseLongArrayElements(env, jids, (jlong*)ids, JNI_ABORT);
}

JNIEXPORT jobject JNICALL Java_mapper_Signal_oldestActiveInstance
  (JNIEnv *env, jobject obj)
{
    mapper_signal sig = (mapper_signal)get_mapper_object_from_jobject(env, obj);
    if (!sig) {
        printf("couldn't retrieve signal in "
               " Java_mapper_Signal_oldestActiveInstance()\n");
        return 0;
    }
    mapper_id id = mapper_signal_get_oldest_instance_id(sig);
    instance_jni_context ctx = ((instance_jni_context)
                                mapper_signal_get_instance_user_data(sig, id));
    return ctx ? ctx->instance : 0;
}

JNIEXPORT jobject JNICALL Java_mapper_Signal_newestActiveInstance
  (JNIEnv *env, jobject obj)
{
    mapper_signal sig = (mapper_signal)get_mapper_object_from_jobject(env, obj);
    if (!sig)
        return 0;
    mapper_id id = mapper_signal_get_newest_instance_id(sig);
    instance_jni_context ctx = ((instance_jni_context)
                                mapper_signal_get_instance_user_data(sig, id));
    return ctx ? ctx->instance : 0;
}

JNIEXPORT jint JNICALL Java_mapper_Signal_numActiveInstances
  (JNIEnv *env, jobject obj)
{
    mapper_signal sig = (mapper_signal)get_mapper_object_from_jobject(env, obj);
    return sig ? mapper_signal_get_num_active_instances(sig) : 0;
}

JNIEXPORT jint JNICALL Java_mapper_Signal_numReservedInstances
  (JNIEnv *env, jobject obj)
{
    mapper_signal sig = (mapper_signal)get_mapper_object_from_jobject(env, obj);
    return sig ? mapper_signal_get_num_reserved_instances(sig) : 0;
}

JNIEXPORT void JNICALL Java_mapper_Signal_mapperSetStealingMode
  (JNIEnv *env, jobject obj, jint mode)
{
    mapper_signal sig = (mapper_signal)get_mapper_object_from_jobject(env, obj);
    if (sig)
        mapper_signal_set_stealing_mode(sig, mode);
}

JNIEXPORT jint JNICALL Java_mapper_Signal_mapperStealingMode
  (JNIEnv *env, jobject obj, jlong jsig)
{
    mapper_signal sig = (mapper_signal) ptr_jlong(jsig);
    return sig ? mapper_signal_get_stealing_mode(sig) : 0;
}

JNIEXPORT jobject JNICALL Java_mapper_Signal_updateInstance__JILmapper_Time_2
  (JNIEnv *env, jobject obj, jlong jid, jint val, jobject jtime)
{
    mapper_signal sig = (mapper_signal)get_mapper_object_from_jobject(env, obj);
    if (!sig)
        return obj;

    mapper_id id = (mapper_id)ptr_jlong(jid);

    mapper_time_t time, *ptime = 0;
    ptime = get_time_from_jobject(env, jtime, &time);

    mapper_signal_set_value(sig, id, 1, MAPPER_INT32, &val,
                            ptime ? *ptime : MAPPER_NOW);

    return obj;
}

JNIEXPORT jobject JNICALL Java_mapper_Signal_updateInstance__JFLmapper_Time_2
  (JNIEnv *env, jobject obj, jlong jid, jfloat val, jobject jtime)
{
    mapper_signal sig = (mapper_signal)get_mapper_object_from_jobject(env, obj);
    if (!sig)
        return obj;

    mapper_id id = (mapper_id)ptr_jlong(jid);

    mapper_time_t time, *ptime = 0;
    ptime = get_time_from_jobject(env, jtime, &time);

    mapper_signal_set_value(sig, id, 1, MAPPER_FLOAT, &val,
                            ptime ? *ptime : MAPPER_NOW);

    return obj;
}

JNIEXPORT jobject JNICALL Java_mapper_Signal_updateInstance__JDLmapper_Time_2
  (JNIEnv *env, jobject obj, jlong jid, jdouble val, jobject jtime)
{
    mapper_signal sig = (mapper_signal)get_mapper_object_from_jobject(env, obj);
    if (!sig)
        return obj;

    mapper_id id = (mapper_id)ptr_jlong(jid);

    mapper_time_t time, *ptime = 0;
    ptime = get_time_from_jobject(env, jtime, &time);

    mapper_signal_set_value(sig, id, 1, MAPPER_DOUBLE, &val,
                            ptime ? *ptime : MAPPER_NOW);

    return obj;
}

JNIEXPORT jobject JNICALL Java_mapper_Signal_updateInstance__J_3ILmapper_Time_2
  (JNIEnv *env, jobject obj, jlong jid, jintArray vals, jobject jtime)
{
    mapper_signal sig = (mapper_signal)get_mapper_object_from_jobject(env, obj);
    if (!sig)
        return obj;

    int len = (*env)->GetArrayLength(env, vals);

    mapper_id id = (mapper_id)ptr_jlong(jid);

    mapper_time_t time, *ptime = 0;
    ptime = get_time_from_jobject(env, jtime, &time);

    jint *ivals = (*env)->GetIntArrayElements(env, vals, 0);
    if (!ivals)
        return obj;

    mapper_signal_set_value(sig, id, len, MAPPER_INT32, ivals,
                            ptime ? *ptime : MAPPER_NOW);

    (*env)->ReleaseIntArrayElements(env, vals, ivals, JNI_ABORT);
    return obj;
}

JNIEXPORT jobject JNICALL Java_mapper_Signal_updateInstance__J_3FLmapper_Time_2
  (JNIEnv *env, jobject obj, jlong jid, jfloatArray vals, jobject jtime)
{
    mapper_signal sig = (mapper_signal)get_mapper_object_from_jobject(env, obj);
    if (!sig)
        return obj;

    int len = (*env)->GetArrayLength(env, vals);

    mapper_id id = (mapper_id)ptr_jlong(jid);

    mapper_time_t time, *ptime = 0;
    ptime = get_time_from_jobject(env, jtime, &time);

    jfloat *fvals = (*env)->GetFloatArrayElements(env, vals, 0);
    if (!fvals)
        return obj;

    mapper_signal_set_value(sig, id, len, MAPPER_FLOAT, fvals,
                            ptime ? *ptime : MAPPER_NOW);

    (*env)->ReleaseFloatArrayElements(env, vals, fvals, JNI_ABORT);
    return obj;
}

JNIEXPORT jobject JNICALL Java_mapper_Signal_updateInstance__J_3DLmapper_Time_2
  (JNIEnv *env, jobject obj, jlong jid, jdoubleArray vals, jobject jtime)
{
    mapper_signal sig = (mapper_signal)get_mapper_object_from_jobject(env, obj);
    if (!sig)
        return obj;

    int len = (*env)->GetArrayLength(env, vals);

    mapper_id id = (mapper_id)ptr_jlong(jid);

    mapper_time_t time, *ptime = 0;
    ptime = get_time_from_jobject(env, jtime, &time);

    jdouble *dvals = (*env)->GetDoubleArrayElements(env, vals, 0);
    if (!dvals)
        return obj;

    mapper_signal_set_value(sig, id, len, MAPPER_DOUBLE, dvals,
                            ptime ? *ptime : MAPPER_NOW);

    (*env)->ReleaseDoubleArrayElements(env, vals, dvals, JNI_ABORT);
    return obj;
}

JNIEXPORT jobject JNICALL Java_mapper_Signal_instanceValue
  (JNIEnv *env, jobject obj, jlong jid)
{
    mapper_signal sig = (mapper_signal)get_mapper_object_from_jobject(env, obj);
    mapper_type type = MAPPER_INT32;
    int len = 0;
    const void *val = 0;
    mapper_time_t time = {0,1};

    mapper_id id = (mapper_id)ptr_jlong(jid);
    if (sig) {
        type = signal_type(sig);
        len = signal_length(sig);
        val = mapper_signal_get_value(sig, id, &time);
    }
    return build_Value(env, len, type, val, &time);
}

JNIEXPORT jlong JNICALL Java_mapper_Signal_maps
  (JNIEnv *env, jobject obj, jlong jsig, jint dir)
{
    mapper_signal sig = (mapper_signal) ptr_jlong(jsig);
    return sig ? jlong_ptr(mapper_signal_get_maps(sig, dir)) : 0;
}

/**** mapper_Time.h ****/

JNIEXPORT void JNICALL Java_mapper_Time_mapperNow
  (JNIEnv *env, jobject obj)
{
    jclass cls = (*env)->GetObjectClass(env, obj);
    if (cls) {
        jfieldID sec = (*env)->GetFieldID(env, cls, "sec", "J");
        jfieldID frac = (*env)->GetFieldID(env, cls, "frac", "J");
        if (sec && frac) {
            mapper_time_t time;
            mapper_time_now(&time);
            (*env)->SetLongField(env, obj, sec, time.sec);
            (*env)->SetLongField(env, obj, frac, time.frac);
        }
    }
}
