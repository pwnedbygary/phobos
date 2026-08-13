#include <map>
#include <vector>
#include <jni.h>
#include <android/log.h>
#include <ares/ares.hpp>
#include "PhobosRunner.hpp"

using namespace nall;
using namespace nall::primitives;

extern "C" JNIEXPORT jstring JNICALL
Java_com_phobos_emulator_PhobosCore_stringFromJNI(JNIEnv* env, jobject) {
    return env->NewStringUTF("Phobos Engine Ready");
}

static std::map<string, std::vector<string>> systemExtensions = {
    {"Atari 2600", {"a26", "bin"}},
    {"ColecoVision", {"col", "cv"}},
    {"Famicom", {"fc", "nes", "unf", "unif", "unh", "fds"}},
    {"Super Famicom", {"sfc", "smc", "swc", "fig", "bs", "st"}},
    {"Nintendo 64", {"n64", "v64", "z64", "n64dd", "ndd", "d64"}},
    {"Game Boy", {"gb"}},
    {"Game Boy Color", {"gb", "gbc", "nbc"}},
    {"Game Boy Advance", {"gba"}},
    {"SG-1000", {"sg1000", "sg"}},
    {"Master System", {"ms", "sms"}},
    {"Mega Drive", {"md", "gen", "bin"}},
    {"Game Gear", {"gg"}},
    {"Mega CD", {"cue", "chd", "iso"}},
    {"PlayStation", {"cue", "chd", "exe", "ps-exe", "pbp", "iso", "mdf", "img"}},
    {"Sega Saturn", {"cue", "chd", "iso", "mdf"}},
    {"Neo Geo", {"ng", "neo"}},
    {"Neo Geo CD", {"cue", "chd"}},
    {"Neo Geo Pocket", {"ngp", "nap"}},
    {"Neo Geo Pocket Color", {"ngpc", "ngc", "nbc"}},
    {"PC Engine", {"pce", "tg16"}},
    {"PC Engine CD", {"cue", "chd"}},
    {"SuperGrafx", {"sgx"}},
    {"WonderSwan", {"ws"}},
    {"WonderSwan Color", {"wsc"}},
    {"MSX", {"msx", "rom", "wav", "tzx", "tsx", "cas"}},
    {"MSX2", {"msx2", "rom", "wav", "tzx", "tsx", "cas"}},
};

extern "C" JNIEXPORT jobject JNICALL
Java_com_phobos_emulator_PhobosCore_enumerateSystems(JNIEnv* env, jobject) {
    jclass listClass = env->FindClass("java/util/ArrayList");
    jmethodID listConstructor = env->GetMethodID(listClass, "<init>", "()V");
    jmethodID listAdd = env->GetMethodID(listClass, "add", "(Ljava/lang/Object;)Z");
    jobject list = env->NewObject(listClass, listConstructor);

    for (const auto& pair : systemExtensions) {
        jstring s = env->NewStringUTF((const char*)pair.first);
        env->CallBooleanMethod(list, listAdd, s);
        env->DeleteLocalRef(s);
    }

    return list;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_phobos_emulator_PhobosCore_loadRom(JNIEnv* env, jobject, jstring systemName, jstring uriString, jstring romName) {
    const char* nativeSystemName = env->GetStringUTFChars(systemName, 0);
    const char* nativeUriString = env->GetStringUTFChars(uriString, 0);
    const char* nativeRomName = romName ? env->GetStringUTFChars(romName, 0) : "";

    __android_log_print(ANDROID_LOG_INFO, "PhobosJNI", "loadRom: system=%s, uri=%s, rom=%s", nativeSystemName, nativeUriString, nativeRomName);

    bool success = ares::initialize(nativeSystemName, nativeUriString, nativeRomName);

    env->ReleaseStringUTFChars(systemName, nativeSystemName);
    env->ReleaseStringUTFChars(uriString, nativeUriString);
    if (nativeRomName[0]) env->ReleaseStringUTFChars(romName, nativeRomName);

    return success ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_phobos_emulator_PhobosCore_loadSecondaryRom(JNIEnv* env, jobject, jstring systemName, jstring uriString) {
    const char* nativeSystemName = env->GetStringUTFChars(systemName, 0);
    const char* nativeUriString = env->GetStringUTFChars(uriString, 0);

    __android_log_print(ANDROID_LOG_INFO, "PhobosJNI", "loadSecondaryRom: system=%s, uri=%s", nativeSystemName, nativeUriString);

    bool success = ares::loadSecondaryRom(nativeSystemName, nativeUriString);

    env->ReleaseStringUTFChars(systemName, nativeSystemName);
    env->ReleaseStringUTFChars(uriString, nativeUriString);

    return success ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT void JNICALL
Java_com_phobos_emulator_PhobosCore_unloadSystem(JNIEnv* env, jobject) {
    ares::unloadSystem();
}

extern "C" JNIEXPORT void JNICALL
Java_com_phobos_emulator_PhobosCore_setPause(JNIEnv* env, jobject, jboolean paused) {
    ares::setPause(paused);
}

extern "C" JNIEXPORT void JNICALL
Java_com_phobos_emulator_PhobosCore_setFastForward(JNIEnv* env, jobject, jboolean enabled) {
    ares::setFastForward(enabled);
}

extern "C" JNIEXPORT void JNICALL
Java_com_phobos_emulator_PhobosCore_setFastForwardSpeed(JNIEnv* env, jobject, jfloat speed) {
    ares::setFastForwardSpeed((f32)speed);
}

extern "C" JNIEXPORT void JNICALL
Java_com_phobos_emulator_PhobosCore_setN64DebugLogging(JNIEnv* env, jobject, jboolean enabled) {
    ares::setN64DebugLogging(enabled);
}

extern "C" JNIEXPORT void JNICALL
Java_com_phobos_emulator_PhobosCore_resetSystem(JNIEnv* env, jobject) {
    ares::resetSystem();
}

extern "C" JNIEXPORT void JNICALL
Java_com_phobos_emulator_PhobosCore_frameAdvance(JNIEnv* env, jobject) {
    ares::frameAdvance();
}

extern "C" JNIEXPORT void JNICALL
Java_com_phobos_emulator_PhobosCore_setMuteAudio(JNIEnv* env, jobject, jboolean muted) {
    ares::setMuteAudio(muted);
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_phobos_emulator_PhobosCore_setShader(JNIEnv* env, jobject, jstring path) {
    const char* nativePath = env->GetStringUTFChars(path, 0);
    bool success = ares::setShader(nativePath);
    env->ReleaseStringUTFChars(path, nativePath);
    return success ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_phobos_emulator_PhobosCore_saveState(JNIEnv* env, jobject, jstring path) {
    const char* nativePath = env->GetStringUTFChars(path, 0);
    bool success = ares::saveState(nativePath);
    env->ReleaseStringUTFChars(path, nativePath);
    return success ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_phobos_emulator_PhobosCore_loadState(JNIEnv* env, jobject, jstring path) {
    const char* nativePath = env->GetStringUTFChars(path, 0);
    bool success = ares::loadState(nativePath);
    env->ReleaseStringUTFChars(path, nativePath);
    return success ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_phobos_emulator_PhobosCore_takeScreenshot(JNIEnv* env, jobject, jstring path) {
    const char* nativePath = env->GetStringUTFChars(path, 0);
    bool success = ares::takeScreenshot(nativePath);
    env->ReleaseStringUTFChars(path, nativePath);
    return success ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT void JNICALL
Java_com_phobos_emulator_PhobosCore_setFastBoot(JNIEnv* env, jobject, jboolean enabled) {
    ares::setFastBoot(enabled);
}

extern "C" JNIEXPORT void JNICALL
Java_com_phobos_emulator_PhobosCore_setSkipBootRom(JNIEnv* env, jobject, jboolean enabled) {
    ares::setSkipBootRom(enabled);
}

extern "C" JNIEXPORT void JNICALL
Java_com_phobos_emulator_PhobosCore_setRegion(JNIEnv* env, jobject, jint regionIndex) {
    ares::setRegion((s32)regionIndex);
}

extern "C" JNIEXPORT void JNICALL
Java_com_phobos_emulator_PhobosCore_setN64Upscale(JNIEnv* env, jobject, jint factor) {
    ares::setN64Upscale((s32)factor);
}

extern "C" JNIEXPORT void JNICALL
Java_com_phobos_emulator_PhobosCore_setN64Recompiler(JNIEnv* env, jobject, jboolean enabled) {
    ares::setN64Recompiler(enabled);
}

extern "C" JNIEXPORT void JNICALL
Java_com_phobos_emulator_PhobosCore_setLogLevel(JNIEnv* env, jobject, jint level) {
    ares::setLogLevel((s32)level);
}

extern "C" JNIEXPORT void JNICALL
Java_com_phobos_emulator_PhobosCore_setCustomDriverPath(JNIEnv* env, jobject, jstring path) {
    const char* nativePath = env->GetStringUTFChars(path, 0);
    ares::setCustomDriverPath(nativePath);
    env->ReleaseStringUTFChars(path, nativePath);
}

extern "C" JNIEXPORT void JNICALL
Java_com_phobos_emulator_PhobosCore_setPs1AnalogMode(JNIEnv* env, jobject, jboolean enabled) {
    ares::setPs1AnalogMode(enabled);
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_phobos_emulator_PhobosCore_togglePs1AnalogMode(JNIEnv* env, jobject) {
    return ares::togglePs1AnalogMode() ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT void JNICALL
Java_com_phobos_emulator_PhobosCore_setStickToDpad(JNIEnv* env, jobject, jboolean enabled) {
    ares::setStickToDpad(enabled);
}

extern "C" JNIEXPORT void JNICALL
Java_com_phobos_emulator_PhobosCore_setN64ExpansionPak(JNIEnv* env, jobject, jboolean enabled) {
    ares::setN64ExpansionPak(enabled);
}

extern "C" JNIEXPORT void JNICALL
Java_com_phobos_emulator_PhobosCore_setN64DisableVIProcessing(JNIEnv* env, jobject, jboolean enabled) {
    ares::setN64DisableVIProcessing(enabled);
}

extern "C" JNIEXPORT void JNICALL
Java_com_phobos_emulator_PhobosCore_setN64WeaveDeinterlacing(JNIEnv* env, jobject, jboolean enabled) {
    ares::setN64WeaveDeinterlacing(enabled);
}

extern "C" JNIEXPORT void JNICALL
Java_com_phobos_emulator_PhobosCore_setN64SupersampleScanout(JNIEnv* env, jobject, jboolean enabled) {
    ares::setN64SupersampleScanout(enabled);
}

extern "C" JNIEXPORT void JNICALL
Java_com_phobos_emulator_PhobosCore_setN64ViOverclock(JNIEnv* env, jobject, jint percent) {
    ares::setN64ViOverclock((s32)percent);
}

extern "C" JNIEXPORT void JNICALL
Java_com_phobos_emulator_PhobosCore_setN64CountPerOp(JNIEnv* env, jobject, jint value) {
    ares::setN64CountPerOp((s32)value);
}

extern "C" JNIEXPORT void JNICALL
Java_com_phobos_emulator_PhobosCore_setN64CpuOverclock(JNIEnv* env, jobject, jint factor) {
    ares::setN64CpuOverclock((s32)factor);
}

extern "C" JNIEXPORT void JNICALL
Java_com_phobos_emulator_PhobosCore_setN64Pak(JNIEnv* env, jobject, jstring pakName) {
    const char* nativePakName = env->GetStringUTFChars(pakName, 0);
    ares::setN64Pak(nativePakName);
    env->ReleaseStringUTFChars(pakName, nativePakName);
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_phobos_emulator_PhobosCore_getRumbleState(JNIEnv* env, jobject) {
    return ares::getRumbleState() ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT void JNICALL
Java_com_phobos_emulator_PhobosCore_setRomFd(JNIEnv* env, jobject, jint fd) {
    ares::setRomFd((s32)fd);
}

extern "C" JNIEXPORT void JNICALL
Java_com_phobos_emulator_PhobosCore_setSecondaryRomFd(JNIEnv* env, jobject, jint fd) {
    ares::setSecondaryRomFd((s32)fd);
}

extern "C" JNIEXPORT void JNICALL
Java_com_phobos_emulator_PhobosCore_setSurface(JNIEnv* env, jobject, jobject surface) {
    ares::setSurface(env, surface);
}

extern "C" JNIEXPORT void JNICALL
Java_com_phobos_emulator_PhobosCore_setEmulationRunning(JNIEnv* env, jobject, jboolean running) {
    ares::setEmulationRunning(running);
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_phobos_emulator_PhobosCore_isFirstFrameRendered(JNIEnv* env, jobject) {
    return ares::isFirstFrameRendered() ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jint JNICALL
Java_com_phobos_emulator_PhobosCore_getBlacklistedPipelineCount(JNIEnv* env, jobject) {
    // Pipeline failure count is reported as part of getPerformanceStats(),
    // which has access to the N64 headers via PhobosRunner.cpp.
    return 0;
}

extern "C" JNIEXPORT jobject JNICALL
Java_com_phobos_emulator_PhobosCore_getNewLogs(JNIEnv* env, jobject) {
    std::vector<ares::LogEntry> logs = ares::getNewLogs();

    jclass listClass = env->FindClass("java/util/ArrayList");
    jmethodID listConstructor = env->GetMethodID(listClass, "<init>", "()V");
    jmethodID listAdd = env->GetMethodID(listClass, "add", "(Ljava/lang/Object;)Z");
    jobject list = env->NewObject(listClass, listConstructor);

    jclass entryClass = env->FindClass("com/phobos/emulator/LogEntry");
    jmethodID entryConstructor = env->GetMethodID(entryClass, "<init>", "(ILjava/lang/String;)V");

    for (const auto& entry : logs) {
        jstring s = env->NewStringUTF((const char*)entry.message);
        jobject jEntry = env->NewObject(entryClass, entryConstructor, (s32)entry.level, s);
        env->CallBooleanMethod(list, listAdd, jEntry);
        env->DeleteLocalRef(s);
        env->DeleteLocalRef(jEntry);
    }

    return list;
}

extern "C" JNIEXPORT jobject JNICALL
Java_com_phobos_emulator_PhobosCore_getSystemExtensions(JNIEnv* env, jobject, jstring systemName) {
    const char* nativeSystemName = env->GetStringUTFChars(systemName, 0);
    std::vector<string> extensions;
    auto it = systemExtensions.find(nativeSystemName);
    if (it != systemExtensions.end()) {
        extensions = it->second;
    }
    env->ReleaseStringUTFChars(systemName, nativeSystemName);

    jclass listClass = env->FindClass("java/util/ArrayList");
    jmethodID listConstructor = env->GetMethodID(listClass, "<init>", "()V");
    jmethodID listAdd = env->GetMethodID(listClass, "add", "(Ljava/lang/Object;)Z");
    jobject list = env->NewObject(listClass, listConstructor);

    for (const auto& ext : extensions) {
        jstring s = env->NewStringUTF((const char*)ext);
        env->CallBooleanMethod(list, listAdd, s);
        env->DeleteLocalRef(s);
    }

    return list;
}

extern "C" JNIEXPORT void JNICALL
Java_com_phobos_emulator_PhobosCore_setTempFilePath(JNIEnv* env, jobject, jstring path) {
    const char* nativePath = env->GetStringUTFChars(path, 0);
    ares::setTempFilePath(nativePath);
    env->ReleaseStringUTFChars(path, nativePath);
}

extern "C" JNIEXPORT void JNICALL
Java_com_phobos_emulator_PhobosCore_setLoadDiskImageToRam(JNIEnv* env, jobject, jboolean enabled) {
    ares::setLoadDiskImageToRam(enabled);
}

extern "C" JNIEXPORT void JNICALL
Java_com_phobos_emulator_PhobosCore_setOrientationMode(JNIEnv* env, jobject, jboolean vertical) {
    ares::setOrientationMode(vertical);
}

extern "C" JNIEXPORT void JNICALL
Java_com_phobos_emulator_PhobosCore_setHomePath(JNIEnv* env, jobject, jstring path) {
    const char* nativePath = env->GetStringUTFChars(path, 0);
    ares::setHomePath(nativePath);
    env->ReleaseStringUTFChars(path, nativePath);
}

extern "C" JNIEXPORT void JNICALL
Java_com_phobos_emulator_PhobosCore_setSavesPath(JNIEnv* env, jobject, jstring path) {
    const char* nativePath = env->GetStringUTFChars(path, 0);
    ares::setSavesPath(nativePath);
    env->ReleaseStringUTFChars(path, nativePath);
}

extern "C" JNIEXPORT void JNICALL
Java_com_phobos_emulator_PhobosCore_setVulkanCachePath(JNIEnv* env, jobject, jstring path) {
    const char* nativePath = env->GetStringUTFChars(path, 0);
    ares::setVulkanCachePath(nativePath);
    env->ReleaseStringUTFChars(path, nativePath);
}

extern "C" JNIEXPORT void JNICALL
Java_com_phobos_emulator_PhobosCore_setNativeLibraryDir(JNIEnv* env, jobject, jstring path) {
    const char* nativePath = env->GetStringUTFChars(path, 0);
    ares::setNativeLibraryDir(nativePath);
    env->ReleaseStringUTFChars(path, nativePath);
}

extern "C" JNIEXPORT void JNICALL
Java_com_phobos_emulator_PhobosCore_setFirmwarePath(JNIEnv* env, jobject, jstring path) {
    const char* nativePath = env->GetStringUTFChars(path, 0);
    ares::setFirmwarePath(nativePath);
    env->ReleaseStringUTFChars(path, nativePath);
}

extern "C" JNIEXPORT void JNICALL
Java_com_phobos_emulator_PhobosCore_mapFirmwareFile(JNIEnv* env, jobject, jstring name, jstring path) {
    const char* nativeName = env->GetStringUTFChars(name, 0);
    const char* nativePath = env->GetStringUTFChars(path, 0);
    ares::mapFirmwareFile(nativeName, nativePath);
    env->ReleaseStringUTFChars(name, nativeName);
    env->ReleaseStringUTFChars(path, nativePath);
}

extern "C" JNIEXPORT void JNICALL
Java_com_phobos_emulator_PhobosCore_setInput(JNIEnv* env, jobject, jfloat lx, jfloat ly, jfloat rx, jfloat ry, jint buttons) {
    ares::setInput((f32)lx, (f32)ly, (f32)rx, (f32)ry, (s32)buttons);
}

extern "C" JNIEXPORT jobject JNICALL
Java_com_phobos_emulator_PhobosCore_getPerformanceStats(JNIEnv* env, jobject) {
    ares::PerformanceStats stats = ares::getPerformanceStats();

    jclass cls = env->FindClass("com/phobos/emulator/PerformanceStats");
    jmethodID constructor = env->GetMethodID(cls, "<init>", "(DDIIZ)V");

    return env->NewObject(cls, constructor, (f64)stats.fps, (f64)stats.frameTime, (s32)stats.activeCore, (s32)stats.pipelineFailures, (jboolean)stats.isAdrenoDriver);
}
