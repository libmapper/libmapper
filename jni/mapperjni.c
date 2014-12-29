
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <mapper/mapper.h>

#include "Mapper_Device.h"
#include "Mapper_Device_Signal.h"
#include "Mapper_Db_Signal.h"

#define jlong_ptr(a) ((jlong)(uintptr_t)(a))
#define ptr_jlong(a) ((void *)(uintptr_t)(a))

JNIEnv *genv=0;
int bailing=0;

typedef struct {
    jobject listener;
    jobject signal;
    jobject db_signal;
    jobject instanceHandler;
} msig_jni_context_t, *msig_jni_context;

/**** Helpers ****/

static void throwIllegalArgumentTruncate(JNIEnv *env, mapper_signal sig)
{
    jclass newExcCls =
        (*env)->FindClass(env, "java/lang/IllegalArgumentException");
    if (newExcCls) {
        char msg[1024];
        snprintf(msg, 1024,
                 "Signal %s has integer type, floating-"
                 "point value would truncate.",
                 msig_properties(sig)->name);
        (*env)->ThrowNew(env, newExcCls, msg);
    }
}

static void throwIllegalArgumentLength(JNIEnv *env, mapper_signal sig, int al)
{
    jclass newExcCls =
        (*env)->FindClass(env, "java/lang/IllegalArgumentException");
    if (newExcCls) {
        char msg[1024];
        mapper_db_signal p = msig_properties(sig);
        snprintf(msg, 1024,
                 "Signal %s length is %d, but array argument has length %d.",
                 p->name, p->length, al);
        (*env)->ThrowNew(env, newExcCls, msg);
    }
}

static void throwIllegalArgumentSignal(JNIEnv *env)
{
    jclass newExcCls =
        (*env)->FindClass(env, "java/lang/IllegalArgumentException");
    if (newExcCls) {
        (*env)->ThrowNew(env, newExcCls,
                         "Signal object is not associated with "
                         "a mapper_signal.");
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

static mapper_device get_device_from_jobject(JNIEnv *env, jobject obj)
{
    // TODO check device here
    jclass cls = (*env)->GetObjectClass(env, obj);
    if (cls) {
        jfieldID val = (*env)->GetFieldID(env, cls, "_device", "J");
        if (val) {
            jlong s = (*env)->GetLongField(env, obj, val);
            return (mapper_device)ptr_jlong(s);
        }
    }
    return 0;
}

static mapper_signal get_signal_from_jobject(JNIEnv *env, jobject obj)
{
    // TODO check device here
    jclass cls = (*env)->GetObjectClass(env, obj);
    if (cls) {
        jfieldID val = (*env)->GetFieldID(env, cls, "_signal", "J");
        if (val) {
            jlong s = (*env)->GetLongField(env, obj, val);
            return (mapper_signal)ptr_jlong(s);
        }
    }
    throwIllegalArgumentSignal(env);
    return 0;
}

static mapper_db get_db_from_jobject(JNIEnv *env, jobject obj)
{
    // TODO check device here
    jclass cls = (*env)->GetObjectClass(env, obj);
    if (cls) {
        jfieldID val = (*env)->GetFieldID(env, cls, "_db", "J");
        if (val) {
            jlong s = (*env)->GetLongField(env, obj, val);
            return (mapper_db)ptr_jlong(s);
        }
    }
    throwIllegalArgument(env, "Couldn't retrieve db pointer.");
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
    jobject objtt = 0;
    if (tt) {
        jclass cls = (*env)->FindClass(env, "Mapper/TimeTag");
        if (cls) {
            jmethodID cons = (*env)->GetMethodID(env, cls,
                                                 "<init>", "(JJ)V");
            if (cons) {
                objtt = (*env)->NewObject(env, cls, cons,
                                          tt->sec, tt->frac);
            }
            else {
                printf("Error looking up TimeTag constructor.\n");
                exit(1);
            }
        }
    }
    return objtt;
}

static mapper_db_device* get_db_device_ptr_from_jobject(JNIEnv *env, jobject obj)
{
    jclass cls = (*env)->GetObjectClass(env, obj);
    if (cls) {
        jfieldID val = (*env)->GetFieldID(env, cls, "_devprops_p", "J");
        if (val) {
            jlong s = (*env)->GetLongField(env, obj, val);
            return (mapper_db_device*)ptr_jlong(s);
        }
    }
    throwIllegalArgument(env, "Couldn't retrieve mapper_db_device* ptr.");
    return 0;
}

static mapper_db_signal* get_db_signal_ptr_from_jobject(JNIEnv *env, jobject obj)
{
    jclass cls = (*env)->GetObjectClass(env, obj);
    if (cls) {
        jfieldID val = (*env)->GetFieldID(env, cls, "_sigprops_p", "J");
        if (val) {
            jlong s = (*env)->GetLongField(env, obj, val);
            return (mapper_db_signal*)ptr_jlong(s);
        }
    }
    throwIllegalArgument(env, "Couldn't retrieve mapper_db_signal* ptr.");
    return 0;
}

static jobject build_PropertyValue(JNIEnv *env, const char type,
                                   const void *value, const int length)
{
    if (length <= 0)
        return 0;

    jmethodID methodID;
    jclass cls = (*env)->FindClass(env, "Mapper/PropertyValue");

    switch (type) {
        case 'i': {
            if (length == 1) {
                methodID = (*env)->GetMethodID(env, cls, "<init>", "(CI)V");
                if (methodID)
                    return (*env)->NewObject(env, cls, methodID, type,
                                             *((int *)value));
            }
            else {
                methodID = (*env)->GetMethodID(env, cls, "<init>", "(C[I)V");
                if (methodID) {
                    jintArray arr = (*env)->NewIntArray(env, length);
                    (*env)->SetIntArrayRegion(env, arr, 0, length, value);
                    return (*env)->NewObject(env, cls, methodID, type, arr);
                }
            }
            break;
        }
        case 'f': {
            if (length == 1) {
                methodID = (*env)->GetMethodID(env, cls, "<init>", "(CF)V");
                if (methodID)
                    return (*env)->NewObject(env, cls, methodID, type,
                                             *((float *)value));
            }
            else {
                methodID = (*env)->GetMethodID(env, cls, "<init>", "(C[F)V");
                if (methodID) {
                    jfloatArray arr = (*env)->NewFloatArray(env, length);
                    (*env)->SetFloatArrayRegion(env, arr, 0, length, value);
                    return (*env)->NewObject(env, cls, methodID, type, arr);
                }
            }
            break;
        }
        case 'd': {
            if (length == 1) {
                methodID = (*env)->GetMethodID(env, cls, "<init>", "(CD)V");
                if (methodID)
                    return (*env)->NewObject(env, cls, methodID, type,
                                             *((double *)value));
            }
            else {
                methodID = (*env)->GetMethodID(env, cls, "<init>", "(C[D)V");
                if (methodID) {
                    jdoubleArray arr = (*env)->NewDoubleArray(env, length);
                    (*env)->SetDoubleArrayRegion(env, arr, 0, length, value);
                    return (*env)->NewObject(env, cls, methodID, type, arr);
                }
            }
            break;
        }
        case 's': {
            if (length == 1) {
                methodID = (*env)->GetMethodID(env, cls, "<init>",
                                               "(CLjava/lang/String;)V");
                if (methodID) {
                    jobject s = (*env)->NewStringUTF(env, (char *)value);
                    if (s)
                        return (*env)->NewObject(env, cls, methodID, type, s);
                }
            }
            else {
                methodID = (*env)->GetMethodID(env, cls, "<init>",
                                               "(C[Ljava/lang/String;)V");
                if (methodID) {
                    jobjectArray arr = (*env)->NewObjectArray(env, length,
                        (*env)->FindClass(env, "java/lang/String"),
                        (*env)->NewStringUTF(env, ""));
                    int i;
                    char **strings = (char**)value;
                    for (i = 0; i < length; i++) {
                        (*env)->SetObjectArrayElement(env, arr, i,
                            (*env)->NewStringUTF(env, strings[i]));
                    }
                    return (*env)->NewObject(env, cls, methodID, type, arr);
                }
            }
            break;
        }
        default:
            break;
    }
    return 0;
}

static int get_PropertyValue_elements(JNIEnv *env, jobject jprop, void **value,
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

    switch (*type)
    {
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
        case 'S':
        {
            valf = (*env)->GetFieldID(env, cls, "_s", "[Ljava/lang/String;");
            o = (*env)->GetObjectField(env, jprop, valf);
            // need to unpack string array and rebuild
            jstring jstrings[length];
            const char **cstrings = malloc(sizeof(char*) * length);
            int i;
            for (i = 0; i < length; i++) {
                jstrings[i] = (jstring) (*env)->GetObjectArrayElement(env, o, i);
                cstrings[i] = (*env)->GetStringUTFChars(env, jstrings[i], 0);
            }
            *value = cstrings;
            break;
        }
        default:
            return 0;
    }
    return length;
}

static void release_PropertyValue_elements(JNIEnv *env, jobject jprop,
                                           void *value)
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

    switch (type)
    {
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
        case 'S':
        {
            valf = (*env)->GetFieldID(env, cls, "_s", "[Ljava/lang/String;");
            o = (*env)->GetObjectField(env, jprop, valf);

            jstring jstr;
            const char **cstrings = (const char**)value;
            int i;
            for (i = 0; i < length; i++) {
                jstr = (jstring) (*env)->GetObjectArrayElement(env, o, i);
                (*env)->ReleaseStringUTFChars(env, jstr, cstrings[i]);
            }
            free(cstrings);
            break;
        }
    }
}

/**** Mapper.Device ****/

JNIEXPORT jlong JNICALL Java_Mapper_Device_mdev_1new
  (JNIEnv *env, jobject obj, jstring name, jint port)
{
    const char *cname = (*env)->GetStringUTFChars(env, name, 0);
    mapper_device dev = mdev_new(cname, port, 0);
    (*env)->ReleaseStringUTFChars(env, name, cname);
    return jlong_ptr(dev);
}

JNIEXPORT void JNICALL Java_Mapper_Device_mdev_1free
  (JNIEnv *env, jobject obj, jlong d)
{
    mapper_device dev = (mapper_device)ptr_jlong(d);

    /* Free all references to Java objects. */
    int i, n = mdev_num_inputs(dev), m = mdev_num_outputs(dev);
    for (i=0; i<n+m; i++) {
        mapper_signal sig;
        if (i<n)
            sig = mdev_get_input_by_index(dev, i);
        else
            sig = mdev_get_output_by_index(dev, i-n);
        mapper_db_signal props = msig_properties(sig);
        msig_jni_context ctx = (msig_jni_context)props->user_data;
        if (ctx->listener)
            (*env)->DeleteGlobalRef(env, ctx->listener);
        if (ctx->signal)
            (*env)->DeleteGlobalRef(env, ctx->signal);
        if (ctx->db_signal)
            (*env)->DeleteGlobalRef(env, ctx->db_signal);
        if (ctx->instanceHandler)
            (*env)->DeleteGlobalRef(env, ctx->instanceHandler);
        free(ctx);
        props->user_data = 0;
    }

    mdev_free(dev);
}

JNIEXPORT jint JNICALL Java_Mapper_Device_mdev_1poll
  (JNIEnv *env, jobject obj, jlong d, jint timeout)
{
    genv = env;
    bailing = 0;
    mapper_device dev = (mapper_device)ptr_jlong(d);
    return mdev_poll(dev, timeout);
}

static void java_msig_input_cb(mapper_signal sig, mapper_db_signal props,
                               int instance_id, void *v, int count,
                               mapper_timetag_t *tt)
{
    if (bailing)
        return;

    jobject vobj = 0;
    if (props->type == 'f' && v) {
        jfloatArray varr = (*genv)->NewFloatArray(genv, props->length);
        if (varr)
            (*genv)->SetFloatArrayRegion(genv, varr, 0, props->length, v);
        vobj = (jobject) varr;
    }
    else if (props->type == 'i' && v) {
        jintArray varr = (*genv)->NewIntArray(genv, props->length);
        if (varr)
            (*genv)->SetIntArrayRegion(genv, varr, 0, props->length, v);
        vobj = (jobject) varr;
    }
    else if (props->type == 'd' && v) {
        jdoubleArray varr = (*genv)->NewDoubleArray(genv, props->length);
        if (varr)
            (*genv)->SetDoubleArrayRegion(genv, varr, 0, props->length, v);
        vobj = (jobject) varr;
    }

    if (!vobj && v) {
        char msg[1024];
        snprintf(msg, 1024,
                 "Unknown signal type for %s in callback handler (%c,%d).",
                 props->name, props->type, props->length);
        jclass newExcCls =
            (*genv)->FindClass(genv, "java/lang/IllegalArgumentException");
        if (newExcCls)
            (*genv)->ThrowNew(genv, newExcCls, msg);
        bailing = 1;
        return;
    }

    jobject objtt = get_jobject_from_timetag(genv, tt);

    msig_jni_context ctx = (msig_jni_context)props->user_data;
    jobject input_cb = ctx->listener;

    if (instance_id != 0)
        input_cb = (jobject)msig_get_instance_data(sig, instance_id);

    if (input_cb && ctx->signal) {
        jclass cls = (*genv)->GetObjectClass(genv, input_cb);
        if (cls) {
            jmethodID mid=0;
            if (props->type=='i')
                mid = (*genv)->GetMethodID(genv, cls, "onInput",
                                           "(LMapper/Device$Signal;"
                                           "I[I"
                                           "LMapper/TimeTag;)V");
            else if (props->type=='f')
                mid = (*genv)->GetMethodID(genv, cls, "onInput",
                                           "(LMapper/Device$Signal;"
                                           "I[F"
                                           "LMapper/TimeTag;)V");
            else if (props->type=='d')
                mid = (*genv)->GetMethodID(genv, cls, "onInput",
                                           "(LMapper/Device$Signal;"
                                           "I[D"
                                           "LMapper/TimeTag;)V");

            if (mid) {
                (*genv)->CallVoidMethod(genv, input_cb, mid, ctx->signal,
                                        instance_id, vobj, objtt);
                if ((*genv)->ExceptionOccurred(genv))
                    bailing = 1;
            }
            else {
                printf("Did not successfully look up onInput method.\n");
                exit(1);
            }
        }
    }

    if (vobj)
        (*genv)->DeleteLocalRef(genv, vobj);
    if (objtt)
        (*genv)->DeleteLocalRef(genv, objtt);
}

static void java_msig_instance_event_cb(mapper_signal sig,
                                        mapper_db_signal props,
                                        int instance_id,
                                        msig_instance_event_t event,
                                        mapper_timetag_t *tt)
{
    if (bailing)
        return;

    jobject objtt = get_jobject_from_timetag(genv, tt);

    msig_jni_context ctx = (msig_jni_context)props->user_data;
    if (ctx->instanceHandler && ctx->signal) {
        jclass cls = (*genv)->GetObjectClass(genv, ctx->instanceHandler);
        if (cls) {
            jmethodID mid=0;
            mid = (*genv)->GetMethodID(genv, cls, "onEvent",
                                       "(LMapper/Device$Signal;II"
                                       "LMapper/TimeTag;)V");

            if (mid) {
                (*genv)->CallVoidMethod(genv, ctx->instanceHandler, mid,
                                        ctx->signal, instance_id, event, objtt);
                if ((*genv)->ExceptionOccurred(genv))
                    bailing = 1;
            }
            else {
                printf("Did not successfully look up onEvent method.\n");
                exit(1);
            }
        }
    }

    if (objtt)
        (*genv)->DeleteLocalRef(genv, objtt);
}

static jobject create_signal_object(JNIEnv *env, jobject devobj,
                                    msig_jni_context ctx,
                                    jobject listener,
                                    mapper_signal s)
{
    jobject sigobj = 0, sigdbobj = 0;
    // Create a wrapper class for this signal
    jclass cls = (*env)->FindClass(env, "Mapper/Device$Signal");
    if (cls) {
        jmethodID mid = (*env)->GetMethodID(env, cls, "<init>",
                            "(LMapper/Device;JLMapper/Device;)V");
        sigobj = (*env)->NewObject(env, cls, mid,
                                   devobj, jlong_ptr(s), devobj);
    }

    if (sigobj) {
        mapper_db_signal props = msig_properties(s);
        props->user_data = ctx;
        ctx->listener = listener ? (*env)->NewGlobalRef(env, listener) : 0;
        ctx->signal = (*env)->NewGlobalRef(env, sigobj);
        ctx->db_signal = (*env)->NewGlobalRef(env, sigdbobj);

        // Create a wrapper class for this signal's properties
        cls = (*env)->FindClass(env, "Mapper/Db/Signal");
        if (cls) {
            jmethodID mid = (*env)->GetMethodID(env, cls, "<init>", "(J)V");
            sigdbobj = (*env)->NewObject(env, cls, mid,
                                         jlong_ptr(msig_properties(s)));
        }
    }

    if (sigdbobj) {
        mapper_db_signal props = msig_properties(s);
        props->user_data = ctx;
        ctx->listener = (*env)->NewGlobalRef(env, listener);
        ctx->signal = (*env)->NewGlobalRef(env, sigobj);
        ctx->db_signal = (*env)->NewGlobalRef(env, sigdbobj);
    }
    else {
        printf("Error creating signal wrapper class.\n");
        exit(1);
    }

    return sigobj;
}

JNIEXPORT jobject JNICALL Java_Mapper_Device_addInput
  (JNIEnv *env, jobject obj, jstring name, jint length, jchar type,
   jstring unit, jobject minimum, jobject maximum, jobject listener)
{
    if (!name || (length<=0) || (type!='f' && type!='i' && type!='d'))
        return 0;

    mapper_device dev = get_device_from_jobject(env, obj);

    const char *cname = (*env)->GetStringUTFChars(env, name, 0);
    const char *cunit = 0;
    if (unit) cunit = (*env)->GetStringUTFChars(env, unit, 0);

    msig_jni_context ctx =
        (msig_jni_context)calloc(1, sizeof(msig_jni_context_t));
    if (!ctx) {
        throwOutOfMemory(env);
        return 0;
    }

    mapper_signal s = mdev_add_input(dev, cname, length, type, cunit,
                                     0, 0, java_msig_input_cb, ctx);

    (*env)->ReleaseStringUTFChars(env, name, cname);
    if (unit) (*env)->ReleaseStringUTFChars(env, unit, cunit);

    if (minimum) {
        jstring minstring = (*env)->NewStringUTF(env, "min");
        Java_Mapper_Device_00024Signal_msig_1set_1property(env, obj, (jlong)s,
                                                           minstring, minimum);
    }
    if (maximum) {
        jstring maxstring = (*env)->NewStringUTF(env, "max");
        Java_Mapper_Device_00024Signal_msig_1set_1property(env, obj, (jlong)s,
                                                           maxstring, maximum);
    }

    return create_signal_object(env, obj, ctx, listener, s);
}

JNIEXPORT jobject JNICALL Java_Mapper_Device_addOutput
  (JNIEnv *env, jobject obj, jstring name, jint length, jchar type,
   jstring unit, jobject minimum, jobject maximum)
{
    if (!name || (length<=0) || (type!='f' && type!='i' && type!='d'))
        return 0;

    mapper_device dev = get_device_from_jobject(env, obj);

    const char *cname = (*env)->GetStringUTFChars(env, name, 0);
    const char *cunit = 0;
    if (unit) cunit = (*env)->GetStringUTFChars(env, unit, 0);

    msig_jni_context ctx =
        (msig_jni_context)calloc(1, sizeof(msig_jni_context_t));
    if (!ctx) {
        throwOutOfMemory(env);
        return 0;
    }

    mapper_signal s = mdev_add_output(dev, cname, length, type, cunit, 0, 0);

    (*env)->ReleaseStringUTFChars(env, name, cname);
    if (unit) (*env)->ReleaseStringUTFChars(env, unit, cunit);

    if (minimum) {
        jstring minstring = (*env)->NewStringUTF(env, "min");
        Java_Mapper_Device_00024Signal_msig_1set_1property(env, obj, (jlong)s,
                                                           minstring, minimum);
    }
    if (maximum) {
        jstring maxstring = (*env)->NewStringUTF(env, "max");
        Java_Mapper_Device_00024Signal_msig_1set_1property(env, obj, (jlong)s,
                                                           maxstring, maximum);
    }

    return create_signal_object(env, obj, ctx, 0, s);
}

JNIEXPORT void JNICALL Java_Mapper_Device_mdev_1remove_1input
  (JNIEnv *env, jobject obj, jlong d, jlong s)
{
    mapper_device dev = (mapper_device)ptr_jlong(d);
    mapper_signal sig = (mapper_signal)ptr_jlong(s);
    mdev_remove_input(dev, sig);
}

JNIEXPORT void JNICALL Java_Mapper_Device_mdev_1remove_1output
  (JNIEnv *env, jobject obj, jlong d, jlong s)
{
    mapper_device dev = (mapper_device)ptr_jlong(d);
    mapper_signal sig = (mapper_signal)ptr_jlong(s);
    mdev_remove_output(dev, sig);
}

JNIEXPORT jint JNICALL Java_Mapper_Device_mdev_1num_1inputs
  (JNIEnv *env, jobject obj, jlong d)
{
    mapper_device dev = (mapper_device)ptr_jlong(d);
    return mdev_num_inputs(dev);
}

JNIEXPORT jint JNICALL Java_Mapper_Device_mdev_1num_1outputs
  (JNIEnv *env, jobject obj, jlong d)
{
    mapper_device dev = (mapper_device)ptr_jlong(d);
    return mdev_num_outputs(dev);
}

JNIEXPORT jint JNICALL Java_Mapper_Device_mdev_1num_1links_1in
  (JNIEnv *env, jobject obj, jlong d)
{
    mapper_device dev = (mapper_device)ptr_jlong(d);
    return mdev_num_links_in(dev);
}

JNIEXPORT jint JNICALL Java_Mapper_Device_mdev_1num_1links_1out
  (JNIEnv *env, jobject obj, jlong d)
{
    mapper_device dev = (mapper_device)ptr_jlong(d);
    return mdev_num_links_out(dev);
}

JNIEXPORT jint JNICALL Java_Mapper_Device_mdev_1num_1connections_1in
  (JNIEnv *env, jobject obj, jlong d)
{
    mapper_device dev = (mapper_device)ptr_jlong(d);
    return mdev_num_connections_in(dev);
}

JNIEXPORT jint JNICALL Java_Mapper_Device_mdev_1num_1connections_1out
  (JNIEnv *env, jobject obj, jlong d)
{
    mapper_device dev = (mapper_device)ptr_jlong(d);
    return mdev_num_connections_out(dev);
}

JNIEXPORT jlong JNICALL Java_Mapper_Device_mdev_1get_1input_1by_1name
  (JNIEnv *env, jobject obj, jlong d, jstring name, jobject index)
{
    int i;
    mapper_device dev = (mapper_device)ptr_jlong(d);
    const char *cname = (*env)->GetStringUTFChars(env, name, 0);
    mapper_signal sig = mdev_get_input_by_name(dev, cname, &i);
    (*env)->ReleaseStringUTFChars(env, name, cname);

    if (sig && index) {
        jclass cls = (*env)->GetObjectClass(env, index);
        if (cls) {
            jfieldID val = (*env)->GetFieldID(env, cls, "value", "I");
            if (val)
                (*env)->SetIntField(env, index, val, i);
        }
    }

    return jlong_ptr(sig);
}

JNIEXPORT jlong JNICALL Java_Mapper_Device_mdev_1get_1output_1by_1name
  (JNIEnv *env, jobject obj, jlong d, jstring name, jobject index)
{
    int i;
    mapper_device dev = (mapper_device)ptr_jlong(d);
    const char *cname = (*env)->GetStringUTFChars(env, name, 0);
    mapper_signal sig = mdev_get_output_by_name(dev, cname, &i);
    (*env)->ReleaseStringUTFChars(env, name, cname);

    if (sig && index) {
        jclass cls = (*env)->GetObjectClass(env, index);
        if (cls) {
            jfieldID val = (*env)->GetFieldID(env, cls, "value", "I");
            if (val)
                (*env)->SetIntField(env, index, val, i);
        }
    }

    return jlong_ptr(sig);
}

JNIEXPORT jlong JNICALL Java_Mapper_Device_mdev_1get_1input_1by_1index
  (JNIEnv *env, jobject obj, jlong d, jint index)
{
    mapper_device dev = (mapper_device)ptr_jlong(d);
    mapper_signal sig = mdev_get_input_by_index(dev, index);
    return jlong_ptr(sig);
}

JNIEXPORT jlong JNICALL Java_Mapper_Device_mdev_1get_1output_1by_1index
  (JNIEnv *env, jobject obj, jlong d, jint index)
{
    mapper_device dev = (mapper_device)ptr_jlong(d);
    mapper_signal sig = mdev_get_output_by_index(dev, index);
    return jlong_ptr(sig);
}

JNIEXPORT jobject JNICALL Java_Mapper_Device_properties
  (JNIEnv *env, jobject obj)
{
    mapper_device dev = get_device_from_jobject(env, obj);
    if (!dev) return 0;

    // Create a wrapper class for this device's properties
    mapper_db_device props = mdev_properties(dev);
    jclass cls = (*env)->FindClass(env, "Mapper/Db/Device");
    if (!cls) return 0;

    jmethodID mid = (*env)->GetMethodID(env, cls, "<init>", "(J)V");
    jobject devdbobj = (*env)->NewObject(env, cls, mid,
                                         jlong_ptr(props));
    return devdbobj;
}

JNIEXPORT void JNICALL Java_Mapper_Device_mdev_1set_1property
  (JNIEnv *env, jobject obj, jlong d, jstring key, jobject value)
{
    mapper_device dev = (mapper_device)ptr_jlong(d);
    const char *ckey = (*env)->GetStringUTFChars(env, key, 0);
    jclass cls = (*env)->GetObjectClass(env, value);
    if (cls) {
        jfieldID typeid = (*env)->GetFieldID(env, cls, "type", "C");
        jfieldID lengthid = (*env)->GetFieldID(env, cls, "length", "I");
        if (typeid && lengthid) {
            char type;
            int length = 1;
            void *propval = 0;
            jfieldID valf = 0;
            jobject o;
            type = (*env)->GetCharField(env, value, typeid);
            length = (*env)->GetIntField(env, value, lengthid);
            switch (type)
            {
                case 'i':
                    valf = (*env)->GetFieldID(env, cls, "_i", "[I");
                    o = (*env)->GetObjectField(env, value, valf);
                    propval = (*env)->GetIntArrayElements(env, o, NULL);
                    mdev_set_property(dev, ckey, type, propval, length);
                    (*env)->ReleaseIntArrayElements(env, o, propval, JNI_ABORT);
                    break;
                case 'f':
                    valf = (*env)->GetFieldID(env, cls, "_f", "[F");
                    o = (*env)->GetObjectField(env, value, valf);
                    propval = (*env)->GetFloatArrayElements(env, o, NULL);
                    mdev_set_property(dev, ckey, type, propval, length);
                    (*env)->ReleaseFloatArrayElements(env, o, propval, JNI_ABORT);
                    break;
                case 'd':
                    valf = (*env)->GetFieldID(env, cls, "_d", "[D");
                    o = (*env)->GetObjectField(env, value, valf);
                    propval = (*env)->GetDoubleArrayElements(env, o, NULL);
                    mdev_set_property(dev, ckey, type, propval, length);
                    (*env)->ReleaseDoubleArrayElements(env, o, propval, JNI_ABORT);
                    break;
                case 's':
                case 'S':
                    valf = (*env)->GetFieldID(env, cls, "_s", "[Ljava/lang/String;");
                    o = (*env)->GetObjectField(env, value, valf);
                    // need to unpack string array and rebuild
                    jstring jstrings[length];
                    const char *cstrings[length];
                    int i;
                    for (i = 0; i < length; i++) {
                        jstrings[i] = (jstring) (*env)->GetObjectArrayElement(env, o, i);
                        cstrings[i] = (*env)->GetStringUTFChars(env, jstrings[i], 0);
                    }
                    if (length == 1)
                        mdev_set_property(dev, ckey, type, (void*)&cstrings[0], 1);
                    else
                        mdev_set_property(dev, ckey, type, (void*)&cstrings, length);
                    for (i = 0; i < length; i++)
                        (*env)->ReleaseStringUTFChars(env, jstrings[i], cstrings[i]);
                    break;
            }
        }
    }
    (*env)->ReleaseStringUTFChars(env, key, ckey);
}

JNIEXPORT void JNICALL Java_Mapper_Device_mdev_1remove_1property
  (JNIEnv *env, jobject obj, jlong d, jstring key)
{
    mapper_device dev = (mapper_device)ptr_jlong(d);
    const char *ckey = (*env)->GetStringUTFChars(env, key, 0);
    mdev_remove_property(dev, ckey);
    (*env)->ReleaseStringUTFChars(env, key, ckey);
}

JNIEXPORT jboolean JNICALL Java_Mapper_Device_mdev_1ready
  (JNIEnv *env, jobject obj, jlong d)
{
    mapper_device dev = (mapper_device)ptr_jlong(d);
    return mdev_ready(dev);
}

JNIEXPORT jstring JNICALL Java_Mapper_Device_mdev_1name
  (JNIEnv *env, jobject obj, jlong d)
{
    mapper_device dev = (mapper_device)ptr_jlong(d);
    const char *n = mdev_name(dev);
    if (n)
        return (*env)->NewStringUTF(env, n);
    return 0;
}

JNIEXPORT jint JNICALL Java_Mapper_Device_mdev_1port
  (JNIEnv *env, jobject obj, jlong d)
{
    mapper_device dev = (mapper_device)ptr_jlong(d);
    return mdev_port(dev);
}

JNIEXPORT jstring JNICALL Java_Mapper_Device_mdev_1ip4
  (JNIEnv *env, jobject obj, jlong d)
{
    mapper_device dev = (mapper_device)ptr_jlong(d);
    const struct in_addr* a = mdev_ip4(dev);
    if (a)
        return (*env)->NewStringUTF(env, inet_ntoa(*a));
    return 0;
}

JNIEXPORT jstring JNICALL Java_Mapper_Device_mdev_1interface
  (JNIEnv *env, jobject obj, jlong d)
{
    mapper_device dev = (mapper_device)ptr_jlong(d);
    const char *iface = mdev_interface(dev);
    if (iface)
        return (*env)->NewStringUTF(env, iface);
    return 0;
}

JNIEXPORT jint JNICALL Java_Mapper_Device_mdev_1ordinal
  (JNIEnv *env, jobject obj, jlong d)
{
    mapper_device dev = (mapper_device)ptr_jlong(d);
    return mdev_ordinal(dev);
}

JNIEXPORT jint JNICALL Java_Mapper_Device_mdev_1id
  (JNIEnv *env, jobject obj, jlong d)
{
    mapper_device dev = (mapper_device)ptr_jlong(d);
    return mdev_id(dev);
}

JNIEXPORT void JNICALL Java_Mapper_Device_mdev_1start_1queue
  (JNIEnv *env, jobject obj, jlong d, jobject objtt)
{
    mapper_device dev = (mapper_device)ptr_jlong(d);

    mapper_timetag_t tt, *ptt=0;
    ptt = get_timetag_from_jobject(env, objtt, &tt);
    if (ptt)
        mdev_start_queue(dev, *ptt);
}

JNIEXPORT void JNICALL Java_Mapper_Device_mdev_1send_1queue
  (JNIEnv *env, jobject obj, jlong d, jobject objtt)
{
    mapper_device dev = (mapper_device)ptr_jlong(d);

    mapper_timetag_t tt, *ptt=0;
    ptt = get_timetag_from_jobject(env, objtt, &tt);
    if (ptt)
        mdev_send_queue(dev, *ptt);
}

JNIEXPORT jobject JNICALL Java_Mapper_Device_mdev_1now
  (JNIEnv *env, jobject obj, jlong p)
{
    mapper_device dev = (mapper_device)ptr_jlong(p);
    mapper_timetag_t tt;
    mdev_now(dev, &tt);
    jobject o = get_jobject_from_timetag(genv, &tt);
    return o;
}

/**** Mapper.Device.Signal ****/

JNIEXPORT jstring JNICALL Java_Mapper_Device_00024Signal_msig_1full_1name
  (JNIEnv *env, jobject obj, jlong s)
{
    mapper_signal sig=(mapper_signal)ptr_jlong(s);
    char str[1024];

    if (sig) {
        msig_full_name(sig, str, 1024);
        return (*env)->NewStringUTF(env, str);
    }
    else
        return 0;
}

JNIEXPORT jstring JNICALL Java_Mapper_Device_00024Signal_msig_1name
  (JNIEnv *env, jobject obj, jlong s)
{
    mapper_signal sig=(mapper_signal)ptr_jlong(s);

    if (sig) {
        mapper_db_signal p = msig_properties(sig);
        return (*env)->NewStringUTF(env, p->name);
    }
    else
        return 0;
}

JNIEXPORT jboolean JNICALL Java_Mapper_Device_00024Signal_msig_1is_1output
  (JNIEnv *env, jobject obj, jlong s)
{
    mapper_signal sig=(mapper_signal)ptr_jlong(s);
    if (sig) {
        mapper_db_signal p = msig_properties(sig);
        return p->is_output;
    }
    return 0;
}

JNIEXPORT void JNICALL Java_Mapper_Device_00024Signal_msig_1set_1rate
  (JNIEnv *env, jobject obj, jlong s, jdouble rate)
{
    mapper_signal sig=(mapper_signal)ptr_jlong(s);
    if (sig)
        msig_set_rate(sig, (float)rate);
}

JNIEXPORT jint JNICALL Java_Mapper_Device_00024Signal_queryRemotes
  (JNIEnv *env, jobject obj, jobject objtt)
{
    mapper_signal sig = get_signal_from_jobject(env, obj);
    if (!sig)
        return 0;
    if (objtt)
    {
        mapper_timetag_t tt, *ptt;
        ptt = get_timetag_from_jobject(env, objtt, &tt);
        return msig_query_remotes(sig, ptt ? *ptt : MAPPER_NOW);
    }
    else
        return msig_query_remotes(sig, MAPPER_NOW);
}

JNIEXPORT jobject JNICALL Java_Mapper_Device_00024Signal_properties
  (JNIEnv *env, jobject obj)
{
    mapper_signal sig = get_signal_from_jobject(env, obj);
    if (!sig) return 0;
    mapper_db_signal props = msig_properties(sig);
    msig_jni_context ctx = (msig_jni_context)props->user_data;
    return ctx->db_signal;
}

JNIEXPORT void JNICALL Java_Mapper_Device_00024Signal_msig_1set_1property
  (JNIEnv *env, jobject obj, jlong s, jstring key, jobject value)
{
    mapper_signal sig = (mapper_signal)ptr_jlong(s);
    const char *ckey = (*env)->GetStringUTFChars(env, key, 0);
    jclass cls = (*env)->GetObjectClass(env, value);
    if (cls) {
        jfieldID typeid = (*env)->GetFieldID(env, cls, "type", "C");
        jfieldID lengthid = (*env)->GetFieldID(env, cls, "length", "I");
        if (typeid && lengthid) {
            char type;
            int length;
            void *propval = 0;
            jfieldID valf = 0;
            jobject o;
            type = (*env)->GetCharField(env, value, typeid);
            length = (*env)->GetIntField(env, value, lengthid);
            switch (type)
            {
                case 'i':
                    valf = (*env)->GetFieldID(env, cls, "_i", "[I");
                    o = (*env)->GetObjectField(env, value, valf);
                    propval = (*env)->GetIntArrayElements(env, o, NULL);
                    msig_set_property(sig, ckey, type, propval, length);
                    (*env)->ReleaseIntArrayElements(env, o, propval, JNI_ABORT);
                    break;
                case 'f':
                    valf = (*env)->GetFieldID(env, cls, "_f", "[F");
                    o = (*env)->GetObjectField(env, value, valf);
                    propval = (*env)->GetFloatArrayElements(env, o, NULL);
                    msig_set_property(sig, ckey, type, propval, length);
                    (*env)->ReleaseFloatArrayElements(env, o, propval, JNI_ABORT);
                    break;
                case 'd':
                    valf = (*env)->GetFieldID(env, cls, "_d", "[D");
                    o = (*env)->GetObjectField(env, value, valf);
                    propval = (*env)->GetDoubleArrayElements(env, o, NULL);
                    msig_set_property(sig, ckey, type, propval, length);
                    (*env)->ReleaseDoubleArrayElements(env, o, propval, JNI_ABORT);
                    break;
                case 's':
                case 'S':
                    valf = (*env)->GetFieldID(env, cls, "_s", "[Ljava/lang/String;");
                    o = (*env)->GetObjectField(env, value, valf);
                    jstring jstrings[length];
                    const char *cstrings[length];
                    int i;
                    for (i = 0; i < length; i++) {
                        jstrings[i] = (jstring) (*env)->GetObjectArrayElement(env, o, i);
                        cstrings[i] = (*env)->GetStringUTFChars(env, jstrings[i], 0);
                    }
                    if (length == 1)
                        msig_set_property(sig, ckey, type, (void*)&cstrings[0], 1);
                    else
                        msig_set_property(sig, ckey, type, (void*)&cstrings, length);
                    for (i = 0; i < length; i++)
                        (*env)->ReleaseStringUTFChars(env, jstrings[i], cstrings[i]);
                    break;
            }
        }
    }
    (*env)->ReleaseStringUTFChars(env, key, ckey);
}

JNIEXPORT void JNICALL Java_Mapper_Device_00024Signal_msig_1remove_1property
  (JNIEnv *env, jobject obj, jlong s, jstring key)
{
    mapper_signal sig = (mapper_signal)ptr_jlong(s);
    const char *ckey = (*env)->GetStringUTFChars(env, key, 0);
    msig_remove_property(sig, ckey);
    (*env)->ReleaseStringUTFChars(env, key, ckey);
}

JNIEXPORT void JNICALL Java_Mapper_Device_00024Signal_updateInstance__IILMapper_TimeTag_2
  (JNIEnv *env, jobject obj, jint id, jint value, jobject objtt)
{
    mapper_signal sig = get_signal_from_jobject(env, obj);
    if (!sig) return;

    mapper_db_signal props = msig_properties(sig);
    if (props->length != 1) {
        throwIllegalArgumentLength(env, sig, 1);
        return;
    }

    mapper_timetag_t tt, *ptt;
    ptt = get_timetag_from_jobject(env, objtt, &tt);

    if (props->type == 'i')
        msig_update_instance(sig, id, &value, 1, ptt ? *ptt : MAPPER_NOW);
    else if (props->type == 'f') {
        float v = (float)value;
        msig_update_instance(sig, id, &v, 1, ptt ? *ptt : MAPPER_NOW);
    }
    else if (props->type == 'd') {
        double v = (double)value;
        msig_update_instance(sig, id, &v, 1, ptt ? *ptt : MAPPER_NOW);
    }
}

JNIEXPORT void JNICALL Java_Mapper_Device_00024Signal_updateInstance__IFLMapper_TimeTag_2
  (JNIEnv *env, jobject obj, jint id, jfloat value, jobject objtt)
{
    mapper_signal sig = get_signal_from_jobject(env, obj);
    if (!sig) return;

    mapper_db_signal props = msig_properties(sig);
    if (props->length != 1) {
        throwIllegalArgumentLength(env, sig, 1);
        return;
    }

    mapper_timetag_t tt, *ptt;
    ptt = get_timetag_from_jobject(env, objtt, &tt);

    if (props->type == 'i')
        throwIllegalArgumentTruncate(env, sig);
    else if (props->type == 'f') {
        msig_update_instance(sig, id, &value, 1, ptt ? *ptt : MAPPER_NOW);
    }
    else if (props->type == 'd') {
        double v = (double)value;
        msig_update_instance(sig, id, &v, 1, ptt ? *ptt : MAPPER_NOW);
    }
}

JNIEXPORT void JNICALL Java_Mapper_Device_00024Signal_updateInstance__IDLMapper_TimeTag_2
  (JNIEnv *env, jobject obj, jint id, jdouble value, jobject objtt)
{
    mapper_signal sig = get_signal_from_jobject(env, obj);
    if (!sig) return;

    mapper_db_signal props = msig_properties(sig);
    if (props->length != 1) {
        throwIllegalArgumentLength(env, sig, 1);
        return;
    }

    mapper_timetag_t tt, *ptt;
    ptt = get_timetag_from_jobject(env, objtt, &tt);

    if (props->type == 'i')
        throwIllegalArgumentTruncate(env, sig);
    else if (props->type == 'f') {
        float v = (float)value;
        msig_update_instance(sig, id, &v, 1, ptt ? *ptt : MAPPER_NOW);
    }
    else if (props->type == 'd') {
        msig_update_instance(sig, id, &value, 1, ptt ? *ptt : MAPPER_NOW);
    }
}

JNIEXPORT void JNICALL Java_Mapper_Device_00024Signal_updateInstance__I_3ILMapper_TimeTag_2
  (JNIEnv *env, jobject obj, jint id, jintArray value, jobject objtt)
{
    mapper_signal sig = get_signal_from_jobject(env, obj);
    if (!sig) return;

    mapper_db_signal props = msig_properties(sig);
    int length = (*env)->GetArrayLength(env, value);
    if (length != props->length) {
        throwIllegalArgumentLength(env, sig, length);
        return;
    }

    mapper_timetag_t tt, *ptt;
    ptt = get_timetag_from_jobject(env, objtt, &tt);

    jint *array = (*env)->GetIntArrayElements(env, value, 0);
    if (array) {
        if (props->type == 'i') {
            msig_update(sig, array, 0, MAPPER_NOW);
        }
        else if (props->type == 'f') {
            float *arraycopy = malloc(sizeof(float)*length);
            int i;
            for (i=0; i<length; i++)
                arraycopy[i] = (float)array[i];
            msig_update_instance(sig, id, arraycopy, 0,
                                 ptt ? *ptt : MAPPER_NOW);
            free(arraycopy);
        }
        else if (props->type == 'd') {
            double *arraycopy = malloc(sizeof(double)*length);
            int i;
            for (i=0; i<length; i++)
                arraycopy[i] = (double)array[i];
            msig_update_instance(sig, id, arraycopy, 0,
                                 ptt ? *ptt : MAPPER_NOW);
            free(arraycopy);
        }
        (*env)->ReleaseIntArrayElements(env, value, array, JNI_ABORT);
    }
}

JNIEXPORT void JNICALL Java_Mapper_Device_00024Signal_updateInstance__I_3FLMapper_TimeTag_2
  (JNIEnv *env, jobject obj, jint id, jfloatArray value, jobject objtt)
{
    mapper_signal sig = get_signal_from_jobject(env, obj);
    if (!sig) return;

    mapper_db_signal props = msig_properties(sig);
    int length = (*env)->GetArrayLength(env, value);
    if (length != props->length) {
        throwIllegalArgumentLength(env, sig, length);
        return;
    }
    if (props->type == 'i') {
        throwIllegalArgumentTruncate(env, sig);
        return;
    }

    mapper_timetag_t tt, *ptt;
    ptt = get_timetag_from_jobject(env, objtt, &tt);

    jfloat *array = (*env)->GetFloatArrayElements(env, value, 0);
    if (array) {
        if (props->type == 'f') {
            msig_update_instance(sig, id, array, 0, ptt ? *ptt : MAPPER_NOW);
        }
        else {
            double *arraycopy = malloc(sizeof(double)*length);
            int i;
            for (i=0; i<length; i++)
                arraycopy[i] = (double)array[i];
            msig_update_instance(sig, id, arraycopy, 0,
                                 ptt ? *ptt : MAPPER_NOW);
            free(arraycopy);
        }
        (*env)->ReleaseFloatArrayElements(env, value, array, JNI_ABORT);
    }
}

JNIEXPORT void JNICALL Java_Mapper_Device_00024Signal_updateInstance__I_3DLMapper_TimeTag_2
  (JNIEnv *env, jobject obj, jint id, jdoubleArray value, jobject objtt)
{
    mapper_signal sig = get_signal_from_jobject(env, obj);
    if (!sig) return;

    mapper_db_signal props = msig_properties(sig);
    int length = (*env)->GetArrayLength(env, value);
    if (length != props->length) {
        throwIllegalArgumentLength(env, sig, length);
        return;
    }
    if (props->type == 'i') {
        throwIllegalArgumentTruncate(env, sig);
        return;
    }

    mapper_timetag_t tt, *ptt;
    ptt = get_timetag_from_jobject(env, objtt, &tt);

    jdouble *array = (*env)->GetDoubleArrayElements(env, value, 0);
    if (array) {
        if (props->type == 'd') {
            msig_update_instance(sig, id, array, 0, ptt ? *ptt : MAPPER_NOW);
        }
        else if (props->type == 'f') {
            float *arraycopy = malloc(sizeof(float)*length);
            int i;
            for (i=0; i<length; i++)
                arraycopy[i] = (float)array[i];
            msig_update_instance(sig, id, arraycopy, 0,
                                 ptt ? *ptt : MAPPER_NOW);
            free(arraycopy);
        }
        (*env)->ReleaseDoubleArrayElements(env, value, array, JNI_ABORT);
    }
}

JNIEXPORT jboolean JNICALL Java_Mapper_Device_00024Signal_instanceValue__I_3ILMapper_TimeTag_2
  (JNIEnv *env, jobject obj, jint id, jintArray ar, jobject objtt)
{
    mapper_signal sig = get_signal_from_jobject(env, obj);
    if (!sig) return JNI_FALSE;
    mapper_db_signal props = msig_properties(sig);
    mapper_timetag_t tt;

    int length = (*env)->GetArrayLength(env, ar);
    if (length < props->length) {
        throwIllegalArgumentLength(env, sig, length);
        return JNI_FALSE;
    }
    if (props->type != 'i') {
        throwIllegalArgumentTruncate(env, sig);
        return JNI_FALSE;
    }

    int *value = msig_instance_value(sig, id, &tt);
    if (!value)
        return JNI_FALSE;

    jint *array = (*env)->GetIntArrayElements(env, ar, 0);
    if (array) {
        int i;
        for (i=0; i < props->length; i++)
            array[i] = (jint)value[i];
        (*env)->ReleaseIntArrayElements(env, ar, array, 0);
    }

    if (objtt) {
        jclass cls = (*env)->GetObjectClass(env, objtt);
        if (cls) {
            jfieldID sec = (*env)->GetFieldID(env, cls, "sec", "J");
            jfieldID frac = (*env)->GetFieldID(env, cls, "frac", "J");
            if (sec && frac) {
                (*env)->SetLongField(env, objtt, sec, tt.sec);
                (*env)->SetLongField(env, objtt, frac, tt.frac);
            }
        }
    }

    return JNI_TRUE;
}

JNIEXPORT jboolean JNICALL Java_Mapper_Device_00024Signal_instanceValue__I_3FLMapper_TimeTag_2
  (JNIEnv *env, jobject obj, jint id, jfloatArray ar, jobject objtt)
{
    mapper_signal sig = get_signal_from_jobject(env, obj);
    if (!sig) return JNI_FALSE;
    mapper_db_signal props = msig_properties(sig);
    mapper_timetag_t tt;

    int length = (*env)->GetArrayLength(env, ar);
    if (length < props->length) {
        throwIllegalArgumentLength(env, sig, length);
        return JNI_FALSE;
    }

    jfloat *array = (*env)->GetFloatArrayElements(env, ar, 0);
    if (array)
    {
        int i;
        switch (props->type)
        {
            case 'i': {
                int *value = msig_instance_value(sig, id, &tt);
                if (!value) {
                    (*env)->ReleaseFloatArrayElements(env, ar, array, JNI_ABORT);
                    return JNI_FALSE;
                }
                for (i=0; i < props->length; i++)
                    array[i] = (jfloat)value[i];
            } break;

            case 'f': {
                float *value = msig_instance_value(sig, id, &tt);
                if (!value) {
                    (*env)->ReleaseFloatArrayElements(env, ar, array, JNI_ABORT);
                    return JNI_FALSE;
                }
                for (i=0; i < props->length; i++)
                    array[i] = (jfloat)value[i];
            } break;

            case 'd': {
                double *value = msig_instance_value(sig, id, &tt);
                if (!value) {
                    (*env)->ReleaseFloatArrayElements(env, ar, array, JNI_ABORT);
                    return JNI_FALSE;
                }
                for (i=0; i < props->length; i++)
                    array[i] = (jfloat)value[i];
            } break;
        }

        (*env)->ReleaseFloatArrayElements(env, ar, array, 0);
    }

    if (objtt) {
        jclass cls = (*env)->GetObjectClass(env, objtt);
        if (cls) {
            jfieldID sec = (*env)->GetFieldID(env, cls, "sec", "J");
            jfieldID frac = (*env)->GetFieldID(env, cls, "frac", "J");
            if (sec && frac) {
                (*env)->SetLongField(env, objtt, sec, tt.sec);
                (*env)->SetLongField(env, objtt, frac, tt.frac);
            }
        }
    }

    return JNI_TRUE;
}

JNIEXPORT jboolean JNICALL Java_Mapper_Device_00024Signal_instanceValue__I_3DLMapper_TimeTag_2
  (JNIEnv *env, jobject obj, jint id, jdoubleArray ar, jobject objtt)
{
    mapper_signal sig = get_signal_from_jobject(env, obj);
    if (!sig) return JNI_FALSE;
    mapper_db_signal props = msig_properties(sig);
    mapper_timetag_t tt;

    int length = (*env)->GetArrayLength(env, ar);
    if (length < props->length) {
        throwIllegalArgumentLength(env, sig, length);
        return JNI_FALSE;
    }

    jdouble *array = (*env)->GetDoubleArrayElements(env, ar, 0);
    if (array)
    {
        int i;
        switch (props->type)
        {
            case 'i': {
                int *value = msig_instance_value(sig, id, &tt);
                if (!value) {
                    (*env)->ReleaseDoubleArrayElements(env, ar, array, JNI_ABORT);
                    return JNI_FALSE;
                }
                for (i=0; i < props->length; i++)
                    array[i] = (jdouble)value[i];
            } break;

            case 'f': {
                float *value = msig_instance_value(sig, id, &tt);
                if (!value) {
                    (*env)->ReleaseDoubleArrayElements(env, ar, array, JNI_ABORT);
                    return JNI_FALSE;
                }
                for (i=0; i < props->length; i++)
                    array[i] = (jdouble)value[i];
            } break;

            case 'd': {
                double *value = msig_instance_value(sig, id, &tt);
                if (!value) {
                    (*env)->ReleaseDoubleArrayElements(env, ar, array, JNI_ABORT);
                    return JNI_FALSE;
                }
                for (i=0; i < props->length; i++)
                    array[i] = (jdouble)value[i];
            } break;
        }

        (*env)->ReleaseDoubleArrayElements(env, ar, array, 0);
    }

    if (objtt) {
        jclass cls = (*env)->GetObjectClass(env, objtt);
        if (cls) {
            jfieldID sec = (*env)->GetFieldID(env, cls, "sec", "J");
            jfieldID frac = (*env)->GetFieldID(env, cls, "frac", "J");
            if (sec && frac) {
                (*env)->SetLongField(env, objtt, sec, tt.sec);
                (*env)->SetLongField(env, objtt, frac, tt.frac);
            }
        }
    }

    return JNI_TRUE;
}

JNIEXPORT void JNICALL Java_Mapper_Device_00024Signal_setInstanceEventCallback
  (JNIEnv *env, jobject obj, jobject handler, jint flags)
{
    mapper_signal sig = get_signal_from_jobject(env, obj);
    if (!sig) return;

    mapper_db_signal props = msig_properties(sig);
    msig_jni_context ctx = (msig_jni_context)props->user_data;

    if (ctx->instanceHandler)
        (*env)->DeleteGlobalRef(env, ctx->instanceHandler);
    if (handler) {
        ctx->instanceHandler = (*env)->NewGlobalRef(env, handler);
        msig_set_instance_event_callback(sig,
                                         java_msig_instance_event_cb,
                                         flags, ctx);
    }
    else {
        ctx->instanceHandler = 0;
        msig_set_instance_event_callback(sig, 0, flags, 0);
    }
}

JNIEXPORT void JNICALL Java_Mapper_Device_00024Signal_setCallback
  (JNIEnv *env, jobject obj, jobject handler)
{
    mapper_signal sig = get_signal_from_jobject(env, obj);
    if (!sig) return;

    mapper_db_signal props = msig_properties(sig);
    msig_jni_context ctx = (msig_jni_context)props->user_data;
    if (ctx->listener)
        (*env)->DeleteGlobalRef(env, ctx->listener);
    if (handler) {
        ctx->listener = (*env)->NewGlobalRef(env, handler);
        msig_set_callback(sig, java_msig_input_cb, ctx);
    }
    else {
        ctx->listener = 0;
        msig_set_callback(sig, 0, 0);
    }
}

JNIEXPORT void JNICALL Java_Mapper_Device_00024Signal_setInstanceCallback
  (JNIEnv *env, jobject obj, jint instance_id, jobject data)
{
    mapper_signal sig = get_signal_from_jobject(env, obj);
    if (!sig)
        return;
    jobject prev = (jobject)msig_get_instance_data(sig, instance_id);
    if (prev)
        (*env)->DeleteGlobalRef(env, prev);

    // Note that msig_set_instance_data() can trigger the instance
    // event callback, so we need to use the global to pass the
    // environment to the handler.
    genv = env;
    bailing = 0;

    msig_set_instance_data(sig, instance_id,
                           data ? (*env)->NewGlobalRef(env, data) : 0);
}

JNIEXPORT jobject JNICALL Java_Mapper_Device_00024Signal_getInstanceCallback
  (JNIEnv *env, jobject obj, jint instance_id)
{
    mapper_signal sig = get_signal_from_jobject(env, obj);
    if (!sig)
        return 0;
    return (jobject)msig_get_instance_data(sig, instance_id);
}

JNIEXPORT jint JNICALL Java_Mapper_Device_00024Signal_reserveInstances
  (JNIEnv *env, jobject obj, jintArray ids, jint num, jobject cb)
{
    mapper_signal sig = get_signal_from_jobject(env, obj);
    if (!sig)
        return 0;
    int i, reserved = 0;
    jobject ref = cb ? (*env)->NewGlobalRef(env, cb) : 0;
    if (ids) {
        int length = (*env)->GetArrayLength(env, ids);
        jint *array = (*env)->GetIntArrayElements(env, ids, 0);
        for (i = 0; i < length; i++) {
            int id = (int)array[i];
            reserved += msig_reserve_instances(sig, 1, &id, ref ? (void **)&ref : 0);
        }
        (*env)->ReleaseIntArrayElements(env, ids, array, JNI_ABORT);
        return reserved;
    }
    else {
        for (i = 0; i < num; i++) {
            reserved += msig_reserve_instances(sig, 1, 0, (void **)&ref);
        }
        return reserved;
    }
    return 0;
}

JNIEXPORT void JNICALL Java_Mapper_Device_00024Signal_releaseInstance
  (JNIEnv *env, jobject obj, jint instance_id, jobject objtt)
{
    mapper_signal sig = get_signal_from_jobject(env, obj);
    if (!sig) return;
    mapper_timetag_t tt, *ptt=0;
    ptt = get_timetag_from_jobject(env, objtt, &tt);
    msig_release_instance(sig, instance_id, ptt ? *ptt : MAPPER_NOW);
}

JNIEXPORT void JNICALL Java_Mapper_Device_00024Signal_removeInstance
  (JNIEnv *env, jobject obj, jint instance_id)
{
    mapper_signal sig = get_signal_from_jobject(env, obj);
    if (!sig) return;
    msig_remove_instance(sig, instance_id);
}

JNIEXPORT jobject JNICALL Java_Mapper_Device_00024Signal_oldestActiveInstance
  (JNIEnv *env, jobject obj)
{
    mapper_signal sig = get_signal_from_jobject(env, obj);
    if (!sig) return 0;
    int i, r = msig_get_oldest_active_instance(sig, &i);
    jobject iobj = 0;
    if (r == 0) {
        jclass cls = (*env)->FindClass(env, "java/lang/Integer");
        if (cls) {
            jmethodID cons = (*env)->GetMethodID(env, cls,
                                                 "<init>", "(I)V");
            if (cons) {
                iobj = (*env)->NewObject(env, cls, cons, i);
                return iobj;
            }
        }
    }
    return 0;
}

JNIEXPORT jobject JNICALL Java_Mapper_Device_00024Signal_newestActiveInstance
  (JNIEnv *env, jobject obj)
{
    mapper_signal sig = get_signal_from_jobject(env, obj);
    if (!sig) return 0;
    int i, r = msig_get_newest_active_instance(sig, &i);
    jobject iobj = 0;
    if (r == 0) {
        jclass cls = (*env)->FindClass(env, "java/lang/Integer");
        if (cls) {
            jmethodID cons = (*env)->GetMethodID(env, cls,
                                                 "<init>", "(I)V");
            if (cons) {
                iobj = (*env)->NewObject(env, cls, cons, i);
                return iobj;
            }
        }
    }
    return 0;
}

JNIEXPORT void JNICALL Java_Mapper_Device_00024Signal_match_1instances
  (JNIEnv *env, jobject obj, jobject from, jobject to, jint instance_id)
{
    mapper_signal sigfrom = get_signal_from_jobject(env, from);
    mapper_signal sigto = get_signal_from_jobject(env, to);
    if (!sigto || !sigfrom) return;

    msig_match_instances(sigfrom, sigto, instance_id);
}

JNIEXPORT jint JNICALL Java_Mapper_Device_00024Signal_numActiveInstances
  (JNIEnv *env, jobject obj)
{
    mapper_signal sig = get_signal_from_jobject(env, obj);
    if (!sig) return 0;
    return msig_num_active_instances(sig);
}

JNIEXPORT jint JNICALL Java_Mapper_Device_00024Signal_numReservedInstances
  (JNIEnv *env, jobject obj)
{
    mapper_signal sig = get_signal_from_jobject(env, obj);
    if (!sig) return 0;
    return msig_num_reserved_instances(sig);
}

JNIEXPORT jint JNICALL Java_Mapper_Device_00024Signal_activeInstanceId
  (JNIEnv *env, jobject obj, jint index)
{
    mapper_signal sig = get_signal_from_jobject(env, obj);
    if (!sig) return 0;
    return msig_active_instance_id(sig, index);
}

JNIEXPORT void JNICALL Java_Mapper_Device_00024Signal_setInstanceAllocationMode
  (JNIEnv *env, jobject obj, jint mode)
{
    mapper_signal sig = get_signal_from_jobject(env, obj);
    if (!sig) return;
    msig_set_instance_allocation_mode(sig, mode);
}

JNIEXPORT jint JNICALL Java_Mapper_Device_00024Signal_instanceAllocationMode
  (JNIEnv *env, jobject obj)
{
    mapper_signal sig = get_signal_from_jobject(env, obj);
    if (!sig) return 0;
    return msig_get_instance_allocation_mode(sig);
}

JNIEXPORT jint JNICALL Java_Mapper_Device_00024Signal_numConnections
  (JNIEnv *env, jobject obj)
{
    mapper_signal sig = get_signal_from_jobject(env, obj);
    if (!sig) return 0;
    return msig_num_connections(sig);
}

/**** Mapper.Monitor ****/

JNIEXPORT jlong JNICALL Java_Mapper_Monitor_mmon_1new
  (JNIEnv *env, jobject obj, jint autosubscribe_flags)
{
    mapper_monitor mon = mapper_monitor_new(0, autosubscribe_flags);
    return jlong_ptr(mon);
}

JNIEXPORT jlong JNICALL Java_Mapper_Monitor_mmon_1get_1db
(JNIEnv *env, jobject obj, jlong p)
{
    mapper_monitor mon = (mapper_monitor)ptr_jlong(p);
    mapper_db db = mapper_monitor_get_db(mon);
    return jlong_ptr(db);
}

JNIEXPORT void JNICALL Java_Mapper_Monitor_mmon_1free
  (JNIEnv *env, jobject obj, jlong p)
{
    mapper_monitor mon = (mapper_monitor)ptr_jlong(p);
    mapper_monitor_free(mon);
}

JNIEXPORT jint JNICALL Java_Mapper_Monitor_mmon_1poll
  (JNIEnv *env, jobject obj, jlong d, jint timeout)
{
    genv = env;
    bailing = 0;
    mapper_monitor mon = (mapper_monitor)ptr_jlong(d);
    return mapper_monitor_poll(mon, timeout);
}

JNIEXPORT void JNICALL Java_Mapper_Monitor_mmon_1subscribe
  (JNIEnv *env, jobject obj, jlong p, jstring name, jint flags, jint timeout)
{
    mapper_monitor mon = (mapper_monitor)ptr_jlong(p);
    const char *cname = (*env)->GetStringUTFChars(env, name, 0);
    mapper_monitor_subscribe(mon, cname, flags, timeout);
    (*env)->ReleaseStringUTFChars(env, name, cname);
}

JNIEXPORT void JNICALL Java_Mapper_Monitor_mmon_1unsubscribe
  (JNIEnv *env, jobject obj, jlong p, jstring name)
{
    mapper_monitor mon = (mapper_monitor)ptr_jlong(p);
    const char *cname = (*env)->GetStringUTFChars(env, name, 0);
    mapper_monitor_unsubscribe(mon, cname);
    (*env)->ReleaseStringUTFChars(env, name, cname);
}

JNIEXPORT void JNICALL Java_Mapper_Monitor_mmon_1link
  (JNIEnv *env, jobject obj, jlong p, jstring src_name, jstring dest_name,
   jobject link_props)
{
    mapper_monitor mon = (mapper_monitor)ptr_jlong(p);
    const char *csrc_name = (*env)->GetStringUTFChars(env, src_name, 0);
    const char *cdest_name = (*env)->GetStringUTFChars(env, dest_name, 0);
// TODO: process props!
    mapper_monitor_link(mon, csrc_name, cdest_name, 0, 0);
    (*env)->ReleaseStringUTFChars(env, src_name, csrc_name);
    (*env)->ReleaseStringUTFChars(env, dest_name, cdest_name);
}

JNIEXPORT void JNICALL Java_Mapper_Monitor_mmon_1unlink
  (JNIEnv *env, jobject obj, jlong p, jstring src_name, jstring dest_name)
{
    mapper_monitor mon = (mapper_monitor)ptr_jlong(p);
    const char *csrc_name = (*env)->GetStringUTFChars(env, src_name, 0);
    const char *cdest_name = (*env)->GetStringUTFChars(env, dest_name, 0);
    mapper_monitor_unlink(mon, csrc_name, cdest_name);
    (*env)->ReleaseStringUTFChars(env, src_name, csrc_name);
    (*env)->ReleaseStringUTFChars(env, dest_name, cdest_name);
}

JNIEXPORT void JNICALL Java_Mapper_Monitor_mmon_1connect_1or_1mod
  (JNIEnv *env, jobject obj, jlong p, jstring src_name, jstring dest_name,
   jobject jprops, jint modify)
{
    mapper_monitor mon = (mapper_monitor)ptr_jlong(p);
    const char *csrc_name = (*env)->GetStringUTFChars(env, src_name, 0);
    const char *cdest_name = (*env)->GetStringUTFChars(env, dest_name, 0);

    // process props
    // don't bother letting user define signal types or lengths (will be overwritten)
    mapper_db_connection_t cprops;
    jstring expr_jstr = 0;
    int src_length = 0, dest_length = 0;
    char src_type = 0, dest_type = 0;
    jobject src_min_field = NULL, src_max_field = NULL;
    jobject dest_min_field = NULL, dest_max_field = NULL;
    // TODO: "extra" props
    int props_flags = 0;
    if (jprops) {
        jclass cls = (*env)->GetObjectClass(env, jprops);
        if (cls) {
            // mode
            jfieldID fid = (*env)->GetFieldID(env, cls, "mode", "I");
            if (fid) {
                cprops.mode = (*env)->GetIntField(env, jprops, fid);
                if ((int)cprops.mode >= 0)
                    props_flags |= CONNECTION_MODE;
            }
            // expression
            fid = (*env)->GetFieldID(env, cls, "expression", "Ljava/lang/String;");
            if (fid) {
                expr_jstr = (*env)->GetObjectField(env, jprops, fid);
                if (expr_jstr) {
                    cprops.expression = (char*)(*env)->GetStringUTFChars(env, expr_jstr, 0);
                    props_flags |= CONNECTION_EXPRESSION;
                }
            }
            // bound_min
            fid = (*env)->GetFieldID(env, cls, "boundMin", "I");
            if (fid) {
                cprops.bound_min = (*env)->GetIntField(env, jprops, fid);
                if ((int)cprops.bound_min >= 0)
                    props_flags |= CONNECTION_BOUND_MIN;
            }
            // bound_max
            fid = (*env)->GetFieldID(env, cls, "boundMax", "I");
            if (fid) {
                cprops.bound_max = (*env)->GetIntField(env, jprops, fid);
                if ((int)cprops.bound_max >= 0)
                    props_flags |= CONNECTION_BOUND_MAX;
            }
            // src_min
            fid = (*env)->GetFieldID(env, cls, "srcMin", "LMapper/PropertyValue;");
            if (fid) {
                src_min_field = (*env)->GetObjectField(env, jprops, fid);
                if (src_min_field) {
                    src_length = get_PropertyValue_elements(env, src_min_field,
                                                            &cprops.src_min,
                                                            &src_type);
                    if (src_length) {
                        cprops.src_length = src_length;
                        cprops.src_type = src_type;
                        props_flags |= (CONNECTION_RANGE_SRC_MIN
                                        | CONNECTION_SRC_LENGTH
                                        | CONNECTION_SRC_TYPE);
                    }
                }
            }
            // src_max
            fid = (*env)->GetFieldID(env, cls, "srcMax", "LMapper/PropertyValue;");
            if (fid) {
                src_max_field = (*env)->GetObjectField(env, jprops, fid);
                if (src_max_field) {
                    char type;
                    int length = get_PropertyValue_elements(env, src_max_field,
                                                            &cprops.src_max,
                                                            &type);
                    if (length) {
                        // check if length and type match, abort or cast otherwise
                        if (src_length && length != src_length)
                            printf("differing lengths for src!\n");
                        else if (src_type && type != src_type)
                            printf("differing types for src!\n");
                        else {
                            cprops.src_length = length;
                            cprops.src_type = type;
                            props_flags |= (CONNECTION_RANGE_SRC_MAX
                                            | CONNECTION_SRC_LENGTH
                                            | CONNECTION_SRC_TYPE);
                        }
                    }
                }
            }
            // dest_min
            fid = (*env)->GetFieldID(env, cls, "destMin", "LMapper/PropertyValue;");
            if (fid) {
                dest_min_field = (*env)->GetObjectField(env, jprops, fid);
                if (dest_min_field) {
                    dest_length = get_PropertyValue_elements(env, dest_min_field,
                                                             &cprops.dest_min,
                                                             &dest_type);
                    if (dest_length) {
                        cprops.dest_length = dest_length;
                        cprops.dest_type = dest_type;
                        props_flags |= (CONNECTION_RANGE_DEST_MIN
                                        | CONNECTION_DEST_LENGTH
                                        | CONNECTION_DEST_TYPE);
                    }
                }
            }
            // dest_max
            fid = (*env)->GetFieldID(env, cls, "destMax", "LMapper/PropertyValue;");
            if (fid) {
                dest_max_field = (*env)->GetObjectField(env, jprops, fid);
                if (dest_max_field) {
                    char type;
                    int length = get_PropertyValue_elements(env, dest_max_field,
                                                            &cprops.dest_max,
                                                            &type);
                    if (length) {
                        if (dest_length && length != dest_length)
                            printf("differing lengths for dest!\n");
                        else if (dest_type && type != dest_type)
                            printf("differing types for dest!\n");
                        else {
                            // check if length and type match, abort or cast otherwise
                            cprops.dest_length = length;
                            cprops.dest_type = type;
                            props_flags |= (CONNECTION_RANGE_DEST_MAX
                                            | CONNECTION_DEST_LENGTH
                                            | CONNECTION_DEST_TYPE);
                        }
                    }
                }
            }
        }
    }

    if (modify)
        mapper_monitor_connection_modify(mon, csrc_name, cdest_name,
                                         &cprops, props_flags);
    else
        mapper_monitor_connect(mon, csrc_name, cdest_name, &cprops, props_flags);
    (*env)->ReleaseStringUTFChars(env, src_name, csrc_name);
    (*env)->ReleaseStringUTFChars(env, dest_name, cdest_name);
    if (expr_jstr)
        (*env)->ReleaseStringUTFChars(env, expr_jstr, cprops.expression);
    if (src_min_field)
        release_PropertyValue_elements(env, src_min_field, cprops.src_min);
    if (src_max_field)
        release_PropertyValue_elements(env, src_max_field, cprops.src_max);
    if (dest_min_field)
        release_PropertyValue_elements(env, dest_min_field, cprops.dest_min);
    if (dest_max_field)
        release_PropertyValue_elements(env, dest_max_field, cprops.dest_max);
}

JNIEXPORT void JNICALL Java_Mapper_Monitor_mmon_1disconnect
  (JNIEnv *env, jobject obj, jlong p, jstring src_name, jstring dest_name)
{
    mapper_monitor mon = (mapper_monitor)ptr_jlong(p);
    const char *csrc_name = (*env)->GetStringUTFChars(env, src_name, 0);
    const char *cdest_name = (*env)->GetStringUTFChars(env, dest_name, 0);
    mapper_monitor_disconnect(mon, csrc_name, cdest_name);
    (*env)->ReleaseStringUTFChars(env, src_name, csrc_name);
    (*env)->ReleaseStringUTFChars(env, dest_name, cdest_name);
}

JNIEXPORT void JNICALL Java_Mapper_Monitor_mmon_1autosubscribe
  (JNIEnv *env, jobject obj, jlong p, jint flags)
{
    mapper_monitor mon = (mapper_monitor)ptr_jlong(p);
    mapper_monitor_autosubscribe(mon, flags);
}

JNIEXPORT jobject JNICALL Java_Mapper_Monitor_mmon_1now
  (JNIEnv *env, jobject obj, jlong p)
{
    mapper_monitor mon = (mapper_monitor)ptr_jlong(p);
    mapper_timetag_t tt;
    mapper_monitor_now(mon, &tt);
    jobject o = get_jobject_from_timetag(genv, &tt);
    return o;
}

/**** Mapper.Monitor.Db ****/

static void java_db_device_cb(mapper_db_device record,
                              mapper_db_action_t action,
                              void *user_data)
{
    if (bailing || !user_data)
        return;

    // Create a wrapper class for the device properties
    jclass cls = (*genv)->FindClass(genv, "Mapper/Db/Device");
    if (!cls)
    return;
    jmethodID mid = (*genv)->GetMethodID(genv, cls, "<init>", "(J)V");
    jobject devdbobj = (*genv)->NewObject(genv, cls, mid, jlong_ptr(record));

    jobject obj = (jobject)user_data;
    cls = (*genv)->GetObjectClass(genv, user_data);
    if (cls) {
        mid = (*genv)->GetMethodID(genv, cls, "onEvent",
                                   "(LMapper/Db/Device;I)V");
        if (mid) {
            (*genv)->CallVoidMethod(genv, obj, mid, devdbobj, action);
            if ((*genv)->ExceptionOccurred(genv))
                bailing = 1;
        }
        else {
            printf("Did not successfully look up onEvent method.\n");
        }
    }
}

static void java_db_signal_cb(mapper_db_signal record,
                              mapper_db_action_t action,
                              void *user_data)
{
    if (bailing || !user_data)
        return;

    // Create a wrapper class for the signal properties
    jclass cls = (*genv)->FindClass(genv, "Mapper/Db/Signal");
    if (!cls)
        return;

    jmethodID mid = (*genv)->GetMethodID(genv, cls, "<init>", "(J)V");
    jobject sigdbobj = (*genv)->NewObject(genv, cls, mid, jlong_ptr(record));

    jobject obj = (jobject)user_data;
    cls = (*genv)->GetObjectClass(genv, user_data);
    if (cls) {
        mid = (*genv)->GetMethodID(genv, cls, "onEvent",
                                   "(LMapper/Db/Signal;I)V");
        if (mid) {
            (*genv)->CallVoidMethod(genv, obj, mid, sigdbobj, action);
            if ((*genv)->ExceptionOccurred(genv))
                bailing = 1;
        }
        else {
            printf("Did not successfully look up onEvent method.\n");
        }
    }
}

static void java_db_link_cb(mapper_db_link record,
                            mapper_db_action_t action,
                            void *user_data)
{
    if (bailing || !user_data)
    return;

    // Create a wrapper class for the link properties
    jclass cls = (*genv)->FindClass(genv, "Mapper/Db/Link");
    if (!cls)
        return;

    jmethodID mid = (*genv)->GetMethodID(genv, cls, "<init>", "(J)V");
    jobject linkdbobj = (*genv)->NewObject(genv, cls, mid, jlong_ptr(record));

    jobject obj = (jobject)user_data;
    cls = (*genv)->GetObjectClass(genv, user_data);
    if (cls) {
        mid = (*genv)->GetMethodID(genv, cls, "onEvent",
                                   "(LMapper/Db/Link;I)V");
        if (mid) {
            (*genv)->CallVoidMethod(genv, obj, mid, linkdbobj, action);
            if ((*genv)->ExceptionOccurred(genv))
            bailing = 1;
        }
        else {
            printf("Did not successfully look up onEvent method.\n");
        }
    }
}

static void java_db_connection_cb(mapper_db_connection record,
                                  mapper_db_action_t action,
                                  void *user_data)
{
    if (bailing || !user_data)
        return;

    // Create a wrapper class for the connection properties
    jclass cls = (*genv)->FindClass(genv, "Mapper/Db/Connection");
    if (!cls)
        return;

    jmethodID mid = (*genv)->GetMethodID(genv, cls, "<init>", "(J)V");
    jobject condbobj = (*genv)->NewObject(genv, cls, mid, jlong_ptr(record));

    jobject obj = (jobject)user_data;
    cls = (*genv)->GetObjectClass(genv, user_data);
    if (cls) {
        mid = (*genv)->GetMethodID(genv, cls, "onEvent",
                                   "(LMapper/Db/Connection;I)V");
        if (mid) {
            (*genv)->CallVoidMethod(genv, obj, mid, condbobj, action);
            if ((*genv)->ExceptionOccurred(genv))
                bailing = 1;
        }
        else {
            printf("Did not successfully look up onEvent method.\n");
        }
    }
}

JNIEXPORT void JNICALL Java_Mapper_Monitor_00024Db_mdb_1add_1device_1callback
  (JNIEnv *env, jobject obj, jlong p, jobject listener)
{
    // TODO: check listener hasn't already been added
    mapper_db db = (mapper_db)ptr_jlong(p);
    if (!db || !listener)
        return;
    jobject o = (*env)->NewGlobalRef(env, listener);
    mapper_db_add_device_callback(db, java_db_device_cb, o);
}

JNIEXPORT void JNICALL Java_Mapper_Monitor_00024Db_mdb_1remove_1device_1callback
  (JNIEnv *env, jobject obj, jlong p, jobject listener)
{
    mapper_db db = (mapper_db)ptr_jlong(p);
    if (!db || !listener)
        return;
    mapper_db_remove_device_callback(db, java_db_device_cb, listener);
    (*env)->DeleteGlobalRef(env, listener);
}

JNIEXPORT void JNICALL Java_Mapper_Monitor_00024Db_mdb_1add_1signal_1callback
  (JNIEnv *env, jobject obj, jlong p, jobject listener)
{
    // TODO: check listener hasn't already been added
    mapper_db db = (mapper_db)ptr_jlong(p);
    if (!db || !listener)
        return;
    jobject o = (*env)->NewGlobalRef(env, listener);
    mapper_db_add_signal_callback(db, java_db_signal_cb, o);
}

JNIEXPORT void JNICALL Java_Mapper_Monitor_00024Db_mdb_1remove_1signal_1callback
  (JNIEnv *env, jobject obj, jlong p, jobject listener)
{
    mapper_db db = (mapper_db)ptr_jlong(p);
    if (!db || !listener)
        return;
    mapper_db_remove_signal_callback(db, java_db_signal_cb, ptr_jlong(listener));
    (*env)->DeleteGlobalRef(env, listener);
}

JNIEXPORT void JNICALL Java_Mapper_Monitor_00024Db_mdb_1add_1link_1callback
  (JNIEnv *env, jobject obj, jlong p, jobject listener)
{
    // TODO: check listener hasn't already been added
    mapper_db db = (mapper_db)ptr_jlong(p);
    if (!db || !listener)
        return;

    jobject o = (*env)->NewGlobalRef(env, listener);
    mapper_db_add_link_callback(db, java_db_link_cb, o);
}

JNIEXPORT void JNICALL Java_Mapper_Monitor_00024Db_mdb_1remove_1link_1callback
  (JNIEnv *env, jobject obj, jlong p, jobject listener)
{
    mapper_db db = (mapper_db)ptr_jlong(p);
    if (!db || !listener)
        return;
    mapper_db_remove_link_callback(db, java_db_link_cb, ptr_jlong(listener));
    (*env)->DeleteGlobalRef(env, listener);
}

JNIEXPORT void JNICALL Java_Mapper_Monitor_00024Db_mdb_1add_1connection_1callback
  (JNIEnv *env, jobject obj, jlong p, jobject listener)
{
    // TODO: check listener hasn't already been added
    mapper_db db = (mapper_db)ptr_jlong(p);
    if (!db || !listener)
        return;

    jobject o = (*env)->NewGlobalRef(env, listener);
    mapper_db_add_connection_callback(db, java_db_connection_cb, o);
}

JNIEXPORT void JNICALL Java_Mapper_Monitor_00024Db_mdb_1remove_1connection_1callback
  (JNIEnv *env, jobject obj, jlong p, jobject listener)
{
    mapper_db db = (mapper_db)ptr_jlong(p);
    if (!db || !listener)
        return;
    mapper_db_remove_connection_callback(db, java_db_connection_cb,
                                         ptr_jlong(listener));
    (*env)->DeleteGlobalRef(env, listener);
}

/**** Mapper.Db.Device ****/

JNIEXPORT jstring JNICALL Java_Mapper_Db_Device_mdb_1device_1get_1name
  (JNIEnv *env, jobject obj, jlong p)
{
    mapper_db_device props = (mapper_db_device)ptr_jlong(p);
    return (*env)->NewStringUTF(env, props->name);
}

JNIEXPORT jint JNICALL Java_Mapper_Db_Device_mdb_1device_1get_1ordinal
  (JNIEnv *env, jobject obj, jlong p)
{
    mapper_db_device props = (mapper_db_device)ptr_jlong(p);
    return props->ordinal;
}

JNIEXPORT jstring JNICALL Java_Mapper_Db_Device_mdb_1device_1get_1host
  (JNIEnv *env, jobject obj, jlong p)
{
    mapper_db_device props = (mapper_db_device)ptr_jlong(p);
    return (*env)->NewStringUTF(env, props->host);
}

JNIEXPORT jint JNICALL Java_Mapper_Db_Device_mdb_1device_1get_1port
  (JNIEnv *env, jobject obj, jlong p)
{
    mapper_db_device props = (mapper_db_device)ptr_jlong(p);
    return props->port;
}

JNIEXPORT jint JNICALL Java_Mapper_Db_Device_mdb_1device_1get_1num_1inputs
  (JNIEnv *env, jobject obj, jlong p)
{
    mapper_db_device props = (mapper_db_device)ptr_jlong(p);
    return props->num_inputs;
}

JNIEXPORT jint JNICALL Java_Mapper_Db_Device_mdb_1device_1get_1num_1outputs
  (JNIEnv *env, jobject obj, jlong p)
{
    mapper_db_device props = (mapper_db_device)ptr_jlong(p);
    return props->num_outputs;
}

JNIEXPORT jint JNICALL Java_Mapper_Db_Device_mdb_1device_1get_1num_1links_1in
  (JNIEnv *env, jobject obj, jlong p)
{
    mapper_db_device props = (mapper_db_device)ptr_jlong(p);
    return props->num_links_in;
}

JNIEXPORT jint JNICALL Java_Mapper_Db_Device_mdb_1device_1get_1num_1links_1out
  (JNIEnv *env, jobject obj, jlong p)
{
    mapper_db_device props = (mapper_db_device)ptr_jlong(p);
    return props->num_links_out;
}

JNIEXPORT jint JNICALL Java_Mapper_Db_Device_mdb_1device_1get_1num_1connections_1in
  (JNIEnv *env, jobject obj, jlong p)
{
    mapper_db_device props = (mapper_db_device)ptr_jlong(p);
    return props->num_connections_in;
}

JNIEXPORT jint JNICALL Java_Mapper_Db_Device_mdb_1device_1get_1num_1connections_1out
  (JNIEnv *env, jobject obj, jlong p)
{
    mapper_db_device props = (mapper_db_device)ptr_jlong(p);
    return props->num_connections_out;
}

JNIEXPORT jint JNICALL Java_Mapper_Db_Device_mdb_1device_1get_1version
  (JNIEnv *env, jobject obj, jlong p)
{
    mapper_db_device props = (mapper_db_device)ptr_jlong(p);
    return props->version;
}

JNIEXPORT jobject JNICALL Java_Mapper_Db_Device_mdb_1device_1get_1synced
  (JNIEnv *env, jobject obj, jlong p)
{
    mapper_db_device props = (mapper_db_device)ptr_jlong(p);
    jobject o = get_jobject_from_timetag(genv, &props->synced);
    return o;
}

JNIEXPORT jobject JNICALL Java_Mapper_Db_Device_mdb_1device_1property_1lookup
  (JNIEnv *env, jobject obj, jlong p, jstring property)
{
    mapper_db_device props = (mapper_db_device)ptr_jlong(p);
    const char *cprop = (*env)->GetStringUTFChars(env, property, 0);
    char type;
    int length;
    const void *value;
    jobject o = 0;

    if (!mapper_db_device_property_lookup(props, cprop, &type, &value, &length))
        o = build_PropertyValue(env, type, value, length);

    (*env)->ReleaseStringUTFChars(env, property, cprop);
    return o;
}

JNIEXPORT jobject JNICALL Java_Mapper_Monitor_00024Db_devices
  (JNIEnv *env, jobject obj)
{
    mapper_db db = get_db_from_jobject(env, obj);
    if (!db) return 0;

    mapper_db_device *devs = mapper_db_get_all_devices(db);
    if (!devs) return 0;

    jclass cls = (*env)->FindClass(env, "Mapper/Db/DeviceCollection");
    if (!cls) return 0;

    jmethodID mid = (*env)->GetMethodID(env, cls, "<init>", "(J)V");
    return (*env)->NewObject(env, cls, mid, jlong_ptr(devs));
}

JNIEXPORT jobject JNICALL Java_Mapper_Monitor_00024Db_get_1device
  (JNIEnv *env, jobject obj, jstring s)
{
    mapper_db db = get_db_from_jobject(env, obj);
    if (!db) return 0;

    const char *name = (*env)->GetStringUTFChars(env, s, 0);
    mapper_db_device dev = mapper_db_get_device_by_name(db, name);
    (*env)->ReleaseStringUTFChars(env, s, name);
    if (!dev) return 0;

    jclass cls = (*env)->FindClass(env, "Mapper/Db/Device");
    if (!cls) return 0;

    jmethodID mid = (*env)->GetMethodID(env, cls, "<init>", "(J)V");
    return (*env)->NewObject(env, cls, mid, jlong_ptr(dev));
}

JNIEXPORT jobject JNICALL Java_Mapper_Monitor_00024Db_match_1devices
  (JNIEnv *env, jobject obj, jstring s)
{
    mapper_db db = get_db_from_jobject(env, obj);
    if (!db) return 0;

    const char *pattern = (*env)->GetStringUTFChars(env, s, 0);
    mapper_db_device *devs = mapper_db_match_devices_by_name(db, pattern);
    (*env)->ReleaseStringUTFChars(env, s, pattern);
    if (!devs) return 0;

    jclass cls = (*env)->FindClass(env, "Mapper/Db/DeviceCollection");
    if (!cls) return 0;

    jmethodID mid = (*env)->GetMethodID(env, cls, "<init>", "(J)V");
    return (*env)->NewObject(env, cls, mid, jlong_ptr(devs));
}

/**** Mapper.Db.Device.Iterator ****/

JNIEXPORT jlong JNICALL Java_Mapper_Db_DeviceIterator_mdb_1deref
  (JNIEnv *env, jobject obj, jlong p)
{
    void **ptr = (void**)ptr_jlong(p);
    return jlong_ptr(*ptr);
}

JNIEXPORT jlong JNICALL Java_Mapper_Db_DeviceIterator_mdb_1device_1next
  (JNIEnv *env, jobject obj, jlong p)
{
    mapper_db_device_t **devs = (mapper_db_device_t**)ptr_jlong(p);
    return jlong_ptr(mapper_db_device_next(devs));
}

JNIEXPORT void JNICALL Java_Mapper_Db_DeviceIterator_mdb_1device_1done
(JNIEnv *env, jobject obj, jlong p)
{
    mapper_db_device_t **devs = (mapper_db_device_t**)ptr_jlong(p);
    if (devs)
        mapper_db_device_done(devs);
}

/**** Mapper.Db.Signal ****/

JNIEXPORT jstring JNICALL Java_Mapper_Db_Signal_mdb_1signal_1get_1name
  (JNIEnv *env, jobject obj, jlong p)
{
    mapper_db_signal props = (mapper_db_signal)ptr_jlong(p);
    return (*env)->NewStringUTF(env, props->name);
}

JNIEXPORT jstring JNICALL Java_Mapper_Db_Signal_mdb_1signal_1get_1device_1name
  (JNIEnv *env, jobject obj, jlong p)
{
    mapper_db_signal props = (mapper_db_signal)ptr_jlong(p);
    return (*env)->NewStringUTF(env, props->device_name);
}

JNIEXPORT jboolean JNICALL Java_Mapper_Db_Signal_mdb_1signal_1get_1is_1output
  (JNIEnv *env, jobject obj, jlong p)
{
    mapper_db_signal props = (mapper_db_signal)ptr_jlong(p);
    return props->is_output!=0;
}

JNIEXPORT jchar JNICALL Java_Mapper_Db_Signal_mdb_1signal_1get_1type
  (JNIEnv *env, jobject obj, jlong p)
{
    mapper_db_signal props = (mapper_db_signal)ptr_jlong(p);
    return props->type;
}

JNIEXPORT jint JNICALL Java_Mapper_Db_Signal_mdb_1signal_1get_1length
  (JNIEnv *env, jobject obj, jlong p)
{
    mapper_db_signal props = (mapper_db_signal)ptr_jlong(p);
    return props->length;
}

JNIEXPORT jstring JNICALL Java_Mapper_Db_Signal_mdb_1signal_1get_1unit
  (JNIEnv *env, jobject obj, jlong p)
{
    mapper_db_signal props = (mapper_db_signal)ptr_jlong(p);
    return (*env)->NewStringUTF(env, props->unit);
}

JNIEXPORT jobject JNICALL Java_Mapper_Db_Signal_mdb_1signal_1get_1minimum
  (JNIEnv *env, jobject obj, jlong p)
{
    mapper_db_signal props = (mapper_db_signal)ptr_jlong(p);

    if (!props->minimum)
        return 0;
    return build_PropertyValue(env, props->type, props->minimum, props->length);
}

JNIEXPORT jobject JNICALL Java_Mapper_Db_Signal_mdb_1signal_1get_1maximum
  (JNIEnv *env, jobject obj, jlong p)
{
    mapper_db_signal props = (mapper_db_signal)ptr_jlong(p);

    if (!props->maximum)
        return 0;
    return build_PropertyValue(env, props->type, props->maximum, props->length);
}

JNIEXPORT jdouble JNICALL Java_Mapper_Db_Signal_mdb_1signal_1get_1rate
  (JNIEnv *env, jobject obj, jlong p)
{
    mapper_db_signal props = (mapper_db_signal)ptr_jlong(p);
    return (jdouble)props->rate;
}

JNIEXPORT jobject JNICALL Java_Mapper_Db_Signal_mdb_1signal_1property_1lookup
  (JNIEnv *env, jobject obj, jlong p, jstring property)
{
    mapper_db_signal props = (mapper_db_signal)ptr_jlong(p);
    const char *cprop = (*env)->GetStringUTFChars(env, property, 0);
    char type;
    int length;
    const void *value;
    jobject o = 0;

    if (!mapper_db_signal_property_lookup(props, cprop, &type, &value, &length))
        o = build_PropertyValue(env, type, value, length);

    (*env)->ReleaseStringUTFChars(env, property, cprop);
    return o;
}

JNIEXPORT jlong JNICALL Java_Mapper_Monitor_00024Db_mdb_1inputs
  (JNIEnv *env, jobject obj, jlong p, jstring s)
{
    mapper_db db = (mapper_db)ptr_jlong(p);
    mapper_db_signal *sigs;
    if (s) {
        const char *name = (*env)->GetStringUTFChars(env, s, 0);
        sigs = mapper_db_get_inputs_by_device_name(db, name);
        (*env)->ReleaseStringUTFChars(env, s, name);
    }
    else {
        sigs = mapper_db_get_all_inputs(db);
    }
    return jlong_ptr(sigs);
}

JNIEXPORT jobject JNICALL Java_Mapper_Monitor_00024Db_getInput
  (JNIEnv *env, jobject obj, jstring s1, jstring s2)
{
    mapper_db db = get_db_from_jobject(env, obj);
    if (!db) return 0;

    const char *dev_name = (*env)->GetStringUTFChars(env, s1, 0);
    const char *sig_name = (*env)->GetStringUTFChars(env, s2, 0);
    mapper_db_signal sig =
        mapper_db_get_input_by_device_and_signal_names(db, dev_name, sig_name);
    (*env)->ReleaseStringUTFChars(env, s1, dev_name);
    (*env)->ReleaseStringUTFChars(env, s2, sig_name);
    if (!sig) return 0;

    jclass cls = (*env)->FindClass(env, "Mapper/Db/Signal");
    if (!cls) return 0;

    jmethodID mid = (*env)->GetMethodID(env, cls, "<init>", "(J)V");
    return (*env)->NewObject(env, cls, mid, jlong_ptr(sig));
}

JNIEXPORT jobject JNICALL Java_Mapper_Monitor_00024Db_matchInputs
  (JNIEnv *env, jobject obj, jstring s1, jstring s2)
{
    mapper_db db = get_db_from_jobject(env, obj);
    if (!db) return 0;

    const char *dev_name = (*env)->GetStringUTFChars(env, s1, 0);
    const char *sig_pattern = (*env)->GetStringUTFChars(env, s2, 0);
    mapper_db_signal *sigs = mapper_db_match_inputs_by_device_name(db, dev_name,
                                                                   sig_pattern);
    (*env)->ReleaseStringUTFChars(env, s1, dev_name);
    (*env)->ReleaseStringUTFChars(env, s2, sig_pattern);
    if (!sigs) return 0;

    jclass cls = (*env)->FindClass(env, "Mapper/Db/SignalCollection");
    if (!cls) return 0;

    jmethodID mid = (*env)->GetMethodID(env, cls, "<init>", "(J)V");
    return (*env)->NewObject(env, cls, mid, jlong_ptr(sigs));
}

JNIEXPORT jlong JNICALL Java_Mapper_Monitor_00024Db_mdb_1outputs
(JNIEnv *env, jobject obj, jlong p, jstring s)
{
    mapper_db db = (mapper_db)ptr_jlong(p);
    mapper_db_signal *sigs;
    if (s) {
        const char *name = (*env)->GetStringUTFChars(env, s, 0);
        sigs = mapper_db_get_outputs_by_device_name(db, name);
        (*env)->ReleaseStringUTFChars(env, s, name);
    }
    else {
        sigs = mapper_db_get_all_outputs(db);
    }
    return jlong_ptr(sigs);
}

JNIEXPORT jobject JNICALL Java_Mapper_Monitor_00024Db_getOutput
(JNIEnv *env, jobject obj, jstring s1, jstring s2)
{
    mapper_db db = get_db_from_jobject(env, obj);
    if (!db) return 0;

    const char *dev_name = (*env)->GetStringUTFChars(env, s1, 0);
    const char *sig_name = (*env)->GetStringUTFChars(env, s2, 0);
    mapper_db_signal sig =
    mapper_db_get_output_by_device_and_signal_names(db, dev_name, sig_name);
    (*env)->ReleaseStringUTFChars(env, s1, dev_name);
    (*env)->ReleaseStringUTFChars(env, s2, sig_name);
    if (!sig) return 0;

    jclass cls = (*env)->FindClass(env, "Mapper/Db/Signal");
    if (!cls) return 0;

    jmethodID mid = (*env)->GetMethodID(env, cls, "<init>", "(J)V");
    return (*env)->NewObject(env, cls, mid, jlong_ptr(sig));
}

JNIEXPORT jobject JNICALL Java_Mapper_Monitor_00024Db_matchOutputs
(JNIEnv *env, jobject obj, jstring s1, jstring s2)
{
    mapper_db db = get_db_from_jobject(env, obj);
    if (!db) return 0;

    const char *dev_name = (*env)->GetStringUTFChars(env, s1, 0);
    const char *sig_pattern = (*env)->GetStringUTFChars(env, s2, 0);
    mapper_db_signal *sigs = mapper_db_match_outputs_by_device_name(db, dev_name,
                                                                    sig_pattern);
    (*env)->ReleaseStringUTFChars(env, s1, dev_name);
    (*env)->ReleaseStringUTFChars(env, s2, sig_pattern);
    if (!sigs) return 0;

    jclass cls = (*env)->FindClass(env, "Mapper/Db/SignalCollection");
    if (!cls) return 0;

    jmethodID mid = (*env)->GetMethodID(env, cls, "<init>", "(J)V");
    return (*env)->NewObject(env, cls, mid, jlong_ptr(sigs));
}

/**** Mapper.Db.Signal.Iterator ****/

JNIEXPORT jlong JNICALL Java_Mapper_Db_SignalIterator_mdb_1deref
  (JNIEnv *env, jobject obj, jlong p)
{
    void **ptr = (void**)ptr_jlong(p);
    return jlong_ptr(*ptr);
}

JNIEXPORT jlong JNICALL Java_Mapper_Db_SignalIterator_mdb_1signal_1next
  (JNIEnv *env, jobject obj, jlong p)
{
    mapper_db_signal_t **sigs = (mapper_db_signal_t**)ptr_jlong(p);
    return jlong_ptr(mapper_db_signal_next(sigs));
}

JNIEXPORT void JNICALL Java_Mapper_Db_SignalIterator_mdb_1signal_1done
  (JNIEnv *env, jobject obj, jlong p)
{
    mapper_db_signal_t **sigs = (mapper_db_signal_t**)ptr_jlong(p);
    if (sigs)
        mapper_db_signal_done(sigs);
}

/**** Mapper.Db.Link ****/

JNIEXPORT jstring JNICALL Java_Mapper_Db_Link_mdb_1link_1get_1src_1name
  (JNIEnv *env, jobject obj, jlong p)
{
    mapper_db_link props = (mapper_db_link)ptr_jlong(p);
    return (*env)->NewStringUTF(env, props->src_name);
}

JNIEXPORT jstring JNICALL Java_Mapper_Db_Link_mdb_1link_1get_1dest_1name
  (JNIEnv *env, jobject obj, jlong p)
{
    mapper_db_link props = (mapper_db_link)ptr_jlong(p);
    return (*env)->NewStringUTF(env, props->dest_name);
}

JNIEXPORT jstring JNICALL Java_Mapper_Db_Link_mdb_1link_1get_1src_1host
  (JNIEnv *env, jobject obj, jlong p)
{
    mapper_db_link props = (mapper_db_link)ptr_jlong(p);
    return (*env)->NewStringUTF(env, props->src_host);
}

JNIEXPORT jint JNICALL Java_Mapper_Db_Link_mdb_1link_1get_1src_1port
  (JNIEnv *env, jobject obj, jlong p)
{
    mapper_db_link props = (mapper_db_link)ptr_jlong(p);
    return props->src_port;
}

JNIEXPORT jstring JNICALL Java_Mapper_Db_Link_mdb_1link_1get_1dest_1host
  (JNIEnv *env, jobject obj, jlong p)
{
    mapper_db_link props = (mapper_db_link)ptr_jlong(p);
    return (*env)->NewStringUTF(env, props->dest_host);
}

JNIEXPORT jint JNICALL Java_Mapper_Db_Link_mdb_1link_1get_1dest_1port
  (JNIEnv *env, jobject obj, jlong p)
{
    mapper_db_link props = (mapper_db_link)ptr_jlong(p);
    return props->dest_port;
}

JNIEXPORT jobject JNICALL Java_Mapper_Db_Link_mapper_1db_1link_1property_1lookup
  (JNIEnv *env, jobject obj, jlong p, jstring property)
{
    mapper_db_link props = (mapper_db_link)ptr_jlong(p);
    const char *cprop = (*env)->GetStringUTFChars(env, property, 0);
    char type;
    int length;
    const void *value;
    jobject o = 0;

    if (!mapper_db_link_property_lookup(props, cprop, &type, &value, &length))
        o = build_PropertyValue(env, type, value, length);

    (*env)->ReleaseStringUTFChars(env, property, cprop);
    return o;
}

JNIEXPORT jobject JNICALL Java_Mapper_Monitor_00024Db_getLink
  (JNIEnv *env, jobject obj, jstring s1, jstring s2)
{
    jobject linkobj = 0;
    mapper_db db = get_db_from_jobject(env, obj);
    if (!db) return 0;

    const char *src_name = (*env)->GetStringUTFChars(env, s1, 0);
    const char *dest_name = (*env)->GetStringUTFChars(env, s2, 0);
    mapper_db_link link = mapper_db_get_link_by_src_dest_names(db, src_name, dest_name);
    if (link) {;
        jclass cls = (*env)->FindClass(env, "Mapper/Db/Link");
        if (cls) {
            jmethodID mid = (*env)->GetMethodID(env, cls, "<init>", "(J)V");
            linkobj = (*env)->NewObject(env, cls, mid, jlong_ptr(link));
        }
    }
    (*env)->ReleaseStringUTFChars(env, s1, src_name);
    (*env)->ReleaseStringUTFChars(env, s2, dest_name);
    return linkobj;
}

JNIEXPORT jobject JNICALL Java_Mapper_Monitor_00024Db_mdb_1links
  (JNIEnv *env, jobject obj, jlong p, jstring s)
{
    mapper_db_link *links;
    mapper_db db = (mapper_db)ptr_jlong(p);
    if (!db) return 0;

    if (s) {
        const char *name = (*env)->GetStringUTFChars(env, s, 0);
        links = mapper_db_get_links_by_device_name(db, name);
        (*env)->ReleaseStringUTFChars(env, s, name);
    }
    else {
        links = mapper_db_get_all_links(db);
    }

    if (!links) return 0;

    jclass cls = (*env)->FindClass(env, "Mapper/Db/LinkCollection");
    if (!cls) return 0;

    jmethodID mid = (*env)->GetMethodID(env, cls, "<init>", "(J)V");
    jobject linksobj = (*env)->NewObject(env, cls, mid, jlong_ptr(links));
    return linksobj;
}

JNIEXPORT jobject JNICALL Java_Mapper_Monitor_00024Db_links
  (JNIEnv *env, jobject obj, jobject srcobj, jobject destobj)
{
    mapper_db_link *links;
    mapper_db db = get_db_from_jobject(env, obj);
    if (!db) return 0;

    // retrieve mapper_db_device* ptrs from DeviceCollection objects
    mapper_db_device *src = get_db_device_ptr_from_jobject(env, srcobj);
    mapper_db_device *dest = get_db_device_ptr_from_jobject(env, destobj);
    if (!src || !dest) return 0;

    links = mapper_db_get_links_by_src_dest_devices(db, src, dest);
    if (!links) return 0;

    jclass cls = (*env)->FindClass(env, "Mapper/Db/LinkCollection");
    if (!cls) return 0;

    jmethodID mid = (*env)->GetMethodID(env, cls, "<init>", "(J)V");
    jobject linksobj = (*env)->NewObject(env, cls, mid, jlong_ptr(links));
    return linksobj;
}

JNIEXPORT jobject JNICALL Java_Mapper_Monitor_00024Db_linksBySrc
  (JNIEnv *env, jobject obj, jstring s)
{
    mapper_db_link *links;
    mapper_db db = get_db_from_jobject(env, obj);
    if (!db) return 0;

    const char *name = (*env)->GetStringUTFChars(env, s, 0);
    links = mapper_db_get_links_by_src_device_name(db, name);
    (*env)->ReleaseStringUTFChars(env, s, name);

    if (!links) return 0;

    jclass cls = (*env)->FindClass(env, "Mapper/Db/LinkCollection");
    if (!cls) return 0;

    jmethodID mid = (*env)->GetMethodID(env, cls, "<init>", "(J)V");
    jobject linksobj = (*env)->NewObject(env, cls, mid, jlong_ptr(links));
    return linksobj;
}

JNIEXPORT jobject JNICALL Java_Mapper_Monitor_00024Db_linksByDest
  (JNIEnv *env, jobject obj, jstring s)
{
    mapper_db_link *links;
    mapper_db db = get_db_from_jobject(env, obj);
    if (!db) return 0;

    const char *name = (*env)->GetStringUTFChars(env, s, 0);
    links = mapper_db_get_links_by_dest_device_name(db, name);
    (*env)->ReleaseStringUTFChars(env, s, name);

    if (!links) return 0;

    jclass cls = (*env)->FindClass(env, "Mapper/Db/LinkCollection");
    if (!cls) return 0;

    jmethodID mid = (*env)->GetMethodID(env, cls, "<init>", "(J)V");
    jobject linksobj = (*env)->NewObject(env, cls, mid, jlong_ptr(links));
    return linksobj;
}

/**** Mapper.Db.Link.Iterator ****/

JNIEXPORT jlong JNICALL Java_Mapper_Db_LinkIterator_mdb_1deref
(JNIEnv *env, jobject obj, jlong p)
{
    void **ptr = (void**)ptr_jlong(p);
    return jlong_ptr(*ptr);
}

JNIEXPORT jlong JNICALL Java_Mapper_Db_LinkIterator_mdb_1link_1next
(JNIEnv *env, jobject obj, jlong p)
{
    mapper_db_link_t **links = (mapper_db_link_t**)ptr_jlong(p);
    return jlong_ptr(mapper_db_link_next(links));
}

JNIEXPORT void JNICALL Java_Mapper_Db_LinkIterator_mdb_1link_1done
(JNIEnv *env, jobject obj, jlong p)
{
    mapper_db_link_t **links = (mapper_db_link_t**)ptr_jlong(p);
    if (links)
        mapper_db_link_done(links);
}

/**** Mapper.Db.Connection ****/

JNIEXPORT jstring JNICALL Java_Mapper_Db_Connection_mdb_1connection_1get_1src_1name
  (JNIEnv *env, jobject obj, jlong p)
{
    mapper_db_connection props = (mapper_db_connection)ptr_jlong(p);
    return (*env)->NewStringUTF(env, props->src_name);
}

JNIEXPORT jstring JNICALL Java_Mapper_Db_Connection_mdb_1connection_1get_1dest_1name
  (JNIEnv *env, jobject obj, jlong p)
{
    mapper_db_connection props = (mapper_db_connection)ptr_jlong(p);
    return (*env)->NewStringUTF(env, props->dest_name);
}

JNIEXPORT jchar JNICALL Java_Mapper_Db_Connection_mdb_1connection_1get_1src_1type
  (JNIEnv *env, jobject obj, jlong p)
{
    mapper_db_connection props = (mapper_db_connection)ptr_jlong(p);
    return props->src_type;
}

JNIEXPORT jchar JNICALL Java_Mapper_Db_Connection_mdb_1connection_1get_1dest_1type
  (JNIEnv *env, jobject obj, jlong p)
{
    mapper_db_connection props = (mapper_db_connection)ptr_jlong(p);
    return props->dest_type;
}

JNIEXPORT jint JNICALL Java_Mapper_Db_Connection_mdb_1connection_1get_1src_1length
  (JNIEnv *env, jobject obj, jlong p)
{
    mapper_db_connection props = (mapper_db_connection)ptr_jlong(p);
    return props->src_length;
}

JNIEXPORT jint JNICALL Java_Mapper_Db_Connection_mdb_1connection_1get_1dest_1length
  (JNIEnv *env, jobject obj, jlong p)
{
    mapper_db_connection props = (mapper_db_connection)ptr_jlong(p);
    return props->dest_length;
}

JNIEXPORT jint JNICALL Java_Mapper_Db_Connection_mdb_1connection_1get_1bound_1min
  (JNIEnv *env, jobject obj, jlong p)
{
    mapper_db_connection props = (mapper_db_connection)ptr_jlong(p);
    return props->bound_min;
}

JNIEXPORT jint JNICALL Java_Mapper_Db_Connection_mdb_1connection_1get_1bound_1max
  (JNIEnv *env, jobject obj, jlong p)
{
    mapper_db_connection props = (mapper_db_connection)ptr_jlong(p);
    return props->bound_max;
}

JNIEXPORT jobject JNICALL Java_Mapper_Db_Connection_mdb_1connection_1get_1src_1min
  (JNIEnv *env, jobject obj, jlong p)
{
    mapper_db_connection props = (mapper_db_connection)ptr_jlong(p);

    if (!props->src_min)
        return 0;
    return build_PropertyValue(env, props->src_type, props->src_min, props->src_length);
}

JNIEXPORT jobject JNICALL Java_Mapper_Db_Connection_mdb_1connection_1get_1src_1max
  (JNIEnv *env, jobject obj, jlong p)
{
    mapper_db_connection props = (mapper_db_connection)ptr_jlong(p);

    if (!props->src_max)
        return 0;
    return build_PropertyValue(env, props->src_type, props->src_max, props->src_length);
}

JNIEXPORT jobject JNICALL Java_Mapper_Db_Connection_mdb_1connection_1get_1dest_1min
  (JNIEnv *env, jobject obj, jlong p)
{
    mapper_db_connection props = (mapper_db_connection)ptr_jlong(p);

    if (!props->dest_min)
        return 0;
    return build_PropertyValue(env, props->dest_type, props->dest_min, props->dest_length);
}

JNIEXPORT jobject JNICALL Java_Mapper_Db_Connection_mdb_1connection_1get_1dest_1max
  (JNIEnv *env, jobject obj, jlong p)
{
    mapper_db_connection props = (mapper_db_connection)ptr_jlong(p);

    if (!props->dest_max)
        return 0;
    return build_PropertyValue(env, props->dest_type, props->dest_max, props->dest_length);
}

JNIEXPORT jint JNICALL Java_Mapper_Db_Connection_mdb_1connection_1get_1mode
  (JNIEnv *env, jobject obj, jlong p)
{
    mapper_db_connection props = (mapper_db_connection)ptr_jlong(p);
    return props->mode;
}

JNIEXPORT jstring JNICALL Java_Mapper_Db_Connection_mdb_1connection_1get_1expression
  (JNIEnv *env, jobject obj, jlong p)
{
    mapper_db_connection props = (mapper_db_connection)ptr_jlong(p);
    if (props->expression)
        return (*env)->NewStringUTF(env, props->expression);
    return 0;
}

JNIEXPORT jint JNICALL Java_Mapper_Db_Connection_mdb_1connection_1get_1send_1as_1instance
(JNIEnv *env, jobject obj, jlong p)
{
    mapper_db_connection props = (mapper_db_connection)ptr_jlong(p);
    return props->send_as_instance;
}

JNIEXPORT jint JNICALL Java_Mapper_Db_Connection_mdb_1connection_1get_1num_1scopes
(JNIEnv *env, jobject obj, jlong p)
{
    mapper_db_connection props = (mapper_db_connection)ptr_jlong(p);
    return props->scope.size;
}

JNIEXPORT jobject JNICALL Java_Mapper_Db_Connection_mdb_1connection_1get_1scope_1names
(JNIEnv *env, jobject obj, jlong p)
{
    mapper_db_connection props = (mapper_db_connection)ptr_jlong(p);
    int size = props->scope.size;
    if (!size)
        return 0;
    return build_PropertyValue(env, 's', props->scope.names, size);
}

JNIEXPORT jobject JNICALL Java_Mapper_Db_Connection_mapper_1db_1connection_1property_1lookup
  (JNIEnv *env, jobject obj, jlong p, jstring property)
{
    mapper_db_connection props = (mapper_db_connection)ptr_jlong(p);
    const char *cprop = (*env)->GetStringUTFChars(env, property, 0);
    char type;
    int length;
    const void *value;
    jobject o = 0;

    if (!mapper_db_connection_property_lookup(props, cprop, &type, &value, &length))
        o = build_PropertyValue(env, type, value, length);

    (*env)->ReleaseStringUTFChars(env, property, cprop);
    return o;
}

JNIEXPORT jobject JNICALL Java_Mapper_Monitor_00024Db_mdb_1connections
  (JNIEnv *env, jobject obj, jlong p, jstring s)
{
    mapper_db_connection *cons;
    mapper_db db = (mapper_db)ptr_jlong(p);
    if (!db) return 0;

    if (s) {
        const char *name = (*env)->GetStringUTFChars(env, s, 0);
        cons = mapper_db_get_connections_by_device_name(db, name);
        (*env)->ReleaseStringUTFChars(env, s, name);
    }
    else {
        cons = mapper_db_get_all_connections(db);
    }

    if (!cons) return 0;

    jclass cls = (*env)->FindClass(env, "Mapper/Db/ConnectionCollection");
    if (!cls) return 0;

    jmethodID mid = (*env)->GetMethodID(env, cls, "<init>", "(J)V");
    jobject consobj = (*env)->NewObject(env, cls, mid, jlong_ptr(cons));
    return consobj;
}

JNIEXPORT jobject JNICALL Java_Mapper_Monitor_00024Db_connections
  (JNIEnv *env, jobject obj, jobject srcobj, jobject destobj)
{
    mapper_db_connection *cons;
    mapper_db db = get_db_from_jobject(env, obj);
    if (!db) return 0;

    // retrieve mapper_db_signal* ptrs from SignalCollection objects
    mapper_db_signal *src = get_db_signal_ptr_from_jobject(env, srcobj);
    mapper_db_signal *dest = get_db_signal_ptr_from_jobject(env, destobj);
    if (!src || !dest) return 0;

    cons = mapper_db_get_connections_by_signal_queries(db, src, dest);
    if (!cons) return 0;

    jclass cls = (*env)->FindClass(env, "Mapper/Db/ConnectionCollection");
    if (!cls) return 0;

    jmethodID mid = (*env)->GetMethodID(env, cls, "<init>", "(J)V");
    jobject consobj = (*env)->NewObject(env, cls, mid, jlong_ptr(cons));
    return consobj;
}

JNIEXPORT jobject JNICALL Java_Mapper_Monitor_00024Db_mdb_1connections_1by_1src
  (JNIEnv *env, jobject obj, jlong p, jstring s1, jstring s2)
{
    mapper_db_connection *cons;
    mapper_db db = get_db_from_jobject(env, obj);
    if (!db || !s2) return 0;

    const char *sig_name = (*env)->GetStringUTFChars(env, s2, 0);
    if (s1) {
        const char *dev_name = (*env)->GetStringUTFChars(env, s1, 0);
        cons = mapper_db_get_connections_by_src_device_and_signal_names(db, dev_name,
                                                                        sig_name);
        (*env)->ReleaseStringUTFChars(env, s1, dev_name);
    }
    else {
        cons = mapper_db_get_connections_by_src_signal_name(db, sig_name);
    }
    (*env)->ReleaseStringUTFChars(env, s2, sig_name);

    jclass cls = (*env)->FindClass(env, "Mapper/Db/ConnectionCollection");
    if (!cls) return 0;

    jmethodID mid = (*env)->GetMethodID(env, cls, "<init>", "(J)V");
    jobject consobj = (*env)->NewObject(env, cls, mid, jlong_ptr(cons));
    return consobj;
}

JNIEXPORT jobject JNICALL Java_Mapper_Monitor_00024Db_mdb_1connections_1by_1dest
  (JNIEnv *env, jobject obj, jlong p, jstring s1, jstring s2)
{
    mapper_db_connection *cons;
    mapper_db db = get_db_from_jobject(env, obj);
    if (!db || !s2) return 0;

    const char *sig_name = (*env)->GetStringUTFChars(env, s2, 0);
    if (s1) {
        const char *dev_name = (*env)->GetStringUTFChars(env, s1, 0);
        cons = mapper_db_get_connections_by_dest_device_and_signal_names(db, dev_name,
                                                                         sig_name);
        (*env)->ReleaseStringUTFChars(env, s1, dev_name);
    }
    else {
        cons = mapper_db_get_connections_by_dest_signal_name(db, sig_name);
    }
    (*env)->ReleaseStringUTFChars(env, s2, sig_name);

    jclass cls = (*env)->FindClass(env, "Mapper/Db/ConnectionCollection");
    if (!cls) return 0;

    jmethodID mid = (*env)->GetMethodID(env, cls, "<init>", "(J)V");
    jobject consobj = (*env)->NewObject(env, cls, mid, jlong_ptr(cons));
    return consobj;
}

JNIEXPORT jobject JNICALL Java_Mapper_Monitor_00024Db_connectionBySignals
  (JNIEnv *env, jobject obj, jstring s1, jstring s2)
{
    mapper_db_connection con;
    mapper_db db = get_db_from_jobject(env, obj);
    if (!db || !s1 || !s2) return 0;

    const char *src_name = (*env)->GetStringUTFChars(env, s1, 0);
    const char *dest_name = (*env)->GetStringUTFChars(env, s2, 0);
    con = mapper_db_get_connection_by_signal_full_names(db, src_name, dest_name);

    (*env)->ReleaseStringUTFChars(env, s1, src_name);
    (*env)->ReleaseStringUTFChars(env, s2, dest_name);

    jclass cls = (*env)->FindClass(env, "Mapper/Db/Connection");
    if (!cls) return 0;

    jmethodID mid = (*env)->GetMethodID(env, cls, "<init>", "(J)V");
    jobject conobj = (*env)->NewObject(env, cls, mid, jlong_ptr(con));
    return conobj;
}

JNIEXPORT jobject JNICALL Java_Mapper_Monitor_00024Db_connectionsByDevices
  (JNIEnv *env, jobject obj, jstring s1, jstring s2)
{
    mapper_db_connection *cons;
    mapper_db db = get_db_from_jobject(env, obj);
    if (!db || !s1 || !s2) return 0;

    const char *src_name = (*env)->GetStringUTFChars(env, s1, 0);
    const char *dest_name = (*env)->GetStringUTFChars(env, s2, 0);
    cons = mapper_db_get_connections_by_src_dest_device_names(db, src_name, dest_name);

    (*env)->ReleaseStringUTFChars(env, s1, src_name);
    (*env)->ReleaseStringUTFChars(env, s2, dest_name);

    jclass cls = (*env)->FindClass(env, "Mapper/Db/ConnectionCollection");
    if (!cls) return 0;

    jmethodID mid = (*env)->GetMethodID(env, cls, "<init>", "(J)V");
    jobject consobj = (*env)->NewObject(env, cls, mid, jlong_ptr(cons));
    return consobj;
}

/**** Mapper.Db.Connection.Iterator ****/

JNIEXPORT jlong JNICALL Java_Mapper_Db_ConnectionIterator_mdb_1deref
(JNIEnv *env, jobject obj, jlong p)
{
    void **ptr = (void**)ptr_jlong(p);
    return jlong_ptr(*ptr);
}

JNIEXPORT jlong JNICALL Java_Mapper_Db_ConnectionIterator_mdb_1connection_1next
(JNIEnv *env, jobject obj, jlong p)
{
    mapper_db_connection_t **connections = (mapper_db_connection_t**)ptr_jlong(p);
    return jlong_ptr(mapper_db_connection_next(connections));
}

JNIEXPORT void JNICALL Java_Mapper_Db_ConnectionIterator_mdb_1connection_1done
(JNIEnv *env, jobject obj, jlong p)
{
    mapper_db_connection_t **connections = (mapper_db_connection_t**)ptr_jlong(p);
    if (connections)
        mapper_db_connection_done(connections);
}
