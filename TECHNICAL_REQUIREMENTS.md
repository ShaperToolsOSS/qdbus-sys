# Technical Requirements

## Purpose

Provide Qt/QML-visible wrappers around system D-Bus services used on embedded Shaper devices. The current working implementation is network and connectivity focused, but the project identity is broader: `qdbus-sys` should be able to grow into wrappers for services such as logind, resolved, portals, power/session state, and Shaper product daemons.

The API should expose useful daemon state without creating undue churn or D-Bus traffic. It should remain lightweight and should not introduce meaningful CPU load in long-running UI processes.

## Target Services

The current service set is:

- IWD (`net.connman.iwd`)
- systemd-networkd (`org.freedesktop.network1`)
- Connectivity daemon (`com.shapertools.qdbussys.connectivityd`)

Near-term logical extensions are:

- systemd-resolved (`org.freedesktop.resolve1`)
- systemd-logind (`org.freedesktop.login1`)
- freedesktop power, notification, portal, and secret-service interfaces where a thin Qt/QML wrapper adds product value

The networking target is systemd-networkd, not NetworkManager.

## Existing Working Scope

### IWD

- Public C++ types live in the `QDBusSys` namespace; QML types use short names in modules prefixed with `DBusSys`.
- The `DBusSysIWD` QML module exposes `IWD`, which owns an `IWDNetworkModel` through its read-only `networks` property. Their C++ types are `QDBusSys::IWD` and `QDBusSys::IWDNetworkModel`.
- `IWD` supports scanning, connecting, disconnecting, forgetting known networks, toggling autoconnect, preparing passphrases, registering a signal-level agent, exposing connected-network summary data, listing saved networks (including those out of range), and publishing adapter/device path lists.
- The nested network model is a curated `QAbstractListModel`, not a direct export of every generated D-Bus proxy. Device state and operations belong to the owning `IWD` object.
- The `qdbus-sys-iwd` system D-Bus daemon owns IWD observation and agents. It maintains a backend per device; each QML client's `selectedDevice` controls its own view and operation target.
- Its service is `com.shapertools.qdbussys.iwd`, with versioned API interface `com.shapertools.qdbussys.iwd1`.
- The daemon starts through D-Bus activation only; its systemd unit is not intended for `systemctl enable`.
- Operations are serialized per device, scans are coalesced, and credential requests/results are routed privately to their initiating client. Client exit cancels its connection attempt.
- Network and device state changes are cached and coalesced before reaching QML to limit signal churn.
- Clients read one full snapshot from the broker, then apply `SnapshotDelta` signals carrying only changed keys; a revision gap triggers a full resynchronisation.
- The daemon mirrors IWD's object tree from ObjectManager data and a single `PropertiesChanged` match rule, without per-object proxies or blocking property reads. Its credential and signal-level agents answer only IWD.

### systemd-networkd

- The `DBusSysSystemdNetworkd` QML module exposes `SystemdNetworkd` (`QDBusSys::SystemdNetworkd` in C++) with aggregate networkd state: operational state, carrier state, address state, online state, and discovered link interface names.
- It publishes the Manager's own aggregate state, which honours `RequiredForOnline`, from `GetAll` calls and Manager `PropertiesChanged` signals. Construction reads a running networkd's state synchronously (2 s timeout) so it is valid immediately; later reads are asynchronous. It tracks link discovery through rtnetlink when available and falls back to polling when needed. It never activates networkd and reports `Unknown` while networkd is absent.
- The wrapper intentionally presents a small product-facing state surface; `org.freedesktop.network1.Manager` exposes more.

### Connectivity daemon

- The `DBusSysConnectivity` QML module exposes `Connectivity` (`QDBusSys::Connectivity` in C++), which talks to `qdbus-sys-connectivityd`. Construction reads a running daemon's state synchronously (2 s timeout) so it is valid immediately; activation and later reads are asynchronous.
- The D-Bus service is `com.shapertools.qdbussys.connectivityd` and its versioned API interface is `com.shapertools.qdbussys.connectivityd1`; the installed daemon/package name uses the hyphenated `qdbus-sys-connectivityd` form.
- The daemon starts through D-Bus activation only; its systemd unit is not intended for `systemctl enable`.
- The daemon centralizes product-level reachability checks, debounce/backoff behavior, D-Bus publication, and the `/run/shaper-state/connectivity/connected` side effect.
- Connectivity level is derived from networkd address readiness, a configurable HTTP(S) endpoint check, and DNS resolution checks through Qt networking. `QDBusSys::Connectivity::EndpointAccessible` denotes checks that passed or were unconfigured, rather than reachability of any vendor's services.
- Endpoint checks optionally require an expected HTTP status and/or an exact UTF-8 response body. Mismatches use the existing retry and DNS fallback behavior; the reference Google probe expects HTTP 204 with an empty body.
- Settled results are revalidated quietly at a configurable interval. Bus properties are read-only and announced with `PropertiesChanged`; unprivileged check requests are rate-limited.
- This is not yet a general central state daemon for all IWD, networkd, and resolved data.

### Tooling and tests

- Interface XML files and reference proxies are kept under each component's `dbus-interfaces/` and regenerated by `scripts/generate-code-from-xml-interfaces.sh`. The wrappers and daemons do not compile these proxies.
- Automated tests cover source contracts, model changes, IWD lifecycle, shared broker state, operation ownership, credential routing, client exit, connectivity behavior, and connectivity D-Bus activation.
- Tester applications exist for observing networkd and connectivity signal behavior.

## Known Gaps and Inconsistencies

- systemd-resolved has no generated proxy, Qt/QML wrapper, or QML module yet.
- The underlying daemons expose more state and methods than the public QML wrappers currently surface.
- Each `IWD` presents one selected device; applications can create multiple instances to observe multiple devices concurrently.
- IWD authentication beyond simple passphrase entry is not implemented; 802.1X, private-key, username/password, and user-password flows are future work.
- Demo UI and documentation should stay aligned with the public property names exposed by the QML modules.

## Desired Long-Term Goals

- Keep raw D-Bus proxies and QML-friendly wrappers as separate layers.
- Expose enough state for diagnostics and product UI without making every transient D-Bus signal a QML event.
- Prefer object/path-aware APIs where multiple adapters, devices, links, or resolver contexts can exist.
- Preserve recognizable naming from the underlying daemons unless a Qt/QML convention materially improves usability.
- Add a systemd-resolved wrapper that can report DNS servers, domains, per-link resolver state, DNSSEC/DNS-over-TLS settings, and lookup health.
- Add a systemd-logind wrapper for session, seat, idle, lid, reboot/poweroff, and inhibitor state where those are useful to QML.
- Use the `qdbus-sys-` prefix for daemon executables and systemd units: `qdbus-sys-iwd` and `qdbus-sys-connectivityd`.
- Expand tests from source contracts into behavioral coverage for model updates, service disappearance/restart, multiple adapters/links, daemon state transitions, and authentication failure paths.

## Central Daemon Guidance

A central daemon makes sense when the work is active, shared, policy-bearing, or expensive:

- connectivity health checks with backoff and debounce
- one owner for user-visible connectivity state
- scan/auth coordination that should not be duplicated by every app
- selected interface/device policy
- shared DNS/resolved diagnostics
- OS-level side effects such as files under `/run`

A central daemon is less useful for passive, cheap read-only state that can be exposed through direct client wrappers without meaningful duplication. Where a daemon is added, the client API should remain small and stable, and the daemon should own the noisy D-Bus observation behind it.

## Compatibility baseline

- The current API establishes the initial 1.0.0 baseline; no earlier ABI has shipped.
- Generate and install `qdbus-sys-version.h` from the project version. Header guards use `QDBUS_SYS_VERSION` and `QDBUS_SYS_VERSION_CHECK`.
- Preserve source and binary compatibility within a major version, with project-major SONAMEs. Keep new storage in private implementations and deprecate old APIs before major-version removal.
- Build the frozen 1.0 client and check installed headers/libraries with the standalone consumer. Compare unstripped release SDKs with `scripts/check-abi.py` and libabigail before compatible releases.
- Export only the public `QDBusSys` classes, through `QDBUS_SYS_EXPORT`, hidden default visibility and the `QDBUS_SYS_1` version script.
- The detailed policy and validation commands are in [VERSIONING.md](VERSIONING.md).
