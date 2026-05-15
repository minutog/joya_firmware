#!/usr/bin/env bash
set -euo pipefail

NCS_TOOLCHAIN_BIN="${NCS_TOOLCHAIN_BIN:-/opt/nordic/ncs/toolchains/b8efef2ad5/bin}"

if [ ! -d "${NCS_TOOLCHAIN_BIN}" ]; then
	echo "NCS toolchain bin not found: ${NCS_TOOLCHAIN_BIN}"
	exit 1
fi

export PATH="${NCS_TOOLCHAIN_BIN}:${PATH}"
nrfjprog --ids
