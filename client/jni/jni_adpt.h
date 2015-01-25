#ifdef __ANDROID__
#include <jni.h>
#else
#define JNIEnv void
typedef int jint;
typedef long jlong;
typedef int jboolean;
typedef unsigned char jbyte;
typedef unsigned char *jbyteArray;

typedef void *jobject;
typedef char *jstring;
typedef void *jclass;
typedef void *jobjectArray;
typedef int *jintArray;

#define JNICALL
#define JNIEXPORT extern

#endif

