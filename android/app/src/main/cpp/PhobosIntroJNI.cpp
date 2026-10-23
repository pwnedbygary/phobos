// The intro (PhobosIntro.hpp) for the app's IntroSplash: its canvas, and its frames drawn into a bitmap.
#include "PhobosIntro.hpp"

#include <android/bitmap.h>
#include <jni.h>

extern "C" JNIEXPORT jintArray JNICALL
Java_com_phobos_emulator_PhobosCore_introCanvas(JNIEnv* env, jobject, jint width, jint height) {
  auto canvas = phobos::intro::canvasFor(width, height);
  jint values[] = {canvas.width, canvas.height, canvas.scale};
  jintArray out = env->NewIntArray(3);
  if (out) env->SetIntArrayRegion(out, 0, 3, values);
  return out;
}

// False once the intro is over, and if the bitmap can't be drawn into, so the Library is shown.
extern "C" JNIEXPORT jboolean JNICALL
Java_com_phobos_emulator_PhobosCore_renderIntro(JNIEnv* env, jobject, jobject bitmap, jint ms, jint skippedAt) {
  AndroidBitmapInfo info{};
  if (AndroidBitmap_getInfo(env, bitmap, &info) != ANDROID_BITMAP_RESULT_SUCCESS ||
      info.format != ANDROID_BITMAP_FORMAT_RGBA_8888) {
    return JNI_FALSE;
  }
  void* pixels = nullptr;
  if (AndroidBitmap_lockPixels(env, bitmap, &pixels) != ANDROID_BITMAP_RESULT_SUCCESS) return JNI_FALSE;
  bool running = pixels && phobos::intro::render(static_cast<std::uint8_t*>(pixels), int(info.width),
                                                 int(info.height), int(info.stride), ms, skippedAt);
  AndroidBitmap_unlockPixels(env, bitmap);
  return running ? JNI_TRUE : JNI_FALSE;
}
