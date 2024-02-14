#!/bin/sh
# Copyright 2018 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

bootstat shill-start

# Double check to make sure shill related paths have been created with the
# intended ownership and permissions. These paths should already have been
# handled during boot, but check them again.
systemd-tmpfiles --create --remove --clean /usr/lib/tmpfiles.d/shill.conf
systemd-tmpfiles --create --remove --clean /usr/lib/tmpfiles.d/dhcpcd.conf

# tmpfiles.d does not handle chown/chmod -R because of unsafe ownership
# transitions.
(
  # Log any cases where the old behavior actually results in a change,
  # for /run paths until we are sure these are not needed.
  chown -c -R shill:shill /run/shill

  chown -c -R vpn:vpn \
    /run/ipsec \
    /run/xl2tpd \
    /run/wireguard

  chown -c -R dhcp:dhcp /run/dhcpcd
) | logger -p err -t "shill-pre-start"

chown -R shill:shill \
  /var/cache/shill \
  /var/lib/shill/metrics

chown -R dhcp:dhcp /var/lib/dhcpcd
chmod -R u+rwX,g+rwX,o+rX /var/lib/dhcpcd

# Increase kernel neighbor table size.
echo 1024 > /proc/sys/net/ipv4/neigh/default/gc_thresh1
echo 4096 > /proc/sys/net/ipv4/neigh/default/gc_thresh2
echo 8192 > /proc/sys/net/ipv4/neigh/default/gc_thresh3
echo 1024 > /proc/sys/net/ipv6/neigh/default/gc_thresh1
echo 4096 > /proc/sys/net/ipv6/neigh/default/gc_thresh2
echo 8192 > /proc/sys/net/ipv6/neigh/default/gc_thresh3

# The default max size of expect table is 1024, which is not enough in the lab
# environment. The main purpose here is to reduce the number of "nf_conntrack:
# expectation table full" log in the lab environment.
echo 4096 > /proc/sys/net/netfilter/nf_conntrack_expect_max

# Increase the maximum size that processes can request with setsockopt
# SOL_SOCKET SO_RCVBUF for controlling the size of socket receive buffers.
echo 524288 > /proc/sys/net/core/rmem_max
