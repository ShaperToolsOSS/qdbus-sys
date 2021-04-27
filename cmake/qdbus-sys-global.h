// Copyright (C) 2026 Shaper Tools
// SPDX-License-Identifier: LGPL-2.1-only

#pragma once

#include <QtCore/qglobal.h>

// The libraries are built with hidden symbol visibility; only classes marked
// QDBUS_SYS_EXPORT form their binary interface.
#if defined(QDBUS_SYS_BUILD_LIBRARY)
#  define QDBUS_SYS_EXPORT Q_DECL_EXPORT
#else
#  define QDBUS_SYS_EXPORT Q_DECL_IMPORT
#endif
