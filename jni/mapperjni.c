
#include <mapper/mapper.h>

#include "Mapper_Device.h"

JNIEXPORT void JNICALL Java_Mdev_t
  (JNIEnv *env, jobject obj, jint i, jstring s)
{
    printf("(native) i: %d\n", i);

    const char *str = (*env)->GetStringUTFChars(env, s, 0);
    if (str) {
        printf("(native) s: %s\n", str);
        (*env)->ReleaseStringUTFChars(env, s, str);
    }
    else
        printf("Could not get string?\n");

    jclass cls = (*env)->GetObjectClass(env, obj);
    jfieldID val = (*env)->GetFieldID(env, cls, "value", "I");
    if (val) {
        jint j = (*env)->GetIntField(env, obj, val);
        printf("(native) value: %d\n", j);
        j = 42;
        (*env)->SetIntField(env, obj, val, j);
        printf("(native) value = 42\n");
    }
    else
        printf("Could not get value field?\n");
}

JNIEXPORT jlong JNICALL Java_Mapper_Device_mdev_1new
  (JNIEnv *env, jobject obj, jstring name, jint port)
{
    const char *cname = (*env)->GetStringUTFChars(env, name, 0);
    mapper_device d = mdev_new(cname, port, 0);
    (*env)->ReleaseStringUTFChars(env, name, cname);
    printf("mapper device allocated\n");
    return (jlong)d;
}

JNIEXPORT void JNICALL Java_Mapper_Device_mdev_1free
  (JNIEnv *env, jobject obj, jlong d)
{
    mapper_device dev = (mapper_device)d;
    mdev_free(dev);
    printf("mapper device freed\n");
}

JNIEXPORT int JNICALL Java_Mapper_Device_mdev_1poll
  (JNIEnv *env, jobject obj, jlong d, jint timeout)
{
    mapper_device dev = (mapper_device)d;
    return mdev_poll(dev, timeout);
}

JNIEXPORT jlong JNICALL Java_Mapper_Device_mdev_1add_1input
  (JNIEnv *env, jobject obj, jlong d, jstring name, jint length, jchar type, jstring unit, jobject minimum, jobject maximum, jobject handler)
{
    if (!d || !name || (length<=0) || (type!='f' && type!='i'))
        return 0;

    mapper_device dev = (mapper_device)d;

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
                                     0, 0);

    (*env)->ReleaseStringUTFChars(env, name, cname);
    if (unit) (*env)->ReleaseStringUTFChars(env, unit, cunit);

    return s;
}
