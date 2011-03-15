
#include <stdint.h>
#include <mapper/mapper.h>

#include "Mapper_Device.h"

#define jlong_ptr(a) ((jlong)(uintptr_t)(a))
#define ptr_jlong(a) ((void *)(uintptr_t)(a))

JNIEnv *genv=0;

JNIEXPORT jlong JNICALL Java_Mapper_Device_mdev_1new
  (JNIEnv *env, jobject obj, jstring name, jint port)
{
    const char *cname = (*env)->GetStringUTFChars(env, name, 0);
    mapper_device d = mdev_new(cname, port, 0);
    (*env)->ReleaseStringUTFChars(env, name, cname);
    printf("mapper device allocated\n");
    return jlong_ptr(d);
}

JNIEXPORT void JNICALL Java_Mapper_Device_mdev_1free
  (JNIEnv *env, jobject obj, jlong d)
{
    mapper_device dev = (mapper_device)ptr_jlong(d);
    mdev_free(dev);
    printf("mapper device freed\n");
}

JNIEXPORT int JNICALL Java_Mapper_Device_mdev_1poll
  (JNIEnv *env, jobject obj, jlong d, jint timeout)
{
    genv = env;
    mapper_device dev = (mapper_device)ptr_jlong(d);
    return mdev_poll(dev, timeout);
}

static void java_msig_input_cb(mapper_signal sig, void *v)
{
    printf("in callback, genv=%p\n", genv);
    mapper_db_signal props = msig_properties(sig);
    jobject listener = (jobject)props->user_data;
    if (listener) {
        jclass cls = (*genv)->GetObjectClass(genv, listener);
        printf("listener class: %p\n", cls);
        if (cls) {
            jmethodID val = (*genv)->GetMethodID(genv, cls, "onInput", "()V");
            if (val) {
                printf("got onInput: %p\n", val);
                (*genv)->CallVoidMethod(genv, listener, val);
            }
            else
                printf("couldn't get onInput\n");
        }
    }
}

JNIEXPORT jlong JNICALL Java_Mapper_Device_mdev_1add_1input
  (JNIEnv *env, jobject obj, jlong d, jstring name, jint length, jchar type, jstring unit, jobject minimum, jobject maximum, jobject listener)
{
    if (!d || !name || (length<=0) || (type!='f' && type!='i'))
        return 0;

    mapper_device dev = (mapper_device)ptr_jlong(d);

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

    mapper_signal s = mdev_add_input(dev, cname, length, type, cunit,
                                     minimum ? &mn : 0,
                                     maximum ? &mx : 0,
                                     java_msig_input_cb,
                                     (*env)->NewGlobalRef(env, listener));

    (*env)->ReleaseStringUTFChars(env, name, cname);
    if (unit) (*env)->ReleaseStringUTFChars(env, unit, cunit);

    return jlong_ptr(s);
}

JNIEXPORT jlong JNICALL Java_Mapper_Device_mdev_1add_1output
  (JNIEnv *env, jobject obj, jlong d, jstring name, jint length, jchar type, jstring unit, jobject minimum, jobject maximum)
{
    if (!d || !name || (length<=0) || (type!='f' && type!='i'))
        return 0;

    mapper_device dev = (mapper_device)ptr_jlong(d);

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

    mapper_signal s = mdev_add_output(dev, cname, length, type, cunit,
                                      minimum ? &mn : 0,
                                      maximum ? &mx : 0);

    (*env)->ReleaseStringUTFChars(env, name, cname);
    if (unit) (*env)->ReleaseStringUTFChars(env, unit, cunit);

    return jlong_ptr(s);
}
