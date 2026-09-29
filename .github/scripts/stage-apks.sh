#!/usr/bin/env bash
# Stages the release APKs for upload as Phobos-<version>-<Flavor>.apk, with update.json beside them:
# the build's version code and name, and each flavor's APK name, size and SHA-256, which the in-app
# updater reads.
#
# Usage: stage-apks.sh <apk outputs dir> <dist dir> <channel: stable|nightly> <commit>
set -euo pipefail

apk_dir=$1
dist=$2
channel=$3
commit=$4
aapt2="${ANDROID_SDK_ROOT:-${ANDROID_HOME:?set ANDROID_SDK_ROOT or ANDROID_HOME}}/build-tools/36.0.0/aapt2"

version() {
    local badging
    badging=$("$aapt2" dump badging "$apk_dir/$1/release/app-$1-release.apk" | head -n 1)
    echo "$(sed -E "s/.*versionCode='([0-9]+)'.*/\1/" <<< "$badging") $(sed -E "s/.*versionName='([^']+)'.*/\1/" <<< "$badging")"
}

read -r code name <<< "$(version modern)"
if [[ "$(version legacy)" != "$code $name" ]]; then
    echo "The legacy and modern APKs have different versions" >&2
    exit 1
fi

mkdir -p "$dist"
entry() {
    local file="$dist/Phobos-$name-$2.apk"
    cp "$apk_dir/$1/release/app-$1-release.apk" "$file"
    jq -n --arg name "$(basename "$file")" \
        --argjson size "$(wc -c < "$file" | tr -d ' ')" \
        --arg sha256 "$(shasum -a 256 "$file" | cut -d ' ' -f 1)" \
        '{name: $name, size: $size, sha256: $sha256}'
}
legacy=$(entry legacy Legacy)
modern=$(entry modern Modern)

jq -n --argjson versionCode "$code" --arg versionName "$name" --arg channel "$channel" --arg commit "$commit" \
    --argjson legacy "$legacy" --argjson modern "$modern" \
    '{versionCode: $versionCode, versionName: $versionName, channel: $channel, commit: $commit, apks: {legacy: $legacy, modern: $modern}}' \
    > "$dist/update.json"

ls -l "$dist"
cat "$dist/update.json"
