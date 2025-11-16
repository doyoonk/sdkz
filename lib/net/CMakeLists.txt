#
# Copyright (c) 2025 HU Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

zephyr_library()

zephyr_library_sources(enet.c enet4.c enet6.c net_common.c)
zephyr_library_sources_ifdef(CONFIG_NET_L2_IPIP tunnel.c)
zephyr_library_sources_ifdef(CONFIG_NET_VLAN vlan.c)
