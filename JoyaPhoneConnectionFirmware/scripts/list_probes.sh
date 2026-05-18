#!/usr/bin/env bash
set -euo pipefail

NCS_TOOLCHAIN_BIN="${NCS_TOOLCHAIN_BIN:-/opt/nordic/ncs/toolchains/b8efef2ad5/bin}"
NRF_COMMAND_LINE_TOOLS_BIN="${NRF_COMMAND_LINE_TOOLS_BIN:-/Applications/Nordic Semiconductor/bin}"

if [ ! -d "${NCS_TOOLCHAIN_BIN}" ]; then
	echo "NCS toolchain bin not found: ${NCS_TOOLCHAIN_BIN}"
	exit 1
fi

if [ -d "${NRF_COMMAND_LINE_TOOLS_BIN}" ]; then
	export PATH="${NRF_COMMAND_LINE_TOOLS_BIN}:${NCS_TOOLCHAIN_BIN}:${PATH}"
else
	export PATH="${NCS_TOOLCHAIN_BIN}:${PATH}"
fi
nrfjprog --ids
