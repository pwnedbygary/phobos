plugins {
    alias(libs.plugins.android.application)
    alias(libs.plugins.kotlinCompose)
}

fun git(vararg args: String): String? = runCatching {
    providers.exec {
        commandLine("git", *args)
        isIgnoreExitValue = true
    }.standardOutput.asText.get().trim().takeIf { it.isNotEmpty() }
}.getOrNull()

// Every build is numbered from git history, locally and on CI alike: the code is 100000 plus the commit
// count, above the codes the old tag mapping gave (v1.1.0 is 10100), so an update is never a downgrade;
// the name is the tag on a tagged commit and "<tag>-<commits since>-g<hash>" otherwise.
val gitVersionCode = git("rev-list", "--count", "HEAD")?.toIntOrNull()?.let { 100_000 + it } ?: 1
val gitVersionName = git("describe", "--tags", "--match", "v[0-9]*")?.removePrefix("v") ?: "0.0.0-dev"

android {
    namespace = "com.phobos.emulator"
    compileSdk = 37

    defaultConfig {
        applicationId = "com.phobos.emulator"
        minSdk = 26
        targetSdk = 37

        // -PversionCode / -PversionName override the numbering from git history.
        versionCode = (findProperty("versionCode") as String?)?.toInt() ?: gitVersionCode
        versionName = findProperty("versionName") as String? ?: gitVersionName

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
                    cFlags += "-march=armv8-a+simd"
                    cppFlags += "-march=armv8-a+simd"
                }
            }
        }
        create("modern") {
            dimension = "abiTarget"
            externalNativeBuild {
                cmake {
                    cFlags += "-march=armv8.2-a+fp16+dotprod"
                    cppFlags += "-march=armv8.2-a+fp16+dotprod"
                }
            }
        }
    }

    signingConfigs {
        create("release") {
            val keystoreFile = System.getenv("PHOBOS_KEYSTORE_FILE")?.let { file(it) }
                ?: rootProject.file("keystore/release.keystore")
            if (keystoreFile.exists()) {
                storeFile = keystoreFile
                storePassword = System.getenv("PHOBOS_KEYSTORE_PASSWORD") ?: "phobos-emulator"
                keyAlias = System.getenv("PHOBOS_KEY_ALIAS") ?: "phobos"
                keyPassword = System.getenv("PHOBOS_KEY_PASSWORD") ?: "phobos-emulator"
            } else {
                initWith(getByName("debug"))
            }
        }
    }

    buildTypes {
        debug {
            // Local debug builds use standard debug keystore (~/.android/debug.keystore)
            signingConfig = signingConfigs.getByName("debug")
        }
        release {
            isMinifyEnabled = false
            proguardFiles(getDefaultProguardFile("proguard-android-optimize.txt"), "proguard-rules.pro")

            // Release builds use stable release keystore so every release
            // artifact updates in-place across CI runs and releases.
            signingConfig = signingConfigs.getByName("release")
        }
    }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_11
        targetCompatibility = JavaVersion.VERSION_11
    }
    
    buildFeatures {
        compose = true
        buildConfig = true
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

// The repository's LICENSE, with every third-party notice, ships in the APK as
// assets/licenses/LICENSE.txt for Settings → About → Open-source licenses.
abstract class CopyLicenseNotices : DefaultTask() {
    @get:InputFile
    @get:PathSensitive(PathSensitivity.NONE)
    abstract val license: RegularFileProperty

    @get:OutputDirectory
    abstract val outputDir: DirectoryProperty

    @TaskAction
    fun copy() {
        val dir = outputDir.get().asFile.resolve("licenses")
        dir.mkdirs()
        license.get().asFile.copyTo(dir.resolve("LICENSE.txt"), overwrite = true)
    }
}

androidComponents {
    onVariants { variant ->
        val copyNotices = tasks.register<CopyLicenseNotices>("copy${variant.name.replaceFirstChar { it.uppercase() }}LicenseNotices") {
            license.set(rootProject.file("../LICENSE"))
        }
        variant.sources.assets?.addGeneratedSourceDirectory(copyNotices, CopyLicenseNotices::outputDir)
    }
}

// Build ALL variants (legacy + modern, debug + release) whenever assembleDebug
// runs, so one command produces every installable APK for A/B and distribution.
// Release is signed with the release keystore, or with the debug key where there
// is none (see signingConfigs above).
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
    implementation(libs.androidx.compose.material.icons.extended)
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
    // org.json's real implementation; android.jar only has throwing stubs off the device.
    testImplementation(libs.org.json)
    androidTestImplementation(libs.androidx.junit)
    androidTestImplementation(libs.androidx.espresso.core)
    androidTestImplementation(platform(libs.androidx.compose.bom))
    debugImplementation(libs.androidx.compose.ui.tooling)
}
