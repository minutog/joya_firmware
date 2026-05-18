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
NRF_COMMAND_LINE_TOOLS_BIN="${NRF_COMMAND_LINE_TOOLS_BIN:-/Applications/Nordic Semiconductor/bin}"
APP_LINK="${APP_LINK:-/tmp/joya_phone_connection_fw}"
JLINK_EXE="${JLINK_EXE:-JLinkExe}"
JLINK_DEVICE="${JLINK_DEVICE:-nRF52832_xxAA}"
JLINK_SPEED="${JLINK_SPEED:-1000}"

if [ ! -d "${NCS_ROOT}" ]; then
	echo "NCS root not found: ${NCS_ROOT}"
	exit 1
fi

if [ ! -d "${NCS_TOOLCHAIN_BIN}" ]; then
	echo "NCS toolchain bin not found: ${NCS_TOOLCHAIN_BIN}"
	exit 1
fi

ln -sfn "${WORKSPACE_DIR}" "${APP_LINK}"
if [ -d "${NRF_COMMAND_LINE_TOOLS_BIN}" ]; then
	export PATH="${NRF_COMMAND_LINE_TOOLS_BIN}:${NCS_TOOLCHAIN_BIN}:${PATH}"
else
	export PATH="${NCS_TOOLCHAIN_BIN}:${PATH}"
fi

JLINK_SCRIPT="$(mktemp /tmp/joya_jlink_flash.XXXXXX)"
trap 'rm -f "${JLINK_SCRIPT}"' EXIT

cat > "${JLINK_SCRIPT}" <<EOF
r
h
exit
EOF

if command -v "${JLINK_EXE}" >/dev/null 2>&1; then
	"${JLINK_EXE}" -NoGui 1 -SelectEmuBySN "${DK_SERIAL}" -Device "${JLINK_DEVICE}" -If SWD -Speed "${JLINK_SPEED}" -CommanderScript "${JLINK_SCRIPT}" >/dev/null || true
fi

cd "${NCS_ROOT}"
set +e
west flash -d "${APP_LINK}/build" --softreset --dev-id "${DK_SERIAL}"
WEST_STATUS="$?"
set -e

if [ "${WEST_STATUS}" -eq 0 ]; then
	exit 0
fi

MERGED_HEX="${APP_LINK}/build/merged.hex"
if [ ! -f "${MERGED_HEX}" ] || ! command -v "${JLINK_EXE}" >/dev/null 2>&1; then
	exit "${WEST_STATUS}"
fi

echo "west flash failed; trying J-Link direct flash fallback"
cat > "${JLINK_SCRIPT}" <<EOF
r
h
loadfile ${MERGED_HEX}
r
g
exit
EOF

"${JLINK_EXE}" -NoGui 1 -SelectEmuBySN "${DK_SERIAL}" -Device "${JLINK_DEVICE}" -If SWD -Speed "${JLINK_SPEED}" -CommanderScript "${JLINK_SCRIPT}"
