
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <mapper/mapper.h>

#include "mapper_Database.h"
#include "mapper_device_Query.h"
#include "mapper_Device.h"
#include "mapper_map_Query.h"
#include "mapper_Map_Slot.h"
#include "mapper_Map.h"
#include "mapper_Network.h"
#include "mapper_Signal_Instance.h"
#include "mapper_signal_Query.h"
#include "mapper_Signal.h"

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

typedef struct {
    jobject linkListener;
    jobject mapListener;
} device_jni_context_t, *device_jni_context;

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

static void throwIllegalArgumentLength(JNIEnv *env, mapper_signal sig, int al)
{
    jclass newExcCls =
        (*env)->FindClass(env, "java/lang/IllegalArgumentException");
    if (newExcCls) {
        char msg[1024];
        snprintf(msg, 1024,
                 "Signal %s length is %d, but array argument has length %d.",
                 mapper_signal_name(sig), mapper_signal_length(sig), al);
        (*env)->ThrowNew(env, newExcCls, msg);
    }
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

static mapper_network get_network_from_jobject(JNIEnv *env, jobject obj)
{
    jclass cls = (*env)->GetObjectClass(env, obj);
    if (cls) {
        jfieldID val = (*env)->GetFieldID(env, cls, "_net", "J");
        if (val) {
            jlong s = (*env)->GetLongField(env, obj, val);
            return (mapper_network)ptr_jlong(s);
        }
    }
    return 0;
}

static mapper_device get_device_from_jobject(JNIEnv *env, jobject obj)
{
    // TODO check device here
    jclass cls = (*env)->GetObjectClass(env, obj);
    if (cls) {
        jfieldID val = (*env)->GetFieldID(env, cls, "_dev", "J");
        if (val) {
            jlong s = (*env)->GetLongField(env, obj, val);
            return (mapper_device)ptr_jlong(s);
        }
    }
    return 0;
}

static mapper_signal get_signal_from_jobject(JNIEnv *env, jobject obj)
{
    // TODO check signal here
    jclass cls = (*env)->GetObjectClass(env, obj);
    if (cls) {
        jfieldID val = (*env)->GetFieldID(env, cls, "_sig", "J");
        if (val) {
            jlong s = (*env)->GetLongField(env, obj, val);
            return (mapper_signal)ptr_jlong(s);
        }
    }
    throwIllegalArgumentSignal(env);
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

static mapper_link get_link_from_jobject(JNIEnv *env, jobject obj)
{
    jclass cls = (*env)->GetObjectClass(env, obj);
    if (cls) {
        jfieldID val = (*env)->GetFieldID(env, cls, "_link", "J");
        if (val) {
            jlong s = (*env)->GetLongField(env, obj, val);
            return (mapper_link)ptr_jlong(s);
        }
    }
    return 0;
}

static mapper_map get_map_from_jobject(JNIEnv *env, jobject obj)
{
    jclass cls = (*env)->GetObjectClass(env, obj);
    if (cls) {
        jfieldID val = (*env)->GetFieldID(env, cls, "_map", "J");
        if (val) {
            jlong s = (*env)->GetLongField(env, obj, val);
            return (mapper_map)ptr_jlong(s);
        }
    }
    return 0;
}

static mapper_slot get_slot_from_jobject(JNIEnv *env, jobject obj)
{
    jclass cls = (*env)->GetObjectClass(env, obj);
    if (cls) {
        jfieldID val = (*env)->GetFieldID(env, cls, "_slot", "J");
        if (val) {
            jlong s = (*env)->GetLongField(env, obj, val);
            return (mapper_slot)ptr_jlong(s);
        }
    }
    return 0;
}

static mapper_database get_database_from_jobject(JNIEnv *env, jobject obj)
{
    // TODO check database here
    jclass cls = (*env)->GetObjectClass(env, obj);
    if (cls) {
        jfieldID val = (*env)->GetFieldID(env, cls, "_db", "J");
        if (val) {
            jlong s = (*env)->GetLongField(env, obj, val);
            return (mapper_database)ptr_jlong(s);
        }
    }
    throwIllegalArgument(env, "Couldn't retrieve database pointer.");
    return 0;
}

static mapper_timetag_t *get_timetag_from_jobject(JNIEnv *env, jobject obj,
                                                  mapper_timetag_t *tt)
{
    if (!obj) return 0;
    jclass cls = (*env)->GetObjectClass(env, obj);
    if (cls) {
        jfieldID sec = (*env)->GetFieldID(env, cls, "sec", "J");
        jfieldID frac = (*env)->GetFieldID(env, cls, "frac", "J");
        if (sec && frac) {
            tt->sec = (*env)->GetLongField(env, obj, sec);
            tt->frac = (*env)->GetLongField(env, obj, frac);
            return tt;
        }
    }
    return 0;
}

static jobject get_jobject_from_timetag(JNIEnv *env, mapper_timetag_t *tt)
{
    jobject ttobj = 0;
    if (tt) {
        jclass cls = (*env)->FindClass(env, "mapper/TimeTag");
        if (cls) {
            jmethodID mid = (*env)->GetMethodID(env, cls, "<init>", "(JJ)V");
            if (mid) {
                ttobj = (*env)->NewObject(env, cls, mid, tt->sec, tt->frac);
            }
            else {
                printf("Error looking up TimeTag constructor.\n");
                exit(1);
            }
        }
    }
//    else
    // TODO: return MAPPER_NOW ?
    return ttobj;
}

static jobject get_jobject_from_database_record_action(JNIEnv *env,
                                                       mapper_record_action event)
{
    jobject obj = 0;
    jclass cls = (*env)->FindClass(env, "mapper/database/Event");
    if (cls) {
        jfieldID fid = (*env)->GetStaticFieldID(env, cls, event_strings[event],
                                                "Lmapper/database/Event;");
        if (fid) {
            obj = (*env)->GetStaticObjectField(env, cls, fid);
        }
        else {
            printf("Error looking up database/Event field '%s'.\n",
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

static jobject build_Property(JNIEnv *env, const char *name, jobject value)
{
    jclass cls = (*env)->FindClass(env, "mapper/Property");
    jmethodID methodID = (*env)->GetMethodID(env, cls, "<init>",
                                             "(Ljava/lang/String;Lmapper/Value;)V");
    if (methodID) {
        jobject s = (*env)->NewStringUTF(env, (char*)name);
        return (*env)->NewObject(env, cls, methodID, s, value);
    }
    return 0;
}

static jobject build_Value(JNIEnv *env, const int length, const char type,
                           const void *value, mapper_timetag_t *tt)
{
    jmethodID mid;
    jclass cls = (*env)->FindClass(env, "mapper/Value");
    if (!cls)
        return 0;
    jobject ret = 0;

    if (length <= 0 || !value) {
        // return empty Value
        mid = (*env)->GetMethodID(env, cls, "<init>", "()V");
        if (mid)
            ret = (*env)->NewObject(env, cls, mid);
    }
    else {
        switch (type) {
            case 'b':
            case 'i': {
                if (length == 1) {
                    mid = (*env)->GetMethodID(env, cls, "<init>", "(CI)V");
                    if (mid)
                        ret = (*env)->NewObject(env, cls, mid, type,
                                                *((int *)value));
                }
                else {
                    mid = (*env)->GetMethodID(env, cls, "<init>", "(C[I)V");
                    if (mid) {
                        jintArray arr = (*env)->NewIntArray(env, length);
                        (*env)->SetIntArrayRegion(env, arr, 0, length, value);
                        ret = (*env)->NewObject(env, cls, mid, type, arr);
                    }
                }
                break;
            }
            case 'f': {
                if (length == 1) {
                    mid = (*env)->GetMethodID(env, cls, "<init>", "(CF)V");
                    if (mid)
                        ret = (*env)->NewObject(env, cls, mid, type,
                                                *((float *)value));
                }
                else {
                    mid = (*env)->GetMethodID(env, cls, "<init>", "(C[F)V");
                    if (mid) {
                        jfloatArray arr = (*env)->NewFloatArray(env, length);
                        (*env)->SetFloatArrayRegion(env, arr, 0, length, value);
                        ret = (*env)->NewObject(env, cls, mid, type, arr);
                    }
                }
                break;
            }
            case 'd': {
                if (length == 1) {
                    mid = (*env)->GetMethodID(env, cls, "<init>", "(CD)V");
                    if (mid)
                        ret = (*env)->NewObject(env, cls, mid, type,
                                                *((double *)value));
                }
                else {
                    mid = (*env)->GetMethodID(env, cls, "<init>", "(C[D)V");
                    if (mid) {
                        jdoubleArray arr = (*env)->NewDoubleArray(env, length);
                        (*env)->SetDoubleArrayRegion(env, arr, 0, length, value);
                        ret = (*env)->NewObject(env, cls, mid, type, arr);
                    }
                }
                break;
            }
            case 's': {
                if (length == 1) {
                    mid = (*env)->GetMethodID(env, cls, "<init>",
                                              "(CLjava/lang/String;)V");
                    if (mid) {
                        jobject s = (*env)->NewStringUTF(env, (char *)value);
                        if (s)
                            ret = (*env)->NewObject(env, cls, mid, type, s);
                    }
                }
                else {
                    mid = (*env)->GetMethodID(env, cls, "<init>",
                                              "(C[Ljava/lang/String;)V");
                    if (mid) {
                        jobjectArray arr = (*env)->NewObjectArray(env, length,
                            (*env)->FindClass(env, "java/lang/String"),
                            (*env)->NewStringUTF(env, ""));
                        int i;
                        char **strings = (char**)value;
                        for (i = 0; i < length; i++) {
                            (*env)->SetObjectArrayElement(env, arr, i,
                                (*env)->NewStringUTF(env, strings[i]));
                        }
                        ret = (*env)->NewObject(env, cls, mid, type, arr);
                    }
                }
                break;
            }
            default:
                break;
    }
    }
    if (ret && tt) {
        jfieldID fid = (*env)->GetFieldID(env, cls, "timetag",
                                          "Lmapper/TimeTag;");
        if (!fid) {
            printf("error: couldn't find value.timetag fieldId\n");
            return ret;
        }
        jobject jtt = (*env)->GetObjectField(env, ret, fid);
        if (!jtt) {
            printf("error: couldn't find value.timetag\n");
            return ret;
        }
        cls = (*env)->GetObjectClass(env, jtt);
        if (!cls) {
            printf("error: could find timetag class.\n");
            return ret;
        }
        fid = (*env)->GetFieldID(env, cls, "sec", "J");
        if (fid)
            (*env)->SetLongField(env, jtt, fid, tt->sec);
        fid = (*env)->GetFieldID(env, cls, "frac", "J");
        if (fid)
            (*env)->SetLongField(env, jtt, fid, tt->frac);
    }
    return ret;
}

static int get_Value_elements(JNIEnv *env, jobject jprop, void **value,
                              char *type)
{
    jclass cls = (*env)->GetObjectClass(env, jprop);
    if (!cls)
        return 0;

    jfieldID typeid = (*env)->GetFieldID(env, cls, "type", "C");
    jfieldID lengthid = (*env)->GetFieldID(env, cls, "length", "I");
    if (!typeid || !lengthid)
        return 0;

    *type = (*env)->GetCharField(env, jprop, typeid);
    int length = (*env)->GetIntField(env, jprop, lengthid);
    if (!length)
        return 0;

    jfieldID valf = 0;
    jobject o = 0;

    switch (*type) {
        case 'b': {
            valf = (*env)->GetFieldID(env, cls, "_b", "[Z");
            o = (*env)->GetObjectField(env, jprop, valf);
            int *ints = malloc(sizeof(int) * length);
            jboolean jbool;
            for (int i = 0; i < length; i++) {
                jbool = (jboolean) (*env)->GetObjectArrayElement(env, o, i);
                ints[i] = jbool ? 1 : 0;
            }
            *value = ints;
            break;
        }
        case 'i':
            valf = (*env)->GetFieldID(env, cls, "_i", "[I");
            o = (*env)->GetObjectField(env, jprop, valf);
            *value = (*env)->GetIntArrayElements(env, o, NULL);
            break;
        case 'f':
            valf = (*env)->GetFieldID(env, cls, "_f", "[F");
            o = (*env)->GetObjectField(env, jprop, valf);
            *value = (*env)->GetFloatArrayElements(env, o, NULL);
            break;
        case 'd':
            valf = (*env)->GetFieldID(env, cls, "_d", "[D");
            o = (*env)->GetObjectField(env, jprop, valf);
            *value = (*env)->GetDoubleArrayElements(env, o, NULL);
            break;
        case 's':
        case 'S': {
            valf = (*env)->GetFieldID(env, cls, "_s", "[Ljava/lang/String;");
            o = (*env)->GetObjectField(env, jprop, valf);
            // need to unpack string array and rebuild
            if (length == 1) {
                jstring jstr = (jstring) (*env)->GetObjectArrayElement(env, o, 0);
                *value = (void*) (*env)->GetStringUTFChars(env, jstr, 0);
            }
            else {
                jstring jstrs[length];
                const char **cstrs = malloc(sizeof(char*) * length);
                int i;
                for (i = 0; i < length; i++) {
                    jstrs[i] = (jstring) (*env)->GetObjectArrayElement(env, o, i);
                    cstrs[i] = (*env)->GetStringUTFChars(env, jstrs[i], 0);
                }
                *value = cstrs;
            }
            break;
        }
        default:
            return 0;
    }
    return length;
}

static void release_Value_elements(JNIEnv *env, jobject jprop, void *value)
{
    jclass cls = (*env)->GetObjectClass(env, jprop);
    if (!cls)
        return;

    jfieldID typeid = (*env)->GetFieldID(env, cls, "type", "C");
    jfieldID lengthid = (*env)->GetFieldID(env, cls, "length", "I");
    if (!typeid || !lengthid)
        return;

    char type = (*env)->GetCharField(env, jprop, typeid);
    int length = (*env)->GetIntField(env, jprop, lengthid);
    if (!length)
        return;

    jfieldID valf = 0;
    jobject o = 0;

    switch (type) {
        case 'b': {
            int *ints = (int*)value;
            if (ints)
                free(ints);
            break;
        }
        case 'i':
            valf = (*env)->GetFieldID(env, cls, "_i", "[I");
            o = (*env)->GetObjectField(env, jprop, valf);
            (*env)->ReleaseIntArrayElements(env, o, value, JNI_ABORT);
            break;
        case 'f':
            valf = (*env)->GetFieldID(env, cls, "_f", "[F");
            o = (*env)->GetObjectField(env, jprop, valf);
            (*env)->ReleaseFloatArrayElements(env, o, value, JNI_ABORT);
            break;
        case 'd':
            valf = (*env)->GetFieldID(env, cls, "_d", "[D");
            o = (*env)->GetObjectField(env, jprop, valf);
            (*env)->ReleaseDoubleArrayElements(env, o, value, JNI_ABORT);
            break;
        case 's':
        case 'S': {
            valf = (*env)->GetFieldID(env, cls, "_s", "[Ljava/lang/String;");
            o = (*env)->GetObjectField(env, jprop, valf);

            jstring jstr;
            if (length == 1) {
                const char *cstring = (const char*)value;
                jstr = (jstring) (*env)->GetObjectArrayElement(env, o, 0);
                (*env)->ReleaseStringUTFChars(env, jstr, cstring);
            }
            else {
                const char **cstrings = (const char**)value;
                int i;
                for (i = 0; i < length; i++) {
                    jstr = (jstring) (*env)->GetObjectArrayElement(env, o, i);
                    (*env)->ReleaseStringUTFChars(env, jstr, cstrings[i]);
                }
                free(cstrings);
            }
            break;
        }
    }
}

static void java_signal_update_cb(mapper_signal sig, mapper_id instance,
                                  const void *v, int count, mapper_timetag_t *tt)
{
    if (bailing)
        return;
    char type = mapper_signal_type(sig);
    int length = mapper_signal_length(sig);

    jobject vobj = 0;
    if (type == 'f' && v) {
        jfloatArray varr = (*genv)->NewFloatArray(genv, length);
        if (varr)
            (*genv)->SetFloatArrayRegion(genv, varr, 0, length, v);
        vobj = (jobject) varr;
    }
    else if (type == 'i' && v) {
        jintArray varr = (*genv)->NewIntArray(genv, length);
        if (varr)
            (*genv)->SetIntArrayRegion(genv, varr, 0, length, v);
        vobj = (jobject) varr;
    }
    else if (type == 'd' && v) {
        jdoubleArray varr = (*genv)->NewDoubleArray(genv, length);
        if (varr)
            (*genv)->SetDoubleArrayRegion(genv, varr, 0, length, v);
        vobj = (jobject) varr;
    }

    if (!vobj && v) {
        char msg[1024];
        snprintf(msg, 1024,
                 "Unknown signal type for %s in callback handler (%c,%d).",
                 mapper_signal_name(sig), type, length);
        jclass newExcCls =
            (*genv)->FindClass(genv, "java/lang/IllegalArgumentException");
        if (newExcCls)
            (*genv)->ThrowNew(genv, newExcCls, msg);
        bailing = 1;
        return;
    }

    jobject ttobj = get_jobject_from_timetag(genv, tt);

    jobject update_cb;
    signal_jni_context ctx = (signal_jni_context)mapper_signal_user_data(sig);
    if (!ctx)
        return;

    if (ctx->instanceUpdateListener) {
        instance_jni_context ictx;
        ictx = ((instance_jni_context)
                mapper_signal_instance_user_data(sig, instance));
        if (!ictx)
            return;
        update_cb = ictx->listener;
        if (update_cb && ictx->instance) {
            jclass cls = (*genv)->GetObjectClass(genv, update_cb);
            if (cls) {
                jmethodID mid=0;
                if (type=='i') {
                    mid = (*genv)->GetMethodID(genv, cls, "onUpdate",
                                               "(Lmapper/Signal$Instance;"
                                               "[I"
                                               "Lmapper/TimeTag;)V");
                }
                else if (type=='f') {
                    mid = (*genv)->GetMethodID(genv, cls, "onUpdate",
                                               "(Lmapper/Signal$Instance;"
                                               "[F"
                                               "Lmapper/TimeTag;)V");
                }
                else if (type=='d') {
                    mid = (*genv)->GetMethodID(genv, cls, "onUpdate",
                                               "(Lmapper/Signal$Instance;"
                                               "[D"
                                               "Lmapper/TimeTag;)V");
                }
                if (mid) {
                    (*genv)->CallVoidMethod(genv, update_cb, mid,
                                            ictx->instance, vobj, ttobj);
                    if ((*genv)->ExceptionOccurred(genv))
                        bailing = 1;
                }
                else {
                    printf("Error looking up onUpdate method.\n");
                    exit(1);
                }
                if (vobj)
                    (*genv)->DeleteLocalRef(genv, vobj);
                if (ttobj)
                    (*genv)->DeleteLocalRef(genv, ttobj);
                return;
            }
        }

    }

    update_cb = ctx->listener;
    if (update_cb && ctx->signal) {
        jclass cls = (*genv)->GetObjectClass(genv, update_cb);
        if (cls) {
            jmethodID mid=0;
            if (type=='i') {
                mid = (*genv)->GetMethodID(genv, cls, "onUpdate",
                                           "(Lmapper/Signal;"
                                           "[I"
                                           "Lmapper/TimeTag;)V");
            }
            else if (type=='f') {
                mid = (*genv)->GetMethodID(genv, cls, "onUpdate",
                                           "(Lmapper/Signal;"
                                           "[F"
                                           "Lmapper/TimeTag;)V");
            }
            else if (type=='d') {
                mid = (*genv)->GetMethodID(genv, cls, "onUpdate",
                                           "(Lmapper/Signal;"
                                           "[D"
                                           "Lmapper/TimeTag;)V");
            }
            if (mid) {
                (*genv)->CallVoidMethod(genv, update_cb, mid, ctx->signal,
                                        vobj, ttobj);
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
    if (ttobj)
        (*genv)->DeleteLocalRef(genv, ttobj);
}

static void java_signal_instance_event_cb(mapper_signal sig, mapper_id instance,
                                          mapper_instance_event event,
                                          mapper_timetag_t *tt)
{
    if (bailing)
        return;

    jobject ttobj = get_jobject_from_timetag(genv, tt);

    signal_jni_context ctx = (signal_jni_context)mapper_signal_user_data(sig);
    if (!ctx->instanceEventListener || !ctx->signal)
        return;
    jclass cls = (*genv)->GetObjectClass(genv, ctx->instanceEventListener);
    if (!cls)
        return;
    instance_jni_context ictx;
    ictx = (instance_jni_context)mapper_signal_instance_user_data(sig, instance);
    if (!ictx || !ictx->instance)
        return;
    jmethodID mid;
    mid = (*genv)->GetMethodID(genv, cls, "onEvent", "(Lmapper/Signal$Instance;"
                               "Lmapper/signal/InstanceEvent;Lmapper/TimeTag;)V");
    if (mid) {
        jobject eventobj = get_jobject_from_instance_event(genv, event);
        (*genv)->CallVoidMethod(genv, ctx->instanceEventListener, mid,
                                ictx->instance, eventobj, ttobj);
        if ((*genv)->ExceptionOccurred(genv))
            bailing = 1;
    }
    else {
        printf("Error looking up onEvent method.\n");
        exit(1);
    }

    if (ttobj)
        (*genv)->DeleteLocalRef(genv, ttobj);
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
        mapper_signal_set_user_data(sig, ctx);
        ctx->signal = (*env)->NewGlobalRef(env, sigobj);
        ctx->listener = listener ? (*env)->NewGlobalRef(env, listener) : 0;
    }
    else {
        printf("Error creating signal wrapper class.\n");
        exit(1);
    }
    return sigobj;
}

/**** mapper_Database.h ****/

JNIEXPORT jlong JNICALL Java_mapper_Database_mapperDatabaseNew
  (JNIEnv *env, jobject obj, jint flags)
{
    return jlong_ptr(mapper_database_new(0, flags));
}

JNIEXPORT void JNICALL Java_mapper_Database_mapperDatabaseFree
  (JNIEnv *env, jobject obj, jlong jdb)
{
    mapper_database db = (mapper_database)ptr_jlong(jdb);
    mapper_database_free(db);
}

JNIEXPORT jlong JNICALL Java_mapper_Database_mapperDatabaseNetwork
  (JNIEnv *env, jobject obj, jlong jdb)
{
    mapper_database db = (mapper_database)ptr_jlong(jdb);
    mapper_network net = db ? mapper_database_network(db) : 0;
    return jlong_ptr(net);
}

JNIEXPORT jint JNICALL Java_mapper_Database_timeout
  (JNIEnv *env, jobject obj)
{
    mapper_database db = get_database_from_jobject(env, obj);
    return db ? mapper_database_timeout(db) : 0;
}

JNIEXPORT jobject JNICALL Java_mapper_Database_setTimeout
  (JNIEnv *env, jobject obj, jint timeout)
{
    mapper_database db = get_database_from_jobject(env, obj);
    if (db)
        mapper_database_set_timeout(db, timeout);
    return obj;
}

JNIEXPORT jobject JNICALL Java_mapper_Database_flush
  (JNIEnv *env, jobject obj)
{
    mapper_database db = get_database_from_jobject(env, obj);
    if (db)
        mapper_database_flush(db, mapper_database_timeout(db), 0);
    return obj;
}

JNIEXPORT jobject JNICALL Java_mapper_Database_poll
  (JNIEnv *env, jobject obj, jint block_ms)
{
    genv = env;
    bailing = 0;
    mapper_database db = get_database_from_jobject(env, obj);
    if (db)
        mapper_database_poll(db, block_ms);
    return obj;
}

JNIEXPORT void JNICALL Java_mapper_Database_mapperDatabaseSubscribe
  (JNIEnv *env, jobject obj, jlong jdb, jobject jdev, jint flags, jint lease)
{
    mapper_database db = (mapper_database)ptr_jlong(jdb);
    mapper_device dev = get_device_from_jobject(env, jdev);
    if (db && dev)
        mapper_database_subscribe(db, dev, flags, lease);
}

JNIEXPORT jobject JNICALL Java_mapper_Database_unsubscribe
  (JNIEnv *env, jobject obj, jobject jdev)
{
    mapper_database db = get_database_from_jobject(env, obj);
    mapper_device dev = get_device_from_jobject(env, jdev);
    if (db && dev)
        mapper_database_unsubscribe(db, dev);
    return obj;
}

JNIEXPORT jobject JNICALL Java_mapper_Database_requestDevices
  (JNIEnv *env, jobject obj)
{
    mapper_database db = get_database_from_jobject(env, obj);
    if (db)
        mapper_database_request_devices(db);
    return obj;
}

static void java_database_device_cb(mapper_database db, mapper_device dev,
                                    mapper_record_action action,
                                    const void *user_data)
{
    if (bailing || !user_data)
        return;

    // Create a wrapper class for the device
    jclass cls = (*genv)->FindClass(genv, "mapper/Device");
    if (!cls)
        return;
    jmethodID mid = (*genv)->GetMethodID(genv, cls, "<init>", "(J)V");
    jobject devobj;
    if (mid)
        devobj = (*genv)->NewObject(genv, cls, mid, jlong_ptr(dev));
    else {
        printf("Error looking up Device init method\n");
        return;
    }
    jobject eventobj = get_jobject_from_database_record_action(genv, action);
    if (!eventobj) {
        printf("Error looking up database event\n");
        return;
    }

    jobject obj = (jobject)user_data;
    cls = (*genv)->GetObjectClass(genv, obj);
    if (cls) {
        mid = (*genv)->GetMethodID(genv, cls, "onEvent",
                                   "(Lmapper/Device;Lmapper/database/Event;)V");
        if (mid) {
            (*genv)->CallVoidMethod(genv, obj, mid, devobj, eventobj);
            if ((*genv)->ExceptionOccurred(genv))
                bailing = 1;
        }
        else {
            printf("Error looking up onEvent method.\n");
        }
    }
}

JNIEXPORT void JNICALL Java_mapper_Database_mapperDatabaseAddDeviceCB
  (JNIEnv *env, jobject obj, jlong jdb, jobject listener)
{
    mapper_database db = (mapper_database)ptr_jlong(jdb);
    if (!db || !listener)
        return;
    jobject o = (*env)->NewGlobalRef(env, listener);
    mapper_database_add_device_callback(db, java_database_device_cb, o);
}

JNIEXPORT void JNICALL Java_mapper_Database_mapperDatabaseRemoveDeviceCB
  (JNIEnv *env, jobject obj, jlong jdb, jobject listener)
{
    mapper_database db = (mapper_database)ptr_jlong(jdb);
    if (!db || !listener)
        return;
    // TODO: fix mismatch in user_data
    mapper_database_remove_device_callback(db, java_database_device_cb,
                                           listener);
    (*env)->DeleteGlobalRef(env, listener);
}

static void java_database_link_cb(mapper_database db, mapper_link link,
                                  mapper_record_action action,
                                  const void *user_data)
{
    if (bailing || !user_data)
        return;

    // Create a wrapper class for the link
    jclass cls = (*genv)->FindClass(genv, "mapper/Link");
    if (!cls)
        return;
    jmethodID mid = (*genv)->GetMethodID(genv, cls, "<init>", "(J)V");
    jobject linkobj;
    if (mid)
        linkobj = (*genv)->NewObject(genv, cls, mid, jlong_ptr(link));
    else {
        printf("Error looking up Link init method\n");
        return;
    }
    jobject eventobj = get_jobject_from_database_record_action(genv, action);
    if (!eventobj) {
        printf("Error looking up database event\n");
        return;
    }

    jobject obj = (jobject)user_data;
    cls = (*genv)->GetObjectClass(genv, obj);
    if (cls) {
        mid = (*genv)->GetMethodID(genv, cls, "onEvent",
                                   "(Lmapper/Link;Lmapper/database/Event;)V");
        if (mid) {
            (*genv)->CallVoidMethod(genv, obj, mid, linkobj, eventobj);
            if ((*genv)->ExceptionOccurred(genv))
                bailing = 1;
        }
        else {
            printf("Error looking up onEvent method.\n");
        }
    }
}

JNIEXPORT void JNICALL Java_mapper_Database_mapperDatabaseAddLinkCB
(JNIEnv *env, jobject obj, jlong jdb, jobject listener)
{
    mapper_database db = (mapper_database)ptr_jlong(jdb);
    if (!db || !listener)
        return;
    jobject o = (*env)->NewGlobalRef(env, listener);
    mapper_database_add_link_callback(db, java_database_link_cb, o);
}

JNIEXPORT void JNICALL Java_mapper_Database_mapperDatabaseRemoveLinkCB
(JNIEnv *env, jobject obj, jlong jdb, jobject listener)
{
    mapper_database db = (mapper_database)ptr_jlong(jdb);
    if (!db || !listener)
        return;
    // TODO: fix mismatch in user_data
    mapper_database_remove_link_callback(db, java_database_link_cb, listener);
    (*env)->DeleteGlobalRef(env, listener);
}

static void java_database_signal_cb(mapper_database db, mapper_signal sig,
                                    mapper_record_action action,
                                    const void *user_data)
{
    if (bailing || !user_data)
        return;

    // Create a wrapper class for the signal
    jclass cls = (*genv)->FindClass(genv, "mapper/Signal");
    if (!cls)
    return;
    jmethodID mid = (*genv)->GetMethodID(genv, cls, "<init>", "(J)V");
    jobject sigobj = (*genv)->NewObject(genv, cls, mid, jlong_ptr(sig));
    jobject eventobj = get_jobject_from_database_record_action(genv, action);

    jobject obj = (jobject)user_data;
    cls = (*genv)->GetObjectClass(genv, obj);
    if (cls) {
        mid = (*genv)->GetMethodID(genv, cls, "onEvent",
                                   "(Lmapper/Signal;Lmapper/database/Event;)V");
        if (mid) {
            (*genv)->CallVoidMethod(genv, obj, mid, sigobj, eventobj);
            if ((*genv)->ExceptionOccurred(genv))
                bailing = 1;
        }
        else {
            printf("Error looking up onEvent method.\n");
        }
    }
}

JNIEXPORT void JNICALL Java_mapper_Database_mapperDatabaseAddSignalCB
  (JNIEnv *env, jobject obj, jlong jdb, jobject listener)
{
    mapper_database db = (mapper_database)ptr_jlong(jdb);
    if (!db || !listener)
        return;
    jobject o = (*env)->NewGlobalRef(env, listener);
    mapper_database_add_signal_callback(db, java_database_signal_cb, o);
}

JNIEXPORT void JNICALL Java_mapper_Database_mapperDatabaseRemoveSignalCB
  (JNIEnv *env, jobject obj, jlong jdb, jobject listener)
{
    mapper_database db = (mapper_database)ptr_jlong(jdb);
    if (!db || !listener)
        return;
    // TODO: fix mismatch in user_data
    mapper_database_remove_signal_callback(db, java_database_signal_cb,
                                           listener);
    (*env)->DeleteGlobalRef(env, listener);
}

static void java_database_map_cb(mapper_database db, mapper_map map,
                                 mapper_record_action action,
                                 const void *user_data)
{
    if (bailing || !user_data)
        return;

    // Create a wrapper class for the map
    jclass cls = (*genv)->FindClass(genv, "mapper/Map");
    if (!cls)
        return;
    jmethodID mid = (*genv)->GetMethodID(genv, cls, "<init>", "(J)V");
    jobject mapobj = (*genv)->NewObject(genv, cls, mid, jlong_ptr(map));
    jobject eventobj = get_jobject_from_database_record_action(genv, action);

    jobject obj = (jobject)user_data;
    cls = (*genv)->GetObjectClass(genv, obj);
    if (cls) {
        mid = (*genv)->GetMethodID(genv, cls, "onEvent",
                                   "(Lmapper/Map;Lmapper/database/Event;)V");
        if (mid) {
            (*genv)->CallVoidMethod(genv, obj, mid, mapobj, eventobj);
            if ((*genv)->ExceptionOccurred(genv))
                bailing = 1;
        }
        else {
            printf("Error looking up onEvent method.\n");
        }
    }
}

JNIEXPORT void JNICALL Java_mapper_Database_mapperDatabaseAddMapCB
  (JNIEnv *env, jobject obj, jlong jdb, jobject listener)
{
    mapper_database db = (mapper_database)ptr_jlong(jdb);
    if (!db || !listener)
        return;
    jobject o = (*env)->NewGlobalRef(env, listener);
    mapper_database_add_map_callback(db, java_database_map_cb, o);
}

JNIEXPORT void JNICALL Java_mapper_Database_mapperDatabaseRemoveMapCB
  (JNIEnv *env, jobject obj, jlong jdb, jobject listener)
{
    mapper_database db = (mapper_database)ptr_jlong(jdb);
    if (!db || !listener)
        return;
    // TODO: fix mismatch in user_data
    mapper_database_remove_map_callback(db, java_database_map_cb, listener);
    (*env)->DeleteGlobalRef(env, listener);
}

JNIEXPORT jint JNICALL Java_mapper_Database_numDevices
  (JNIEnv *env, jobject obj)
{
    mapper_database db = get_database_from_jobject(env, obj);
    return db ? mapper_database_num_devices(db) : 0;
}

JNIEXPORT jobject JNICALL Java_mapper_Database_device__Ljava_lang_String_2
  (JNIEnv *env, jobject obj, jstring s)
{
    jclass cls = (*env)->FindClass(env, "mapper/Device");
    if (!cls)
        return 0;

    mapper_device dev = 0;
    mapper_database db = get_database_from_jobject(env, obj);
    if (db) {
        const char *name = (*env)->GetStringUTFChars(env, s, 0);
        dev = mapper_database_device_by_name(db, name);
        (*env)->ReleaseStringUTFChars(env, s, name);
    }

    jmethodID mid = (*env)->GetMethodID(env, cls, "<init>", "(J)V");
    return (*env)->NewObject(env, cls, mid, jlong_ptr(dev));
}

JNIEXPORT jobject JNICALL Java_mapper_Database_device__J
  (JNIEnv *env, jobject obj, jlong jid)
{
    jclass cls = (*env)->FindClass(env, "mapper/Device");
    if (!cls)
        return 0;

    mapper_device dev = 0;
    mapper_database db = get_database_from_jobject(env, obj);
    if (db)
        dev = mapper_database_device_by_id(db, jid);

    jmethodID mid = (*env)->GetMethodID(env, cls, "<init>", "(J)V");
    return (*env)->NewObject(env, cls, mid, jlong_ptr(dev));
}

JNIEXPORT jobject JNICALL Java_mapper_Database_devices__
  (JNIEnv *env, jobject obj)
{
    jclass cls = (*env)->FindClass(env, "mapper/device/Query");
    if (!cls)
        return 0;

    mapper_device *devs = 0;
    mapper_database db = get_database_from_jobject(env, obj);
    if (db)
        devs = mapper_database_devices(db);

    jmethodID mid = (*env)->GetMethodID(env, cls, "<init>", "(J)V");
    return (*env)->NewObject(env, cls, mid, jlong_ptr(devs));
}

JNIEXPORT jobject JNICALL Java_mapper_Database_devices__Ljava_lang_String_2
  (JNIEnv *env, jobject obj, jstring s)
{
    jclass cls = (*env)->FindClass(env, "mapper/device/Query");
    if (!cls)
        return 0;

    mapper_device *devs = 0;
    mapper_database db = get_database_from_jobject(env, obj);
    if (db) {
        const char *name = (*env)->GetStringUTFChars(env, s, 0);
        devs = mapper_database_devices_by_name(db, name);
        (*env)->ReleaseStringUTFChars(env, s, name);
    }

    jmethodID mid = (*env)->GetMethodID(env, cls, "<init>", "(J)V");
    return (*env)->NewObject(env, cls, mid, jlong_ptr(devs));
}

JNIEXPORT jlong JNICALL Java_mapper_Database_mapperDatabaseDevicesByProp
  (JNIEnv *env, jobject obj, jlong jdb, jstring key, jobject jprop, jint op)
{
    if (!key)
        return 0;

    mapper_database db = (mapper_database) ptr_jlong(jdb);

    const char *ckey = (*env)->GetStringUTFChars(env, key, 0);
    char type;
    void *value;
    int length = get_Value_elements(env, jprop, &value, &type);

    return jlong_ptr(mapper_database_devices_by_property(db, ckey, length, type,
                                                         value, op));
}

JNIEXPORT jint JNICALL Java_mapper_Database_numLinks
  (JNIEnv *env, jobject obj)
{
    mapper_database db = get_database_from_jobject(env, obj);
    return db ? mapper_database_num_links(db) : 0;
}

JNIEXPORT jobject JNICALL Java_mapper_Database_link
  (JNIEnv *env, jobject obj, jlong jid)
{
    jclass cls = (*env)->FindClass(env, "mapper/Link");
    if (!cls)
        return 0;

    mapper_link link = 0;
    mapper_database db = get_database_from_jobject(env, obj);
    if (db)
        link = mapper_database_link_by_id(db, jid);

    jmethodID mid = (*env)->GetMethodID(env, cls, "<init>", "(J)V");
    return (*env)->NewObject(env, cls, mid, jlong_ptr(link));
}

JNIEXPORT jobject JNICALL Java_mapper_Database_links
  (JNIEnv *env, jobject obj)
{
    jclass cls = (*env)->FindClass(env, "mapper/link/Query");
    if (!cls)
        return 0;

    mapper_link *links = 0;
    mapper_database db = get_database_from_jobject(env, obj);
    if (db)
        links = mapper_database_links(db);

    jmethodID mid = (*env)->GetMethodID(env, cls, "<init>", "(J)V");
    return (*env)->NewObject(env, cls, mid, jlong_ptr(links));
}

JNIEXPORT jlong JNICALL Java_mapper_Database_mapperDatabaseLinksByProp
  (JNIEnv *env, jobject obj, jlong jdb, jstring key, jobject jprop, jint op)
{
    if (!key)
        return 0;

    mapper_database db = (mapper_database) ptr_jlong(jdb);

    const char *ckey = (*env)->GetStringUTFChars(env, key, 0);
    char type;
    void *value;
    int length = get_Value_elements(env, jprop, &value, &type);

    return jlong_ptr(mapper_database_links_by_property(db, ckey, length, type,
                                                       value, op));
}

JNIEXPORT jint JNICALL Java_mapper_Database_numSignals
  (JNIEnv *env, jobject obj, jint dir)
{
    mapper_database db = get_database_from_jobject(env, obj);
    return db ? mapper_database_num_signals(db, dir) : 0;
}

JNIEXPORT jobject JNICALL Java_mapper_Database_signal
  (JNIEnv *env, jobject obj, jlong jid)
{
    jclass cls = (*env)->FindClass(env, "mapper/Signal");
    if (!cls)
        return 0;

    mapper_signal sig = 0;
    mapper_database db = get_database_from_jobject(env, obj);
    if (db)
        sig = mapper_database_signal_by_id(db, jid);

    jmethodID mid = (*env)->GetMethodID(env, cls, "<init>", "(J)V");
    return (*env)->NewObject(env, cls, mid, jlong_ptr(sig));
}

JNIEXPORT jlong JNICALL Java_mapper_Database_mapperDatabaseSignals
  (JNIEnv *env, jobject obj, jlong jdb, jstring s, jint jdir)
{
    mapper_database db = (mapper_database)ptr_jlong(jdb);
    if (!db)
        return 0;

    mapper_signal *sigs = 0;
    if (s) {
        const char *name = (*env)->GetStringUTFChars(env, s, 0);
        sigs = mapper_database_signals_by_name(db, name);
        (*env)->ReleaseStringUTFChars(env, s, name);
    }
    else {
        sigs = mapper_database_signals(db, jdir);
    }
    return jlong_ptr(sigs);
}

JNIEXPORT jlong JNICALL Java_mapper_Database_mapperDatabaseSignalsByProp
  (JNIEnv *env, jobject obj, jlong jdb, jstring key, jobject jprop, jint op)
{
    if (!key)
        return 0;

    mapper_database db = (mapper_database) ptr_jlong(jdb);

    const char *ckey = (*env)->GetStringUTFChars(env, key, 0);
    char type;
    void *value;
    int length = get_Value_elements(env, jprop, &value, &type);

    return jlong_ptr(mapper_database_signals_by_property(db, ckey, length, type,
                                                         value, op));
}

JNIEXPORT jint JNICALL Java_mapper_Database_numMaps
  (JNIEnv *env, jobject obj)
{
    mapper_database db = get_database_from_jobject(env, obj);
    return db ? mapper_database_num_maps(db) : 0;
}

JNIEXPORT jobject JNICALL Java_mapper_Database_map
  (JNIEnv *env, jobject obj, jlong jid)
{
    jclass cls = (*env)->FindClass(env, "mapper/Map");
    if (!cls)
        return 0;

    mapper_map map = 0;
    mapper_database db = get_database_from_jobject(env, obj);
    if (db)
        map = mapper_database_map_by_id(db, jid);

    jmethodID mid = (*env)->GetMethodID(env, cls, "<init>", "(J)V");
    return (*env)->NewObject(env, cls, mid, jlong_ptr(map));
}

JNIEXPORT jobject JNICALL Java_mapper_Database_maps
  (JNIEnv *env, jobject obj)
{
    jclass cls = (*env)->FindClass(env, "mapper/map/Query");
    if (!cls)
        return 0;

    mapper_map *maps = 0;
    mapper_database db = get_database_from_jobject(env, obj);
    if (db)
        maps = mapper_database_maps(db);

    jmethodID mid = (*env)->GetMethodID(env, cls, "<init>", "(J)V");
    return (*env)->NewObject(env, cls, mid, jlong_ptr(maps));
}

JNIEXPORT jlong JNICALL Java_mapper_Database_mapperDatabaseMapsByProp
  (JNIEnv *env, jobject obj, jlong jdb, jstring key, jobject jprop, jint op)
{
    if (!key)
        return 0;

    mapper_database db = (mapper_database) ptr_jlong(jdb);

    const char *ckey = (*env)->GetStringUTFChars(env, key, 0);
    char type;
    void *value;
    int length = get_Value_elements(env, jprop, &value, &type);

    return jlong_ptr(mapper_database_maps_by_property(db, ckey, length, type,
                                                      value, op));
}

JNIEXPORT jlong JNICALL Java_mapper_Database_mapperDatabaseMapsBySlotProp
  (JNIEnv *env, jobject obj, jlong jdb, jstring key, jobject jprop, jint op)
{
    if (!key)
        return 0;

    mapper_database db = (mapper_database) ptr_jlong(jdb);

    const char *ckey = (*env)->GetStringUTFChars(env, key, 0);
    char type;
    void *value;
    int length = get_Value_elements(env, jprop, &value, &type);

    return jlong_ptr(mapper_database_maps_by_slot_property(db, ckey, length,
                                                           type, value, op));
}

/**** mapper_device_Query.h ****/

JNIEXPORT jlong JNICALL Java_mapper_device_Query_mapperDeviceDeref
  (JNIEnv *env, jobject obj, jlong jdev)
{
    void **ptr = (void**)ptr_jlong(jdev);
    return jlong_ptr(*ptr);
}

JNIEXPORT jlong JNICALL Java_mapper_device_Query_mapperDeviceQueryCopy
  (JNIEnv *env, jobject obj, jlong query)
{
    mapper_device *devs = (mapper_device*) ptr_jlong(query);
    if (!devs)
        return 0;
    return jlong_ptr(mapper_device_query_copy(devs));
}

JNIEXPORT jlong JNICALL Java_mapper_device_Query_mapperDeviceQueryJoin
  (JNIEnv *env, jobject obj, jlong query_lhs, jlong query_rhs)
{
    mapper_device *devs_lhs = (mapper_device*) ptr_jlong(query_lhs);
    mapper_device *devs_rhs = (mapper_device*) ptr_jlong(query_rhs);

    if (!devs_rhs)
        return query_lhs;

    // use a copy of rhs
    mapper_device *devs_rhs_cpy = mapper_device_query_copy(devs_rhs);
    return jlong_ptr(mapper_device_query_union(devs_lhs, devs_rhs_cpy));
}

JNIEXPORT jlong JNICALL Java_mapper_device_Query_mapperDeviceQueryIntersect
  (JNIEnv *env, jobject obj, jlong query_lhs, jlong query_rhs)
{
    mapper_device *devs_lhs = (mapper_device*) ptr_jlong(query_lhs);
    mapper_device *devs_rhs = (mapper_device*) ptr_jlong(query_rhs);

    if (!devs_lhs || !devs_rhs)
        return 0;

    // use a copy of rhs
    mapper_device *devs_rhs_cpy = mapper_device_query_copy(devs_rhs);
    return jlong_ptr(mapper_device_query_intersection(devs_lhs, devs_rhs_cpy));
}

JNIEXPORT jlong JNICALL Java_mapper_device_Query_mapperDeviceQuerySubtract
  (JNIEnv *env, jobject obj, jlong query_lhs, jlong query_rhs)
{
    mapper_device *devs_lhs = (mapper_device*) ptr_jlong(query_lhs);
    mapper_device *devs_rhs = (mapper_device*) ptr_jlong(query_rhs);

    if (!devs_lhs || !devs_rhs)
        return query_lhs;

    // use a copy of rhs
    mapper_device *devs_rhs_cpy = mapper_device_query_copy(devs_rhs);
    return jlong_ptr(mapper_device_query_difference(devs_lhs, devs_rhs_cpy));
}

JNIEXPORT jlong JNICALL Java_mapper_device_Query_mapperDeviceQueryNext
  (JNIEnv *env, jobject obj, jlong query)
{
    mapper_device *devs = (mapper_device*) ptr_jlong(query);
    return devs ? jlong_ptr(mapper_device_query_next(devs)) : 0;
}

JNIEXPORT void JNICALL Java_mapper_device_Query_mapperDeviceQueryDone
  (JNIEnv *env, jobject obj, jlong query)
{
    mapper_device *devs = (mapper_device*) ptr_jlong(query);
    if (devs)
        mapper_device_query_done(devs);
}

/**** mapper_Device.h ****/

JNIEXPORT jlong JNICALL Java_mapper_Device_mapperDeviceNew
  (JNIEnv *env, jobject obj, jstring name, jint port)
{
    const char *cname = (*env)->GetStringUTFChars(env, name, 0);
    mapper_device dev = mapper_device_new(cname, port, 0);
    (*env)->ReleaseStringUTFChars(env, name, cname);
    device_jni_context ctx = ((device_jni_context)
                              calloc(1, sizeof(device_jni_context_t)));
    mapper_device_set_user_data(dev, ctx);
    return jlong_ptr(dev);
}

JNIEXPORT void JNICALL Java_mapper_Device_mapperDeviceFree
  (JNIEnv *env, jobject obj, jlong jdev)
{
    mapper_device dev = (mapper_device)ptr_jlong(jdev);
    if (!dev || !mapper_device_is_local(dev))
        return;
    /* Free all references to Java objects. */
    mapper_signal *sigs = mapper_device_signals(dev, MAPPER_DIR_ANY);
    while (sigs) {
        mapper_signal temp = *sigs;
        sigs = mapper_signal_query_next(sigs);

        // do not call instance event callback
        mapper_signal_set_instance_event_callback(temp, 0, 0);

        // check if we have active instances
        int i;
        for (i = 0; i < mapper_signal_num_active_instances(temp); i++) {
            mapper_id id = mapper_signal_instance_id(temp, i);
            instance_jni_context ictx;
            ictx = mapper_signal_instance_user_data(temp, id);
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

        signal_jni_context ctx = (signal_jni_context)mapper_signal_user_data(temp);
        if (ctx->signal)
            (*env)->DeleteGlobalRef(env, ctx->signal);
        if (ctx->listener)
            (*env)->DeleteGlobalRef(env, ctx->listener);
        if (ctx->instanceEventListener)
            (*env)->DeleteGlobalRef(env, ctx->instanceEventListener);
        free(ctx);
    }
    device_jni_context ctx = (device_jni_context)mapper_device_user_data(dev);
    if (ctx)
        free(ctx);
    mapper_device_free(dev);
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
  (JNIEnv *env, jobject obj, jint dir, jstring name, jint length, jchar type,
   jstring unit, jobject minimum, jobject maximum, jobject listener)
{
    if (!name || (length<=0) || (type!='f' && type!='i' && type!='d'))
        return 0;

    mapper_device dev = get_device_from_jobject(env, obj);
    if (!dev || !mapper_device_is_local(dev))
        return 0;
    const char *cname = (*env)->GetStringUTFChars(env, name, 0);
    const char *cunit = unit ? (*env)->GetStringUTFChars(env, unit, 0) : 0;

    signal_jni_context ctx = ((signal_jni_context)
                              calloc(1, sizeof(signal_jni_context_t)));
    if (!ctx) {
        throwOutOfMemory(env);
        return 0;
    }

    mapper_signal sig = mapper_device_add_signal(dev, dir, cname, length, type,
                                                 cunit, 0, 0, listener
                                                 ? java_signal_update_cb : 0,
                                                 ctx);

    (*env)->ReleaseStringUTFChars(env, name, cname);
    if (unit)
        (*env)->ReleaseStringUTFChars(env, unit, cunit);

    jobject sigobj = create_signal_object(env, obj, ctx, listener, sig);

    if (sigobj) {
        if (minimum) {
            jstring minstring = (*env)->NewStringUTF(env, "min");
            Java_mapper_Signal_setProperty(env, sigobj, minstring, minimum);
        }
        if (maximum) {
            jstring maxstring = (*env)->NewStringUTF(env, "max");
            Java_mapper_Signal_setProperty(env, sigobj, maxstring, maximum);
        }
    }
    return sigobj;
}

JNIEXPORT void JNICALL Java_mapper_Device_mapperDeviceRemoveSignal
  (JNIEnv *env, jobject obj, jlong jdev, jobject jsig)
{
    mapper_device dev = (mapper_device) ptr_jlong(jdev);
    if (!dev || !mapper_device_is_local(dev))
        return;
    mapper_signal sig = get_signal_from_jobject(env, jsig);
    if (sig) {
        // do not call instance event callback
        mapper_signal_set_instance_event_callback(sig, 0, 0);

        // check if we have active instances
        while (mapper_signal_num_active_instances(sig)) {
            mapper_id id = mapper_signal_active_instance_id(sig, 0);
            if (!id)
                continue;
            instance_jni_context ictx;
            ictx = mapper_signal_instance_user_data(sig, id);
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

        signal_jni_context ctx = ((signal_jni_context)
                                  mapper_signal_user_data(sig));
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

JNIEXPORT jlong JNICALL Java_mapper_Device_mapperDeviceNetwork
  (JNIEnv *env, jobject obj, jlong jdev)
{
    mapper_device dev = (mapper_device)ptr_jlong(jdev);
    mapper_network net = dev ? mapper_device_network(dev) : 0;
    return jlong_ptr(net);
}

JNIEXPORT jint JNICALL Java_mapper_Device_numProperties
  (JNIEnv *env, jobject obj)
{
    mapper_device dev = get_device_from_jobject(env, obj);
    return dev ? mapper_device_num_properties(dev) : 0;
}

JNIEXPORT jobject JNICALL Java_mapper_Device_property__Ljava_lang_String_2
  (JNIEnv *env, jobject obj, jstring key)
{
    mapper_device dev = get_device_from_jobject(env, obj);
    const char *ckey = (*env)->GetStringUTFChars(env, key, 0);
    char type;
    int length;
    const void *value;
    jobject o = 0;

    if (!mapper_device_property(dev, ckey, &length, &type, &value))
        o = build_Value(env, length, type, value, 0);

    (*env)->ReleaseStringUTFChars(env, key, ckey);
    return o;
}

JNIEXPORT jobject JNICALL Java_mapper_Device_property__I
  (JNIEnv *env, jobject obj, jint index)
{
    mapper_device dev = get_device_from_jobject(env, obj);
    const char *key = 0;
    char type;
    int length;
    const void *value;
    jobject val = 0, prop = 0;

    if (mapper_device_property_index(dev, index, &key, &length, &type, &value))
        return 0;

    val = build_Value(env, length, type, value, 0);
    prop = build_Property(env, key, val);

    return prop;
}

JNIEXPORT jobject JNICALL Java_mapper_Device_setProperty
  (JNIEnv *env, jobject obj, jstring key, jobject jprop)
{
    mapper_device dev = get_device_from_jobject(env, obj);
    const char *ckey = (*env)->GetStringUTFChars(env, key, 0);
    char type;
    void *value;
    int length = get_Value_elements(env, jprop, &value, &type);
    if (length) {
        mapper_device_set_property(dev, ckey, length, type, value);
        release_Value_elements(env, jprop, value);
    }
    return obj;
}

JNIEXPORT jobject JNICALL Java_mapper_Device_removeProperty
  (JNIEnv *env, jobject obj, jstring key)
{
    mapper_device dev = get_device_from_jobject(env, obj);
    if (dev) {
        const char *ckey = (*env)->GetStringUTFChars(env, key, 0);
        mapper_device_remove_property(dev, ckey);
        (*env)->ReleaseStringUTFChars(env, key, ckey);
    }
    return obj;
}

JNIEXPORT jstring JNICALL Java_mapper_Device_host
  (JNIEnv *env, jobject obj)
{
    mapper_device dev = get_device_from_jobject(env, obj);
    return dev ? (*env)->NewStringUTF(env, mapper_device_host(dev)) : 0;
}

JNIEXPORT jlong JNICALL Java_mapper_Device_id
  (JNIEnv *env, jobject obj)
{
    mapper_device dev = get_device_from_jobject(env, obj);
    return dev ? mapper_device_id(dev) : 0;
}

JNIEXPORT jboolean JNICALL Java_mapper_Device_isLocal
  (JNIEnv *env, jobject obj)
{
    mapper_device dev = get_device_from_jobject(env, obj);
    return dev ? mapper_device_is_local(dev) : 0;
}

JNIEXPORT jstring JNICALL Java_mapper_Device_name
  (JNIEnv *env, jobject obj)
{
    mapper_device dev = get_device_from_jobject(env, obj);
    return dev ? (*env)->NewStringUTF(env, mapper_device_name(dev)) : 0;
}

JNIEXPORT jint JNICALL Java_mapper_Device_numSignals
  (JNIEnv *env, jobject obj, jint dir)
{
    mapper_device dev = get_device_from_jobject(env, obj);
    return dev ? mapper_device_num_signals(dev, dir) : 0;
}

JNIEXPORT jint JNICALL Java_mapper_Device_numMaps
  (JNIEnv *env, jobject obj, jint dir)
{
    mapper_device dev = get_device_from_jobject(env, obj);
    return dev ? mapper_device_num_maps(dev, dir) : 0;
}

JNIEXPORT jint JNICALL Java_mapper_Device_numLinks
  (JNIEnv *env, jobject obj, jint dir)
{
    mapper_device dev = get_device_from_jobject(env, obj);
    return dev ? mapper_device_num_links(dev, dir) : 0;
}

JNIEXPORT jint JNICALL Java_mapper_Device_ordinal
  (JNIEnv *env, jobject obj)
{
    mapper_device dev = get_device_from_jobject(env, obj);
    return dev ? mapper_device_ordinal(dev) : 0;
}

JNIEXPORT jint JNICALL Java_mapper_Device_port
  (JNIEnv *env, jobject obj)
{
    mapper_device dev = get_device_from_jobject(env, obj);
    return dev ? mapper_device_port(dev) : 0;
}

JNIEXPORT jboolean JNICALL Java_mapper_Device_ready
  (JNIEnv *env, jobject obj)
{
    mapper_device dev = get_device_from_jobject(env, obj);
    return dev ? mapper_device_ready(dev) : 0;
}

JNIEXPORT jobject JNICALL Java_mapper_Device_synced
  (JNIEnv *env, jobject obj)
{
    mapper_device dev = get_device_from_jobject(env, obj);
    if (!dev)
        return 0;
    mapper_timetag_t tt;
    mapper_device_synced(dev, &tt);
    jobject o = get_jobject_from_timetag(env, &tt);
    return o;
}

JNIEXPORT jint JNICALL Java_mapper_Device_version
  (JNIEnv *env, jobject obj)
{
    mapper_device dev = get_device_from_jobject(env, obj);
    return dev ? mapper_device_version(dev) : 0;
}

JNIEXPORT jobject JNICALL Java_mapper_Device_startQueue
  (JNIEnv *env, jobject obj, jobject ttobj)
{
    mapper_device dev = get_device_from_jobject(env, obj);
    if (!dev || !mapper_device_is_local(dev))
        return 0;
    mapper_timetag_t tt, *ptt = 0;
    ptt = get_timetag_from_jobject(env, ttobj, &tt);
    if (dev && ptt)
        mapper_device_start_queue(dev, *ptt);
    return obj;
}

JNIEXPORT jobject JNICALL Java_mapper_Device_sendQueue
  (JNIEnv *env, jobject obj, jobject ttobj)
{
    mapper_device dev = get_device_from_jobject(env, obj);
    if (!dev || !mapper_device_is_local(dev))
        return 0;
    mapper_timetag_t tt, *ptt = 0;
    ptt = get_timetag_from_jobject(env, ttobj, &tt);
    if (dev && ptt)
        mapper_device_send_queue(dev, *ptt);
    return obj;
}

JNIEXPORT jobject JNICALL Java_mapper_Device_now
  (JNIEnv *env, jobject obj)
{
    mapper_device dev = get_device_from_jobject(env, obj);
    mapper_timetag_t tt;
    mapper_device_now(dev, &tt);
    jobject o = get_jobject_from_timetag(env, &tt);
    return o;
}

static void java_device_link_cb(mapper_device dev, mapper_link link,
                                mapper_record_action action)
{
    if (bailing)
        return;

    // Create a wrapper class for the link
    jclass cls = (*genv)->FindClass(genv, "mapper/Link");
    if (!cls)
        return;
    jmethodID mid = (*genv)->GetMethodID(genv, cls, "<init>", "(J)V");
    jobject linkobj;
    if (mid)
        linkobj = (*genv)->NewObject(genv, cls, mid, jlong_ptr(link));
    else {
        printf("Error looking up Link init method\n");
        return;
    }
    jobject eventobj = get_jobject_from_database_record_action(genv, action);
    if (!eventobj) {
        printf("Error looking up database event\n");
        return;
    }

    device_jni_context ctx = (device_jni_context)mapper_device_user_data(dev);
    if (!ctx || !ctx->linkListener)
        return;
    cls = (*genv)->GetObjectClass(genv, ctx->linkListener);
    if (cls) {
        mid = (*genv)->GetMethodID(genv, cls, "onEvent",
                                   "(Lmapper/Link;Lmapper/database/Event;)V");
        if (mid) {
            (*genv)->CallVoidMethod(genv, ctx->linkListener, mid, linkobj,
                                    eventobj);
            if ((*genv)->ExceptionOccurred(genv))
                bailing = 1;
        }
        else {
            printf("Error looking up onEvent method.\n");
        }
    }
}

JNIEXPORT void JNICALL Java_mapper_Device_mapperDeviceSetLinkCB
  (JNIEnv *env, jobject obj, jlong jdev, jobject listener)
{
    mapper_device dev = (mapper_device)ptr_jlong(jdev);
    if (!dev || !listener)
        return;
    device_jni_context ctx = (device_jni_context)mapper_device_user_data(dev);
    if (!ctx)
        return;
    if (ctx->linkListener)
        (*env)->DeleteGlobalRef(env, ctx->linkListener);
    ctx->linkListener = listener ? (*env)->NewGlobalRef(env, listener) : 0;
    mapper_device_set_link_callback(dev, java_device_link_cb);
}

static void java_device_map_cb(mapper_device dev, mapper_map map,
                               mapper_record_action action)
{
    if (bailing)
        return;

    // Create a wrapper class for the map
    jclass cls = (*genv)->FindClass(genv, "mapper/Map");
    if (!cls)
        return;
    jmethodID mid = (*genv)->GetMethodID(genv, cls, "<init>", "(J)V");
    jobject mapobj;
    if (mid)
        mapobj = (*genv)->NewObject(genv, cls, mid, jlong_ptr(map));
    else {
        printf("Error looking up Map init method\n");
        return;
    }
    jobject eventobj = get_jobject_from_database_record_action(genv, action);
    if (!eventobj) {
        printf("Error looking up database event\n");
        return;
    }

    device_jni_context ctx = (device_jni_context)mapper_device_user_data(dev);
    if (!ctx || !ctx->mapListener)
        return;
    cls = (*genv)->GetObjectClass(genv, ctx->mapListener);
    if (cls) {
        mid = (*genv)->GetMethodID(genv, cls, "onEvent",
                                   "(Lmapper/Map;Lmapper/database/Event;)V");
        if (mid) {
            (*genv)->CallVoidMethod(genv, ctx->mapListener, mid, mapobj,
                                    eventobj);
            if ((*genv)->ExceptionOccurred(genv))
                bailing = 1;
        }
        else {
            printf("Error looking up onEvent method.\n");
        }
    }
}

JNIEXPORT void JNICALL Java_mapper_Device_mapperDeviceSetMapCB
  (JNIEnv *env, jobject obj, jlong jdev, jobject listener)
{
    mapper_device dev = (mapper_device)ptr_jlong(jdev);
    if (!dev || !listener)
        return;
    device_jni_context ctx = (device_jni_context)mapper_device_user_data(dev);
    if (!ctx)
        return;
    if (ctx->mapListener)
        (*env)->DeleteGlobalRef(env, ctx->mapListener);
    ctx->mapListener = listener ? (*env)->NewGlobalRef(env, listener) : 0;
    mapper_device_set_map_callback(dev, java_device_map_cb);
}

JNIEXPORT jobject JNICALL Java_mapper_Device_signal__J
  (JNIEnv *env, jobject obj, jlong id)
{
    jclass cls = (*env)->FindClass(env, "mapper/Signal");
    if (!cls)
        return 0;
    mapper_device dev = get_device_from_jobject(env, obj);
    if (!dev)
        return 0;
    mapper_signal sig = mapper_device_signal_by_id(dev, id);
    if (!sig)
        return 0;
    jmethodID mid = (*env)->GetMethodID(env, cls, "<init>", "(J)V");
    return (*env)->NewObject(env, cls, mid, jlong_ptr(sig));
}

JNIEXPORT jobject JNICALL Java_mapper_Device_signal__Ljava_lang_String_2
  (JNIEnv *env, jobject obj, jstring name)
{
    jclass cls = (*env)->FindClass(env, "mapper/Signal");
    if (!cls)
        return 0;
    mapper_device dev = get_device_from_jobject(env, obj);
    if (!dev)
        return 0;
    const char *cname = (*env)->GetStringUTFChars(env, name, 0);
    mapper_signal sig = mapper_device_signal_by_name(dev, cname);
    (*env)->ReleaseStringUTFChars(env, name, cname);

    jmethodID mid = (*env)->GetMethodID(env, cls, "<init>", "(J)V");
    return (*env)->NewObject(env, cls, mid, jlong_ptr(sig));
}

JNIEXPORT jobject JNICALL Java_mapper_Device_signals
  (JNIEnv *env, jobject obj, jint dir)
{
    jclass cls = (*env)->FindClass(env, "mapper/signal/Query");
    if (!cls)
        return 0;
    mapper_device dev = get_device_from_jobject(env, obj);
    if (!dev)
        return 0;
    mapper_signal *sigs = mapper_device_signals(dev, dir);
    jmethodID mid = (*env)->GetMethodID(env, cls, "<init>", "(J)V");
    return (*env)->NewObject(env, cls, mid, jlong_ptr(sigs));
}

JNIEXPORT jlong JNICALL Java_mapper_Device_mapperDeviceMaps
  (JNIEnv *env, jobject obj, jlong jdev, jint dir)
{
    mapper_device dev = (mapper_device) ptr_jlong(jdev);
    return dev ? jlong_ptr(mapper_device_maps(dev, dir)) : 0;
}

/**** mapper_link_Query.h ****/

JNIEXPORT jlong JNICALL Java_mapper_link_Query_mapperLinkDeref
  (JNIEnv *env, jobject obj, jlong jlink)
{
    void **ptr = (void**)ptr_jlong(jlink);
    return jlong_ptr(*ptr);
}

JNIEXPORT jlong JNICALL Java_mapper_link_Query_mapperLinkQueryCopy
  (JNIEnv *env, jobject obj, jlong query)
{
    mapper_link *links = (mapper_link*) ptr_jlong(query);
    if (!links)
        return 0;
    return jlong_ptr(mapper_link_query_copy(links));
}

JNIEXPORT jlong JNICALL Java_mapper_link_Query_mapperLinkQueryJoin
  (JNIEnv *env, jobject obj, jlong query_lhs, jlong query_rhs)
{
    mapper_link *links_lhs = (mapper_link*) ptr_jlong(query_lhs);
    mapper_link *links_rhs = (mapper_link*) ptr_jlong(query_rhs);
    
    if (!links_rhs)
        return query_lhs;
    
    // use a copy of rhs
    mapper_link *links_rhs_cpy = mapper_link_query_copy(links_rhs);
    return jlong_ptr(mapper_link_query_union(links_lhs, links_rhs_cpy));
}

JNIEXPORT jlong JNICALL Java_mapper_link_Query_mapperLinkQueryIntersect
  (JNIEnv *env, jobject obj, jlong query_lhs, jlong query_rhs)
{
    mapper_link *links_lhs = (mapper_link*) ptr_jlong(query_lhs);
    mapper_link *links_rhs = (mapper_link*) ptr_jlong(query_rhs);
    
    if (!links_lhs || !links_rhs)
        return 0;
    
    // use a copy of rhs
    mapper_link *links_rhs_cpy = mapper_link_query_copy(links_rhs);
    return jlong_ptr(mapper_link_query_intersection(links_lhs, links_rhs_cpy));
}

JNIEXPORT jlong JNICALL Java_mapper_link_Query_mapperLinkQuerySubtract
  (JNIEnv *env, jobject obj, jlong query_lhs, jlong query_rhs)
{
    mapper_link *links_lhs = (mapper_link*) ptr_jlong(query_lhs);
    mapper_link *links_rhs = (mapper_link*) ptr_jlong(query_rhs);
    
    if (!links_lhs || !links_rhs)
        return query_lhs;
    
    // use a copy of rhs
    mapper_link *links_rhs_cpy = mapper_link_query_copy(links_rhs);
    return jlong_ptr(mapper_link_query_difference(links_lhs, links_rhs_cpy));
}

JNIEXPORT jlong JNICALL Java_mapper_link_Query_mapperLinkQueryNext
  (JNIEnv *env, jobject obj, jlong query)
{
    mapper_link *links = (mapper_link*) ptr_jlong(query);
    return links ? jlong_ptr(mapper_link_query_next(links)) : 0;
}

JNIEXPORT void JNICALL Java_mapper_link_Query_mapperLinkQueryDone
  (JNIEnv *env, jobject obj, jlong query)
{
    mapper_link *links = (mapper_link*) ptr_jlong(query);
    if (links)
        mapper_link_query_done(links);
}

/**** mapper_Link.h ****/

JNIEXPORT jobject JNICALL Java_mapper_Link_push
  (JNIEnv *env, jobject obj)
{
    mapper_link link = get_link_from_jobject(env, obj);
    if (link)
        mapper_link_push(link);
    return obj;
}

JNIEXPORT jint JNICALL Java_mapper_Link_numProperties
  (JNIEnv *env, jobject obj)
{
    mapper_link link = get_link_from_jobject(env, obj);
    return link ? mapper_link_num_properties(link) : 0;
}

JNIEXPORT jobject JNICALL Java_mapper_Link_property__Ljava_lang_String_2
  (JNIEnv *env, jobject obj, jstring key)
{
    mapper_link link = get_link_from_jobject(env, obj);
    const char *ckey = (*env)->GetStringUTFChars(env, key, 0);
    char type;
    int length;
    const void *value;
    jobject o = 0;

    if (!mapper_link_property(link, ckey, &length, &type, &value))
        o = build_Value(env, length, type, value, 0);

    (*env)->ReleaseStringUTFChars(env, key, ckey);
    return o;
}

JNIEXPORT jobject JNICALL Java_mapper_Link_property__I
  (JNIEnv *env, jobject obj, jint index)
{
    mapper_link link = get_link_from_jobject(env, obj);
    const char *key = 0;
    char type;
    int length;
    const void *value;
    jobject val = 0, prop = 0;

    if (mapper_link_property_index(link, index, &key, &length, &type, &value))
        return 0;

    val = build_Value(env, length, type, value, 0);
    prop = build_Property(env, key, val);

    return prop;
}

JNIEXPORT jobject JNICALL Java_mapper_Link_setProperty
  (JNIEnv *env, jobject obj, jstring key, jobject jprop)
{
    mapper_link link = get_link_from_jobject(env, obj);
    const char *ckey = (*env)->GetStringUTFChars(env, key, 0);
    char type;
    void *value;
    int length = get_Value_elements(env, jprop, &value, &type);
    if (length) {
        mapper_link_set_property(link, ckey, length, type, value);
        release_Value_elements(env, jprop, value);
    }
    return obj;
}

JNIEXPORT jobject JNICALL Java_mapper_Link_removeProperty
  (JNIEnv *env, jobject obj, jstring key)
{
    mapper_link link = get_link_from_jobject(env, obj);
    if (link) {
        const char *ckey = (*env)->GetStringUTFChars(env, key, 0);
        mapper_link_remove_property(link, ckey);
        (*env)->ReleaseStringUTFChars(env, key, ckey);
    }
    return obj;
}

JNIEXPORT jlong JNICALL Java_mapper_Link_id
  (JNIEnv *env, jobject obj)
{
    mapper_link link = get_link_from_jobject(env, obj);
    return link ? mapper_link_id(link) : 0;
}

JNIEXPORT jint JNICALL Java_mapper_Link_numMaps
  (JNIEnv *env, jobject obj, jint index, jint dir)
{
    mapper_link link = get_link_from_jobject(env, obj);
    return link ? mapper_link_num_maps(link, index, dir) : 0;
}

JNIEXPORT jobject JNICALL Java_mapper_Link_device
  (JNIEnv *env, jobject obj, jint index)
{
    jclass cls = (*env)->FindClass(env, "mapper/Device");
    if (!cls)
        return 0;

    mapper_device dev = 0;
    mapper_link link = get_link_from_jobject(env, obj);
    if (link)
        dev = mapper_link_device(link, index);

    jmethodID mid = (*env)->GetMethodID(env, cls, "<init>", "(J)V");
    return (*env)->NewObject(env, cls, mid, jlong_ptr(dev));
}

/**** mapper_map_Query.h ****/

JNIEXPORT jlong JNICALL Java_mapper_map_Query_mapperMapDeref
  (JNIEnv *env, jobject obj, jlong jmap)
{
    void **ptr = (void**)ptr_jlong(jmap);
    return jlong_ptr(*ptr);
}

JNIEXPORT jlong JNICALL Java_mapper_map_Query_mapperMapQueryCopy
  (JNIEnv *env, jobject obj, jlong query)
{
    mapper_map *maps = (mapper_map*) ptr_jlong(query);
    if (!maps)
        return 0;
    return jlong_ptr(mapper_map_query_copy(maps));
}

JNIEXPORT jlong JNICALL Java_mapper_map_Query_mapperMapQueryJoin
  (JNIEnv *env, jobject obj, jlong query_lhs, jlong query_rhs)
{
    mapper_map *maps_lhs = (mapper_map*) ptr_jlong(query_lhs);
    mapper_map *maps_rhs = (mapper_map*) ptr_jlong(query_rhs);

    if (!maps_rhs)
        return query_lhs;

    // use a copy of rhs
    mapper_map *maps_rhs_cpy = mapper_map_query_copy(maps_rhs);
    return jlong_ptr(mapper_map_query_union(maps_lhs, maps_rhs_cpy));
}

JNIEXPORT jlong JNICALL Java_mapper_map_Query_mapperMapQueryIntersect
  (JNIEnv *env, jobject obj, jlong query_lhs, jlong query_rhs)
{
    mapper_map *maps_lhs = (mapper_map*) ptr_jlong(query_lhs);
    mapper_map *maps_rhs = (mapper_map*) ptr_jlong(query_rhs);

    if (!maps_lhs || !maps_rhs)
        return 0;

    // use a copy of rhs
    mapper_map *maps_rhs_cpy = mapper_map_query_copy(maps_rhs);
    return jlong_ptr(mapper_map_query_intersection(maps_lhs, maps_rhs_cpy));
}

JNIEXPORT jlong JNICALL Java_mapper_map_Query_mapperMapQuerySubtract
  (JNIEnv *env, jobject obj, jlong query_lhs, jlong query_rhs)
{
    mapper_map *maps_lhs = (mapper_map*) ptr_jlong(query_lhs);
    mapper_map *maps_rhs = (mapper_map*) ptr_jlong(query_rhs);

    if (!maps_lhs || !maps_rhs)
        return query_lhs;

    // use a copy of rhs
    mapper_map *maps_rhs_cpy = mapper_map_query_copy(maps_rhs);
    return jlong_ptr(mapper_map_query_difference(maps_lhs, maps_rhs_cpy));
}

JNIEXPORT jlong JNICALL Java_mapper_map_Query_mapperMapQueryNext
  (JNIEnv *env, jobject obj, jlong query)
{
    mapper_map *maps = (mapper_map*) ptr_jlong(query);
    return maps ? jlong_ptr(mapper_map_query_next(maps)) : 0;
}

JNIEXPORT void JNICALL Java_mapper_map_Query_mapperMapQueryDone
  (JNIEnv *env, jobject obj, jlong query)
{
    mapper_map *maps = (mapper_map*) ptr_jlong(query);
    if (maps)
        mapper_map_query_done(maps);
}

/**** mapper_Map_Slot.h ****/

JNIEXPORT jint JNICALL Java_mapper_Map_00024Slot_numProperties
  (JNIEnv *env, jobject obj)
{
    mapper_slot slot = get_slot_from_jobject(env, obj);
    return slot ? mapper_slot_num_properties(slot) : 0;
}

JNIEXPORT jobject JNICALL Java_mapper_Map_00024Slot_property__Ljava_lang_String_2
  (JNIEnv *env, jobject obj, jstring key)
{
    mapper_slot slot = get_slot_from_jobject(env, obj);
    const char *ckey = (*env)->GetStringUTFChars(env, key, 0);
    char type;
    int length;
    const void *value;
    jobject o = 0;

    if (!mapper_slot_property(slot, ckey, &length, &type, &value))
        o = build_Value(env, length, type, value, 0);

    (*env)->ReleaseStringUTFChars(env, key, ckey);
    return o;
}

JNIEXPORT jobject JNICALL Java_mapper_Map_00024Slot_property__I
  (JNIEnv *env, jobject obj, jint index)
{
    mapper_slot slot = get_slot_from_jobject(env, obj);
    const char *key = 0;
    char type;
    int length;
    const void *value;
    jobject val = 0, prop = 0;

    if (mapper_slot_property_index(slot, index, &key, &length, &type, &value))
        return 0;

    val = build_Value(env, length, type, value, 0);
    prop = build_Property(env, key, val);

    return prop;
}

JNIEXPORT jobject JNICALL Java_mapper_Map_00024Slot_setProperty
  (JNIEnv *env, jobject obj, jstring key, jobject jprop)
{
    mapper_slot slot = get_slot_from_jobject(env, obj);
    const char *ckey = (*env)->GetStringUTFChars(env, key, 0);
    char type;
    void *value;
    int length = get_Value_elements(env, jprop, &value, &type);
    if (length) {
        mapper_slot_set_property(slot, ckey, length, type, value);
        release_Value_elements(env, jprop, value);
    }
    return obj;
}

JNIEXPORT jobject JNICALL Java_mapper_Map_00024Slot_removeProperty
  (JNIEnv *env, jobject obj, jstring key)
{
    mapper_slot slot = get_slot_from_jobject(env, obj);
    if (slot) {
        const char *ckey = (*env)->GetStringUTFChars(env, key, 0);
        mapper_slot_remove_property(slot, ckey);
        (*env)->ReleaseStringUTFChars(env, key, ckey);
    }
    return obj;
}

JNIEXPORT jint JNICALL Java_mapper_Map_00024Slot_mapperSlotBoundMax
  (JNIEnv *env, jobject obj, jlong jslot)
{
    mapper_slot slot = (mapper_slot) ptr_jlong(jslot);
    return slot ? mapper_slot_bound_max(slot) : 0;
}

JNIEXPORT void JNICALL Java_mapper_Map_00024Slot_mapperSetSlotBoundMax
  (JNIEnv *env, jobject obj, jlong jslot, jint action)
{
    mapper_slot slot = (mapper_slot) ptr_jlong(jslot);
    mapper_slot_set_bound_max(slot, action);
}

JNIEXPORT jint JNICALL Java_mapper_Map_00024Slot_mapperSlotBoundMin
  (JNIEnv *env, jobject obj, jlong jslot)
{
    mapper_slot slot = (mapper_slot) ptr_jlong(jslot);
    return slot ? mapper_slot_bound_min(slot) : 0;
}

JNIEXPORT void JNICALL Java_mapper_Map_00024Slot_mapperSetSlotBoundMin
(JNIEnv *env, jobject obj, jlong jslot, jint action)
{
    mapper_slot slot = (mapper_slot) ptr_jlong(jslot);
    mapper_slot_set_bound_min(slot, action);
}

JNIEXPORT jboolean JNICALL Java_mapper_Map_00024Slot_calibrating
  (JNIEnv *env, jobject obj)
{
    mapper_slot slot = get_slot_from_jobject(env, obj);
    return slot ? mapper_slot_calibrating(slot) : 0;
}

JNIEXPORT jobject JNICALL Java_mapper_Map_00024Slot_setCalibrating
  (JNIEnv *env, jobject obj, jboolean value)
{
    mapper_slot slot = get_slot_from_jobject(env, obj);
    mapper_slot_set_calibrating(slot, value);
    return obj;
}

JNIEXPORT jboolean JNICALL Java_mapper_Map_00024Slot_causesUpdate
  (JNIEnv *env, jobject obj)
{
    mapper_slot slot = get_slot_from_jobject(env, obj);
    return slot ? mapper_slot_causes_update(slot) : 0;
}

JNIEXPORT jobject JNICALL Java_mapper_Map_00024Slot_setCauseUpdate
  (JNIEnv *env, jobject obj, jboolean value)
{
    mapper_slot slot = get_slot_from_jobject(env, obj);
    mapper_slot_set_causes_update(slot, value);
    return obj;
}

JNIEXPORT jobject JNICALL Java_mapper_Map_00024Slot_maximum
  (JNIEnv *env, jobject obj)
{
    mapper_slot slot = get_slot_from_jobject(env, obj);
    int length = 0;
    char type = 0;
    void *value = 0;
    if (slot)
        mapper_slot_maximum(slot, &length, &type, &value);
    return build_Value(env, length, type, value, 0);
}

JNIEXPORT jobject JNICALL Java_mapper_Map_00024Slot_setMaximum
  (JNIEnv *env, jobject obj, jobject maximum)
{
    jstring maxstring = (*env)->NewStringUTF(env, "max");
    return Java_mapper_Map_00024Slot_setProperty(env, obj, maxstring, maximum);
}

JNIEXPORT jobject JNICALL Java_mapper_Map_00024Slot_minimum
  (JNIEnv *env, jobject obj)
{
    mapper_slot slot = get_slot_from_jobject(env, obj);
    int length = 0;
    char type = 0;
    void *value = 0;
    if (slot)
        mapper_slot_minimum(slot, &length, &type, &value);
    return build_Value(env, length, type, value, 0);
}

JNIEXPORT jobject JNICALL Java_mapper_Map_00024Slot_setMinimum
  (JNIEnv *env, jobject obj, jobject minimum)
{
    jstring minstring = (*env)->NewStringUTF(env, "min");
    return Java_mapper_Map_00024Slot_setProperty(env, obj, minstring, minimum);
}

JNIEXPORT jboolean JNICALL Java_mapper_Map_00024Slot_useAsInstance
  (JNIEnv *env, jobject obj)
{
    mapper_slot slot = get_slot_from_jobject(env, obj);
    return slot ? mapper_slot_use_as_instance(slot) : 0;
}

JNIEXPORT jobject JNICALL Java_mapper_Map_00024Slot_setUseAsInstance
  (JNIEnv *env, jobject obj, jboolean value)
{
    mapper_slot slot = get_slot_from_jobject(env, obj);
    if (slot)
        mapper_slot_set_use_as_instance(slot, (int)value);
    return obj;
}

JNIEXPORT jobject JNICALL Java_mapper_Map_00024Slot_signal
  (JNIEnv *env, jobject obj)
{
    jclass cls = (*env)->FindClass(env, "mapper/Signal");
    if (!cls)
        return 0;
    mapper_slot slot = get_slot_from_jobject(env, obj);
    if (!slot)
        return 0;
    mapper_signal sig = mapper_slot_signal(slot);
    jmethodID mid = (*env)->GetMethodID(env, cls, "<init>", "(J)V");
    return (*env)->NewObject(env, cls, mid, jlong_ptr(sig));
}

/**** mapper_Map.h ****/

JNIEXPORT jlong JNICALL Java_mapper_Map_mapperMapSrcSlotPtr
  (JNIEnv *env, jobject obj, jlong jmap, jint index)
{
    mapper_map map = (mapper_map) ptr_jlong(jmap);
    return map ? jlong_ptr(mapper_map_slot(map, MAPPER_LOC_SOURCE, index)) : 0;
}

JNIEXPORT jlong JNICALL Java_mapper_Map_mapperMapDstSlotPtr
  (JNIEnv *env, jobject obj, jlong jmap)
{
    mapper_map map = (mapper_map) ptr_jlong(jmap);
    return map ? jlong_ptr(mapper_map_slot(map, MAPPER_LOC_DESTINATION, 0)) : 0;
}

JNIEXPORT jlong JNICALL Java_mapper_Map_mapperMapNew
  (JNIEnv *env, jobject obj, jobjectArray jsources, jobject jdestination)
{
    int i, num_sources = (*env)->GetArrayLength(env, jsources);
    if (num_sources <= 0 || !jdestination)
        return 0;
    mapper_signal sources[num_sources];
    mapper_signal destination;
    for (i = 0; i < num_sources; i++) {
        jobject o = (*env)->GetObjectArrayElement(env, jsources, i);
        sources[i] = get_signal_from_jobject(env, o);
    }
    destination = get_signal_from_jobject(env, jdestination);

    mapper_map map = mapper_map_new(num_sources, sources, destination);
    return map ? jlong_ptr(map) : 0;
}

JNIEXPORT jobject JNICALL Java_mapper_Map_push
  (JNIEnv *env, jobject obj)
{
    mapper_map map = get_map_from_jobject(env, obj);
    if (map)
        mapper_map_push(map);
    return obj;
}

JNIEXPORT jint JNICALL Java_mapper_Map_numProperties
  (JNIEnv *env, jobject obj)
{
    mapper_map map = get_map_from_jobject(env, obj);
    return map ? mapper_map_num_properties(map) : 0;
}

JNIEXPORT jobject JNICALL Java_mapper_Map_property__Ljava_lang_String_2
  (JNIEnv *env, jobject obj, jstring key)
{
    mapper_map map = get_map_from_jobject(env, obj);
    const char *ckey = (*env)->GetStringUTFChars(env, key, 0);
    char type;
    int length;
    const void *value;
    jobject o = 0;

    if (!mapper_map_property(map, ckey, &length, &type, &value))
        o = build_Value(env, length, type, value, 0);

    (*env)->ReleaseStringUTFChars(env, key, ckey);
    return o;
}

JNIEXPORT jobject JNICALL Java_mapper_Map_property__I
  (JNIEnv *env, jobject obj, jint index)
{
    mapper_map map = get_map_from_jobject(env, obj);
    const char *key = 0;
    char type;
    int length;
    const void *value;
    jobject val = 0, prop = 0;

    if (mapper_map_property_index(map, index, &key, &length, &type, &value))
        return 0;

    val = build_Value(env, length, type, value, 0);
    prop = build_Property(env, key, val);

    return prop;
}

JNIEXPORT jobject JNICALL Java_mapper_Map_setProperty
  (JNIEnv *env, jobject obj, jstring key, jobject jprop)
{
    mapper_map map = get_map_from_jobject(env, obj);
    const char *ckey = (*env)->GetStringUTFChars(env, key, 0);
    char type;
    void *value;
    int length = get_Value_elements(env, jprop, &value, &type);
    if (length) {
        mapper_map_set_property(map, ckey, length, type, value);
        release_Value_elements(env, jprop, value);
    }
    return obj;
}

JNIEXPORT jobject JNICALL Java_mapper_Map_removeProperty
  (JNIEnv *env, jobject obj, jstring key)
{
    mapper_map map = get_map_from_jobject(env, obj);
    if (map) {
        const char *ckey = (*env)->GetStringUTFChars(env, key, 0);
        mapper_map_remove_property(map, ckey);
        (*env)->ReleaseStringUTFChars(env, key, ckey);
    }
    return obj;
}

JNIEXPORT jstring JNICALL Java_mapper_Map_expression
  (JNIEnv *env, jobject obj)
{
    mapper_map map = get_map_from_jobject(env, obj);
    return map ? (*env)->NewStringUTF(env, mapper_map_expression(map)) : 0;
}

JNIEXPORT jobject JNICALL Java_mapper_Map_setExpression
  (JNIEnv *env, jobject obj, jstring expression)
{
    mapper_map map = get_map_from_jobject(env, obj);
    if (map) {
        const char *cexpr = (*env)->GetStringUTFChars(env, expression, 0);
        mapper_map_set_expression(map, cexpr);
        (*env)->ReleaseStringUTFChars(env, expression, cexpr);
    }
    return obj;
}

JNIEXPORT jlong JNICALL Java_mapper_Map_id
  (JNIEnv *env, jobject obj)
{
    mapper_map map = get_map_from_jobject(env, obj);
    return map ? mapper_map_id(map) : 0;
}

JNIEXPORT jboolean JNICALL Java_mapper_Map_isLocal
  (JNIEnv *env, jobject obj)
{
    mapper_map map = get_map_from_jobject(env, obj);
    return map ? mapper_map_is_local(map) : 0;
}

JNIEXPORT jint JNICALL Java_mapper_Map_mapperMapMode
  (JNIEnv *env, jobject obj, jlong jmap)
{
    mapper_map map = (mapper_map) ptr_jlong(jmap);
    return map ? mapper_map_mode(map) : 0;
}

JNIEXPORT void JNICALL Java_mapper_Map_mapperMapSetMode
  (JNIEnv *env, jobject obj, jlong jmap, jint mode)
{
    mapper_map map = (mapper_map) ptr_jlong(jmap);
    if (map)
        mapper_map_set_mode(map, mode);
}

JNIEXPORT jboolean JNICALL Java_mapper_Map_muted
  (JNIEnv * env, jobject obj)
{
    mapper_map map = get_map_from_jobject(env, obj);
    return map ? mapper_map_muted(map) : 0;
}

JNIEXPORT jobject JNICALL Java_mapper_Map_setMuted
  (JNIEnv *env, jobject obj, jboolean value)
{
    mapper_map map = get_map_from_jobject(env, obj);
    if (map)
        mapper_map_set_muted(map, (int)value);
    return obj;
}

JNIEXPORT jint JNICALL Java_mapper_Map_mapperMapProcessLoc
  (JNIEnv *env, jobject obj, jlong jmap)
{
    mapper_map map = (mapper_map) ptr_jlong(jmap);
    return map ? mapper_map_process_location(map) : 0;
}

JNIEXPORT void JNICALL Java_mapper_Map_mapperMapSetProcessLoc
  (JNIEnv *env, jobject obj, jlong jmap, jint loc)
{
    mapper_map map = (mapper_map) ptr_jlong(jmap);
    if (map)
        mapper_map_set_process_location(map, loc);
}

JNIEXPORT jobject JNICALL Java_mapper_Map_scopes
  (JNIEnv *env, jobject obj)
{
    jclass cls = (*env)->FindClass(env, "mapper/map/Query");
    if (!cls)
        return 0;
    mapper_map map = get_map_from_jobject(env, obj);
    if (!map)
        return 0;
    mapper_device *scopes = mapper_map_scopes(map);
    jmethodID mid = (*env)->GetMethodID(env, cls, "<init>", "(J)V");
    return (*env)->NewObject(env, cls, mid, jlong_ptr(scopes));
}

JNIEXPORT jobject JNICALL Java_mapper_Map_addScope
  (JNIEnv *env, jobject obj, jobject jdev)
{
    mapper_map map = get_map_from_jobject(env, obj);
    if (map) {
        mapper_device dev = get_device_from_jobject(env, jdev);
        if (dev)
            mapper_map_add_scope(map, dev);
    }
    return obj;
}

JNIEXPORT jobject JNICALL Java_mapper_Map_removeScope
  (JNIEnv *env, jobject obj, jobject jdev)
{
    mapper_map map = get_map_from_jobject(env, obj);
    if (map) {
        mapper_device dev = get_device_from_jobject(env, jdev);
        if (dev)
            mapper_map_remove_scope(map, dev);
    }
    return obj;
}

JNIEXPORT jint JNICALL Java_mapper_Map_numSources
  (JNIEnv *env, jobject obj)
{
    mapper_map map = get_map_from_jobject(env, obj);
    return map ? mapper_map_num_sources(map) : 0;
}

JNIEXPORT jboolean JNICALL Java_mapper_Map_ready
  (JNIEnv *env, jobject obj)
{
    mapper_map map = get_map_from_jobject(env, obj);
    return map ? (mapper_map_ready(map)) : 0;
}

/**** mapper_Network.h ****/

JNIEXPORT jlong JNICALL Java_mapper_Network_mapperNetworkNew
  (JNIEnv *env, jobject obj, jstring iface, jstring group, jint port)
{
    const char *ciface = 0, *cgroup = 0;
    if (iface)
        ciface = (*env)->GetStringUTFChars(env, iface, 0);
    if (group)
        cgroup = (*env)->GetStringUTFChars(env, group, 0);
    mapper_network net = mapper_network_new(ciface, cgroup, port);
    if (iface)
        (*env)->ReleaseStringUTFChars(env, iface, ciface);
    if (group)
        (*env)->ReleaseStringUTFChars(env, group, cgroup);
    return jlong_ptr(net);
}

JNIEXPORT void JNICALL Java_mapper_Network_mapperNetworkFree
  (JNIEnv *env, jobject obj, jlong jnet)
{
    mapper_network net = (mapper_network) ptr_jlong(jnet);
    if (net)
        mapper_network_free(net);
}

JNIEXPORT jobject JNICALL Java_mapper_Network_now
  (JNIEnv *env, jobject obj)
{
    mapper_network net = get_network_from_jobject(env, obj);
    if (!net)
        return 0;
    mapper_timetag_t tt;
    mapper_network_now(net, &tt);
    jobject o = get_jobject_from_timetag(env, &tt);
    return o;
}

JNIEXPORT jstring JNICALL Java_mapper_Network_iface
  (JNIEnv *env, jobject obj)
{
    mapper_network net = get_network_from_jobject(env, obj);
    return net ? (*env)->NewStringUTF(env, mapper_network_interface(net)) : 0;
}

JNIEXPORT jlong JNICALL Java_mapper_Network_mapperNetworkDatabase
  (JNIEnv *env, jobject obj, jlong jnet)
{
    mapper_network net = (mapper_network) ptr_jlong(jnet);
    return net ? jlong_ptr(mapper_network_database(net)) : 0;
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
    else if (mapper_signal_num_reserved_instances(sig)) {
        // retrieve id from a reserved signal instance
        id = mapper_signal_reserved_instance_id(sig, 0);
    }
    else {
        // try to steal an active instance
        mapper_device dev = mapper_signal_device(sig);
        id = mapper_device_generate_unique_id(dev);
        if (!mapper_signal_instance_activate(sig, id)) {
            printf("Could not activate instance with id %llu\n", id);
            return 0;
        }
    }

    instance_jni_context ctx = ((instance_jni_context)
                                mapper_signal_instance_user_data(sig, id));
    if (!ctx) {
        printf("No context found for instance %llu\n", id);
        return 0;
    }

    ctx->instance = (*env)->NewGlobalRef(env, obj);
    if (ref)
        ctx->user_ref = (*env)->NewGlobalRef(env, ref);
    return id;
}

JNIEXPORT void JNICALL Java_mapper_Signal_00024Instance_release
  (JNIEnv *env, jobject obj, jobject jtt)
{
    mapper_id id;
    mapper_signal sig = get_instance_from_jobject(env, obj, &id);
    if (sig) {
        mapper_timetag_t tt, *ptt = 0;
        if (jtt)
            ptt = get_timetag_from_jobject(env, jtt, &tt);
        mapper_signal_instance_release(sig, id, ptt ? *ptt : MAPPER_NOW);
    }
}

JNIEXPORT void JNICALL Java_mapper_Signal_00024Instance_mapperFreeInstance
  (JNIEnv *env, jobject obj, jlong jsig, jlong id, jobject ttobj)
{
    mapper_signal sig = (mapper_signal) ptr_jlong(jsig);
    if (!sig)
        return;
    instance_jni_context ctx = mapper_signal_instance_user_data(sig, id);
    if (ctx) {
        if (ctx->listener)
            (*env)->DeleteGlobalRef(env, ctx->listener);
        if (ctx->instance)
            (*env)->DeleteGlobalRef(env, ctx->instance);
        if (ctx->user_ref)
            (*env)->DeleteGlobalRef(env, ctx->user_ref);
    }
    mapper_timetag_t tt, *ptt = 0;
    ptt = get_timetag_from_jobject(env, ttobj, &tt);
    mapper_signal_instance_release(sig, id, ptt ? *ptt : MAPPER_NOW);
}

JNIEXPORT jint JNICALL Java_mapper_Signal_00024Instance_isActive
  (JNIEnv *env, jobject obj)
{
    mapper_id id;
    mapper_signal sig = get_instance_from_jobject(env, obj, &id);
    return sig ? mapper_signal_instance_is_active(sig, id) : 0;
}

JNIEXPORT jobject JNICALL Java_mapper_Signal_00024Instance_userReference
  (JNIEnv *env, jobject obj)
{
    mapper_id id;
    mapper_signal sig = get_instance_from_jobject(env, obj, &id);
    if (!sig)
        return 0;
    instance_jni_context ctx = ((instance_jni_context)
                                mapper_signal_instance_user_data(sig, id));
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
                                mapper_signal_instance_user_data(sig, id));
    if (ctx) {
        if (ctx->user_ref)
            (*env)->DeleteGlobalRef(env, ctx->user_ref);
        ctx->user_ref = userObj ? (*env)->NewGlobalRef(env, userObj) : 0;
    }
    return obj;
}

/**** mapper_signal_Query.h ****/

JNIEXPORT jlong JNICALL Java_mapper_signal_Query_mapperSignalDeref
  (JNIEnv *env, jobject obj, jlong jsig)
{
    void **ptr = (void**)ptr_jlong(jsig);
    return jlong_ptr(*ptr);
}

JNIEXPORT jlong JNICALL Java_mapper_signal_Query_mapperSignalQueryCopy
  (JNIEnv *env, jobject obj, jlong query)
{
    mapper_signal *sigs = (mapper_signal*) ptr_jlong(query);
    if (!sigs)
        return 0;
    return jlong_ptr(mapper_signal_query_copy(sigs));
}

JNIEXPORT jlong JNICALL Java_mapper_signal_Query_mapperSignalQueryJoin
  (JNIEnv *env, jobject obj, jlong query_lhs, jlong query_rhs)
{
    mapper_signal *sigs_lhs = (mapper_signal*) ptr_jlong(query_lhs);
    mapper_signal *sigs_rhs = (mapper_signal*) ptr_jlong(query_rhs);

    if (!sigs_rhs)
        return query_lhs;

    // use a copy of rhs
    mapper_signal *sigs_rhs_cpy = mapper_signal_query_copy(sigs_rhs);
    return jlong_ptr(mapper_signal_query_union(sigs_lhs, sigs_rhs_cpy));
}

JNIEXPORT jlong JNICALL Java_mapper_signal_Query_mapperSignalQueryIntersect
  (JNIEnv *env, jobject obj, jlong query_lhs, jlong query_rhs)
{
    mapper_signal *sigs_lhs = (mapper_signal*) ptr_jlong(query_lhs);
    mapper_signal *sigs_rhs = (mapper_signal*) ptr_jlong(query_rhs);

    if (!sigs_lhs || !sigs_rhs)
        return 0;

    // use a copy of rhs
    mapper_signal *sigs_rhs_cpy = mapper_signal_query_copy(sigs_rhs);
    return jlong_ptr(mapper_signal_query_intersection(sigs_lhs, sigs_rhs_cpy));
}

JNIEXPORT jlong JNICALL Java_mapper_signal_Query_mapperSignalQuerySubtract
  (JNIEnv *env, jobject obj, jlong query_lhs, jlong query_rhs)
{
    mapper_signal *sigs_lhs = (mapper_signal*) ptr_jlong(query_lhs);
    mapper_signal *sigs_rhs = (mapper_signal*) ptr_jlong(query_rhs);

    if (!sigs_lhs || !sigs_rhs)
        return query_lhs;

    // use a copy of rhs
    mapper_signal *sigs_rhs_cpy = mapper_signal_query_copy(sigs_rhs);
    return jlong_ptr(mapper_signal_query_difference(sigs_lhs, sigs_rhs_cpy));
}

JNIEXPORT jlong JNICALL Java_mapper_signal_Query_mapperSignalQueryNext
  (JNIEnv *env, jobject obj, jlong query)
{
    mapper_signal *sigs = (mapper_signal*) ptr_jlong(query);
    return sigs ? jlong_ptr(mapper_signal_query_next(sigs)) : 0;
}

JNIEXPORT void JNICALL Java_mapper_signal_Query_mapperSignalQueryDone
  (JNIEnv *env, jobject obj, jlong query)
{
    mapper_signal *sigs = (mapper_signal*) ptr_jlong(query);
    if (sigs)
        mapper_signal_query_done(sigs);
}

/**** mapper_Signal.h ****/

JNIEXPORT jobject JNICALL Java_mapper_Signal_device
  (JNIEnv *env, jobject obj)
{
    jclass cls = (*env)->FindClass(env, "mapper/Device");
    if (!cls)
        return 0;

    mapper_device dev = 0;
    mapper_signal sig = get_signal_from_jobject(env, obj);
    if (sig)
        dev = mapper_signal_device(sig);

    jmethodID mid = (*env)->GetMethodID(env, cls, "<init>", "(J)V");
    return (*env)->NewObject(env, cls, mid, jlong_ptr(dev));
}

JNIEXPORT void JNICALL Java_mapper_Signal_mapperSignalSetInstanceEventCB
  (JNIEnv *env, jobject obj, jobject listener, jint flags)
{
    mapper_signal sig = get_signal_from_jobject(env, obj);
    if (!sig)
        return;

    signal_jni_context ctx = (signal_jni_context)mapper_signal_user_data(sig);

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
    mapper_signal sig = get_signal_from_jobject(env, obj);
    if (!sig)
        return obj;

    signal_jni_context ctx = (signal_jni_context)mapper_signal_user_data(sig);
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
    mapper_signal sig = get_signal_from_jobject(env, obj);
    if (!sig)
        return obj;

    int i, idx;
    for (i = 0; i < mapper_signal_num_instances(sig); i++) {
        idx = mapper_signal_instance_id(sig, i);
        instance_jni_context ctx = ((instance_jni_context)
                                    mapper_signal_instance_user_data(sig, idx));
        if (!ctx)
            continue;
        if (ctx->listener)
            (*env)->DeleteGlobalRef(env, ctx->listener);
        ctx->listener = listener ? (*env)->NewGlobalRef(env, listener) : 0;
    }

    // also set it for the signal
    signal_jni_context ctx = (signal_jni_context)mapper_signal_user_data(sig);
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
    mapper_signal sig = get_signal_from_jobject(env, obj);
    if (sig) {
        signal_jni_context ctx = ((signal_jni_context)
                                  mapper_signal_user_data(sig));
        return ctx ? ctx->listener : 0;
    }
    return 0;
}

JNIEXPORT void JNICALL Java_mapper_Signal_mapperSignalReserveInstances
  (JNIEnv *env, jobject obj, jlong jsig, jint num, jlongArray jids)
{
    mapper_signal sig = (mapper_signal) ptr_jlong(jsig);
    mapper_device dev;
    if (!sig || !(dev = mapper_signal_device(sig)))
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
        (*env)->ReleaseLongArrayElements(env, jids, (long long*)ids, JNI_ABORT);
}

JNIEXPORT jobject JNICALL Java_mapper_Signal_oldestActiveInstance
  (JNIEnv *env, jobject obj)
{
    mapper_signal sig = get_signal_from_jobject(env, obj);
    if (!sig) {
        printf("couldn't retrieve signal in "
               " Java_mapper_Signal_oldestActiveInstance()\n");
        return 0;
    }
    mapper_id id = mapper_signal_oldest_active_instance(sig);
    instance_jni_context ctx = ((instance_jni_context)
                                mapper_signal_instance_user_data(sig, id));
    return ctx ? ctx->instance : 0;
}

JNIEXPORT jobject JNICALL Java_mapper_Signal_newestActiveInstance
  (JNIEnv *env, jobject obj)
{
    mapper_signal sig = get_signal_from_jobject(env, obj);
    if (!sig)
        return 0;
    mapper_id id = mapper_signal_newest_active_instance(sig);
    instance_jni_context ctx = ((instance_jni_context)
                                mapper_signal_instance_user_data(sig, id));
    return ctx ? ctx->instance : 0;
}

JNIEXPORT jint JNICALL Java_mapper_Signal_numActiveInstances
  (JNIEnv *env, jobject obj)
{
    mapper_signal sig = get_signal_from_jobject(env, obj);
    return sig ? mapper_signal_num_active_instances(sig) : 0;
}

JNIEXPORT jint JNICALL Java_mapper_Signal_numReservedInstances
  (JNIEnv *env, jobject obj)
{
    mapper_signal sig = get_signal_from_jobject(env, obj);
    return sig ? mapper_signal_num_reserved_instances(sig) : 0;
}

JNIEXPORT void JNICALL Java_mapper_Signal_mapperSetInstanceStealingMode
  (JNIEnv *env, jobject obj, jint mode)
{
    mapper_signal sig = get_signal_from_jobject(env, obj);
    if (sig)
        mapper_signal_set_instance_stealing_mode(sig, mode);
}

JNIEXPORT jint JNICALL Java_mapper_Signal_mapperInstanceStealingMode
  (JNIEnv *env, jobject obj, jlong jsig)
{
    mapper_signal sig = (mapper_signal) ptr_jlong(jsig);
    return sig ? mapper_signal_instance_stealing_mode(sig) : 0;
}

JNIEXPORT jobject JNICALL Java_mapper_Signal_updateInstance__JILmapper_TimeTag_2
  (JNIEnv *env, jobject obj, jlong jinstance, jint value, jobject ttobj)
{
    mapper_signal sig = get_signal_from_jobject(env, obj);
    if (!sig)
        return obj;

    mapper_id instance = (mapper_id)ptr_jlong(jinstance);

    mapper_timetag_t tt, *ptt = 0;
    ptt = get_timetag_from_jobject(env, ttobj, &tt);

    char type = mapper_signal_type(sig);
    switch (type) {
        case 'i':
            mapper_signal_instance_update(sig, instance, &value, 1,
                                          ptt ? *ptt : MAPPER_NOW);
            break;
        case 'f': {
            float fvalue = (float)value;
            mapper_signal_instance_update(sig, instance, &fvalue, 1,
                                          ptt ? *ptt : MAPPER_NOW);
            break;
        }
        case 'd': {
            double dvalue = (double)value;
            mapper_signal_instance_update(sig, instance, &dvalue, 1,
                                          ptt ? *ptt : MAPPER_NOW);
            break;
        }
    }
    return obj;
}

JNIEXPORT jobject JNICALL Java_mapper_Signal_updateInstance__JFLmapper_TimeTag_2
  (JNIEnv *env, jobject obj, jlong jinstance, jfloat value, jobject ttobj)
{
    mapper_signal sig = get_signal_from_jobject(env, obj);
    if (!sig)
        return obj;

    mapper_id instance = (mapper_id)ptr_jlong(jinstance);

    mapper_timetag_t tt, *ptt = 0;
    ptt = get_timetag_from_jobject(env, ttobj, &tt);

    char type = mapper_signal_type(sig);
    switch (type) {
        case 'i': {
            int ivalue = (int)value;
            mapper_signal_instance_update(sig, instance, &ivalue, 1,
                                          ptt ? *ptt : MAPPER_NOW);
            break;
        }
        case 'f':
            mapper_signal_instance_update(sig, instance, &value, 1,
                                          ptt ? *ptt : MAPPER_NOW);
            break;
        case 'd': {
            double dvalue = (double)value;
            mapper_signal_instance_update(sig, instance, &dvalue, 1,
                                          ptt ? *ptt : MAPPER_NOW);
            break;
        }
    }
    return obj;
}

JNIEXPORT jobject JNICALL Java_mapper_Signal_updateInstance__JDLmapper_TimeTag_2
  (JNIEnv *env, jobject obj, jlong jinstance, jdouble value, jobject ttobj)
{
    mapper_signal sig = get_signal_from_jobject(env, obj);
    if (!sig)
        return obj;

    mapper_id instance = (mapper_id)ptr_jlong(jinstance);

    mapper_timetag_t tt, *ptt = 0;
    ptt = get_timetag_from_jobject(env, ttobj, &tt);

    char type = mapper_signal_type(sig);
    switch (type) {
        case 'i': {
            int ivalue = (int)value;
            mapper_signal_instance_update(sig, instance, &ivalue, 1,
                                          ptt ? *ptt : MAPPER_NOW);
            break;
        }
        case 'f': {
            float fvalue = (float)value;
            mapper_signal_instance_update(sig, instance, &fvalue, 1,
                                          ptt ? *ptt : MAPPER_NOW);
            break;
        }
        case 'd':
            mapper_signal_instance_update(sig, instance, &value, 1,
                                          ptt ? *ptt : MAPPER_NOW);
            break;
    }
    return obj;
}

JNIEXPORT jobject JNICALL Java_mapper_Signal_updateInstance__J_3ILmapper_TimeTag_2
  (JNIEnv *env, jobject obj, jlong jinstance, jintArray values, jobject ttobj)
{
    mapper_signal sig = get_signal_from_jobject(env, obj);
    if (!sig)
        return obj;

    int i, length = (*env)->GetArrayLength(env, values);
    if (length != mapper_signal_length(sig)) {
        throwIllegalArgumentLength(env, sig, length);
        return obj;
    }

    mapper_id instance = (mapper_id)ptr_jlong(jinstance);

    mapper_timetag_t tt, *ptt = 0;
    ptt = get_timetag_from_jobject(env, ttobj, &tt);

    jint *ivalues = (*env)->GetIntArrayElements(env, values, 0);
    if (!ivalues)
        return obj;

    char type = mapper_signal_type(sig);
    switch (type) {
        case 'i':
            mapper_signal_instance_update(sig, instance, ivalues, 1,
                                          ptt ? *ptt : MAPPER_NOW);
            break;
        case 'f': {
            float *fvalues = malloc(sizeof(float)*length);
            for (i = 0; i < length; i++)
                fvalues[i] = (float)ivalues[i];
            mapper_signal_instance_update(sig, instance, fvalues, 1,
                                          ptt ? *ptt : MAPPER_NOW);
            free(fvalues);
            break;
        }
        case 'd': {
            double *dvalues = malloc(sizeof(double)*length);
            for (i = 0; i < length; i++)
                dvalues[i] = (double)ivalues[i];
            mapper_signal_instance_update(sig, instance, dvalues, 1,
                                          ptt ? *ptt : MAPPER_NOW);
            free(dvalues);
            break;
        }
    }
    (*env)->ReleaseIntArrayElements(env, values, ivalues, JNI_ABORT);
    return obj;
}

JNIEXPORT jobject JNICALL Java_mapper_Signal_updateInstance__J_3FLmapper_TimeTag_2
  (JNIEnv *env, jobject obj, jlong jinstance, jfloatArray values, jobject ttobj)
{
    mapper_signal sig = get_signal_from_jobject(env, obj);
    if (!sig)
        return obj;

    int i, length = (*env)->GetArrayLength(env, values);
    if (length != mapper_signal_length(sig)) {
        throwIllegalArgumentLength(env, sig, length);
        return obj;
    }

    mapper_id instance = (mapper_id)ptr_jlong(jinstance);

    mapper_timetag_t tt, *ptt = 0;
    ptt = get_timetag_from_jobject(env, ttobj, &tt);

    jfloat *fvalues = (*env)->GetFloatArrayElements(env, values, 0);
    if (!fvalues)
        return obj;

    char type = mapper_signal_type(sig);
    switch (type) {
        case 'i': {
            int *ivalues = malloc(sizeof(int)*length);
            for (i = 0; i < length; i++)
                ivalues[i] = (float)fvalues[i];
            mapper_signal_instance_update(sig, instance, ivalues, 1,
                                          ptt ? *ptt : MAPPER_NOW);
            free(ivalues);
            break;
        }
        case 'f':
            mapper_signal_instance_update(sig, instance, fvalues, 1,
                                          ptt ? *ptt : MAPPER_NOW);
            break;
        case 'd': {
            double *dvalues = malloc(sizeof(double)*length);
            for (i = 0; i < length; i++)
                dvalues[i] = (double)fvalues[i];
            mapper_signal_instance_update(sig, instance, dvalues, 1,
                                          ptt ? *ptt : MAPPER_NOW);
            free(dvalues);
            break;
        }
    }
    (*env)->ReleaseFloatArrayElements(env, values, fvalues, JNI_ABORT);
    return obj;
}

JNIEXPORT jobject JNICALL Java_mapper_Signal_updateInstance__J_3DLmapper_TimeTag_2
  (JNIEnv *env, jobject obj, jlong jinstance, jdoubleArray values, jobject ttobj)
{
    mapper_signal sig = get_signal_from_jobject(env, obj);
    if (!sig)
        return obj;

    int i, length = (*env)->GetArrayLength(env, values);
    if (length != mapper_signal_length(sig)) {
        throwIllegalArgumentLength(env, sig, length);
        return obj;
    }

    mapper_id instance = (mapper_id)ptr_jlong(jinstance);

    mapper_timetag_t tt, *ptt = 0;
    ptt = get_timetag_from_jobject(env, ttobj, &tt);

    jdouble *dvalues = (*env)->GetDoubleArrayElements(env, values, 0);
    if (!dvalues)
        return obj;

    char type = mapper_signal_type(sig);
    switch (type) {
        case 'i': {
            int *ivalues = malloc(sizeof(int)*length);
            for (i = 0; i < length; i++)
                ivalues[i] = (int)dvalues[i];
            mapper_signal_instance_update(sig, instance, ivalues, 1,
                                          ptt ? *ptt : MAPPER_NOW);
            free(ivalues);
            break;
        }
        case 'f': {
            float *fvalues = malloc(sizeof(float)*length);
            for (i = 0; i < length; i++)
                fvalues[i] = (float)dvalues[i];
            mapper_signal_instance_update(sig, instance, fvalues, 1,
                                          ptt ? *ptt : MAPPER_NOW);
            free(fvalues);
            break;
        }
        case 'd':
            mapper_signal_instance_update(sig, instance, dvalues, 1,
                                          ptt ? *ptt : MAPPER_NOW);
            break;
    }
    (*env)->ReleaseDoubleArrayElements(env, values, dvalues, JNI_ABORT);
    return obj;
}

JNIEXPORT jobject JNICALL Java_mapper_Signal_instanceValue
  (JNIEnv *env, jobject obj, jlong jinstance)
{
    mapper_signal sig = get_signal_from_jobject(env, obj);
    char type = 'i';
    int length = 0;
    const void *value = 0;
    mapper_timetag_t tt = {0,1};

    mapper_id instance = (mapper_id)ptr_jlong(jinstance);
    if (sig) {
        type = mapper_signal_type(sig);
        length = mapper_signal_length(sig);
        value = mapper_signal_instance_value(sig, instance, &tt);
    }
    return build_Value(env, length, type, value, &tt);
}

JNIEXPORT jint JNICALL Java_mapper_Signal_queryRemotes
  (JNIEnv *env, jobject obj, jobject ttobj)
{
    mapper_signal sig = get_signal_from_jobject(env, obj);
    if (!sig)
        return 0;
    mapper_timetag_t tt, *ptt = 0;
    ptt = get_timetag_from_jobject(env, ttobj, &tt);
    return mapper_signal_query_remotes(sig, ptt ? *ptt : MAPPER_NOW);
}

JNIEXPORT jint JNICALL Java_mapper_Signal_numProperties
  (JNIEnv *env, jobject obj)
{
    mapper_signal sig = get_signal_from_jobject(env, obj);
    return sig ? mapper_signal_num_properties(sig) : 0;
}

JNIEXPORT jobject JNICALL Java_mapper_Signal_property__Ljava_lang_String_2
  (JNIEnv *env, jobject obj, jstring key)
{
    mapper_signal sig = get_signal_from_jobject(env, obj);
    const char *ckey = (*env)->GetStringUTFChars(env, key, 0);
    char type;
    int length;
    const void *value;
    jobject o = 0;

    if (!mapper_signal_property(sig, ckey, &length, &type, &value))
        o = build_Value(env, length, type, value, 0);

    (*env)->ReleaseStringUTFChars(env, key, ckey);
    return o;
}

JNIEXPORT jobject JNICALL Java_mapper_Signal_property__I
  (JNIEnv *env, jobject obj, jint index)
{
    mapper_signal sig = get_signal_from_jobject(env, obj);
    const char *key = 0;
    char type;
    int length;
    const void *value;
    jobject val = 0, prop = 0;

    if (mapper_signal_property_index(sig, index, &key, &length, &type, &value))
        return 0;

    val = build_Value(env, length, type, value, 0);
    prop = build_Property(env, key, val);

    return prop;
}

JNIEXPORT jobject JNICALL Java_mapper_Signal_setProperty
  (JNIEnv *env, jobject obj, jstring key, jobject jprop)
{
    mapper_signal sig = get_signal_from_jobject(env, obj);
    const char *ckey = (*env)->GetStringUTFChars(env, key, 0);
    char type;
    void *value;
    int length = get_Value_elements(env, jprop, &value, &type);
    if (length) {
        mapper_signal_set_property(sig, ckey, length, type, value);
        release_Value_elements(env, jprop, value);
    }
    return obj;
}

JNIEXPORT jobject JNICALL Java_mapper_Signal_removeProperty
  (JNIEnv *env, jobject obj, jstring key)
{
    mapper_signal sig = get_signal_from_jobject(env, obj);
    if (sig) {
        const char *ckey = (*env)->GetStringUTFChars(env, key, 0);
        mapper_signal_remove_property(sig, ckey);
        (*env)->ReleaseStringUTFChars(env, key, ckey);
    }
    return obj;
}

JNIEXPORT jint JNICALL Java_mapper_Signal_direction
  (JNIEnv *env, jobject obj, jlong jsig)
{
    mapper_signal sig = (mapper_signal) ptr_jlong(jsig);
    return sig ? mapper_signal_direction(sig) : 0;
}

JNIEXPORT jlong JNICALL Java_mapper_Signal_id
  (JNIEnv *env, jobject obj)
{
    mapper_signal sig = get_signal_from_jobject(env, obj);
    return sig ? mapper_signal_id(sig) : 0;
}

JNIEXPORT jboolean JNICALL Java_mapper_Signal_isLocal
  (JNIEnv *env, jobject obj)
{
    mapper_signal sig = get_signal_from_jobject(env, obj);
    return sig ? mapper_signal_is_local(sig) : 0;
}

JNIEXPORT jint JNICALL Java_mapper_Signal_length
  (JNIEnv *env, jobject obj)
{
    mapper_signal sig = get_signal_from_jobject(env, obj);
    return sig ? mapper_signal_length(sig) : 0;
}

JNIEXPORT jobject JNICALL Java_mapper_Signal_maximum
  (JNIEnv *env, jobject obj)
{
    mapper_signal sig = get_signal_from_jobject(env, obj);
    int length = 0;
    char type = 0;
    void *value = 0;
    if (sig) {
        length = mapper_signal_length(sig);
        type = mapper_signal_type(sig);
        value = mapper_signal_maximum(sig);
    }
    return build_Value(env, length, type, value, 0);
}

JNIEXPORT jobject JNICALL Java_mapper_Signal_setMaximum
  (JNIEnv *env, jobject obj, jobject maximum)
{
    jstring maxstring = (*env)->NewStringUTF(env, "max");
    return Java_mapper_Signal_setProperty(env, obj, maxstring, maximum);
}

JNIEXPORT jobject JNICALL Java_mapper_Signal_minimum
  (JNIEnv *env, jobject obj)
{
    mapper_signal sig = get_signal_from_jobject(env, obj);
    int length = 0;
    char type = 0;
    void *value = 0;
    if (sig) {
        length = mapper_signal_length(sig);
        type = mapper_signal_type(sig);
        value = mapper_signal_minimum(sig);
    }
    return build_Value(env, length, type, value, 0);
}

JNIEXPORT jobject JNICALL Java_mapper_Signal_setMinimum
  (JNIEnv *env, jobject obj, jobject minimum)
{
    jstring minstring = (*env)->NewStringUTF(env, "min");
    return Java_mapper_Signal_setProperty(env, obj, minstring, minimum);
}

JNIEXPORT jstring JNICALL Java_mapper_Signal_name
  (JNIEnv *env, jobject obj)
{
    mapper_signal sig = get_signal_from_jobject(env, obj);
    return sig ? (*env)->NewStringUTF(env, mapper_signal_name(sig)) : 0;
}

JNIEXPORT jfloat JNICALL Java_mapper_Signal_rate
  (JNIEnv *env, jobject obj)
{
    mapper_signal sig = get_signal_from_jobject(env, obj);
    return sig ? mapper_signal_rate(sig) : 0;
}

JNIEXPORT jobject JNICALL Java_mapper_Signal_setRate
  (JNIEnv *env, jobject obj, jfloat rate)
{
    mapper_signal sig = get_signal_from_jobject(env, obj);
    if (sig)
        mapper_signal_set_rate(sig, rate);
    return obj;
}

JNIEXPORT jchar JNICALL Java_mapper_Signal_type
  (JNIEnv *env, jobject obj)
{
    mapper_signal sig = get_signal_from_jobject(env, obj);
    return sig ? mapper_signal_type(sig) : 0;
}

JNIEXPORT jstring JNICALL Java_mapper_Signal_unit
  (JNIEnv *env, jobject obj)
{
    mapper_signal sig = get_signal_from_jobject(env, obj);
    const char *unit = 0;
    if (sig)
        unit = mapper_signal_unit(sig);
    return unit ? (*env)->NewStringUTF(env, unit) : 0;
}

JNIEXPORT jint JNICALL Java_mapper_Signal_numMaps
  (JNIEnv *env, jobject obj, jint direction)
{
    mapper_signal sig = get_signal_from_jobject(env, obj);
    return sig ? mapper_signal_num_maps(sig, direction) : 0;
}

JNIEXPORT jobject JNICALL Java_mapper_Signal_maps
  (JNIEnv *env, jobject obj, jint direction)
{
    jclass cls = (*env)->FindClass(env, "mapper/map/Query");
    if (!cls)
        return 0;

    mapper_map *maps = 0;
    mapper_signal sig = get_signal_from_jobject(env, obj);
    if (sig)
        maps = mapper_signal_maps(sig, direction);

    jmethodID mid = (*env)->GetMethodID(env, cls, "<init>", "(J)V");
    return (*env)->NewObject(env, cls, mid, jlong_ptr(maps));
}
