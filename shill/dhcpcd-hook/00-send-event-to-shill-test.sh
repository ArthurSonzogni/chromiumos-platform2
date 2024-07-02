#!/bin/sh
# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# To ensure the script is POSIX-compliant, please run it by dash:
# dash ./00-send-event-to-shill-test.sh

# Set the UNITTEST_FLAG to prevent the main function is executed.
UNITTEST_FLAG=1 . "$(dirname "$0")/00-send-event-to-shill.sh"

assert_eq() {
  local actual="$1"
  local expect="$2"

  if [ "${actual}" != "${expect}" ]; then
    echo "Assert failed: expected <${expect}>, but got <${actual}>"
    exit 1
  fi
}

test_escape_str() {
  assert_eq "$(escape_str "foo")" "foo"
  assert_eq "$(escape_str "foo bar")" "foo bar"
  assert_eq "$(escape_str "foo'")" "foo\'"
  assert_eq "$(escape_str "foo\\")" "foo\\\\"
  assert_eq "$(escape_str "foo\\'")" "foo\\\\\'"
  assert_eq "$(escape_str "8.8.8.8 8.8.4.4")" "8.8.8.8 8.8.4.4"
}
test_escape_str

test_add_key_value() {
  local dict1
  dict1="$(add_key_value "" "key1" "value1")"
  assert_eq "${dict1}" "'key1': 'value1'"

  local dict2
  dict2="$(add_key_value "${dict1}" "key2" "value2")"
  assert_eq "${dict2}" "'key1': 'value1', 'key2': 'value2'"

  local dict3
  dict3="$(add_key_value "${dict2}" "key_without_value" "")"
  assert_eq "${dict3}" "'key1': 'value1', 'key2': 'value2'"

  local dict4
  dict4="$(add_key_value "" "key'" "'value'")"
  assert_eq "${dict4}" "'key\'': '\'value\''"
}
test_add_key_value

test_build_dict() {
  local dict
  dict="$(build_dict "key1" "value1" "key2" "" "key3" "value3")"
  assert_eq "${dict}" "'key1': 'value1', 'key3': 'value3'"
}
test_build_dict

test_build_dhcpcd_configuration() {
  # Ignore shellcheck "foo appears unused. Verify it or export it."
  # shellcheck disable=SC2034

  # The variables from dhcpcd.
  local pid=4
  local interface="wlan0"
  local reason="BOUND"
  local new_ip_address="192.168.1.100"
  local new_subnet_cidr="16"
  local new_broadcast_address="192.168.255.255"
  local new_routers="192.168.1.1"
  local new_domain_name_servers="8.8.8.8 8.8.4.4"
  local new_domain_name="domain.name"
  local new_domain_search="google.com"
  local ifmtu="1450"
  local new_captive_portal_uri="https://example.org/portal.html"
  local new_classless_static_routes="01020304"
  local new_vendor_encapsulated_options="05060708"
  local new_wpad_url="http://abc.def"
  local new_dhcp_lease_time="38600"

  # The expected dictionary string.
  local expect="'Pid': '4',"
  expect="${expect} 'Interface': 'wlan0',"
  expect="${expect} 'Reason': 'BOUND',"
  expect="${expect} 'IPAddress': '192.168.1.100',"
  expect="${expect} 'SubnetCIDR': '16',"
  expect="${expect} 'BroadcastAddress': '192.168.255.255',"
  expect="${expect} 'Routers': '192.168.1.1',"
  expect="${expect} 'DomainNameServers': '8.8.8.8 8.8.4.4',"
  expect="${expect} 'DomainName': 'domain.name',"
  expect="${expect} 'DomainSearch': 'google.com',"
  expect="${expect} 'InterfaceMTU': '1450',"
  expect="${expect} 'CaptivePortalUri': 'https://example.org/portal.html',"
  expect="${expect} 'ClasslessStaticRoutes': '01020304',"
  expect="${expect} 'VendorEncapsulatedOptions': '05060708',"
  expect="${expect} 'WebProxyAutoDiscoveryUrl': 'http://abc.def',"
  expect="${expect} 'DHCPLeaseTime': '38600'"

  assert_eq "$(build_dhcpcd_configuration)" "${expect}"
}
test_build_dhcpcd_configuration

echo "All tests passes"
