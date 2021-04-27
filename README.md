# qdbus-sys

Version: **1.0.0** (shared-library ABI version 1; initial compatibility baseline).

Public headers include `<qdbus-sys-version.h>` with `QDBUS_SYS_VERSION`,
`QDBUS_SYS_VERSION_CHECK(major, minor, patch)`, numeric component macros, and
`QDBUS_SYS_VERSION_STR`. Use `#if` guards when supporting multiple API versions.
Patch/minor releases preserve source and binary compatibility; incompatible
changes require a new major version. See [VERSIONING.md](VERSIONING.md) for the
policy, private implementation rules, installed-consumer test, and release ABI
comparison command.

## description
Provides QObject/QML interfaces to system D-Bus services for direct use from QML.

## License

qdbus-sys is free software: you can redistribute it and/or modify it under the
terms of the GNU Lesser General Public License, version 2.1 only
(`SPDX-License-Identifier: LGPL-2.1-only`). See [LICENSE](LICENSE) for the full text.

This library is distributed without any warranty; without even the implied
warranty of merchantability or fitness for a particular purpose.

## Overview

Anyone subject to yocto and the general "embedded" Linux landscape will be familiar with the dependency graph of NetworkManager, or the pace of development of Connman and the associated Qt wrappers often used to comprise a QML interfaced app.

IWD (connman dos) looks like a suitable replacement for both these options. What is required is the layer between D-Bus services and QML; iwd is a good motivator, but the project scope extends to other system services such as systemd-networkd, logind, resolved, and Shaper product daemons.

When applying a thin wrapper to a service such as iwd, I think it is fairly crucial that learning the wrapper teaches you the underlying daemon, since you will probably need to debug issues/quirks at both levels. With this in mind, one design decision is: the impl should map the underlying impl except when there is clear merit to deviation.

The public C++ APIs live in the `QDBusSys` namespace. Each QML module exposes
the corresponding short type names:

| C++ type | QML module | QML type |
| --- | --- | --- |
| `QDBusSys::IWD` | `DBusSysIWD` | `IWD` |
| `QDBusSys::IWDNetworkModel` | `DBusSysIWD` | `IWDNetworkModel` (through `iwd.networks`) |
| `QDBusSys::SystemdNetworkd` | `DBusSysSystemdNetworkd` | `SystemdNetworkd` |
| `QDBusSys::Connectivity` | `DBusSysConnectivity` | `Connectivity` |

The module directories are `iwd`, `qsystemd-networkd`, and
`qdbus-sys-connectivityd`. Current scope, known gaps, and long-term goals are
captured in [TECHNICAL_REQUIREMENTS.md](TECHNICAL_REQUIREMENTS.md).

## Considerations

The `DBusSysIWD` QML module provides an `IWD` client for the shared `qdbus-sys-iwd` daemon.
The daemon tracks IWD, owns its agents, and coordinates operations for all client
applications. Each client's read-only `networks` property owns its local list
model, updated from the shared state:

```qml
import QtQuick
import DBusSysIWD

Item {
    IWD { id: iwd }

    ListView {
        anchors.fill: parent
        model: iwd.networks
        delegate: Text {
            required property string ssid
            required property string networkId
            text: ssid
            MouseArea {
                anchors.fill: parent
                onClicked: iwd.connectNetwork(parent.networkId)
            }
        }
    }
}
```

State such as `iwd.scanning`, `iwd.connected`, and `iwd.connectedNetworkData`,
and operations such as `iwd.scan()`, belong to `IWD`. Network roles and
the `length` property belong to `iwd.networks`. C++ users include `iwd.h` and access the model
through `QDBusSys::IWD::networks()`.

`selectedDevice` chooses an IWD device path for this client only; an empty value
automatically selects an available device. `devices` lists the choices.
`available` reports whether the broker can currently observe IWD. Clients clear
their cached state when either daemon disappears and reload when it returns.

IWD permits one signal-level agent per station. `qdbus-sys-iwd` owns that agent for all
clients. If an independent application owns
that agent, `signalStrengthLevel` falls back to the connected network's latest
scanned RSSI, using the same thresholds. Scans refresh this fallback value;
live threshold notifications require ownership of the agent.
Levels follow IWD: 0 is the strongest signal (-30 dBm or more) and 7 the weakest
(below -90 dBm). `signalStrengthLevel` is 7 while no network is connected.

`knownNetworks` lists IWD's saved networks, including those out of range, as maps
with `networkId`, `ssid`, `type`, `autoconnect`, `hidden` and `lastTimeConnected`.
`forgetNetwork()` and `setAutoConnectNetwork()` accept these IDs too.

Operations are queued per device; overlapping scan requests share one scan.
`scan()`, `connectNetwork()`, `disconnectNetwork()`, `forgetNetwork()`, and
`setAutoConnectNetwork()` return an operation ID. Observe
`operationFinished(id, success, errorName, message)` for the result. Existing
callers can continue ignoring the return value. As with IWD's `Station.Scan()`,
a successful scan operation means the scan was started; `scanning` reports its
progress and `networks` updates when it completes. Failures also emit
`error()`: `SetAutoConnectFailed` for `setAutoConnectNetwork()`,
`RegisterAuthAgentFailed` when no credential agent could be registered, and
`OperationFailed` otherwise when no specific code applies. For diagnostics,
enable the daemon's `qdbus.iwd.*` logging categories.

`preparePassword(networkId, password)` supplies a passphrase for this client's
next connection to that network. Otherwise, when IWD requests a passphrase,
only the initiating client receives `credentialsRequested(id, networkId)`.
Respond with `provideCredentials(id, password)` or `cancelOperation(id)`.
Closing the client cancels its outstanding connection attempt. Credentials
are excluded from shared snapshots. Enterprise authentication is not supported.

## Shared IWD daemon installation

Installation includes `libexec/qdbus-sys-iwd`, `qdbus-sys-iwd.service`, and system
D-Bus activation and policy files. Reload systemd and D-Bus after installation using the commands
below. Constructing `IWD` asynchronously activates `com.shapertools.qdbussys.iwd`.
Its API interface is `com.shapertools.qdbussys.iwd1`, at object path
`/com/shapertools/qdbussys/iwd`.
The service supports D-Bus activation only; its unit has no `[Install]` section
and is not intended for `systemctl enable`. Deploy the client library and
daemon together. The broker's control methods permit root and members of
`netdev`; adjust the supplied policy to your system's intended network operators.
Other users can read state. The broker registers IWD's credential and
signal-level agents; clients never do.

The versioned D-Bus interface is documented in
[qiwd.xml](src/iwd/daemon/resources/qiwd.xml) and described in the
[broker protocol notes](src/iwd/daemon/README.md). Clients read one full
snapshot, then apply `SnapshotDelta` signals that carry only the changed keys,
tagged with an instance epoch and monotonic revision. A missed revision makes
the client fetch the full snapshot again. Operation results and credential prompts are
sent only to the requesting D-Bus connection and client instance. Commands
already sent are failed, rather than replayed, after broker loss because their
outcome may be unknown.

iwd uses adapter, Qt uses adaptor. I have a prejudice in favour of British spelling and Qt, but I believe in playing cricket (a game I hate, I might add) and cricket is nominally people from all walks write apis in American english, so you dont end up with Symbian. So Adapter is in use throughout as a shoutout to all my ESL homies making me look bad with their multitude of languages.

## Resources/References

* https://stackoverflow.com/questions/22253458/qdbusxml2cpp-unknown-type
* https://blog.mafr.de/2006/12/19/using-dbus-introspection/
* https://iwd.wiki.kernel.org/interface_lifecycle

## Dumping interfaces

scripts/dump-iwd-dbus-introspectable-interface.sh

## Building (CMake)

```bash
cmake -S . -B build -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build
sudo cmake --install build
```

Pass `-DQDBUS_SYS_USE_LLD=ON` to link with lld when building with Clang.
The default prefix is `/usr`. Libraries use the platform library directory
(`lib`, `lib64`, or a multiarch directory), headers use `/usr/include`, and
daemons use `/usr/libexec`. System configuration stays in `/etc`. For package
staging, use `DESTDIR=/path/to/staging cmake --install build`.

### SDK deploy helper

```bash
./scripts/deploy-to-sdk.sh
```

`deploy-to-sdk.sh` uses CMake for configure/build/install and syncs the staged `/usr` tree into your active SDK sysroot.

## Networkd state API

`QDBusSys::SystemdNetworkd` exposes scoped `OperationalState`, `CarrierState`,
`AddressState`, and `OnlineState` enums, registered with `Q_ENUM` for QML.
The values are those of `org.freedesktop.network1.Manager`, which aggregates
links according to their `RequiredForOnline` settings. All default to `Unknown`;
unavailable, empty, or unrecognized D-Bus state strings map to `Unknown` as well,
and every state returns to `Unknown` while networkd is not running. The wrapper
never starts networkd. State properties are read-only, and change signals are
emitted only when a value changes.

```cpp
if (networkd->onlineState == QDBusSys::SystemdNetworkd::OnlineState::Online) {
    // All required links are online.
}
```

In QML, import `DBusSysSystemdNetworkd` and compare against
`SystemdNetworkd.OnlineState.Online`.

## Connectivity daemon configuration

`qdbus-sys-connectivityd` supports D-Bus activation. Constructing a
`QDBusSys::Connectivity` client reads a running daemon's state immediately, so
it can be checked without waiting for a change signal, and requests startup
asynchronously when the daemon is absent. A `checkConnectivity()` request made during startup is sent once
the daemon is ready; repeated requests during that startup are coalesced.
The service supports D-Bus activation only and starts on first client use.
Its unit has no `[Install]` section and is not intended for `systemctl enable`.
Its executable is `libexec/qdbus-sys-connectivityd`, its launcher is
`libexec/qdbus-sys-connectivityd.sh`, and its systemd unit is
`qdbus-sys-connectivityd.service`.

The connectivity service is named `com.shapertools.qdbussys.connectivityd`;
its API interface is `com.shapertools.qdbussys.connectivityd1`, at object path
`/com/shapertools/qdbussys/connectivityd`. C++ clients include
`qdbus-sys-connectivityd.h`; QML clients import `DBusSysConnectivity` and create
`Connectivity` objects. Deploy the client library, QML module,
daemon, and D-Bus activation and policy files together.

Installation supplies the D-Bus activation file, systemd unit, and system-bus
policy. After installing on a running system, run:

```bash
sudo systemctl daemon-reload
sudo busctl --system call org.freedesktop.DBus /org/freedesktop/DBus org.freedesktop.DBus ReloadConfig
```

`qdbus-sys-connectivityd` reads `/etc/qdbus-sys/connectivityd.conf` using QSettings
in INI format. Installation supplies `/etc/qdbus-sys/connectivityd.conf.example`
with the default endpoint and DNS test domains. Copy it to the active settings
path before a client activates the daemon:

```bash
sudo cp /etc/qdbus-sys/connectivityd.conf.example /etc/qdbus-sys/connectivityd.conf
```

The example contains:

```ini
[General]
verboseDebugging=false

[Connectivity]
endpointUrl=https://connectivitycheck.gstatic.com/generate_204
expectedStatusCode=204
expectedResponse=""
dnsTestDomains=google.com, baidu.com, cloudflare.com, shapertools.com
recheckIntervalSec=60
```

Set `endpointUrl` to your HTTP(S) health endpoint and `dnsTestDomains` to a
comma-separated list of hostnames. Restart the daemon after editing the file.

Optionally set `expectedStatusCode` to require a particular HTTP status. Set
`expectedResponse` to require an exact UTF-8 response body, including whitespace
and letter case. For example, `expectedResponse="OK"` accepts only `OK`, while
`expectedResponse=""` requires an empty body. Quote text containing commas and
use INI escapes such as `\n` for a newline. Omit either key to skip that check;
without either key, any reply without a Qt network error passes. Network errors
always fail the endpoint check. An invalid `expectedStatusCode` (not an integer
from 100 to 599) logs a warning and skips status validation. A status or body
mismatch follows the same retry and DNS fallback path as a failed HTTP request.

Once a check settles, the daemon revalidates it every `recheckIntervalSec`
seconds (default 60; 0 disables), so an upstream outage or recovery is noticed
without a local address change. Revalidation publishes only a changed result;
`checkingConnectivity` reports checks triggered by network changes or clients.
DNS checks bypass Qt's host lookup cache.

The daemon reads this file without writing settings back to it. If it cannot
read or parse the file, it logs a warning with the path and error. An unconfigured
probe assumes success: an empty `endpointUrl` skips the HTTP check, and an empty
`dnsTestDomains` skips DNS checks. An invalid endpoint URL logs a warning and
also skips the HTTP check. Configured probes still run; DNS checks run when the
endpoint is absent or its check fails. If neither probe is configured, a routable
interface is reported as `QDBusSys::Connectivity::EndpointAccessible` (connected). An
interface without a routable address is still reported as disconnected.

The daemon's properties are read-only on the bus and change with standard
`org.freedesktop.DBus.Properties.PropertiesChanged` signals carrying the new
values. Any local user may read state and call `checkConnectivity()`; calls
less than two seconds apart are coalesced into one deferred check.

The `QDBusSys::Connectivity::Level` enum is declared in
`qdbus-sys-connectivityd.h`. QML clients use `Connectivity.EndpointAccessible`
from the `DBusSysConnectivity` module. `EndpointAccessible` means
the configured checks passed or were skipped because they were unconfigured;
it does not imply that a particular vendor's services are reachable.

To exercise failures, configure
an endpoint such as `https://health.invalid/ping` and DNS domains such as
`dns.invalid` in this file.
