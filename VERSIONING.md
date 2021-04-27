# Versioning and compatibility

The current public API is the initial **1.0.0** compatibility baseline. No earlier
binary ABI is supported. The CMake `project(... VERSION ...)` declaration is the
single version source for all public libraries and the installed version header.

## Release policy

| Release | Compatibility requirement |
| --- | --- |
| Patch (`1.0.1`) | Compatible bug fixes; retain public source and binary interfaces. |
| Minor (`1.1.0`) | Add functionality while preserving existing source and binary interfaces. |
| Major (`2.0.0`) | Incompatible changes are permitted and must be documented for migration. |

Each library uses the full project version as `VERSION` and the project major as
`SOVERSION`. An incompatible replacement must have a new major/SONAME so an old
application is not silently loaded against the wrong ABI. Keep old major-version
libraries available when deployed applications still need them.

The compatibility promise applies to supported builds using compatible Qt,
compiler, standard-library and architecture combinations. It does not promise
that a binary can move between arbitrary toolchains or platforms.

## Application version guards

Applications can include `<qdbus-sys-version.h>` directly; each public API header
also includes it. Both build-tree users and installed-header users receive:

- `QDBUS_SYS_VERSION_MAJOR`, `QDBUS_SYS_VERSION_MINOR`, `QDBUS_SYS_VERSION_PATCH`
- `QDBUS_SYS_VERSION_STR`, for example `"1.0.0"`
- `QDBUS_SYS_VERSION` and `QDBUS_SYS_VERSION_CHECK(major, minor, patch)`

Use numeric `#if` comparisons for APIs introduced in a particular release:

```cpp
#include <qdbus-sys-version.h>

#if QDBUS_SYS_VERSION >= QDBUS_SYS_VERSION_CHECK(1, 0, 0)
// The current QDBusSys::IWD::devices() API is available.
auto paths = iwd.devices();
#endif
```

For a future optional API, guard at the release that actually introduced it and
provide the older implementation in `#else` where needed. Do not use `#ifdef`
for a numeric version comparison. These macros describe the headers used to
compile an application, not the shared library loaded at runtime. Guards do not
repair an ABI break or make a binary compiled against a new API run with an old
library that lacks that API. Packages must declare their minimum library version.

## Evolving the public API

Prefer adding APIs to replacing them. Retain existing exported functions,
constructors, destructors, overloads, signals, properties and enum values during
1.x. Deprecate a renamed method with `Q_DECL_DEPRECATED_X("Use replacement()")`
and an out-of-line forwarding implementation; retain its exported symbol until
the next major version. Removing or changing an existing signature needs a major
release unless compatibility is preserved by a separate overload or wrapper.
A C++ type alias alone does not retain a renamed class's old binary symbols.

Do not add, remove, reorder or change the type of instance members in an already
released public class, including private members. Do not change base classes or
existing virtual-function layouts. Add implementation state to the private
implementation objects instead. Public classes hold only a private
implementation pointer, and their getters and destructors are out-of-line.
Inline implementation changes require special care because old applications
retain the previously compiled inline code.

## Exported symbols

The public libraries are built with hidden symbol visibility and linked with
the version script `cmake/qdbus-sys.map`. Their binary interface is exactly the
classes marked `QDBUS_SYS_EXPORT` (from the installed `qdbus-sys-global.h`):
their members, vtables and type information, all versioned `QDBUS_SYS_1`, plus
the QML type registration function each plugin calls. Template instantiations
and every other implementation symbol stay local. QML plugins export only Qt's
plugin entry points (`cmake/qdbus-sys-plugin.map`).

Mark a new public class `QDBUS_SYS_EXPORT`, and declare its private
implementation class `Q_DECL_HIDDEN` so it does not inherit the export. Free
functions and types outside `QDBusSys` are never exported; keep public API in
that namespace. Check a candidate with `nm -DC --defined-only` on each library.

QML module/type names and D-Bus service, object, interface and message contracts
are public contracts too. A C++ major bump does not automatically migrate them.
Preserve existing imports and wire interfaces or introduce explicitly versioned
alternatives and document their migration.

Our D-Bus API interface convention appends a version number directly to the
component name: `com.shapertools.qdbussys.iwd1` and
`com.shapertools.qdbussys.connectivityd1`. There is no `Manager` component.
The service names (`com.shapertools.qdbussys.iwd` and
`com.shapertools.qdbussys.connectivityd`) and object paths remain unversioned.
An incompatible wire API can introduce `iwd2` or `connectivityd2` alongside
version 1 under the same service; the old implementation must remain available
for old clients to continue working.

## Source compatibility checks

Normal builds with `BUILD_TESTING=ON` compile a frozen 1.0 application at
`tests/compatibility/1.0/public_api.cpp`. CTest runs it as `public_api_1_0_test`
alongside `version_test`, which checks the generated macros against CMake's
version and exercises numeric preprocessor guards. The frozen application tests
representative public declarations, enum values, signals and public state usage;
it is not a complete proof of source compatibility. Do not edit the fixture to
hide a breaking change. Add a new baseline when introducing a new major.

Also verify the application against only an installed SDK. Use a fresh consumer
build directory, especially when changing the candidate installation root:

```sh
cmake -S . -B build -DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
DESTDIR="$PWD/stage-candidate" cmake --install build
cmake -S tests/compatibility -B consumer-check \
  -DQDBUS_SYS_ROOT="$PWD/stage-candidate"
cmake --build consumer-check
ctest --test-dir consumer-check --output-on-failure
```

For platforms using `lib64` or a multiarch library directory, pass
`-DQDBUS_SYS_LIBDIR=usr/lib64` (or the appropriate relative directory). The
consumer takes its include and library paths exclusively from the staged SDK.
Use the target toolchain when cross-compiling; runtime checks need that target
or its configured CMake emulator.

## ABI comparison between releases

Archive an unstripped, staged installation of each released version, including
public headers, development symlinks, libraries and debug information, as a
platform-specific release artifact. Keep both roots built with the same toolchain
and Qt version for comparison. The first archive is the 1.0.0 baseline.

With libabigail's `abidiff` installed, compare a candidate to the last released
version in the same major line:

```sh
python3 scripts/check-abi.py stage-1.0.0 stage-candidate
# For a different library directory:
python3 scripts/check-abi.py stage-1.0.0 stage-candidate --libdir usr/lib64
```

The script checks all three public interface libraries. It restricts type
analysis to the installed public headers, drops private implementation types,
and requires debug information. It fails on tool errors and ABI differences
requiring review, including layout differences that abidiff may not classify
with its explicit incompatible-change bit. Compatible symbol additions are
excluded from the report. Do not treat a missing tool, stripped libraries, or a
successful source compilation as a passing ABI check.

See the [abidiff documentation](https://sourceware.org/libabigail/manual/abidiff.html)
for the tool's analysis and exit-status limitations. A passing comparison is
useful evidence, not a substitute for reviewing API behavior and Qt meta-object
changes. Release validation consists of the full test suite, the installed
consumer build, an ABI comparison to the previous release, and compatibility
review of public behavior. For the first 1.0.0 release, archive the baseline;
there is no earlier supported release to compare.
