#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SDK_NATIVE="${OHOS_SDK_NATIVE:-$HOME/Library/OpenHarmony/Sdk/20/native}"
DEVECO_SDK_HOME="${DEVECO_SDK_HOME:-/Applications/DevEco-Studio.app/Contents/sdk}"
TARGET="${OHOS_TARGET:-aarch64-unknown-linux-ohos}"
LINKER="${OHOS_LINKER:-$SDK_NATIVE/llvm/bin/aarch64-unknown-linux-ohos-clang}"
STRIP="${OHOS_STRIP:-$SDK_NATIVE/llvm/bin/llvm-strip}"
HNPCLI="${OHOS_HNPCLI:-$DEVECO_SDK_HOME/default/openharmony/toolchains/hnpcli}"

BUILD_DIR="${ROOT_DIR}/.cache/fish-ohos"
DIST_DIR="${BUILD_DIR}/distfiles"
SRC_DIR="${BUILD_DIR}/src"
OUT_DIR="${BUILD_DIR}/out"
PREFIX_DIR="${OUT_DIR}/prefix"
HNP_SRC_DIR="${OUT_DIR}/hnp-src/fish"
HNP_OUT_DIR="${ROOT_DIR}/example/src/main/ets/hnp"
SYSROOT="${OHOS_SYSROOT:-$SDK_NATIVE/sysroot}"
CMAKE_BIN="${OHOS_CMAKE:-$SDK_NATIVE/build-tools/cmake/bin/cmake}"
NINJA_BIN="${OHOS_NINJA:-$SDK_NATIVE/build-tools/cmake/bin/ninja}"

FISH_VERSION="${FISH_VERSION:-4.2.1}"
STARSHIP_VERSION="${STARSHIP_VERSION:-1.24.1}"
FASTFETCH_VERSION="${FASTFETCH_VERSION:-2.60.0}"
FISH_TAR="fish-${FISH_VERSION}.tar.xz"
FISH_URL="${FISH_URL:-https://github.com/fish-shell/fish-shell/releases/download/${FISH_VERSION}/${FISH_TAR}}"
FASTFETCH_TAR="fastfetch-${FASTFETCH_VERSION}.tar.gz"
FASTFETCH_URL="${FASTFETCH_URL:-https://api.github.com/repos/fastfetch-cli/fastfetch/tarball/${FASTFETCH_VERSION}}"

mkdir -p "$DIST_DIR" "$SRC_DIR" "$OUT_DIR" "$PREFIX_DIR" "$HNP_SRC_DIR" "$HNP_OUT_DIR"

if [[ ! -x "$HNPCLI" ]]; then
  echo "hnpcli not found at $HNPCLI" >&2
  exit 1
fi

if [[ ! -x "$LINKER" ]]; then
  echo "OHOS linker not found at $LINKER" >&2
  exit 1
fi

if [[ ! -x "$CMAKE_BIN" ]]; then
  echo "cmake not found at $CMAKE_BIN" >&2
  exit 1
fi

if [[ ! -x "$NINJA_BIN" ]]; then
  echo "ninja not found at $NINJA_BIN" >&2
  exit 1
fi

download() {
  local url="$1"
  local output="$2"
  if [[ -f "$output" ]]; then
    return
  fi
  curl -L --fail --retry 3 "$url" -o "$output"
}

extract() {
  local archive="$1"
  local destination="$2"
  rm -rf "$destination"
  mkdir -p "$destination"
  tar -xf "$archive" -C "$destination" --strip-components=1
}

patch_fish_for_ohos() {
  local fish_dir="$1"

  perl -0pi -e 's/fn detect_apple\(_: &Target\) -> bool \{\n    cfg!\(any\(target_os = "ios", target_os = "macos"\)\)\n\}/fn detect_apple(_: \&Target) -> bool {\n    matches!(env_var("CARGO_CFG_TARGET_OS").as_deref(), Some("ios") | Some("macos"))\n\}/' \
    "$fish_dir/build.rs"

  perl -0pi -e 's@    fn format_for_contents\(s: &\[u8\]\) -> UvarFormat \{\n.*?\n    \}@    fn format_for_contents(s: &[u8]) -> UvarFormat {\n        let iter = LineIterator::new(s);\n        for line in iter {\n            if line.is_empty() {\n                continue;\n            }\n            if line[0] != b'\''#'\'' {\n                break;\n            }\n            let Some(rest) = line.strip_prefix(b\"# VERSION: \") else {\n                continue;\n            };\n            let version = rest\n                .split(|&b| matches!(b, b'\'' '\'' | b'\''\\t'\'' | b'\''\\r'\'' | b'\''\\n'\''))\n                .next()\n                .unwrap_or(rest);\n            return if version == UVARS_VERSION_3_0 {\n                UvarFormat::fish_3_0\n            } else {\n                UvarFormat::future\n            };\n        }\n        UvarFormat::fish_2_x\n    }@s' \
    "$fish_dir/src/env_universal_common.rs"

  perl -0pi -e 's@fn check_for_orphaned_process\(loop_count: usize, shell_pgid: libc::pid_t\) -> bool \{\n@fn check_for_orphaned_process(loop_count: usize, shell_pgid: libc::pid_t) -> bool {\n    if cfg!(target_env = "ohos") {\n        let _ = (loop_count, shell_pgid);\n        return false;\n    }\n@' \
    "$fish_dir/src/reader/reader.rs"

  perl -0pi -e 's@fn acquire_tty_or_exit\(shell_pgid: libc::pid_t\) \{\n    assert_is_main_thread\(\);\n@fn acquire_tty_or_exit(shell_pgid: libc::pid_t) {\n    assert_is_main_thread();\n\n    if cfg!(target_env = "ohos") {\n        let owner = unsafe { libc::tcgetpgrp(STDIN_FILENO) };\n        if owner == shell_pgid || owner == -1 {\n            return;\n        }\n    }\n@' \
    "$fish_dir/src/reader/reader.rs"

}

build_starship_compat() {
  local compat_c="$OUT_DIR/starship_compat.c"
  local compat_o="$OUT_DIR/starship_compat.o"
  cat > "$compat_c" <<'EOF'
#include <stddef.h>
#include <string.h>

int __xpg_strerror_r(int errnum, char *buf, size_t buflen) {
    return strerror_r(errnum, buf, buflen);
}
EOF
  "$LINKER" --target="$TARGET" --sysroot="$SYSROOT" -c "$compat_c" -o "$compat_o"
}

build_fastfetch() {
  local source_dir="$SRC_DIR/fastfetch"
  local build_dir="$OUT_DIR/fastfetch-build"

  download "$FASTFETCH_URL" "$DIST_DIR/$FASTFETCH_TAR"
  extract "$DIST_DIR/$FASTFETCH_TAR" "$source_dir"
  perl -0pi -e 's/#if FF_HAVE_UTMPX/#if FF_HAVE_UTMPX \&\& !defined(__OHOS__)/' \
    "$source_dir/src/detection/users/users_linux.c"
  perl -0pi -e 's/#define getutxent getutent/#define getutxent getutent\n    #define endutxent endutent/' \
    "$source_dir/src/detection/users/users_linux.c"
  mkdir -p "$build_dir"

  "$CMAKE_BIN" -S "$source_dir" -B "$build_dir" -G Ninja \
    -DCMAKE_MAKE_PROGRAM="$NINJA_BIN" \
    -DCMAKE_SYSTEM_NAME=Linux \
    -DCMAKE_C_COMPILER="$LINKER" \
    -DCMAKE_C_COMPILER_TARGET="$TARGET" \
    -DCMAKE_SYSROOT="$SYSROOT" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$PREFIX_DIR" \
    -DBUILD_FLASHFETCH=OFF \
    -DBUILD_TESTS=OFF \
    -DSET_TWEAK=OFF \
    -DENABLE_LTO=OFF \
    -DENABLE_THREADS=ON \
    -DENABLE_ZLIB=ON \
    -DENABLE_WORDEXP=OFF \
    -DENABLE_VULKAN=OFF \
    -DENABLE_WAYLAND=OFF \
    -DENABLE_XCB_RANDR=OFF \
    -DENABLE_XRANDR=OFF \
    -DENABLE_DRM=OFF \
    -DENABLE_DRM_AMDGPU=OFF \
    -DENABLE_GIO=OFF \
    -DENABLE_DCONF=OFF \
    -DENABLE_DBUS=OFF \
    -DENABLE_SQLITE3=OFF \
    -DENABLE_RPM=OFF \
    -DENABLE_IMAGEMAGICK7=OFF \
    -DENABLE_IMAGEMAGICK6=OFF \
    -DENABLE_CHAFA=OFF \
    -DENABLE_EGL=OFF \
    -DENABLE_GLX=OFF \
    -DENABLE_OPENCL=OFF \
    -DENABLE_PULSE=OFF \
    -DENABLE_DDCUTIL=OFF \
    -DENABLE_DIRECTX_HEADERS=OFF \
    -DENABLE_ELF=OFF \
    -DENABLE_LIBZFS=OFF

  "$CMAKE_BIN" --build "$build_dir" --target fastfetch
  "$CMAKE_BIN" --install "$build_dir"
}

if ! rustup target list --installed | grep -q "^${TARGET}$"; then
  rustup target add "$TARGET"
fi

download "$FISH_URL" "$DIST_DIR/$FISH_TAR"
extract "$DIST_DIR/$FISH_TAR" "$SRC_DIR/fish"
patch_fish_for_ohos "$SRC_DIR/fish"

export CARGO_TARGET_AARCH64_UNKNOWN_LINUX_OHOS_LINKER="$LINKER"
export CC_aarch64_unknown_linux_ohos="$LINKER"
export CXX_aarch64_unknown_linux_ohos="${LINKER/clang/clang++}"
export AR_aarch64_unknown_linux_ohos="$SDK_NATIVE/llvm/bin/llvm-ar"
export RANLIB_aarch64_unknown_linux_ohos="$SDK_NATIVE/llvm/bin/llvm-ranlib"
export PKG_CONFIG_ALLOW_CROSS=1
export PCRE2_SYS_STATIC=1
export CARGO_TARGET_DIR="$BUILD_DIR/target"

pushd "$SRC_DIR/fish" >/dev/null
cargo build \
  --locked \
  --target "$TARGET" \
  --release \
  --bin fish
popd >/dev/null

build_starship_compat
export CARGO_TARGET_AARCH64_UNKNOWN_LINUX_OHOS_RUSTFLAGS="${CARGO_TARGET_AARCH64_UNKNOWN_LINUX_OHOS_RUSTFLAGS:-} -C link-arg=$OUT_DIR/starship_compat.o"

cargo install \
  --locked \
  --version "$STARSHIP_VERSION" \
  --target "$TARGET" \
  --root "$PREFIX_DIR" \
  starship

build_fastfetch

"$STRIP" "$BUILD_DIR/target/$TARGET/release/fish" || true
"$STRIP" "$PREFIX_DIR/bin/starship" || true
"$STRIP" "$PREFIX_DIR/bin/fastfetch" || true

rm -rf "$HNP_SRC_DIR"
mkdir -p "$HNP_SRC_DIR/bin"
cp "$BUILD_DIR/target/$TARGET/release/fish" "$HNP_SRC_DIR/bin/fish"
cp "$PREFIX_DIR/bin/starship" "$HNP_SRC_DIR/bin/starship"
cp "$PREFIX_DIR/bin/fastfetch" "$HNP_SRC_DIR/bin/fastfetch"
chmod 0755 "$HNP_SRC_DIR/bin/fish" "$HNP_SRC_DIR/bin/starship" "$HNP_SRC_DIR/bin/fastfetch"

cat > "$HNP_SRC_DIR/hnp.json" <<EOF
{
  "type": "hnp-config",
  "name": "fish",
  "version": "${FISH_VERSION}",
  "install": {
    "links": [
      {"source": "bin/fish", "target": "fish"},
      {"source": "bin/starship", "target": "starship"},
      {"source": "bin/fastfetch", "target": "fastfetch"}
    ]
  }
}
EOF

rm -f "$HNP_OUT_DIR/fish.hnp"
"$HNPCLI" pack -i "$HNP_SRC_DIR" -o "$HNP_OUT_DIR"

echo "Bundled HNP written to $HNP_OUT_DIR/fish.hnp"
