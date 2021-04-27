#!/usr/bin/env bash
# Copyright (C) 2026 Shaper Tools
# SPDX-License-Identifier: LGPL-2.1-only


set -o nounset
set -o pipefail
set -o errexit
#set -o xtrace

executed_file=$(readlink -f "${BASH_SOURCE[0]}")

script_directory=$(dirname "${executed_file}")
root_directory=$(dirname "${script_directory}")

generate_interfaces() {
    local interface_directory=$1
    local include_argument=${2:-}
    local xml_interface_directory="${interface_directory}/xml"
    local generated_interface_directory="${interface_directory}/generated"

    rm -rf -- "${generated_interface_directory}"
    mkdir -p "${generated_interface_directory}"
    cd "${generated_interface_directory}"

    for interface_path in "${xml_interface_directory}"/*.xml; do
        local interface
        interface=$(basename "${interface_path}")
        echo "Processing ${interface}"
        local interface_name="${interface%.*}"
        # be warned, the qdbusxml2cpp command will happily take dashes in a filename and generate dashed (broken) IFDEFs
        if [[ -n "${include_argument}" ]]; then
            qdbusxml2cpp -i "${include_argument}" -p "${interface_name}" "${interface_path}"
        else
            qdbusxml2cpp -p "${interface_name}" "${interface_path}"
        fi
        # Normalize the generator's extra final blank line for clean diffs.
        sed -i '${/^$/d;}' "${interface_name}.h" "${interface_name}.cpp"
        # Retain project attribution when these files are regenerated.
        sed -i '1i// Copyright (C) 2026 Shaper Tools\n// SPDX-License-Identifier: LGPL-2.1-only\n' \
            "${interface_name}.h" "${interface_name}.cpp"
    done
}

iwd_interface_directory=${root_directory}/src/iwd/lib/dbus-interfaces
generate_interfaces "${iwd_interface_directory}" dbus-interface-common.h

network_state_interface_directory=${root_directory}/src/qsystemd-networkd/lib/dbus-interfaces
generate_interfaces "${network_state_interface_directory}"

shaper_connectivity_interface_directory=${root_directory}/src/qdbus-sys-connectivityd/lib/dbus-interfaces
generate_interfaces "${shaper_connectivity_interface_directory}"

exit 0
