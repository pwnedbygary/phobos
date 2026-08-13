plugins {
    alias(libs.plugins.android.application)
    alias(libs.plugins.kotlinCompose)
}

android {
    namespace = "com.phobos.emulator"
    compileSdk = 37

    defaultConfig {
        applicationId = "com.phobos.emulator"
        minSdk = 26
        targetSdk = 37
        versionCode = 1
        versionName = "1.0"

        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"

        externalNativeBuild {
            cmake {
                cppFlags += "-std=c++20"
                arguments += "-DCMAKE_BUILD_TYPE=Release"
                arguments += "-DANDROID_ARM_NEON=TRUE"
            }
        }

        ndk {
            abiFilters += "arm64-v8a"
        }
    }

    flavorDimensions += "abiTarget"
    productFlavors {
        create("legacy") {
            dimension = "abiTarget"
            externalNativeBuild {
                cmake {
                    cppFlags += "-march=armv8-a+simd"
                }
            }
        }
        create("modern") {
            dimension = "abiTarget"
            externalNativeBuild {
                cmake {
                    cppFlags += "-march=armv8.2-a+fp16+dotprod"
                }
            }
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
            proguardFiles(getDefaultProguardFile("proguard-android-optimize.txt"), "proguard-rules.pro")

            // Sign release with the debug keystore so every variant is
            // installable for A/B testing / sideloading without a manual
            // apksigner step (matches the debug cert → updates in place).
            signingConfig = signingConfigs.getByName("debug")
        }
    }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_11
        targetCompatibility = JavaVersion.VERSION_11
    }
    
    buildFeatures {
        compose = true
    }

    packaging {
        jniLibs {
            useLegacyPackaging = true
        }
    }

    externalNativeBuild {
        cmake {
            path = file("../../CMakeLists.txt")
            version = "3.22.1"
        }
    }
}

// Build ALL variants (legacy + modern, debug + release) whenever assembleDebug
// runs, so one command produces every installable APK for A/B and distribution.
// Release is signed with the debug keystore (see buildTypes.release above).
afterEvaluate {
    tasks.named("assembleDebug") {
        dependsOn("assembleLegacyDebug", "assembleModernDebug", "assembleLegacyRelease", "assembleModernRelease")
    }
}

dependencies {
    implementation(libs.androidx.core.ktx)
    implementation(libs.androidx.appcompat)
    implementation(libs.material)
    
    // Compose
    implementation(platform(libs.androidx.compose.bom))
    implementation(libs.androidx.compose.ui)
    implementation(libs.androidx.compose.ui.graphics)
    implementation(libs.androidx.compose.ui.tooling.preview)
    implementation(libs.androidx.compose.material3)
    // implementation(libs.androidx.compose.material.icons.extended)
    implementation(libs.androidx.activity.compose)
    implementation(libs.androidx.navigation.compose)
    implementation(libs.androidx.lifecycle.viewmodel.compose)
    
    // Coil
    implementation(libs.coil.compose)
    implementation(libs.coil.svg)
    
    // DataStore & SAF
    implementation(libs.androidx.datastore.preferences)
    implementation(libs.androidx.documentfile)

    testImplementation(libs.junit)
    androidTestImplementation(libs.androidx.junit)
    androidTestImplementation(libs.androidx.espresso.core)
    androidTestImplementation(platform(libs.androidx.compose.bom))
    debugImplementation(libs.androidx.compose.ui.tooling)
}
