#!/bin/bash
# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Set the UNITTEST_FLAG to prevent the main function is executed.
UNITTEST_FLAG=1 source "$(dirname "$0")/00-send-event-to-shill.sh"

assert_eq() {
  local actual="$1"
  local expect="$2"

  if [ "${actual}" != "${expect}" ]; then
    echo "Assert failed at $(caller): expected '${expect}', but got '${actual}'"
    exit 1
  fi
}

test_add_key_value() {
  local -r dict1=$(add_key_value "" "key1" "value1")
  assert_eq "${dict1}" '"key1","value1"'

  local -r dict2=$(add_key_value "${dict1}" "key2" "value2")
  assert_eq "${dict2}" '"key1","value1","key2","value2"'

  local -r dict3=$(add_key_value "${dict2}" "key_without_value" "")
  assert_eq "${dict3}" '"key1","value1","key2","value2"'
}
test_add_key_value

test_build_dict() {
  local -r dict=$(build_dict "key1" "value1" "key2" "" "key3" "value3")
  assert_eq "${dict}" '"key1","value1","key3","value3"'
}
test_build_dict

test_build_dhcpcd_configuration() {
  # Ignore shellcheck "foo appears unused. Verify it or export it."
  # shellcheck disable=SC2034

  # The variables from dhcpcd.
  local -r pid=4
  local -r interface="wlan0"
  local -r reason="BOUND"
  local -r new_ip_address="192.168.1.100"
  local -r new_subnet_cidr="16"
  local -r new_broadcast_address="192.168.255.255"
  local -r new_routers="192.168.1.1"
  local -r new_domain_name_servers="domain1.org domain2.org"
  local -r new_domain_name="domain.name"
  local -r new_domain_search="google.com"
  local -r ifmtu="1450"
  local -r new_captive_portal_uri="https://example.org/portal.html"
  local -r new_classless_static_routes="01020304"
  local -r new_vendor_encapsulated_options="05060708"
  local -r new_wpad_url="http://abc.def"
  local -r new_dhcp_lease_time="38600"

  # The expected dictionary string.
  local expect='"Pid","4",'
  expect+='"Interface","wlan0",'
  expect+='"Reason","BOUND",'
  expect+='"IPAddress","192.168.1.100",'
  expect+='"SubnetCIDR","16",'
  expect+='"BroadcastAddress","192.168.255.255",'
  expect+='"Routers","192.168.1.1",'
  expect+='"DomainNameServers","domain1.org domain2.org",'
  expect+='"DomainName","domain.name",'
  expect+='"DomainSearch","google.com",'
  expect+='"InterfaceMTU","1450",'
  expect+='"CaptivePortalUri","https://example.org/portal.html",'
  expect+='"ClasslessStaticRoutes","01020304",'
  expect+='"VendorEncapsulatedOptions","05060708",'
  expect+='"WebProxyAutoDiscoveryUrl","http://abc.def",'
  expect+='"DHCPLeaseTime","38600"'

  local -r dict="$(build_dhcpcd_configuration)"
  assert_eq "${dict}" "${expect}"
}
test_build_dhcpcd_configuration

echo "All tests passes"
