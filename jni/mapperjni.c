
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

static mapper_timetag_t *get_timetag_from_jobject
  (JNIEnv *env, jobject obj, mapper_timetag_t *tt)
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

static jobject get_jobject_from_timetag
  (JNIEnv *env, mapper_timetag_t *tt)
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

/**** Mapper.Device ****/

JNIEXPORT jlong JNICALL Java_Mapper_Device_mdev_1new
  (JNIEnv *env, jobject obj, jstring name, jint port)
{
    const char *cname = (*env)->GetStringUTFChars(env, name, 0);
    mapper_device d = mdev_new(cname, port, 0);
    (*env)->ReleaseStringUTFChars(env, name, cname);
    return jlong_ptr(d);
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

    if (input_cb && ctx->signal && ctx->db_signal) {
        jclass cls = (*genv)->GetObjectClass(genv, input_cb);
        if (cls) {
            jmethodID mid=0;
            if (props->type=='i')
                mid = (*genv)->GetMethodID(genv, cls, "onInput",
                                           "(LMapper/Device$Signal;"
                                           "LMapper/Db/Signal;"
                                           "I[I"
                                           "LMapper/TimeTag;)V");
            else if (props->type=='f')
                mid = (*genv)->GetMethodID(genv, cls, "onInput",
                                           "(LMapper/Device$Signal;"
                                           "LMapper/Db/Signal;"
                                           "I[F"
                                           "LMapper/TimeTag;)V");

            if (mid) {
                (*genv)->CallVoidMethod(genv, input_cb, mid,
                                        ctx->signal, ctx->db_signal,
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

static void msig_instance_event_cb(mapper_signal sig,
                                   mapper_db_signal props,
                                   int instance_id,
                                   msig_instance_event_t event,
                                   mapper_timetag_t *tt)
{
    if (bailing)
        return;

    jobject objtt = get_jobject_from_timetag(genv, tt);

    msig_jni_context ctx = (msig_jni_context)props->user_data;
    if (ctx->instanceHandler && ctx->signal && ctx->db_signal) {
        jclass cls = (*genv)->GetObjectClass(genv, ctx->instanceHandler);
        if (cls) {
            jmethodID mid=0;
            mid = (*genv)->GetMethodID(genv, cls, "onEvent",
                                       "(LMapper/Device$Signal;"
                                       "LMapper/Db/Signal;II"
                                       "LMapper/TimeTag;)V");

            if (mid) {
                (*genv)->CallVoidMethod(genv, ctx->instanceHandler, mid,
                                        ctx->signal, ctx->db_signal,
                                        instance_id, event, objtt);
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
    jclass cls = (*env)->FindClass(env, "LMapper/Device$Signal;");
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
        cls = (*env)->FindClass(env, "LMapper/Db/Signal;");
        if (cls) {
            jmethodID mid = (*env)->GetMethodID(env, cls, "<init>",
                                       "(JLMapper/Device$Signal;)V");
            sigdbobj = (*env)->NewObject(env, cls, mid,
                                         jlong_ptr(msig_properties(s)),
                                         sigobj);
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

JNIEXPORT jobject JNICALL Java_Mapper_Device_add_1input
  (JNIEnv *env, jobject obj, jstring name, jint length,
   jchar type, jstring unit, jobject minimum, jobject maximum,
   jobject listener)
{
    if (!name || (length<=0) || (type!='f' && type!='i'))
        return 0;

    mapper_device dev = get_device_from_jobject(env, obj);

    const char *cname = (*env)->GetStringUTFChars(env, name, 0);
    const char *cunit = 0;
    if (unit) cunit = (*env)->GetStringUTFChars(env, unit, 0);

    union {
        float f;
        int i;
    } mn, mx;

    if (minimum) {
        jclass cls = (*env)->GetObjectClass(env, minimum);
        if (cls) {
            jfieldID val = (*env)->GetFieldID(env, cls, "value", "D");
            if (val) {
                if (type == 'f')
                    mn.f = (float)(*env)->GetDoubleField(env, minimum, val);
                else if (type == 'i')
                    mn.i = (int)(*env)->GetDoubleField(env, minimum, val);
            }
        }
    }

    if (maximum) {
        jclass cls = (*env)->GetObjectClass(env, maximum);
        if (cls) {
            jfieldID val = (*env)->GetFieldID(env, cls, "value", "D");
            if (val) {
                if (type == 'f')
                    mx.f = (float)(*env)->GetDoubleField(env, maximum, val);
                else if (type == 'i')
                    mx.i = (int)(*env)->GetDoubleField(env, maximum, val);
            }
        }
    }

    msig_jni_context ctx =
        (msig_jni_context)calloc(1, sizeof(msig_jni_context_t));
    if (!ctx) {
        throwOutOfMemory(env);
        return 0;
    }

    mapper_signal s = mdev_add_input(dev, cname, length, type, cunit,
                                     minimum ? &mn : 0,
                                     maximum ? &mx : 0,
                                     java_msig_input_cb,
                                     ctx);

    (*env)->ReleaseStringUTFChars(env, name, cname);
    if (unit) (*env)->ReleaseStringUTFChars(env, unit, cunit);

    return create_signal_object(env, obj, ctx, listener, s);
}

JNIEXPORT jobject JNICALL Java_Mapper_Device_add_1output
  (JNIEnv *env, jobject obj, jstring name, jint length, jchar type, jstring unit, jobject minimum, jobject maximum)
{
    if (!name || (length<=0) || (type!='f' && type!='i'))
        return 0;

    mapper_device dev = get_device_from_jobject(env, obj);

    const char *cname = (*env)->GetStringUTFChars(env, name, 0);
    const char *cunit = 0;
    if (unit) cunit = (*env)->GetStringUTFChars(env, unit, 0);

    union {
        float f;
        int i;
    } mn, mx;

    if (minimum) {
        jclass cls = (*env)->GetObjectClass(env, minimum);
        if (cls) {
            jfieldID val = (*env)->GetFieldID(env, cls, "value", "D");
            if (val) {
                if (type == 'f')
                    mn.f = (float)(*env)->GetDoubleField(env, minimum, val);
                else if (type == 'i')
                    mn.i = (int)(*env)->GetDoubleField(env, minimum, val);
            }
        }
    }

    if (maximum) {
        jclass cls = (*env)->GetObjectClass(env, maximum);
        if (cls) {
            jfieldID val = (*env)->GetFieldID(env, cls, "value", "D");
            if (val) {
                if (type == 'f')
                    mx.f = (float)(*env)->GetDoubleField(env, maximum, val);
                else if (type == 'i')
                    mx.i = (int)(*env)->GetDoubleField(env, maximum, val);
            }
        }
    }

    msig_jni_context ctx =
        (msig_jni_context)calloc(1, sizeof(msig_jni_context_t));
    if (!ctx) {
        throwOutOfMemory(env);
        return 0;
    }

    mapper_signal s = mdev_add_output(dev, cname, length, type, cunit,
                                      minimum ? &mn : 0,
                                      maximum ? &mx : 0);

    (*env)->ReleaseStringUTFChars(env, name, cname);
    if (unit) (*env)->ReleaseStringUTFChars(env, unit, cunit);

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

JNIEXPORT void JNICALL Java_Mapper_Device_mdev_1set_1property
  (JNIEnv *env, jobject obj, jlong d, jstring key, jobject value)
{
    mapper_device dev = (mapper_device)ptr_jlong(d);
    const char *ckey = (*env)->GetStringUTFChars(env, key, 0);
    jclass cls = (*env)->GetObjectClass(env, value);
    if (cls) {
        jfieldID typeid = (*env)->GetFieldID(env, cls, "type", "C");
        if (typeid) {
            char type;
            lo_arg a, *pa=&a;
            jfieldID valf = 0;
            jobject o;
            type = (*env)->GetCharField(env, value, typeid);
            switch (type)
            {
            case 'i':
                valf = (*env)->GetFieldID(env, cls, "_i", "I");
                a.i = (*env)->GetIntField(env, value, valf);
                break;
            case 'f':
                valf = (*env)->GetFieldID(env, cls, "_f", "F");
                a.f = (*env)->GetFloatField(env, value, valf);
                break;
            case 'd':
                valf = (*env)->GetFieldID(env, cls, "_d", "D");
                a.d = (*env)->GetDoubleField(env, value, valf);
                break;
            case 's':
            case 'S':
                valf = (*env)->GetFieldID(env, cls, "_s", "Ljava/lang/String;");
                o = (*env)->GetObjectField(env, value, valf);
                pa = (lo_arg*)(*env)->GetStringUTFChars(env, o, 0);
                break;
            }
            if (valf) {
                mdev_set_property(dev, ckey, type, pa);
                if (pa != &a)
                    (*env)->ReleaseStringUTFChars(env, o, (const char*)pa);
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

    jclass cls = (*env)->GetObjectClass(env, objtt);
    if (cls) {
        jfieldID secid = (*env)->GetFieldID(env, cls, "sec", "J");
        jfieldID fracid = (*env)->GetFieldID(env, cls, "frac", "J");
        if (secid && fracid) {
            mapper_timetag_t tt;
            tt.sec = (float)(*env)->GetDoubleField(env, objtt, secid);
            tt.frac = (int)(*env)->GetDoubleField(env, objtt, fracid);

            mdev_start_queue(dev, tt);
        }
    }
}

JNIEXPORT void JNICALL Java_Mapper_Device_mdev_1send_1queue
  (JNIEnv *env, jobject obj, jlong d, jobject objtt)
{
    mapper_device dev = (mapper_device)ptr_jlong(d);

    jclass cls = (*env)->GetObjectClass(env, objtt);
    if (cls) {
        jfieldID secid = (*env)->GetFieldID(env, cls, "sec", "J");
        jfieldID fracid = (*env)->GetFieldID(env, cls, "frac", "J");
        if (secid && fracid) {
            mapper_timetag_t tt;
            tt.sec = (float)(*env)->GetDoubleField(env, objtt, secid);
            tt.frac = (int)(*env)->GetDoubleField(env, objtt, fracid);

            mdev_send_queue(dev, tt);
        }
    }
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

JNIEXPORT void JNICALL Java_Mapper_Device_00024Signal_msig_1set_1minimum
  (JNIEnv *env, jobject obj, jlong s, jobject minimum)
{
    mapper_signal sig=(mapper_signal)ptr_jlong(s);
    if (sig) {
        mapper_db_signal p = msig_properties(sig);

        union {
            float f;
            int i;
        } mn;

        if (minimum) {
            jclass cls = (*env)->GetObjectClass(env, minimum);
            if (cls) {
                jfieldID val = (*env)->GetFieldID(env, cls, "value", "D");
                if (val) {
                    if (p->type == 'f')
                        mn.f = (float)(*env)->GetDoubleField(env, minimum, val);
                    else if (p->type == 'i')
                        mn.i = (int)(*env)->GetDoubleField(env, minimum, val);
                    msig_set_minimum(sig, &mn);
                }
            }
        }
        else
            msig_set_minimum(sig, 0);
    }
}

JNIEXPORT void JNICALL Java_Mapper_Device_00024Signal_msig_1set_1maximum
  (JNIEnv *env, jobject obj, jlong s, jobject maximum)
{
    mapper_signal sig=(mapper_signal)ptr_jlong(s);
    if (sig) {
        mapper_db_signal p = msig_properties(sig);

        union {
            float f;
            int i;
        } mx;

        if (maximum) {
            jclass cls = (*env)->GetObjectClass(env, maximum);
            if (cls) {
                jfieldID val = (*env)->GetFieldID(env, cls, "value", "D");
                if (val) {
                    if (p->type == 'f')
                        mx.f = (float)(*env)->GetDoubleField(env, maximum, val);
                    else if (p->type == 'i')
                        mx.i = (int)(*env)->GetDoubleField(env, maximum, val);
                    msig_set_minimum(sig, &mx);
                }
            }
        }
        else
            msig_set_maximum(sig, 0);
    }
}

JNIEXPORT void JNICALL Java_Mapper_Device_00024Signal_msig_1set_1rate
  (JNIEnv *env, jobject obj, jlong s, jdouble rate)
{
    mapper_signal sig=(mapper_signal)ptr_jlong(s);
    if (sig)
        msig_set_rate(sig, (float)rate);
}

JNIEXPORT jint JNICALL Java_Mapper_Device_00024Signal_msig_1query_1remotes
  (JNIEnv *env, jobject obj, jlong s, jobject objtt)
{
    mapper_signal sig = (mapper_signal)ptr_jlong(s);
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
        if (typeid) {
            char type;
            lo_arg a, *pa=&a;
            jfieldID valf = 0;
            jobject o;
            type = (*env)->GetCharField(env, value, typeid);
            switch (type)
            {
            case 'i':
                valf = (*env)->GetFieldID(env, cls, "_i", "I");
                a.i = (*env)->GetIntField(env, value, valf);
                break;
            case 'f':
                valf = (*env)->GetFieldID(env, cls, "_f", "F");
                a.f = (*env)->GetFloatField(env, value, valf);
                break;
            case 'd':
                valf = (*env)->GetFieldID(env, cls, "_d", "D");
                a.d = (*env)->GetDoubleField(env, value, valf);
                break;
            case 's':
            case 'S':
                valf = (*env)->GetFieldID(env, cls, "_s", "Ljava/lang/String;");
                o = (*env)->GetObjectField(env, value, valf);
                pa = (lo_arg*)(*env)->GetStringUTFChars(env, o, 0);
                break;
            }
            if (valf) {
                msig_set_property(sig, ckey, type, pa);
                if (pa != &a)
                    (*env)->ReleaseStringUTFChars(env, o, (const char*)pa);
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

JNIEXPORT void JNICALL Java_Mapper_Device_00024Signal_update__ILMapper_TimeTag_2
  (JNIEnv *env, jobject obj, jint value, jobject objtt)
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
        msig_update(sig, &value, 1, ptt ? *ptt : MAPPER_NOW);
    else if (props->type == 'f') {
        float v = (float)value;
        msig_update(sig, &v, 1, ptt ? *ptt : MAPPER_NOW);
    }
}

JNIEXPORT void JNICALL Java_Mapper_Device_00024Signal_update__FLMapper_TimeTag_2
  (JNIEnv *env, jobject obj, jfloat value, jobject objtt)
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
        msig_update(sig, &value, 1, ptt ? *ptt : MAPPER_NOW);
    }
}

JNIEXPORT void JNICALL Java_Mapper_Device_00024Signal_update__DLMapper_TimeTag_2
  (JNIEnv *env, jobject obj, jdouble value, jobject objtt)
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
        msig_update(sig, &v, 1, ptt ? *ptt : MAPPER_NOW);
    }
}

JNIEXPORT void JNICALL Java_Mapper_Device_00024Signal_update___3ILMapper_TimeTag_2
  (JNIEnv *env, jobject obj, jintArray value, jobject objtt)
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
            msig_update(sig, arraycopy, 0, ptt ? *ptt : MAPPER_NOW);
            free(arraycopy);
        }
        (*env)->ReleaseIntArrayElements(env, value, array, JNI_ABORT);
    }
}

JNIEXPORT void JNICALL Java_Mapper_Device_00024Signal_update___3FLMapper_TimeTag_2
  (JNIEnv *env, jobject obj, jfloatArray value, jobject objtt)
{
    mapper_signal sig = get_signal_from_jobject(env, obj);
    if (!sig) return;

    mapper_db_signal props = msig_properties(sig);
    int length = (*env)->GetArrayLength(env, value);
    if (length != props->length) {
        throwIllegalArgumentLength(env, sig, length);
        return;
    }
    if (props->type != 'f') {
        throwIllegalArgumentTruncate(env, sig);
        return;
    }

    mapper_timetag_t tt, *ptt;
    ptt = get_timetag_from_jobject(env, objtt, &tt);

    jfloat *array = (*env)->GetFloatArrayElements(env, value, 0);
    if (array) {
        msig_update(sig, array, 0, ptt ? *ptt : MAPPER_NOW);
        (*env)->ReleaseFloatArrayElements(env, value, array, JNI_ABORT);
    }
}

JNIEXPORT void JNICALL Java_Mapper_Device_00024Signal_update___3DLMapper_TimeTag_2
  (JNIEnv *env, jobject obj, jdoubleArray value, jobject objtt)
{
    mapper_signal sig = get_signal_from_jobject(env, obj);
    if (!sig) return;

    mapper_db_signal props = msig_properties(sig);
    int length = (*env)->GetArrayLength(env, value);
    if (length != props->length) {
        throwIllegalArgumentLength(env, sig, length);
        return;
    }
    if (props->type != 'f') {
        throwIllegalArgumentTruncate(env, sig);
        return;
    }

    mapper_timetag_t tt, *ptt;
    ptt = get_timetag_from_jobject(env, objtt, &tt);

    jdouble *array = (*env)->GetDoubleArrayElements(env, value, 0);
    if (array) {
        float *arraycopy = malloc(sizeof(float)*length);
        int i;
        for (i=0; i<length; i++)
            arraycopy[i] = (float)array[i];
        (*env)->ReleaseDoubleArrayElements(env, value, array, JNI_ABORT);
        msig_update(sig, arraycopy, 0, ptt ? *ptt : MAPPER_NOW);
        free(arraycopy);
    }
}

JNIEXPORT void JNICALL Java_Mapper_Device_00024Signal_update__I
  (JNIEnv *env, jobject obj, jint value)
{
    Java_Mapper_Device_00024Signal_update__ILMapper_TimeTag_2
        (env, obj, value, NULL);
}

JNIEXPORT void JNICALL Java_Mapper_Device_00024Signal_update__F
  (JNIEnv *env, jobject obj, jfloat value)
{
    Java_Mapper_Device_00024Signal_update__FLMapper_TimeTag_2
        (env, obj, value, NULL);
}

JNIEXPORT void JNICALL Java_Mapper_Device_00024Signal_update__D
  (JNIEnv *env, jobject obj, jdouble value)
{
    Java_Mapper_Device_00024Signal_update__DLMapper_TimeTag_2
        (env, obj, value, NULL);
}

JNIEXPORT void JNICALL Java_Mapper_Device_00024Signal_update___3I
  (JNIEnv *env, jobject obj, jintArray value)
{
    Java_Mapper_Device_00024Signal_update___3ILMapper_TimeTag_2
        (env, obj, value, NULL);
}

JNIEXPORT void JNICALL Java_Mapper_Device_00024Signal_update___3F
  (JNIEnv *env, jobject obj, jfloatArray value)
{
    Java_Mapper_Device_00024Signal_update___3FLMapper_TimeTag_2
        (env, obj, value, NULL);
}

JNIEXPORT void JNICALL Java_Mapper_Device_00024Signal_update___3D
  (JNIEnv *env, jobject obj, jdoubleArray value)
{
    Java_Mapper_Device_00024Signal_update___3DLMapper_TimeTag_2
        (env, obj, value, NULL);
}

JNIEXPORT void JNICALL Java_Mapper_Device_00024Signal_update_1instance__IILMapper_TimeTag_2
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
}

JNIEXPORT void JNICALL Java_Mapper_Device_00024Signal_update_1instance__IFLMapper_TimeTag_2
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
}

JNIEXPORT void JNICALL Java_Mapper_Device_00024Signal_update_1instance__IDLMapper_TimeTag_2
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
}

JNIEXPORT void JNICALL Java_Mapper_Device_00024Signal_update_1instance__I_3ILMapper_TimeTag_2
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
        (*env)->ReleaseIntArrayElements(env, value, array, JNI_ABORT);
    }
}

JNIEXPORT void JNICALL Java_Mapper_Device_00024Signal_update_1instance__I_3FLMapper_TimeTag_2
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
    if (props->type != 'f') {
        throwIllegalArgumentTruncate(env, sig);
        return;
    }

    mapper_timetag_t tt, *ptt;
    ptt = get_timetag_from_jobject(env, objtt, &tt);

    jfloat *array = (*env)->GetFloatArrayElements(env, value, 0);
    if (array) {
        msig_update_instance(sig, id, array, 0, ptt ? *ptt : MAPPER_NOW);
        (*env)->ReleaseFloatArrayElements(env, value, array, JNI_ABORT);
    }
}

JNIEXPORT void JNICALL Java_Mapper_Device_00024Signal_update_1instance__I_3DLMapper_TimeTag_2
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
    if (props->type != 'f') {
        throwIllegalArgumentTruncate(env, sig);
        return;
    }

    mapper_timetag_t tt, *ptt;
    ptt = get_timetag_from_jobject(env, objtt, &tt);

    jdouble *array = (*env)->GetDoubleArrayElements(env, value, 0);
    if (array) {
        float *arraycopy = malloc(sizeof(float)*length);
        int i;
        for (i=0; i<length; i++)
            arraycopy[i] = (float)array[i];
        (*env)->ReleaseDoubleArrayElements(env, value, array, JNI_ABORT);
        msig_update_instance(sig, id, arraycopy, 0,
                             ptt ? *ptt : MAPPER_NOW);
        free(arraycopy);
    }
}

JNIEXPORT jboolean JNICALL Java_Mapper_Device_00024Signal_value___3ILMapper_TimeTag_2
  (JNIEnv *env, jobject obj, jintArray ar, jobject objtt)
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

    int *value = msig_value(sig, &tt);
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

JNIEXPORT jboolean JNICALL Java_Mapper_Device_00024Signal_value___3FLMapper_TimeTag_2
  (JNIEnv *env, jobject obj, jfloatArray ar, jobject objtt)
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
            int *value = msig_value(sig, &tt);
            if (!value) {
                (*env)->ReleaseFloatArrayElements(env, ar, array, JNI_ABORT);
                return JNI_FALSE;
            }
            for (i=0; i < props->length; i++)
                array[i] = (jfloat)value[i];
        } break;

        case 'f': {
            float *value = msig_value(sig, &tt);
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

JNIEXPORT jboolean JNICALL Java_Mapper_Device_00024Signal_value___3DLMapper_TimeTag_2
  (JNIEnv *env, jobject obj, jdoubleArray ar, jobject objtt)
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
            int *value = msig_value(sig, &tt);
            if (!value) {
                (*env)->ReleaseDoubleArrayElements(env, ar, array, JNI_ABORT);
                return JNI_FALSE;
            }
            for (i=0; i < props->length; i++)
                array[i] = (jdouble)value[i];
        } break;

        case 'f': {
            float *value = msig_value(sig, &tt);
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

JNIEXPORT jboolean JNICALL Java_Mapper_Device_00024Signal_instance_1value__I_3ILMapper_TimeTag_2
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

JNIEXPORT jboolean JNICALL Java_Mapper_Device_00024Signal_instance_1value__I_3FLMapper_TimeTag_2
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

JNIEXPORT jboolean JNICALL Java_Mapper_Device_00024Signal_instance_1value__I_3DLMapper_TimeTag_2
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

/**** Mapper.Db.Signal ****/

JNIEXPORT void JNICALL Java_Mapper_Db_Signal_msig_1db_1signal_1set_1name
  (JNIEnv *env, jobject obj, jlong p, jstring name)
{
    mapper_db_signal props = (mapper_db_signal)ptr_jlong(p);
    const char *cname = (*env)->GetStringUTFChars(env, name, 0);
    free((char*)props->name);
    props->name = strdup(cname);
    (*env)->ReleaseStringUTFChars(env, name, cname);
}

JNIEXPORT jstring JNICALL Java_Mapper_Db_Signal_msig_1db_1signal_1get_1name
  (JNIEnv *env, jobject obj, jlong p)
{
    mapper_db_signal props = (mapper_db_signal)ptr_jlong(p);
    return (*env)->NewStringUTF(env, props->name);
}

JNIEXPORT jstring JNICALL Java_Mapper_Db_Signal_msig_1db_1signal_1get_1device_1name
  (JNIEnv *env, jobject obj, jlong p)
{
    mapper_db_signal props = (mapper_db_signal)ptr_jlong(p);
    return (*env)->NewStringUTF(env, props->device_name);
}

JNIEXPORT jboolean JNICALL Java_Mapper_Db_Signal_msig_1db_1signal_1get_1is_1output
  (JNIEnv *env, jobject obj, jlong p)
{
    mapper_db_signal props = (mapper_db_signal)ptr_jlong(p);
    return props->is_output!=0;
}

JNIEXPORT jchar JNICALL Java_Mapper_Db_Signal_msig_1db_1signal_1get_1type
  (JNIEnv *env, jobject obj, jlong p)
{
    mapper_db_signal props = (mapper_db_signal)ptr_jlong(p);
    return props->type!=0;
}

JNIEXPORT jint JNICALL Java_Mapper_Db_Signal_msig_1db_1signal_1get_1length
  (JNIEnv *env, jobject obj, jlong p)
{
    mapper_db_signal props = (mapper_db_signal)ptr_jlong(p);
    return props->length;
}

JNIEXPORT void JNICALL Java_Mapper_Db_Signal_msig_1db_1signal_1set_1unit
  (JNIEnv *env, jobject obj, jlong p, jstring unit)
{
    mapper_db_signal props = (mapper_db_signal)ptr_jlong(p);
    const char *cunit = (*env)->GetStringUTFChars(env, unit, 0);
    free((char*)props->unit);
    props->unit = strdup(cunit);
    (*env)->ReleaseStringUTFChars(env, unit, cunit);
}

JNIEXPORT jstring JNICALL Java_Mapper_Db_Signal_msig_1db_1signal_1get_1unit
  (JNIEnv *env, jobject obj, jlong p)
{
    mapper_db_signal props = (mapper_db_signal)ptr_jlong(p);
    return (*env)->NewStringUTF(env, props->unit);
}

JNIEXPORT jobject JNICALL Java_Mapper_Db_Signal_msig_1db_1signal_1get_1minimum
  (JNIEnv *env, jobject obj, jlong p)
{
    mapper_db_signal props = (mapper_db_signal)ptr_jlong(p);

    if (props->minimum)
    {
        jclass cls = (*env)->FindClass(env, "java/lang/Double");
        if (cls) {
            jmethodID methodID = (*env)->GetMethodID(env, cls,
                                                     "<init>", "(D)V");
            if (methodID)
                return (*env)->NewObject(env, cls, methodID,
                                         *(props->minimum));
        }
    }

    return 0;
}

JNIEXPORT jobject JNICALL Java_Mapper_Db_Signal_msig_1db_1signal_1get_1maximum
  (JNIEnv *env, jobject obj, jlong p)
{
    mapper_db_signal props = (mapper_db_signal)ptr_jlong(p);

    if (props->maximum)
    {
        jclass cls = (*env)->FindClass(env, "java/lang/Double");
        if (cls) {
            jmethodID methodID = (*env)->GetMethodID(env, cls,
                                                     "<init>", "(D)V");
            if (methodID)
                return (*env)->NewObject(env, cls, methodID,
                                         *(props->maximum));
        }
    }

    return 0;
}

JNIEXPORT jdouble JNICALL Java_Mapper_Db_Signal_msig_1db_1signal_1get_1rate
  (JNIEnv *env, jobject obj, jlong p)
{
    mapper_db_signal props = (mapper_db_signal)ptr_jlong(p);
    return (jdouble)props->rate;
}

JNIEXPORT jobject JNICALL Java_Mapper_Db_Signal_mapper_1db_1signal_1property_1lookup
  (JNIEnv *env, jobject obj, jlong p, jstring property)
{
    mapper_db_signal props = (mapper_db_signal)ptr_jlong(p);
    const char *cprop = (*env)->GetStringUTFChars(env, property, 0);
    lo_type t;
    const lo_arg *a;
    jobject o = 0;

    if (mapper_db_signal_property_lookup(props, cprop, &t, &a))
        goto done;

    jmethodID methodID;
    jclass cls = (*env)->FindClass(env, "Mapper/PropertyValue");
    if (cls) {
        switch (t) {
        case 'i':
            methodID = (*env)->GetMethodID(env, cls,
                                           "<init>", "(CI)V");
            if (methodID)
                return (*env)->NewObject(env, cls, methodID, t, a->i);
            break;
        case 'f':
            methodID = (*env)->GetMethodID(env, cls,
                                           "<init>", "(CF)V");
            if (methodID)
                return (*env)->NewObject(env, cls, methodID, t, a->f);
            break;
        case 'd':
            methodID = (*env)->GetMethodID(env, cls,
                                           "<init>", "(CD)V");
            if (methodID)
                return (*env)->NewObject(env, cls, methodID, t, a->d);
            break;
        case 's':
        case 'S':
            methodID = (*env)->GetMethodID(env, cls,
                                           "<init>", "(CLjava/lang/String;)V");
            if (methodID) {
                jobject s = (*env)->NewStringUTF(env, &a->s);
                if (s)
                    return (*env)->NewObject(env, cls, methodID, t, s);
            }
            break;
        default:
            // TODO handle all OSC types
            // Not throwing an exception here because this data comes
            // from the network: just ignore unknown types.
            break;
        }
    }

  done:
    (*env)->ReleaseStringUTFChars(env, property, cprop);
    return o;
}

JNIEXPORT void JNICALL Java_Mapper_Device_00024Signal_set_1instance_1event_1callback
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
                                         msig_instance_event_cb,
                                         flags, ctx);
    }
    else {
        ctx->instanceHandler = 0;
        msig_set_instance_event_callback(sig, 0, flags, 0);
    }
}

JNIEXPORT void JNICALL Java_Mapper_Device_00024Signal_set_1callback
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


JNIEXPORT void JNICALL Java_Mapper_Device_00024Signal_set_1instance_1callback
  (JNIEnv *env, jobject obj, jint instance_id, jobject data)
{
    mapper_signal sig = get_signal_from_jobject(env, obj);
    if (!sig) return;
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

JNIEXPORT jobject JNICALL Java_Mapper_Device_00024Signal_get_1instance_1callback
  (JNIEnv *env, jobject obj, jint instance_id)
{
    mapper_signal sig = get_signal_from_jobject(env, obj);
    if (!sig) return 0;
    return (jobject)msig_get_instance_data(sig, instance_id);
}

JNIEXPORT void JNICALL Java_Mapper_Device_00024Signal_reserve_1instances
  (JNIEnv *env, jobject obj, jint num)
{
    mapper_signal sig = get_signal_from_jobject(env, obj);
    if (!sig) return;
    msig_reserve_instances(sig, num);
}

JNIEXPORT void JNICALL Java_Mapper_Device_00024Signal_release_1instance
  (JNIEnv *env, jobject obj, jint instance_id, jobject objtt)
{
    mapper_signal sig = get_signal_from_jobject(env, obj);
    if (!sig) return;
    mapper_timetag_t tt, *ptt=0;
    ptt = get_timetag_from_jobject(env, objtt, &tt);
    msig_release_instance(sig, instance_id, ptt ? *ptt : MAPPER_NOW);
}

JNIEXPORT jobject JNICALL Java_Mapper_Device_00024Signal_oldest_1active_1instance
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

JNIEXPORT jobject JNICALL Java_Mapper_Device_00024Signal_newest_1active_1instance
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

JNIEXPORT jint JNICALL Java_Mapper_Device_00024Signal_num_1active_1instances
  (JNIEnv *env, jobject obj)
{
    mapper_signal sig = get_signal_from_jobject(env, obj);
    if (!sig) return 0;
    return msig_num_active_instances(sig);
}

JNIEXPORT jint JNICALL Java_Mapper_Device_00024Signal_num_1reserved_1instances
  (JNIEnv *env, jobject obj)
{
    mapper_signal sig = get_signal_from_jobject(env, obj);
    if (!sig) return 0;
    return msig_num_reserved_instances(sig);
}

JNIEXPORT jint JNICALL Java_Mapper_Device_00024Signal_active_1instance_1id
  (JNIEnv *env, jobject obj, jint index)
{
    mapper_signal sig = get_signal_from_jobject(env, obj);
    if (!sig) return 0;
    return msig_active_instance_id(sig, index);
}

JNIEXPORT void JNICALL Java_Mapper_Device_00024Signal_set_1instance_1allocation_1mode
  (JNIEnv *env, jobject obj, jint mode)
{
    mapper_signal sig = get_signal_from_jobject(env, obj);
    if (!sig) return;
    msig_set_instance_allocation_mode(sig, mode);
}

JNIEXPORT jint JNICALL Java_Mapper_Device_00024Signal_instance_1allocation_1mode
  (JNIEnv *env, jobject obj)
{
    mapper_signal sig = get_signal_from_jobject(env, obj);
    if (!sig) return 0;
    return msig_get_instance_allocation_mode(sig);
}

JNIEXPORT jint JNICALL Java_Mapper_Device_00024Signal_num_1connections
  (JNIEnv *env, jobject obj)
{
    mapper_signal sig = get_signal_from_jobject(env, obj);
    if (!sig) return 0;
    return msig_num_connections(sig);
}
