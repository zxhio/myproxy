#!/usr/bin/env bash
set -euo pipefail

APP_NAME="myproxy"
INSTALL_ROOT="/opt/myproxy"
INSTALL_BIN="${INSTALL_ROOT}/bin/myproxy"
CONFIG_DIR="${INSTALL_ROOT}/etc"
CONFIG_DEST="${CONFIG_DIR}/myproxy.conf"
LOG_DIR="${INSTALL_ROOT}/log"
PLIST_DEST="/Library/LaunchDaemons/com.myproxy.plist"

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "${SCRIPT_DIR}/.." && pwd)"

BINARY_SRC="${REPO_ROOT}/build/myproxy"
CONFIG_SRC="${REPO_ROOT}/configs/myproxy.conf.example"
PLIST_SRC="${REPO_ROOT}/deploy/launchd/com.myproxy.plist"

ACTION="install"
DRY_RUN=0
FORCE=0
LOAD_SERVICE=0
START_SERVICE=0
PURGE=0

usage() {
  cat <<'USAGE'
Usage: scripts/install-launchd.sh [action] [options]

Actions:
  install    Install myproxy for macOS launchd deployment (default).
  uninstall  Stop, unload, and remove myproxy service and files.

Options:
  --force     Overwrite existing config file (install) or remove config and logs (uninstall).
  --load      Load myproxy launchd service.
  --start     Start myproxy service after installation, or restart it if already running.
  --purge     Also remove config file and log directory (uninstall).
  --dry-run   Print actions without changing the system.
  -h, --help  Show this help.
USAGE
}

die() {
  echo "error: $*" >&2
  exit 1
}

log() {
  echo "$*"
}

print_cmd() {
  printf 'dry-run:'
  for arg in "$@"; do
    printf ' %q' "$arg"
  done
  printf '\n'
}

run() {
  if [[ ${DRY_RUN} -eq 1 ]]; then
    print_cmd "$@"
    return 0
  fi
  "$@"
}

start_or_restart_service() {
  if [[ ${DRY_RUN} -eq 1 ]]; then
    log "dry-run: would restart ${APP_NAME} if already loaded, otherwise launch it"
    return 0
  fi

  if launchctl list "com.${APP_NAME}" &>/dev/null; then
    log "restart ${APP_NAME}"
    launchctl unload "${PLIST_DEST}" 2>/dev/null || true
    launchctl load "${PLIST_DEST}"
    return 0
  fi

  log "start ${APP_NAME}"
  launchctl load "${PLIST_DEST}"
}

install_file() {
  local mode="$1"
  local src="$2"
  local dest="$3"

  if [[ -e "${dest}" && ${FORCE} -ne 1 ]]; then
    log "keep existing ${dest}; pass --force to overwrite"
    return 0
  fi

  run install -m "${mode}" "${src}" "${dest}"
}

do_uninstall() {
  require_environment

  log "uninstall plan:"
  log "  stop and unload com.${APP_NAME}"
  log "  remove: ${PLIST_DEST}"
  log "  remove: ${INSTALL_BIN}"
  if [[ ${PURGE} -eq 1 ]]; then
    log "  remove: ${CONFIG_DEST}"
    log "  remove: ${LOG_DIR}"
    log "  remove: ${INSTALL_ROOT}"
  else
    log "  keep:   ${CONFIG_DEST} (pass --purge to remove)"
    log "  keep:   ${LOG_DIR} (pass --purge to remove)"
    log "  keep:   ${INSTALL_ROOT}"
  fi

  if launchctl list "com.${APP_NAME}" &>/dev/null 2>&1; then
    run launchctl unload "${PLIST_DEST}"
  fi

  run rm -f "${PLIST_DEST}"
  run rm -f "${INSTALL_BIN}"

  if [[ ${PURGE} -eq 1 ]]; then
    run rm -f "${CONFIG_DEST}"
    run rm -rf "${LOG_DIR}"
    run rm -rf "${INSTALL_ROOT}"
  fi

  log "done"
}

require_environment() {
  if [[ ${DRY_RUN} -eq 1 ]]; then
    if [[ "$(uname -s)" != "Darwin" ]]; then
      log "dry-run: real install requires macOS; current kernel is $(uname -s)"
    fi
    if [[ "${EUID}" -ne 0 ]]; then
      log "dry-run: real install requires root"
    fi
    return 0
  fi

  [[ "$(uname -s)" == "Darwin" ]] || die "launchd deployment must run on macOS"
  [[ "${EUID}" -eq 0 ]] || die "launchd deployment must run as root; retry with sudo"
  command -v launchctl >/dev/null 2>&1 || die "launchctl not found"
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    install|uninstall)
      ACTION="$1"
      shift
      ;;
    --force)
      FORCE=1
      shift
      ;;
    --load)
      LOAD_SERVICE=1
      shift
      ;;
    --start)
      START_SERVICE=1
      shift
      ;;
    --purge)
      PURGE=1
      shift
      ;;
    --dry-run)
      DRY_RUN=1
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      die "unknown option: $1"
      ;;
  esac
done

if [[ "${ACTION}" == "uninstall" ]]; then
  do_uninstall
  exit 0
fi

[[ -f "${CONFIG_SRC}" ]] || die "config source not found: ${CONFIG_SRC}"
[[ -f "${PLIST_SRC}" ]] || die "launchd plist template not found: ${PLIST_SRC}"
if [[ ! -f "${BINARY_SRC}" ]]; then
  if [[ ${DRY_RUN} -eq 1 ]]; then
    log "dry-run: binary not found at ${BINARY_SRC}; real install requires cmake build"
  else
    die "binary not found at ${BINARY_SRC}; run cmake build first"
  fi
fi

require_environment

log "install plan:"
log "  binary: ${BINARY_SRC} -> ${INSTALL_BIN}"
log "  config: ${CONFIG_SRC} -> ${CONFIG_DEST}"
log "  logs:   ${LOG_DIR}"
log "  plist:  ${PLIST_SRC} -> ${PLIST_DEST}"

run install -d -m 0755 "${INSTALL_ROOT}"/bin
run install -d -m 0755 "${CONFIG_DIR}"
run install -d -m 0755 "${LOG_DIR}"
run install -m 0755 "${BINARY_SRC}" "${INSTALL_BIN}"
install_file 0644 "${CONFIG_SRC}" "${CONFIG_DEST}"
run install -m 0644 "${PLIST_SRC}" "${PLIST_DEST}"

if [[ ${LOAD_SERVICE} -eq 1 ]]; then
  run launchctl load "${PLIST_DEST}"
fi

if [[ ${START_SERVICE} -eq 1 ]]; then
  start_or_restart_service
fi

log "done"
