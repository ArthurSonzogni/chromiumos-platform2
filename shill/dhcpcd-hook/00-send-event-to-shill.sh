#!/bin/sh
# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Adds the backslash character before each backslash and single quote character.
escape_str() {
  echo "$1" | sed 's/\\/\\\\/g' | sed "s/'/\\\'/g"
}

# Adds the key-value pair into a dictionary-format string if the value is not
# empty. The dictionary-format is:
#   'key1': 'value1', 'key2': 'value2', 'key3': 'value3'
# Usage:
#   add_key_value <dict> <key> <value>
add_key_value() {
  local quote="'"
  local dict="$1"
  local key
  key="$(escape_str "$2")"
  local value
  value="$(escape_str "$3")"

  if [ -n "${value}" ]; then
    echo "${dict}${dict:+, }${quote}${key}${quote}: ${quote}${value}${quote}"
  else
    echo "${dict}"
  fi
}

# Builds the dictionary-format string given a series of key-value pair.
# Ignore the key-value pair if the value is empty.
# Usage:
#   build_dict <key1> <value1> [ <key2> <value2> ... ]
build_dict() {
  local dict=""
  while [ $# -gt 1 ]; do
    local key="$1"
    shift
    local value="$1"
    shift
    dict="$(add_key_value "${dict}" "${key}" "${value}")"
  done
  echo "${dict}"
}

# Builds the dhcpcd configuration in dictionary format.
# The keys are defined at shill::DHCPv4Config.
build_dhcpcd_configuration() {
  build_dict \
      "Pid" "${pid:=}" \
      "Interface" "${interface:=}" \
      "Reason" "${reason:=}" \
      "IPAddress" "${new_ip_address:=}" \
      "SubnetCIDR" "${new_subnet_cidr:=}" \
      "BroadcastAddress" "${new_broadcast_address:=}" \
      "Routers" "${new_routers:=}" \
      "DomainNameServers" "${new_domain_name_servers:=}" \
      "DomainName" "${new_domain_name:=}" \
      "DomainSearch" "${new_domain_search:=}" \
      "InterfaceMTU" "${ifmtu:=}" \
      "CaptivePortalUri" "${new_captive_portal_uri:=}" \
      "ClasslessStaticRoutes" "${new_classless_static_routes:=}" \
      "VendorEncapsulatedOptions" "${new_vendor_encapsulated_options:=}" \
      "WebProxyAutoDiscoveryUrl" "${new_wpad_url:=}" \
      "DHCPLeaseTime" "${new_dhcp_lease_time:=}"
}

main() {
  /usr/bin/gdbus call \
      --system \
      --dest org.chromium.flimflam \
      --object-path / \
      --method org.chromium.flimflam.Manager.NotifyDHCPEvent \
      "{ $(build_dhcpcd_configuration) }"
}

if [ "${UNITTEST_FLAG:=}" != "1" ]; then
  main
fi
