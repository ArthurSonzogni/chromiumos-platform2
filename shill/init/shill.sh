# Copyright 2018 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

ARGS="--log-level=${SHILL_LOG_LEVEL} --log-scopes=${SHILL_LOG_SCOPES}"

if [ -n "${SHILL_LOG_VMODULES}" ]; then
  ARGS="${ARGS} --vmodule=${SHILL_LOG_VMODULES}"
fi

if [ -n "${BLACKLISTED_DEVICES}" ] && [ -n "${SHILL_TEST_DEVICES}" ]; then
  ARGS="${ARGS} --device-black-list=${BLACKLISTED_DEVICES},${SHILL_TEST_DEVICES}"
elif [ -n "${BLACKLISTED_DEVICES}" ]; then
  ARGS="${ARGS} --device-black-list=${BLACKLISTED_DEVICES}"
elif [ -n "${SHILL_TEST_DEVICES}" ]; then
  ARGS="${ARGS} --device-black-list=${SHILL_TEST_DEVICES}"
fi

if [ -n "${SHILL_PASSIVE_MODE}" ]; then
  ARGS="${ARGS} --passive-mode"
fi

if [ -n "${SHILL_PREPEND_DNS_SERVERS}" ]; then
  ARGS="${ARGS} --prepend-dns-servers=${SHILL_PREPEND_DNS_SERVERS}"
fi

if [ -n "${SHILL_ACCEPT_HOSTNAME_FROM}" ]; then
  ARGS="${ARGS} --accept-hostname-from=${SHILL_ACCEPT_HOSTNAME_FROM}"
fi

if [ -n "${SHILL_MINIMUM_MTU}" ]; then
  ARGS="${ARGS} --minimum-mtu=${SHILL_MINIMUM_MTU}"
fi

if [ -n "${DHCPV6_ENABLED_DEVICES}" ]; then
  ARGS="${ARGS} --dhcpv6-enabled-devices=${DHCPV6_ENABLED_DEVICES}"
fi

if [ -n "${ARC_DEVICE}" ]; then
  ARGS="${ARGS} --arc-device=${ARC_DEVICE}"
fi

ARGS="${ARGS} ${SHILL_TEST_ARGS}"

# If OOBE has not completed (i.e. EULA not agreed to), do not run
# portal checks
if [ ! -f /home/chronos/.oobe_completed ]; then
  ARGS="${ARGS} --portal-list="
fi

if [ -e /var/lib/shill/shill_sandboxing_enabled ]; then
  /usr/bin/metrics_client -e Network.Shill.SandboxingEnabled 1 3 &
  ARGS="${ARGS} --jail-vpn-clients"
  # Run shill as shill user/group in a minijail:
  #   -G so shill programs can inherit supplementary groups.
  #   -n to run shill with no_new_privs.
  #   -B 20 to avoid locking SECURE_KEEP_CAPS flag.
  #   -c for runtime capabilities:
  #     CAP_WAKE_ALARM | CAP_NET_RAW | CAP_NET_ADMIN | CAP_NET_BROADCAST |
  #     CAP_NET_BIND_SERVICE | CAP_SETPCAP | CAP_SETUID | CAP_SETGID | CAP_KILL
  #   --ambient so child processes can inherit runtime capabilities:
  exec /sbin/minijail0 -u shill -g shill -G -n -B 20 -c 800003de0 --ambient \
       -- /usr/bin/shill ${ARGS}
else
  /usr/bin/metrics_client -e Network.Shill.SandboxingEnabled 0 3 &
  exec /usr/bin/shill ${ARGS}
fi
