import os
import shutil
import subprocess

# --- 1. DEFINE FILE CONTENTS ---

CMAKE_LISTS = """cmake_minimum_required(VERSION 3.22.1)
project(PhobosAndroid)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_ANDROID_ARCH_ABI STREQUAL "arm64-v8a")
    message(WARNING "This project is heavily optimized for arm64-v8a. Proceed with caution.")
endif()

add_compile_definitions(BUILD_RELEASE)

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/ares
    ${CMAKE_CURRENT_SOURCE_DIR}/nall
    ${CMAKE_CURRENT_SOURCE_DIR}/mia
    ${CMAKE_CURRENT_SOURCE_DIR}/android/app/src/main/cpp
)

file(GLOB_RECURSE NALL_SOURCES "nall/*.cpp")
file(GLOB_RECURSE MIA_SOURCES "mia/*.cpp")
file(GLOB_RECURSE ARES_SOURCES "ares/*.cpp")

set(LIBCO_SOURCE "libco/aarch64.c")

set(ANDROID_SOURCES 
    android/app/src/main/cpp/PhobosRunner.cpp 
    android/app/src/main/cpp/PhobosJNI.cpp
)

add_library(phobos_android SHARED 
    ${NALL_SOURCES}
    ${MIA_SOURCES}
    ${ARES_SOURCES}
    ${LIBCO_SOURCE}
    ${ANDROID_SOURCES}
)

target_link_libraries(phobos_android android aaudio log)
"""

PHOBOS_RUNNER_HPP = """#pragma once

#include <thread>
#include <atomic>
#include <mutex>
#include <vector>
#include <string>
#include <android/native_window.h>
#include <aaudio/AAudio.h>
#include <ares/ares.hpp>
#include <mia/mia.hpp>

class PhobosRunner {
public:
    static PhobosRunner& instance() {
        static PhobosRunner runner;
        return runner;
    }

    bool init(const std::string& db_path);
    bool loadRom(const std::string& path);
    void start();
    void stop();
    void setSurface(ANativeWindow* window);
    void setInput(uint32_t state);

private:
    PhobosRunner() = default;
    ~PhobosRunner();

    void loop();
    void setupNodes();
    
    bool initAudio();
    void destroyAudio();
    void drawFrame(const uint32_t* pixels, uint32_t width, uint32_t height, uint32_t pitch);
    void writeAudio(const float* samples, uint32_t count);

    ares::Node::System system;
    ares::Node::Screen screen;
    ares::Node::Stream stream;
    std::vector<ares::Node::Button> input_buttons;

    std::thread emu_thread;
    std::atomic<bool> running{false};

    ANativeWindow* native_window = nullptr;
    std::mutex window_mutex;

    AAudioStream* audio_stream = nullptr;
    uint32_t current_input = 0;
};
"""

PHOBOS_RUNNER_CPP = """#include "PhobosRunner.hpp"
#include <android/log.h>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "PhobosNDK", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "PhobosNDK", __VA_ARGS__)

PhobosRunner::~PhobosRunner() {
    stop();
    destroyAudio();
}

bool PhobosRunner::init(const std::string& db_path) {
    mia::setDatabase(db_path);
    return initAudio();
}

bool PhobosRunner::loadRom(const std::string& path) {
    stop();
    auto pak = mia::identify(path);
    if (!pak) {
        LOGE("Could not identify ROM: %s", path.c_str());
        return false;
    }
    system = ares::Node::System::create(pak->system);
    if (!system || !system->load(pak)) {
        LOGE("Failed to load core system.");
        return false;
    }
    setupNodes();
    system->power();
    return true;
}

void PhobosRunner::setupNodes() {
    if (auto screens = system->find<ares::Node::Screen>()) {
        screen = screens.first();
        screen->colors(1 << 24, [](nall::vector<uint32_t>& colors) {
            for(uint32_t c : colors) {
                uint8_t r = c >> 16;
                uint8_t g = c >> 8;
                uint8_t b = c >> 0;
                colors.append((0xFF << 24) | (b << 16) | (g << 8) | (r << 0));
            }
        });
    }

    if (auto streams = system->find<ares::Node::Stream>()) {
        stream = streams.first();
        stream->resampler->frequency(48000.0);
    }

    input_buttons.clear();
    for (auto port : system->find<ares::Node::Port>()) {
        for (auto peripheral : port->find<ares::Node::Peripheral>()) {
            for (auto button : peripheral->find<ares::Node::Button>()) {
                input_buttons.push_back(button);
            }
        }
    }
}

void PhobosRunner::start() {
    if (running) return;
    running = true;
    emu_thread = std::thread(&PhobosRunner::loop, this);
}

void PhobosRunner::stop() {
    running = false;
    if (emu_thread.joinable()) emu_thread.join();
    if (system) system->unload();
}

void PhobosRunner::loop() {
    while (running) {
        for (size_t i = 0; i < input_buttons.size(); i++) {
            bool is_pressed = (current_input & (1 << i)) != 0;
            input_buttons[i]->setValue(is_pressed);
        }

        system->run();

        if (screen) {
            drawFrame(screen->pixels->data(), screen->width(), screen->height(), screen->pitch());
        }

        if (stream) {
            uint32_t count = stream->pending();
            if(count > 0) {
                float* samples = new float[count * 2];
                stream->read(samples, count);
                writeAudio(samples, count);
                delete[] samples;
            }
        }
    }
}

void PhobosRunner::setSurface(ANativeWindow* window) {
    std::lock_guard<std::mutex> lock(window_mutex);
    if (native_window) ANativeWindow_release(native_window);
    native_window = window;
    if (native_window) ANativeWindow_acquire(native_window);
}

void PhobosRunner::setInput(uint32_t state) {
    current_input = state;
}

void PhobosRunner::drawFrame(const uint32_t* pixels, uint32_t width, uint32_t height, uint32_t pitch) {
    std::lock_guard<std::mutex> lock(window_mutex);
    if (!native_window) return;

    ANativeWindow_Buffer buffer;
    if (ANativeWindow_lock(native_window, &buffer, nullptr) == 0) {
        auto* dst = static_cast<uint32_t*>(buffer.bits);
        for (uint32_t y = 0; y < height; y++) {
            memcpy(dst + (y * buffer.stride), pixels + (y * pitch), width * sizeof(uint32_t));
        }
        ANativeWindow_unlockAndPost(native_window);
    }
}

bool PhobosRunner::initAudio() {
    AAudioStreamBuilder* builder;
    AAudio_createStreamBuilder(&builder);
    AAudioStreamBuilder_setPerformanceMode(builder, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
    AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_FLOAT);
    AAudioStreamBuilder_setChannelCount(builder, 2);
    AAudioStreamBuilder_setSampleRate(builder, 48000);
    
    if (AAudioStreamBuilder_openStream(builder, &audio_stream) != AAUDIO_OK) {
        LOGE("Failed to open AAudio stream");
        return false;
    }
    AAudioStreamBuilder_delete(builder);
    AAudioStream_requestStart(audio_stream);
    return true;
}

void PhobosRunner::destroyAudio() {
    if (audio_stream) {
        AAudioStream_requestStop(audio_stream);
        AAudioStream_close(audio_stream);
        audio_stream = nullptr;
    }
}

void PhobosRunner::writeAudio(const float* samples, uint32_t count) {
    if (audio_stream) {
        AAudioStream_write(audio_stream, samples, count, INT64_MAX);
    }
}
"""

PHOBOS_JNI_CPP = """#include <jni.h>
#include <android/native_window_jni.h>
#include "PhobosRunner.hpp"

extern "C" {

JNIEXPORT jboolean JNICALL
Java_com_phobos_emulator_PhobosCore_init(JNIEnv* env, jobject, jstring db_path) {
    const char* path = env->GetStringUTFChars(db_path, nullptr);
    bool result = PhobosRunner::instance().init(path);
    env->ReleaseStringUTFChars(db_path, path);
    return result ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_com_phobos_emulator_PhobosCore_loadRom(JNIEnv* env, jobject, jstring rom_path) {
    const char* path = env->GetStringUTFChars(rom_path, nullptr);
    bool result = PhobosRunner::instance().loadRom(path);
    env->ReleaseStringUTFChars(rom_path, path);
    return result ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_com_phobos_emulator_PhobosCore_start(JNIEnv* env, jobject) {
    PhobosRunner::instance().start();
}

JNIEXPORT void JNICALL
Java_com_phobos_emulator_PhobosCore_stop(JNIEnv* env, jobject) {
    PhobosRunner::instance().stop();
}

JNIEXPORT void JNICALL
Java_com_phobos_emulator_PhobosCore_setSurface(JNIEnv* env, jobject, jobject surface) {
    if (surface) {
        ANativeWindow* window = ANativeWindow_fromSurface(env, surface);
        ANativeWindow_setBuffersGeometry(window, 0, 0, WINDOW_FORMAT_RGBA_8888);
        PhobosRunner::instance().setSurface(window);
    } else {
        PhobosRunner::instance().setSurface(nullptr);
    }
}

JNIEXPORT void JNICALL
Java_com_phobos_emulator_PhobosCore_setInput(JNIEnv* env, jobject, jint button_mask) {
    PhobosRunner::instance().setInput(static_cast<uint32_t>(button_mask));
}

}
"""

PHOBOS_CORE_KT = """package com.phobos.emulator

import android.view.Surface

object PhobosCore {
    init {
        System.loadLibrary("phobos_android")
    }

    external fun init(dbPath: String): Boolean
    external fun loadRom(path: String): Boolean
    external fun start()
    external fun stop()
    external fun setSurface(surface: Surface?)
    external fun setInput(buttonMask: Int)
}
"""

PHOBOS_ACTIVITY_KT = """package com.phobos.emulator

import android.os.Bundle
import android.view.SurfaceHolder
import android.view.SurfaceView
import androidx.appcompat.app.AppCompatActivity
import java.io.File

class PhobosActivity : AppCompatActivity(), SurfaceHolder.Callback {
    private lateinit var surfaceView: SurfaceView

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        surfaceView = SurfaceView(this)
        surfaceView.holder.addCallback(this)
        setContentView(surfaceView)

        val dbPath = File(filesDir, "Database").absolutePath
        
        if (PhobosCore.init(dbPath)) {
            // Note: Manage READ_EXTERNAL_STORAGE permissions before this call.
            // PhobosCore.loadRom("/storage/emulated/0/Download/game.sfc")
        }
    }

    override fun surfaceCreated(holder: SurfaceHolder) {
        PhobosCore.setSurface(holder.surface)
        PhobosCore.start()
    }

    override fun surfaceChanged(holder: SurfaceHolder, format: Int, width: Int, height: Int) {}

    override fun surfaceDestroyed(holder: SurfaceHolder) {
        PhobosCore.stop()
        PhobosCore.setSurface(null)
    }
}
"""

# --- 2. EXECUTE SCRIPT LOGIC ---

def run_cmd(command, ignore_errors=False):
    print(f"Running: {' '.join(command)}")
    result = subprocess.run(command, capture_output=True, text=True)
    if result.returncode != 0 and not ignore_errors:
        print(f"Error executing {' '.join(command)}:\n{result.stderr}")
    return result

def main():
    print("🚀 Starting Phobos Android Scaffolding Process...")

    # 1. Remove bloatware (we use standard Git RM so it tracks the deletion)
    bloat_dirs = ["desktop-ui", "hiro", "ruby"]
    for d in bloat_dirs:
        if os.path.exists(d):
            run_cmd(["git", "rm", "-rf", d], ignore_errors=True)
            print(f"🗑️ Removed {d}/")

    # 2. Create Android Directory Structure natively for Phobos
    cpp_dir = os.path.join("android", "app", "src", "main", "cpp")
    kt_dir = os.path.join("android", "app", "src", "main", "java", "com", "phobos", "emulator")
    os.makedirs(cpp_dir, exist_ok=True)
    os.makedirs(kt_dir, exist_ok=True)
    print("📁 Created Android directory structure.")

    # 3. Write Files with the Phobos namespace
    files_to_write = {
        "CMakeLists.txt": CMAKE_LISTS,
        os.path.join(cpp_dir, "PhobosRunner.hpp"): PHOBOS_RUNNER_HPP,
        os.path.join(cpp_dir, "PhobosRunner.cpp"): PHOBOS_RUNNER_CPP,
        os.path.join(cpp_dir, "PhobosJNI.cpp"): PHOBOS_JNI_CPP,
        os.path.join(kt_dir, "PhobosCore.kt"): PHOBOS_CORE_KT,
        os.path.join(kt_dir, "PhobosActivity.kt"): PHOBOS_ACTIVITY_KT,
    }

    for path, content in files_to_write.items():
        with open(path, "w", encoding="utf-8") as f:
            f.write(content)
        print(f"✏️ Wrote {path}")

    # 4. Commit to Git
    print("📦 Staging files for git...")
    run_cmd(["git", "add", "."])

    print("💾 Creating commit...")
    commit_msg = "feat: Phobos Android 64-bit native scaffolding\n\n- Removed desktop-ui, hiro, and ruby.\n- Added ANativeWindow and AAudio integrations for Phobos.\n- Created Kotlin JNI bindings."
    run_cmd(["git", "commit", "-m", commit_msg])

    print("\n✅ Done! The code has been ported to the Phobos namespace and committed to your local fork.")
    print("You can now open the 'android' folder in Android Studio (after adding basic gradle configs) or push this branch to GitHub.")

if __name__ == "__main__":
    main()