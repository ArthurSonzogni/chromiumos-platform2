// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/common/mojo_type_utils.h"

#include <base/strings/string_split.h>

namespace diagnostics {
namespace {

const auto kEqualStr = "[Equal]";
const auto kNullStr = "[null]";

namespace mojo_ipc = ::chromeos::cros_healthd::mojom;

// Helper macro for defining the |GetDiffString| of mojo structs. See below
// definitions of |GetDiffString| for the usage.
#define FIELD_DS(label) \
  ((#label ":\n") + Indent(GetDiffString(a.label, b.label)))

// For each line, adds a 2-space-indent at the beginning.
std::string Indent(const std::string& s) {
  const auto prefix = "  ";
  std::string res;
  for (const auto& line :
       SplitString(s, "\n", base::KEEP_WHITESPACE, base::SPLIT_WANT_NONEMPTY)) {
    res += prefix + line + "\n";
  }
  return res;
}

std::string StringCompareFormat(const std::string& a, const std::string& b) {
  return "'" + a + "' vs '" + b + "'";
}

}  // namespace

template <>
std::string GetDiffString<std::string>(const std::string& a,
                                       const std::string& b) {
  if (a == b)
    return kEqualStr;
  return StringCompareFormat(a, b);
}

template <>
std::string GetDiffString<base::Optional<std::string>>(
    const base::Optional<std::string>& a,
    const base::Optional<std::string>& b) {
  if (a == b)
    return kEqualStr;
  return StringCompareFormat(a.value_or(kNullStr), b.value_or(kNullStr));
}

template <>
std::string GetDiffString<mojo_ipc::NullableUint64>(
    const mojo_ipc::NullableUint64& a, const mojo_ipc::NullableUint64& b) {
  return GetDiffString(std::to_string(a.value), std::to_string(b.value));
}

template <>
std::string GetDiffString<mojo_ipc::VpdInfo>(const mojo_ipc::VpdInfo& a,
                                             const mojo_ipc::VpdInfo& b) {
  return FIELD_DS(activate_date) + FIELD_DS(mfg_date) + FIELD_DS(model_name) +
         FIELD_DS(region) + FIELD_DS(serial_number) + FIELD_DS(sku_number);
}

template <>
std::string GetDiffString<mojo_ipc::DmiInfo>(const mojo_ipc::DmiInfo& a,
                                             const mojo_ipc::DmiInfo& b) {
  return FIELD_DS(bios_vendor) + FIELD_DS(bios_version) + FIELD_DS(board_name) +
         FIELD_DS(board_vendor) + FIELD_DS(board_version) +
         FIELD_DS(chassis_vendor) + FIELD_DS(chassis_type) +
         FIELD_DS(product_family) + FIELD_DS(product_name) +
         FIELD_DS(product_version) + FIELD_DS(sys_vendor);
}

template <>
std::string GetDiffString<mojo_ipc::OsVersion>(const mojo_ipc::OsVersion& a,
                                               const mojo_ipc::OsVersion& b) {
  return FIELD_DS(release_milestone) + FIELD_DS(build_number) +
         FIELD_DS(patch_number) + FIELD_DS(release_channel);
}

template <>
std::string GetDiffString<mojo_ipc::OsInfo>(const mojo_ipc::OsInfo& a,
                                            const mojo_ipc::OsInfo& b) {
  return FIELD_DS(code_name) + FIELD_DS(marketing_name) + FIELD_DS(boot_mode) +
         FIELD_DS(os_version);
}

template <>
std::string GetDiffString<mojo_ipc::SystemInfoV2>(
    const mojo_ipc::SystemInfoV2& a, const mojo_ipc::SystemInfoV2& b) {
  return FIELD_DS(vpd_info) + FIELD_DS(dmi_info) + FIELD_DS(os_info);
}

}  // namespace diagnostics
