#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -ne 1 ]; then
	echo "Usage: $0 <DK_SERIAL_NUMBER>"
	exit 1
fi

DK_SERIAL="$1"

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
WORKSPACE_DIR="$(cd -- "${SCRIPT_DIR}/.." && pwd)"

NCS_ROOT="${NCS_ROOT:-/opt/nordic/ncs/v2.9.1}"
NCS_TOOLCHAIN_BIN="${NCS_TOOLCHAIN_BIN:-/opt/nordic/ncs/toolchains/b8efef2ad5/bin}"
APP_LINK="${APP_LINK:-/tmp/joya_phone_connection_fw}"

if [ ! -d "${NCS_ROOT}" ]; then
	echo "NCS root not found: ${NCS_ROOT}"
	exit 1
fi

if [ ! -d "${NCS_TOOLCHAIN_BIN}" ]; then
	echo "NCS toolchain bin not found: ${NCS_TOOLCHAIN_BIN}"
	exit 1
fi

ln -sfn "${WORKSPACE_DIR}" "${APP_LINK}"
export PATH="${NCS_TOOLCHAIN_BIN}:${PATH}"

cd "${NCS_ROOT}"
west flash -d "${APP_LINK}/build" --dev-id "${DK_SERIAL}"
