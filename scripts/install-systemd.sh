#!/usr/bin/env bash
set -euo pipefail

APP_NAME="myproxy"
INSTALL_BIN="/usr/local/bin/myproxy"
CONFIG_DIR="/etc/myproxy"
CONFIG_DEST="${CONFIG_DIR}/myproxy.conf"
LOG_DIR="/var/log/myproxy"
UNIT_DEST="/etc/systemd/system/myproxy.service"

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "${SCRIPT_DIR}/.." && pwd)"

BINARY_SRC="${REPO_ROOT}/build/myproxy"
CONFIG_SRC="${REPO_ROOT}/configs/myproxy.conf.example"
UNIT_SRC="${REPO_ROOT}/deploy/systemd/myproxy.service"

ACTION="install"
DRY_RUN=0
FORCE=0
ENABLE_SERVICE=0
START_SERVICE=0
PURGE=0

usage() {
  cat <<'USAGE'
Usage: scripts/install-systemd.sh [action] [options]

Actions:
  install    Install myproxy for Linux systemd deployment (default).
  uninstall  Stop, disable, and remove myproxy service and files.

Options:
  --force     Overwrite existing config file (install) or remove config and logs (uninstall).
  --enable    Enable myproxy.service.
  --start     Start myproxy.service after installation, or restart it if already running.
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
    log "dry-run: would restart ${APP_NAME}.service if already running, otherwise start it"
    return 0
  fi

  if systemctl is-active --quiet "${APP_NAME}.service"; then
    log "restart ${APP_NAME}.service"
    systemctl restart "${APP_NAME}.service"
    return 0
  fi

  log "start ${APP_NAME}.service"
  systemctl start "${APP_NAME}.service"
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
  log "  stop and disable ${APP_NAME}.service"
  log "  remove: ${UNIT_DEST}"
  log "  remove: ${INSTALL_BIN}"
  if [[ ${PURGE} -eq 1 ]]; then
    log "  remove: ${CONFIG_DEST}"
    log "  remove: ${LOG_DIR}"
  else
    log "  keep:   ${CONFIG_DEST} (pass --purge to remove)"
    log "  keep:   ${LOG_DIR} (pass --purge to remove)"
  fi

  if systemctl is-active --quiet "${APP_NAME}.service" 2>/dev/null; then
    run systemctl stop "${APP_NAME}.service"
  fi

  if systemctl is-enabled --quiet "${APP_NAME}.service" 2>/dev/null; then
    run systemctl disable "${APP_NAME}.service"
  fi

  run rm -f "${UNIT_DEST}"
  run systemctl daemon-reload
  run rm -f "${INSTALL_BIN}"

  if [[ ${PURGE} -eq 1 ]]; then
    run rm -f "${CONFIG_DEST}"
    run rm -rf "${LOG_DIR}"
  fi

  log "done"
}

require_environment() {
  if [[ ${DRY_RUN} -eq 1 ]]; then
    if [[ "$(uname -s)" != "Linux" ]]; then
      log "dry-run: real install requires Linux; current kernel is $(uname -s)"
    fi
    if [[ "${EUID}" -ne 0 ]]; then
      log "dry-run: real install requires root"
    fi
    return 0
  fi

  [[ "$(uname -s)" == "Linux" ]] || die "systemd deployment must run on Linux"
  [[ "${EUID}" -eq 0 ]] || die "systemd deployment must run as root; retry with sudo"
  command -v systemctl >/dev/null 2>&1 || die "systemctl not found"
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
    --enable)
      ENABLE_SERVICE=1
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
[[ -f "${UNIT_SRC}" ]] || die "systemd unit template not found: ${UNIT_SRC}"
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
log "  unit:   ${UNIT_SRC} -> ${UNIT_DEST}"

run install -d -m 0755 "${CONFIG_DIR}"
run install -d -m 0755 "${LOG_DIR}"
run install -m 0755 "${BINARY_SRC}" "${INSTALL_BIN}"
install_file 0644 "${CONFIG_SRC}" "${CONFIG_DEST}"
run install -m 0644 "${UNIT_SRC}" "${UNIT_DEST}"
run systemctl daemon-reload

if [[ ${ENABLE_SERVICE} -eq 1 ]]; then
  run systemctl enable "${APP_NAME}.service"
fi

if [[ ${START_SERVICE} -eq 1 ]]; then
  start_or_restart_service
fi

log "done"
