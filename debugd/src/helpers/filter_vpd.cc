// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Outputs a filtered list of VPD key/value pairs.

#include <array>
#include <iostream>
#include <set>

#include <vpd/vpd.h>

namespace {

constexpr std::array kVpdKeyAllowlist{
    "ActivateDate",     "block_devmode",
    "check_enrollment", "customization_id",
    "display_profiles", "initial_locale",
    "initial_timezone", "keyboard_layout",
    "model_name",       "oem_device_requisition",
    "oem_name",         "panel_backlight_max_nits",
    "Product_S/N",      "region",
    "rlz_brand_code",   "rlz_embargo_end_date",
    "serial_number",    "should_send_rlz_ping",
    "sku_number",
};

}  // namespace

int main(int argc, char* argv[]) {
  vpd::Vpd vpd;

  auto ro = vpd.GetValues(vpd::VpdRo);
  auto rw = vpd.GetValues(vpd::VpdRw);

  std::set<std::string> allowlist(std::begin(kVpdKeyAllowlist),
                                  std::end(kVpdKeyAllowlist));

  for (const auto& dict : std::array{ro, rw}) {
    for (const auto& [key, value] : dict) {
      if (allowlist.contains(key)) {
        std::cout << "\"" << key << "\"=\"" << value << "\"\n";
      }
    }
  }

  return 0;
}
