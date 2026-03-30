#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
HDC_BIN="${OHOS_HDC:-$HOME/Library/OpenHarmony/Sdk/20/toolchains/hdc}"
HVIGOR_BIN="${OHOS_HVIGOR:-/Applications/DevEco-Studio.app/Contents/tools/hvigor/hvigor/bin/hvigor.js}"
APP_BUNDLE="${OHOS_APP_BUNDLE:-com.wiedymi.libghosttyohos.example}"
APP_ABILITY="${OHOS_APP_ABILITY:-EntryAbility}"
HAP_PATH="${OHOS_HAP_PATH:-$ROOT_DIR/example/build/default/outputs/default/example-default-signed.hap}"
ALT_HAP_PATH="$ROOT_DIR/example/build/default/outputs/default/example-default-unsigned.hap"
LOG_REGEX="${OHOS_LOG_REGEX:-com\\.wiedymi\\.libghosttyohos\\.example|PTYHandler|Terminal|testTag|fish-start|pty child pre-exec}"
LOG_LINES="${OHOS_LOG_LINES:-200}"

usage() {
  cat <<'EOF'
Usage: tools/ohos-headless-loop.sh [build|install|start|logs|run]

Commands:
  build    Build the app package with hvigor.
  install  Reinstall the built hap on the current OHOS target.
  start    Force-stop and relaunch the example ability.
  logs     Print a filtered hilog snapshot.
  run      Build, install, start, then print logs. This is the default.

Environment overrides:
  OHOS_HDC
  OHOS_HVIGOR
  OHOS_APP_BUNDLE
  OHOS_APP_ABILITY
  OHOS_HAP_PATH
  OHOS_LOG_REGEX
  OHOS_LOG_LINES
EOF
}

require_bin() {
  local path="$1"
  local name="$2"
  if [[ ! -x "$path" ]]; then
    echo "$name not found or not executable: $path" >&2
    exit 1
  fi
}

detect_hap() {
  if [[ -f "$HAP_PATH" ]]; then
    return
  fi
  if [[ -f "$ALT_HAP_PATH" ]]; then
    HAP_PATH="$ALT_HAP_PATH"
    return
  fi
  echo "No hap found. Expected one of:" >&2
  echo "  $HAP_PATH" >&2
  echo "  $ALT_HAP_PATH" >&2
  exit 1
}

check_target() {
  local targets
  targets="$("$HDC_BIN" list targets)"
  if [[ -z "$targets" ]]; then
    echo "No OHOS targets visible to hdc." >&2
    exit 1
  fi
  echo "$targets"
}

build_app() {
  require_bin "$HVIGOR_BIN" "hvigor"
  echo "==> Building app"
  (cd "$ROOT_DIR" && "$HVIGOR_BIN" assembleApp -m project --no-daemon)
}

install_app() {
  require_bin "$HDC_BIN" "hdc"
  detect_hap
  echo "==> Installing $HAP_PATH"
  "$HDC_BIN" install -r "$HAP_PATH"
}

start_app() {
  require_bin "$HDC_BIN" "hdc"
  echo "==> Starting $APP_BUNDLE/$APP_ABILITY"
  "$HDC_BIN" shell aa force-stop "$APP_BUNDLE" >/dev/null 2>&1 || true
  "$HDC_BIN" shell aa start -b "$APP_BUNDLE" -a "$APP_ABILITY"
}

show_logs() {
  require_bin "$HDC_BIN" "hdc"
  echo "==> Filtered logs"
  "$HDC_BIN" shell hilog -x -z "$LOG_LINES" | grep -E "$LOG_REGEX" || true
}

main() {
  local cmd="${1:-run}"
  case "$cmd" in
    build)
      check_target >/dev/null
      build_app
      ;;
    install)
      check_target >/dev/null
      install_app
      ;;
    start)
      check_target >/dev/null
      start_app
      ;;
    logs)
      check_target >/dev/null
      show_logs
      ;;
    run)
      echo "==> OHOS targets"
      check_target
      build_app
      install_app
      start_app
      sleep 2
      show_logs
      ;;
    -h|--help|help)
      usage
      ;;
    *)
      echo "Unknown command: $cmd" >&2
      usage >&2
      exit 1
      ;;
  esac
}

main "$@"
