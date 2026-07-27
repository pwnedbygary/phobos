import os

ROOT_BUILD_GRADLE = """// Top-level build file
plugins {
    id 'com.android.application' version '8.3.0' apply false
    id 'org.jetbrains.kotlin.android' version '1.9.22' apply false
}
"""

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

WRAPPER_PROPERTIES = """distributionBase=GRADLE_USER_HOME
distributionPath=wrapper/dists
distributionUrl=https\\://services.gradle.org/distributions/gradle-8.4-bin.zip
zipStoreBase=GRADLE_USER_HOME
zipStorePath=wrapper/dists
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
        minSdk 26
        targetSdk 34
        versionCode 1
        versionName "1.0"

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

def write_file(path, content):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        f.write(content)
    print(f"✏️ Wrote {path}")

def main():
    print("🛠️ Patching Gradle configuration for modern Android Studio...")

    base_dir = "android"
    app_dir = os.path.join(base_dir, "app")
    wrapper_dir = os.path.join(base_dir, "gradle", "wrapper")

    files_to_create = {
        os.path.join(base_dir, "build.gradle"): ROOT_BUILD_GRADLE,
        os.path.join(base_dir, "settings.gradle"): SETTINGS_GRADLE,
        os.path.join(app_dir, "build.gradle"): APP_BUILD_GRADLE,
        os.path.join(wrapper_dir, "gradle-wrapper.properties"): WRAPPER_PROPERTIES,
    }

    for path, content in files_to_create.items():
        write_file(path, content)

    # Git stage and commit
    os.system("git add android/")
    os.system('git commit -m "fix: Modernize Gradle DSL and lock wrapper to v8.4"')
    
    print("\n✅ Gradle configuration patched successfully!")

if __name__ == "__main__":
    main()
