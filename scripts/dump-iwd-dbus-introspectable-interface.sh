#!/usr/bin/env bash
# Copyright (C) 2026 Shaper Tools
# SPDX-License-Identifier: LGPL-2.1-only


. /etc/profile.d/shaper-env.sh

set -o nounset
set -o pipefail
set -o errexit
#set -o xtrace

# example dbus service hierarchy

#/
#/net
#/net/connman
#/net/connman/iwd
#/net/connman/iwd/0
#/net/connman/iwd/0/2
#/net/connman/iwd/0/2/4561676c6556696577_psk
#/net/connman/iwd/0/2/48502d5072696e742d30342d4f66666963656a65742036373030_open
#/net/connman/iwd/0/2/6e6f6d6f726573637265656e7321_psk
#/net/connman/iwd/0/2/7368616e6c6579_psk
#/net/connman/iwd/0/2/476c6f72696f7573546563686e69636f6c6f7572_psk
#/net/connman/iwd/0/2/64656661756c74_psk
#/net/connman/iwd/476c6f72696f7573546563686e69636f6c6f7572_psk

introspect_dbus_service() {
    service=$1

    # dumps information for every layer of the dbus service hierarchy
    for iwd_interface in $(qdbus ${service}); do
        iwd_interface_fn="${service}"
        if [[ "${iwd_interface}" != "/" ]]; then
            iwd_interface_fn="${iwd_interface_fn}$(echo ${iwd_interface} | tr '/' '-')"
        fi
        echo "dumping introspection information for: ${iwd_interface} to ${iwd_interface_fn}"
        dbus-send --system --type=method_call --print-reply --dest=net.connman.iwd ${iwd_interface} org.freedesktop.DBus.Introspectable.Introspect > ${iwd_interface_fn}
    done
}

introspect_dbus_service net.connman.iwd
introspect_dbus_service org.freedesktop.network1

exit 0
