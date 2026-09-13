# Phobos on Replit

This is a native Android project, not a web app. Preserve the Kotlin/Compose,
C++/JNI, Gradle and CMake structure. No browser server or deployment is needed.

## Build

The Replit Run command builds both ARM64 release flavors. From the Shell:

```sh
bash scripts/build-replit.sh
```

For just one flavor or another Gradle task:

```sh
bash scripts/build-replit.sh assembleLegacyRelease
bash scripts/build-replit.sh assembleModernRelease
bash scripts/build-replit.sh assembleLegacyDebug
```

Avoid `assembleDebug` when you only want one debug APK: the existing Gradle
configuration makes it build all four variants.

APKs are written to `android/app/build/outputs/apk/<flavor>/<type>/`.
They require an Android ARM64 device for runtime testing; building them does
not verify emulator behavior.

## Toolchain

JDK 17 is installed through `.replit` Nix packages. The build script explicitly
selects the JDK containing `javac`. The SDK lives in the ignored
`.local/android-sdk` directory, and the build script sets its paths only for
the build process. Gradle's checksum-pinned wrapper downloads Gradle 9.6.0.

To install the SDK again in a fresh workspace:

```sh
bash scripts/setup-android-replit.sh
```

The setup script prompts for Android SDK licenses and installs platform 37.0,
build-tools 36.0.0, NDK 28.2.13676358, CMake 3.22.1, and platform-tools.
Gradle may install additional required SDK components. Native dependencies
are restored with recursive Git submodule initialization.

## Signing and Git

Existing release signing behavior is unchanged: without the configured release
keystore, Gradle uses the local debug key. These builds are for sideload testing,
not production distribution. They may not update an installed APK signed with
another key. Do not uninstall an existing app without backing up its data.
Never commit signing keys or credentials.

`origin` points to `https://github.com/pwnedbygary/phobos.git`; local `master`
tracks `origin/master`. The ZIP import was preserved while joining its unrelated
history to upstream. Nothing was pushed as part of setup.