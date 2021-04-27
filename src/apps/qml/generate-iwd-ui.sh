#!/usr/bin/env bash
# Copyright (C) 2026 Shaper Tools
# SPDX-License-Identifier: LGPL-2.1-only


set -o nounset
set -o pipefail
set -o errexit

source_qml="${1:-iwd-ui.qml}"
output_file="${2:-iwd-ui}"

printf '#!/usr/bin/env qml\n\n' > "${output_file}"
cat "${source_qml}" >> "${output_file}"
chmod +x "${output_file}"
