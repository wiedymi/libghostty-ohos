#!/bin/sh

set -eu

ROOT_DIR=$(
  CDPATH= cd -- "$(dirname -- "$0")/.." && pwd
)

IMAGE_NAME=${IMAGE_NAME:-nitterm-libghostty-vt-builder}
DOCKERFILE=${DOCKERFILE:-$ROOT_DIR/Dockerfile.libghostty-vt}
CACHE_ROOT=${CACHE_ROOT:-$ROOT_DIR/.cache/libghostty-vt-docker}
GHOSTTY_REPO_URL=${GHOSTTY_REPO_URL:-https://github.com/ghostty-org/ghostty}
GHOSTTY_REF=${GHOSTTY_REF:-}
TARGET_TRIPLE=${TARGET_TRIPLE:-aarch64-linux-musl}
OPTIMIZE_MODE=${OPTIMIZE_MODE:-ReleaseFast}
HOST_TARGET=${HOST_TARGET:-}

mkdir -p "$CACHE_ROOT"

DOCKER_BUILDKIT=0 docker build --pull=false -f "$DOCKERFILE" -t "$IMAGE_NAME" "$ROOT_DIR"

docker run --rm \
  -v "$ROOT_DIR":/workspace \
  -v "$CACHE_ROOT":/cache \
  -w /workspace \
  -e GHOSTTY_REPO_URL="$GHOSTTY_REPO_URL" \
  -e GHOSTTY_REF="$GHOSTTY_REF" \
  -e GHOSTTY_DIR=/cache/ghostty \
  -e BUILD_PREFIX=/cache/out \
  -e TARGET_TRIPLE="$TARGET_TRIPLE" \
  -e OPTIMIZE_MODE="$OPTIMIZE_MODE" \
  -e HOST_TARGET="$HOST_TARGET" \
  -e OUTPUT_DIR=/workspace/libghostty_ohos/prebuilt/arm64-v8a \
  -e OUTPUT_LIB=/workspace/libghostty_ohos/prebuilt/arm64-v8a/libghostty_vt.a \
  -e ZIG_BIN=zig \
  "$IMAGE_NAME" \
  sh -seu <<'INNER'
ROOT_DIR=/workspace
GHOSTTY_REPO_URL=${GHOSTTY_REPO_URL:-https://github.com/ghostty-org/ghostty}
GHOSTTY_REF=${GHOSTTY_REF:-}
GHOSTTY_DIR=${GHOSTTY_DIR:-/cache/ghostty}
BUILD_PREFIX=${BUILD_PREFIX:-/cache/out}
TARGET_TRIPLE=${TARGET_TRIPLE:-aarch64-linux-musl}
OPTIMIZE_MODE=${OPTIMIZE_MODE:-ReleaseFast}
HOST_TARGET=${HOST_TARGET:-}
OUTPUT_DIR=${OUTPUT_DIR:-$ROOT_DIR/libghostty_ohos/prebuilt/arm64-v8a}
OUTPUT_LIB=${OUTPUT_LIB:-$OUTPUT_DIR/libghostty_vt.a}
ZIG_BIN=${ZIG_BIN:-zig}
ZIG_VERSION=$($ZIG_BIN version)

require_tool() {
  if ! command -v "$1" >/dev/null 2>&1; then
    echo "Missing required tool: $1" >&2
    exit 1
  fi
}

require_tool git
require_tool "$ZIG_BIN"

apply_zig_016_compat() {
  repo_dir=$1

  if ! command -v perl >/dev/null 2>&1; then
    echo "Missing required tool: perl" >&2
    exit 1
  fi

  echo "Applying Zig 0.16+ compatibility edits"

  perl -0pi -e '
    s@const file_version: \?\[\]const u8 = if \(b\.build_root\.handle\.readFileAlloc\(\n        b\.allocator,\n        "VERSION",\n        128,\n    \)\)@var threaded_io: std.Io.Threaded = .init(b.allocator, .{});\n    defer threaded_io.deinit();\n    const io = threaded_io.io();\n\n    const file_version: ?[]const u8 = if (b.build_root.handle.readFileAlloc(\n        io,\n        "VERSION",\n        b.allocator,\n        .limited(128),\n    ))@s
  ' "$repo_dir/build.zig"

  perl -0pi -e '
    s@if \(current_vsn\.major != required_vsn\.major or\n        current_vsn\.minor != required_vsn\.minor or\n        current_vsn\.patch < required_vsn\.patch\)\n    \{@if (current_vsn.order(required_vsn) == .lt) {@s
  ' "$repo_dir/src/build/zig.zig"

  find "$repo_dir" -name '*.zig' -print0 | xargs -0 perl -0pi -e '
    s/\bstd\.process\.EnvMap\b/std.process.Environ.Map/g;
    s/\bstd\.process\.getEnvMap\(([^()]*)\)/std.process.Environ.createMap(.{ .block = .{ .slice = std.c.environ } }, $1)/g;
    s@try std\.process\.Environ\.createMap\(\.\{ \.block = \.\{ \.slice = std\.c\.environ \} \}, ([^)]+)\)@blk: {\n        var envp = std.c.environ;\n        var envp_len: usize = 0;\n        while (envp[envp_len] != null) : (envp_len += 1) {}\n        break :blk try std.process.Environ.createMap(.{ .block = .{ .slice = envp[0..envp_len :null] } }, $1);\n    }@g;
    s/\bstd\.mem\.trimRight\b/std.mem.trimEnd/g;
    s/\bstd\.mem\.trimLeft\b/std.mem.trimStart/g;
    s/\bstd\.posix\.getenvZ\("PATH"\) orelse return null/std.mem.span(std.c.getenv("PATH") orelse return null)/g;
    s/\bstd\.posix\.getenvZ\("PATH"\)/std.mem.span(std.c.getenv("PATH") orelse return null)/g;
  '

  perl -0pi -e 's/\.Ignore\b/.ignore/g' "$repo_dir/build.zig" "$repo_dir"/src/build/*.zig

  perl -0pi -e '
    s@defer if \(builtin\.os\.tag == \.windows\) alloc\.free\(PATH\);\n@defer if (builtin.os.tag == .windows) alloc.free(PATH);\n\n    var threaded_io: std.Io.Threaded = .init(alloc, .{});\n    defer threaded_io.deinit();\n    const io = threaded_io.io();\n@s;
    s/std\.fs\.cwd\(\)\.openFile\(/std.Io.Dir.cwd().openFile(io, /g;
    s/defer f\.close\(\);/defer f.close(io);/g;
    s/const stat = try f\.stat\(\);/const stat = try f.stat(io);/g;
  ' "$repo_dir/src/os/path.zig"
}

mkdir -p "$OUTPUT_DIR"

if [ ! -d "$GHOSTTY_DIR/.git" ]; then
  echo "Cloning Ghostty into $GHOSTTY_DIR"
  git clone "$GHOSTTY_REPO_URL" "$GHOSTTY_DIR"
else
  echo "Updating Ghostty in $GHOSTTY_DIR"
  git -C "$GHOSTTY_DIR" remote set-url origin "$GHOSTTY_REPO_URL"
  git -C "$GHOSTTY_DIR" fetch origin
fi

if [ -n "$GHOSTTY_REF" ]; then
  echo "Checking out Ghostty ref $GHOSTTY_REF"
  git -C "$GHOSTTY_DIR" fetch origin "$GHOSTTY_REF"
  git -C "$GHOSTTY_DIR" checkout --force FETCH_HEAD
else
  git -C "$GHOSTTY_DIR" fetch --force origin main
  git -C "$GHOSTTY_DIR" checkout --force main
  git -C "$GHOSTTY_DIR" reset --hard origin/main
fi

case "$ZIG_VERSION" in
  0.16.*|0.17.*|0.18.*|0.19.*|1.*)
    apply_zig_016_compat "$GHOSTTY_DIR"
    ;;
esac

echo "Building upstream libghostty-vt for $TARGET_TRIPLE"
(
  cd "$GHOSTTY_DIR"
  if [ -n "$HOST_TARGET" ]; then
    set -- -target "$HOST_TARGET"
  else
    set --
  fi

  "$ZIG_BIN" build \
    "$@" \
    -Dtarget="$TARGET_TRIPLE" \
    -Doptimize="$OPTIMIZE_MODE" \
    -Demit-lib-vt=true \
    --prefix "$BUILD_PREFIX"
)

SOURCE_LIB=$BUILD_PREFIX/lib/libghostty-vt.a
if [ ! -f "$SOURCE_LIB" ]; then
  echo "Expected build output not found: $SOURCE_LIB" >&2
  exit 1
fi

SIMDUTF_LIB=$(find "$GHOSTTY_DIR/.zig-cache" -name libsimdutf.a -print -quit)
HIGHWAY_LIB=$(find "$GHOSTTY_DIR/.zig-cache" -name libhighway.a -print -quit)

if [ -z "${SIMDUTF_LIB:-}" ] || [ ! -f "$SIMDUTF_LIB" ]; then
  echo "Failed to locate libsimdutf.a in $GHOSTTY_DIR/.zig-cache" >&2
  exit 1
fi

if [ -z "${HIGHWAY_LIB:-}" ] || [ ! -f "$HIGHWAY_LIB" ]; then
  echo "Failed to locate libhighway.a in $GHOSTTY_DIR/.zig-cache" >&2
  exit 1
fi

TMP_OUTPUT=${OUTPUT_LIB}.tmp
rm -f "$TMP_OUTPUT"
cat <<EOF2 | "$ZIG_BIN" ar -M
CREATE $TMP_OUTPUT
ADDLIB $SOURCE_LIB
ADDLIB $SIMDUTF_LIB
ADDLIB $HIGHWAY_LIB
SAVE
END
EOF2

mv "$TMP_OUTPUT" "$OUTPUT_LIB"
echo "Wrote $OUTPUT_LIB"
INNER
