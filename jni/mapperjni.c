
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

JNIEXPORT jlong JNICALL Java_Mapper_Device_device_1new
  (JNIEnv *env, jobject obj, jstring name, jint port)
{
    const char *cname = (*env)->GetStringUTFChars(env, name, 0);
    mapper_device d = mdev_new(cname, port, 0);
    (*env)->ReleaseStringUTFChars(env, name, cname);
    printf("mapper device allocated\n");
    return (jlong)d;
}

JNIEXPORT void JNICALL Java_Mapper_Device_device_1free
  (JNIEnv *env, jobject obj, jlong d)
{
    mapper_device dev = (mapper_device)d;
    mdev_free(dev);
    printf("mapper device freed\n");
}

JNIEXPORT int JNICALL Java_Mapper_Device_device_1poll
  (JNIEnv *env, jobject obj, jlong d, jint timeout)
{
    mapper_device dev = (mapper_device)d;
    return mdev_poll(dev, timeout);
}
