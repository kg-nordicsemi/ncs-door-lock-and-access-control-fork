#
# Copyright (c) 2026 Nordic Semiconductor ASA
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#

file(GLOB src CONFIGURE_DEPENDS ./*.c)

zephyr_library_sources(${src})
