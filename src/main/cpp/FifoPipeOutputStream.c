/* FifoPipeOutputStream.c

Copyright 2015 - 2016 Tideworks Technology
Author: Roger D. Voss

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.

*/
#include <unistd.h>
#if 0
#include <alloca.h>
#include <string.h>
#include <errno.h>
#endif
#include "FifoPipeOutputStream.h"
#if 0
/*
 * Class:     FifoPipeOutputStream
 * Method:    writePByte
 * Signature: (I)Ljava/lang/String;
 */
JNIEXPORT jstring JNICALL Java_FifoPipeOutputStream_writePByte(JNIEnv *env, jobject this, jint fd, jint b) {
  jbyte * const buf = (jbyte*) alloca(sizeof(jint));
  buf[0] = (jbyte) b;
  ssize_t rslt = write(fd, buf, 1);
  if (rslt == -1) {
    return (*env)->NewStringUTF(env, strerror(errno));
  }
  return NULL;
}

/*
 * Class:     FifoPipeOutputStream
 * Method:    writePBlock
 * Signature: ([BII)Ljava/lang/String;
 */
JNIEXPORT jstring JNICALL Java_FifoPipeOutputStream_writePBlock(JNIEnv *env, jobject this,
                                                                jint fd, jbyteArray arr, jint off, jint len) {
  jbyte * buf = (jbyte*) alloca(len);
  (*env)->GetByteArrayRegion(env, arr, off, len, buf);
  jint size = len;
  for(;;) {
    ssize_t rslt = write(fd, buf, size);
    if (rslt == -1) {
      return (*env)->NewStringUTF(env, strerror(errno));
    }
    size -= rslt;
    if (size > 0) {
      buf += rslt;
    } else {
      break;
    }
  }
  return NULL;
}

/*
 * Class:     FifoPipeOutputStream
 * Method:    flushP
 * Signature: ()Ljava/lang/String;
 */
JNIEXPORT jstring JNICALL Java_FifoPipeOutputStream_flushP(JNIEnv *env, jobject this, jint fd) {
  return NULL;
}

/*
 * Class:     FifoPipeOutputStream
 * Method:    closeP
 * Signature: ()Ljava/lang/String;
 */
JNIEXPORT jstring JNICALL Java_FifoPipeOutputStream_closeP(JNIEnv *env, jobject this, jint fd) {
  int rslt = close(fd);
  if (rslt == -1) {
    return (*env)->NewStringUTF(env, strerror(errno));
  }
  return NULL;
}
#endif
/*
 * Class:     FifoPipeOutputStream
 * Method:    unlinkPipeName
 * Signature: (Ljava/lang/String;)V
 */
JNIEXPORT void JNICALL Java_FifoPipeOutputStream_unlinkPipeName(JNIEnv *env, jclass cls, jstring pathname) {
  jboolean isCopy;
  const char* utf_pathname = (*env)->GetStringUTFChars(env, pathname, &isCopy);
  unlink(utf_pathname);
  if (isCopy == JNI_TRUE) {
    (*env)->ReleaseStringUTFChars(env, pathname, utf_pathname);
  }
}
