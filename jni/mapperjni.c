
#include "Mapper_Device.h"

JNIEXPORT jdouble JNICALL Java_Mapper_Device_t
  (JNIEnv *env, jobject obj, jint i, jstring s)
{
    printf("i: %d\n", i);

    const char *str = (*env)->GetStringUTFChars(env, s, 0);
    if (str) {
        printf("s: %s\n", str);
        (*env)->ReleaseStringUTFChars(env, s, str);
    }
    else
        printf("Could not get string?\n");
}
