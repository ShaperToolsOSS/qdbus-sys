// Copyright (C) 2026 Shaper Tools
// SPDX-License-Identifier: LGPL-2.1-only

#include "qdbus-sys-version.h"
#include <cstring>

#if QDBUS_SYS_VERSION != QDBUS_SYS_VERSION_CHECK(EXPECTED_MAJOR, EXPECTED_MINOR, EXPECTED_PATCH)
#error "The generated version does not match the CMake project version"
#endif
#if QDBUS_SYS_VERSION_CHECK(1, 2, 3) >= QDBUS_SYS_VERSION_CHECK(1, 3, 0)
#error "Minor-version guards are not ordered correctly"
#endif
#if QDBUS_SYS_VERSION_CHECK(1, 255, 255) >= QDBUS_SYS_VERSION_CHECK(2, 0, 0)
#error "Major-version guards are not ordered correctly"
#endif
static_assert(QDBUS_SYS_VERSION_MAJOR == EXPECTED_MAJOR);
static_assert(QDBUS_SYS_VERSION_MINOR == EXPECTED_MINOR);
static_assert(QDBUS_SYS_VERSION_PATCH == EXPECTED_PATCH);

int main()
{
    return std::strcmp(QDBUS_SYS_VERSION_STR, EXPECTED_VERSION);
}
