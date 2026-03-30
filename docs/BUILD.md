# Build Notes

This repository contains:

- `libghostty_ohos/`: the reusable `HAR` module
- `example/`: a minimal example app that consumes the `HAR`

## Prerequisites

- DevEco Studio / HarmonyOS SDK with `hvigor`
- `ohpm`
- `git`
- `docker`

## Refresh `libghostty_vt.a`

The native module links a generated static archive at:

`libghostty_ohos/prebuilt/arm64-v8a/libghostty_vt.a`

Refresh it with:

```sh
./tools/build-ghostty-vt-docker.sh
```

What the script does:

- clones or updates upstream Ghostty
- builds `libghostty-vt` for the configured target
- locates upstream `libsimdutf.a`
- locates upstream `libhighway.a`
- merges all three archives into the output `libghostty_vt.a`

Environment variables supported by the script:

- `GHOSTTY_REPO_URL`
- `GHOSTTY_DIR`
- `BUILD_PREFIX`
- `TARGET_TRIPLE`
- `OPTIMIZE_MODE`
- `OUTPUT_DIR`
- `OUTPUT_LIB`

## Refresh The Bundled Fish HNP

The example app ships a bundled shell package at:

`example/src/main/ets/hnp/fish.hnp`

Rebuild it with:

```sh
./tools/build-fish-ohos.sh
```

This script downloads and builds:

- `fish`
- `starship`
- `fastfetch`

and packs them into the HNP consumed by the example app.

## Install Project Dependencies

```sh
ohpm install
```

## Configure Local Signing

The checked-in [build-profile.json5](/Users/uyakauleu/vivy/experiments/NITerm/build-profile.json5) intentionally does not contain app signing config.

Before building the example app package, merge your own local HarmonyOS signing config into the existing `app` block in `build-profile.json5`.

Minimal fragment:

```json5
// add under "app"
"signingConfigs": [
  {
    "name": "local",
    "type": "HarmonyOS",
    "material": {
      "certpath": "/absolute/path/to/your-debug.cer",
      "keyAlias": "debugKey",
      "keyPassword": "your-key-password",
      "profile": "/absolute/path/to/your-debug.p7b",
      "signAlg": "SHA256withECDSA",
      "storeFile": "/absolute/path/to/your-debug.p12",
      "storePassword": "your-store-password"
    }
  }
]

// and set the existing default product to use it
"products": [
  {
    "name": "default",
    "signingConfig": "local"
  }
]
```

In other words, keep the tracked module and product structure, and only add:

- `app.signingConfigs`
- `app.products[default].signingConfig`

Example result:

```json5
{
  "app": {
    "signingConfigs": [
      {
        "name": "local",
        "type": "HarmonyOS",
        "material": {
          "certpath": "/absolute/path/to/your-debug.cer",
          "keyAlias": "debugKey",
          "keyPassword": "your-key-password",
          "profile": "/absolute/path/to/your-debug.p7b",
          "signAlg": "SHA256withECDSA",
          "storeFile": "/absolute/path/to/your-debug.p12",
          "storePassword": "your-store-password"
        }
      }
    ],
    "products": [
      {
        "name": "default",
        "signingConfig": "local"
      }
    ]
  }
}
```

If you prefer repo-relative local files, place them in an ignored `signature/` directory and point `certpath`, `profile`, and `storeFile` there.

This only affects packaging the `example` app. Building the `libghostty_ohos` HAR does not require app signing.

## Build The Example App

```sh
/Applications/DevEco-Studio.app/Contents/tools/hvigor/hvigor/bin/hvigor.js assembleApp -m project --no-daemon
```

This verifies both:

- the `libghostty_ohos` HAR module
- the `example` app that imports `@wiedymi/libghostty-ohos`

## Headless Emulator Loop

If `hdc list targets` shows an emulator or device, you can run the local loop script:

```sh
bash tools/ohos-headless-loop.sh run
```

Useful subcommands:

- `bash tools/ohos-headless-loop.sh build`
- `bash tools/ohos-headless-loop.sh install`
- `bash tools/ohos-headless-loop.sh start`
- `bash tools/ohos-headless-loop.sh logs`

By default the script uses:

- `~/Library/OpenHarmony/Sdk/20/toolchains/hdc`
- `/Applications/DevEco-Studio.app/Contents/tools/hvigor/hvigor/bin/hvigor.js`
- bundle `com.wiedymi.libghosttyohos.example`
- ability `EntryAbility`

You can override those via `OHOS_HDC`, `OHOS_HVIGOR`, `OHOS_APP_BUNDLE`, `OHOS_APP_ABILITY`, and `OHOS_HAP_PATH`.

## Native Build Inputs

The native module is defined by:

- `libghostty_ohos/src/main/cpp/CMakeLists.txt`
- `libghostty_ohos/build-profile.json5`

It currently links:

- HarmonyOS native SDK libraries
- `libghostty_vt.a`

## Updating Third-Party Materials

When you update any of the following, refresh `THIRD_PARTY_NOTICES.md`:

- Ghostty / `libghostty-vt`
- `simdutf`
- `highway`
- bundled fonts or theme resources
