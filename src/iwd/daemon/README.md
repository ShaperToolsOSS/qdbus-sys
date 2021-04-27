# qdbus-sys-iwd protocol

`qdbus-sys-iwd` owns `com.shapertools.qdbussys.iwd` on the system bus. The manager object is
`/com/shapertools/qdbussys/iwd`, interface `com.shapertools.qdbussys.iwd1`.
The wire signatures are in `resources/qiwd.xml`.

Subscribe to `SnapshotDelta` before calling `GetSnapshot`, which returns the
complete `a{sv}` state. Each `epoch` (daemon instance UUID) has a `revision`
(unsigned 64-bit counter) that increases with every change. Clear cached state
when the service owner disappears. Protocol `version` is 1.

Every change is broadcast as `SnapshotDelta`, which carries only what changed:

- `version`, `epoch`, `revision`, and `baseRevision` (the revision it applies to).
- `changes`: changed top-level snapshot keys and their new values.
- `deviceChanges`: map from device path to its changed state keys.
- `replacedDevices`: map from device path to its complete state (new devices).
- `removedDevices`: device paths no longer present.

Ignore deltas while `GetSnapshot` is outstanding; the broker's messages arrive in
order, so the reply already includes them. Ignore a delta whose `revision` is not
newer than yours. If its `epoch` differs or its `baseRevision` is not your
revision, call `GetSnapshot` again.

The snapshot contains:

- `available`: whether IWD has a bus owner.
- `inventoryReady`: whether initial device discovery has completed.
- `adapters`, `devices`: lists of IWD object paths encoded as strings.
- `deviceStates`: map from device path to its state map.

Each device map contains `devicePath`, `initialized`, `supportedModes`, `model`,
`name`, `vendor`, `powered`, `state`, `scanning`, `connected`,
`connectedNetworkData`, `signalStrengthLevel`, `networks`, and `knownNetworks`.
`networks` is a list of maps with the IWDNetworkModel role names. `knownNetworks`
lists saved networks, including those out of range, as maps with `networkId`,
`ssid`, `type`, `autoconnect`, `hidden`, and `lastTimeConnected`.
`signalStrengthLevel` ranges from 0 (strongest) to 7 (weakest) and is 7 while
no network is connected. `initialized` means
initial discovery has finished, including failure; it does not imply that a
station is currently usable. The broker holds commands until discovery finishes.

`Request(client, operation, device, action, arguments)` acknowledges queueing,
not completion. Use a fresh operation UUID and a separate random `client` token
for each client object, even if objects share a D-Bus connection. Completion is
`OperationFinished(client, operation, success, errorName, message)`. Device queues
are independent and bounded; operations are serialized and concurrent scans
share a request. Success means the underlying D-Bus method succeeded; a scan's
results and network state continue to arrive through deltas.

Actions and arguments:

| Action | Arguments |
| --- | --- |
| `scan` | none |
| `disconnect` | none |
| `connect` | `networkId`: string; optional `passphrase`: string |
| `forget` | `networkId`: string |
| `autoconnect` | `networkId`: string, `enabled`: bool |

`forget` and `autoconnect` accept any saved network, in range or not.

`CredentialsRequested(client, operation, networkId)` is emitted when IWD needs
a passphrase that was not supplied with a connect request. Reply using
`ProvideCredentials(client, operation, passphrase)` or `Cancel(client, operation)`.
The sender's unique D-Bus name, client token, and operation ID must all match.
Credential prompts and operation results are unicast. Credentials are excluded
from snapshots and cleared after each operation. An unanswered prompt is
canceled after 60 seconds; underlying D-Bus calls can fail sooner.

`ReleaseClient(client)` cancels outstanding requests belonging to that sender
and token. Losing the sender's D-Bus connection performs the same cleanup.
An active connection attempt is disconnected on cancellation; other active
operations can finish, but their caller receives cancellation only once.
Operations already sent are not automatically retried after broker loss.

Clients select a device locally; the broker has no global UI selection. Each
backend uses its own IWD D-Bus connection, with one authentication agent for that
connection and one signal agent for its station, both registered automatically. An independent application may
already own the station's signal agent; scanned RSSI then supplies the fallback.
