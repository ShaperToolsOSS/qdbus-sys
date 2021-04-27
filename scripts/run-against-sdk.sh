#!/usr/bin/env bash
# Copyright (C) 2026 Shaper Tools
# SPDX-License-Identifier: LGPL-2.1-only

# ./scripts/run-against-sdk.sh ./SmartRouter/UnitTests/origintests testRunOnDesktop

set -o nounset
set -o pipefail
set -o errexit
#set -o xtrace

sourceFile="${BASH_SOURCE[0]}"
scriptName=$(basename "${sourceFile}")
scriptDirectory=$(cd "$(dirname "${sourceFile}")" && pwd)
rootDirectory=$(cd "$(dirname "${scriptDirectory}")" && pwd)
run_prefix=""

resolve_qt_sysroot() {
    if [[ -n "${QT_SYSROOT:-}" ]]; then
        echo "${QT_SYSROOT}"
        return
    fi

    local tool
    for tool in qmake6 qmake; do
        if command -v "${tool}" >/dev/null 2>&1; then
            local candidate
            candidate=$("${tool}" -query QT_SYSROOT)
            if [[ -n "${candidate}" ]]; then
                echo "${candidate}"
                return
            fi
        fi
    done
}

sysroot=$(resolve_qt_sysroot)

if [[ "${scriptName}" == "debug-against-sdk.sh" ]]; then
    run_prefix="gdb --ex=run --args env"
fi

if [[ -z "${sysroot}" ]]; then
    echo "Unable to locate QT_SYSROOT. Source your SDK or export QT_SYSROOT before running ${scriptName}."
    exit 1
fi

sdkroot=$(dirname "$(dirname "${sysroot}")")

if [[ $# -lt 1 ]]; then
    echo "This script expects at least one parameter; the binary to run against your current SDK"
    cmd="${rootDirectory}/SmartRouter/UnitTests/origintests"
    args="testRunOnDesktop"
else
    cmd="$1"
    shift
    args="$*"
fi

echo "Running ${cmd} with args: (${args}) against ${sdkroot}"
cmdDirectory=$(dirname "${cmd}")
cp "${sdkroot}/qt.conf" "${cmdDirectory}"

if [[ -n "${SHAPER_UI_SCALE_FACTOR:-}" ]]; then
    export QT_SCALE_FACTOR=${SHAPER_UI_SCALE_FACTOR}
fi
export QML2_IMPORT_PATH="${rootDirectory}/build/usr/lib/qml:${rootDirectory}/usr/lib/qml:${sysroot}/usr/lib/qml"

# QT_QUICK_BACKEND=software is also performant, but lacks broad GL support
full_cmd="${run_prefix} LIBGL_ALWAYS_SOFTWARE=1 LIBGL_DRIVERS_PATH=${sysroot}/usr/lib/dri LD_LIBRARY_PATH=${sysroot}/usr/lib:${sysroot}/lib ${cmd} ${args}"
echo "Running cmd: ${full_cmd}"
eval "${full_cmd}"
