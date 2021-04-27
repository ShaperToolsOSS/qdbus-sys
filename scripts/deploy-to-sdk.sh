#!/usr/bin/env bash
# Copyright (C) 2026 Shaper Tools
# SPDX-License-Identifier: LGPL-2.1-only


set -o nounset
set -o pipefail
set -o errexit
#set -o xtrace

sourceFile="${BASH_SOURCE[0]}"
scriptName=$(basename "${sourceFile}")
scriptDirectory=$(cd "$(dirname "${sourceFile}")" && pwd)
rootDirectory=$(cd "$(dirname "${scriptDirectory}")" && pwd)

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
if [[ -z "${sysroot}" ]]; then
    echo "Unable to locate QT_SYSROOT. Source your SDK or export QT_SYSROOT before running ${scriptName}."
    exit 1
fi

buildDirectory="${rootDirectory}/build"
deployDirectory=$(mktemp -d "${TMPDIR:-/tmp}/qdbus-sys-deploy.XXXXXX")
trap 'rm -rf -- "${deployDirectory}"' EXIT

echo "Deploying qdbus-sys to ${sysroot}"

echo "Configuring CMake build in ${buildDirectory}"
cmake -S "${rootDirectory}" -B "${buildDirectory}" -DCMAKE_INSTALL_PREFIX=/usr

cmake --build "${buildDirectory}"
DESTDIR="${deployDirectory}" cmake --install "${buildDirectory}"

sudo rsync -rvl "${deployDirectory}/" "${sysroot}/"
