import os

# --- 1. DEFINE GRADLE & MANIFEST FILES ---

SETTINGS_GRADLE = """pluginManagement {
    repositories {
        google()
        mavenCentral()
        gradlePluginPortal()
    }
}
dependencyResolutionManagement {
    repositoriesMode.set(RepositoriesMode.FAIL_ON_PROJECT_REPOS)
    repositories {
        google()
        mavenCentral()
    }
}
rootProject.name = "Phobos"
include ':app'
"""

PROJECT_BUILD_GRADLE = """// Top-level build file
buildscript {
    ext.kotlin_version = '1.9.0'
    repositories {
        google()
        mavenCentral()
    }
    dependencies {
        classpath 'com.android.tools.build:gradle:8.1.0'
        classpath "org.jetbrains.kotlin:kotlin-gradle-plugin:$kotlin_version"
    }
}
"""

APP_BUILD_GRADLE = """plugins {
    id 'com.android.application'
    id 'org.jetbrains.kotlin.android'
}

android {
    namespace 'com.phobos.emulator'
    compileSdk 34

    defaultConfig {
        applicationId "com.phobos.emulator"
        minSdk 26 // Required for AAudio
        targetSdk 34
        versionCode 1
        versionName "1.0"

        // Force 64-bit ARM architecture
        ndk {
            abiFilters 'arm64-v8a'
        }

        externalNativeBuild {
            cmake {
                cppFlags "-std=c++20 -O3 -flto"
                arguments "-DANDROID_STL=c++_shared"
            }
        }
    }

    buildTypes {
        release {
            minifyEnabled false
            proguardFiles getDefaultProguardFile('proguard-android-optimize.txt'), 'proguard-rules.pro'
        }
    }

    externalNativeBuild {
        cmake {
            path "../../../CMakeLists.txt"
            version "3.22.1"
        }
    }

    compileOptions {
        sourceCompatibility JavaVersion.VERSION_1_8
        targetCompatibility JavaVersion.VERSION_1_8
    }

    kotlinOptions {
        jvmTarget = '1.8'
    }
}

dependencies {
    implementation 'androidx.core:core-ktx:1.12.0'
    implementation 'androidx.appcompat:appcompat:1.6.1'
}
"""

ANDROID_MANIFEST = """<?xml version="1.0" encoding="utf-8"?>
<manifest xmlns:android="http://schemas.android.com/apk/res/android"
    package="com.phobos.emulator">

    <!-- Needed to load ROMs from the device -->
    <uses-permission android:name="android.permission.READ_EXTERNAL_STORAGE"/>

    <!-- Ares requires decent hardware -->
    <uses-feature android:glEsVersion="0x00020000" android:required="true" />

    <application
        android:allowBackup="true"
        android:label="Phobos"
        android:supportsRtl="true"
        android:theme="@style/Theme.AppCompat.NoActionBar">

        <!-- The Main Activity we generated earlier -->
        <activity
            android:name=".PhobosActivity"
            android:exported="true"
            android:configChanges="orientation|screenSize|keyboardHidden|uiMode">
            <intent-filter>
                <action android:name="android.intent.action.MAIN" />
                <category android:name="android.intent.category.LAUNCHER" />
            </intent-filter>
        </activity>

    </application>
</manifest>
"""

GRADLE_PROPERTIES = """org.gradle.jvmargs=-Xmx2048m -Dfile.encoding=UTF-8
android.useAndroidX=true
android.nonTransitiveRClass=true
"""

def write_file(path, content):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        f.write(content)
    print(f"✏️  Wrote {path}")

def main():
    print("🚀 Setting up Android Gradle System for Phobos...")

    base_dir = "android"
    app_dir = os.path.join(base_dir, "app")
    src_main_dir = os.path.join(app_dir, "src", "main")

    files_to_create = {
        os.path.join(base_dir, "settings.gradle"): SETTINGS_GRADLE,
        os.path.join(base_dir, "build.gradle"): PROJECT_BUILD_GRADLE,
        os.path.join(base_dir, "gradle.properties"): GRADLE_PROPERTIES,
        os.path.join(app_dir, "build.gradle"): APP_BUILD_GRADLE,
        os.path.join(src_main_dir, "AndroidManifest.xml"): ANDROID_MANIFEST,
    }

    for path, content in files_to_create.items():
        write_file(path, content)

    # Git stage and commit
    os.system("git add android/")
    os.system('git commit -m "chore: Add Android Studio Gradle and Manifest configuration"')

    print("\n✅ Gradle environment generated and committed!")
    print("\n=======================================================")
    print("🎯 NEXT STEPS:")
    print("1. Open Android Studio.")
    print("2. Click 'Open' and select the 'phobos/android' folder (NOT the root phobos folder, select the 'android' folder specifically).")
    print("3. Let Android Studio sync the Gradle files.")
    print("4. Click the 'Make Project' hammer icon or try to build the APK!")
    print("=======================================================")

if __name__ == "__main__":
    main()
