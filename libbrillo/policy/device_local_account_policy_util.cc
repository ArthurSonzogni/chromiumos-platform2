// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <vector>

#include "policy/device_local_account_policy_util.h"

#include <base/containers/fixed_flat_map.h>
#include <base/notreached.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>
#include <components/policy/core/common/device_local_account_type.h>

namespace em = enterprise_management;

namespace policy {

namespace {

const auto kDomainPrefixMap =
    base::MakeFixedFlatMap<em::DeviceLocalAccountInfoProto::AccountType,
                           std::string>({
        {em::DeviceLocalAccountInfoProto::ACCOUNT_TYPE_PUBLIC_SESSION,
         "public-accounts"},
        {em::DeviceLocalAccountInfoProto::ACCOUNT_TYPE_KIOSK_APP, "kiosk-apps"},
        {em::DeviceLocalAccountInfoProto::ACCOUNT_TYPE_KIOSK_ANDROID_APP,
         "arc-kiosk-apps"},
        {em::DeviceLocalAccountInfoProto::ACCOUNT_TYPE_SAML_PUBLIC_SESSION,
         "saml-public-accounts"},
        {em::DeviceLocalAccountInfoProto::ACCOUNT_TYPE_WEB_KIOSK_APP,
         "web-kiosk-apps"},
    });

constexpr char kDeviceLocalAccountDomainSuffix[] = ".device-local.localhost";

}  // namespace

std::string CanonicalizeEmail(const std::string& email_address) {
  std::string lower_case_email = base::ToLowerASCII(email_address);
  std::vector<std::string> parts = base::SplitString(
      lower_case_email, "@", base::TRIM_WHITESPACE, base::SPLIT_WANT_ALL);
  if (parts.size() != 2U)
    return lower_case_email;

  if (parts[1] == "gmail.com")  // only strip '.' for gmail accounts.
    base::RemoveChars(parts[0], ".", &parts[0]);

  std::string new_email = base::JoinString(parts, "@");
  return new_email;
}

std::string GenerateDeviceLocalAccountUserId(
    const std::string& account_id,
    em::DeviceLocalAccountInfoProto_AccountType type) {
  std::string domain_prefix;
  const auto* it = kDomainPrefixMap.find(type);
  if (it == kDomainPrefixMap.end()) {
    domain_prefix = "";
  } else {
    domain_prefix = it->second;
  }

  return CanonicalizeEmail(
      base::HexEncode(account_id.c_str(), account_id.size()) + "@" +
      domain_prefix + ".device-local.localhost");
}

base::expected<em::DeviceLocalAccountInfoProto_AccountType,
               GetDeviceLocalAccountTypeError>
GetDeviceLocalAccountType(std::string* account_id) {
  // For historical reasons, the guest user ID does not contain an @ symbol and
  // therefore, cannot be parsed by gaia::ExtractDomainName().
  if (account_id->find('@') == std::string::npos) {
    return base::unexpected(
        GetDeviceLocalAccountTypeError::kNoDeviceLocalAccountUser);
  }

  const std::string domain = ExtractDomainName(*account_id);
  if (!base::EndsWith(domain, kDeviceLocalAccountDomainSuffix,
                      base::CompareCase::SENSITIVE)) {
    return base::unexpected(
        GetDeviceLocalAccountTypeError::kNoDeviceLocalAccountUser);
  }

  // Strip the domain suffix.
  std::string_view domain_prefix = domain;
  domain_prefix.remove_suffix(sizeof(kDeviceLocalAccountDomainSuffix) - 1);

  // Reverse look up from the map.
  for (const auto& [type, candidate] : kDomainPrefixMap) {
    if (domain_prefix == candidate) {
      return base::ok(type);
    }
  }

  // |user_id| is a device-local account but its type is not recognized.
  NOTREACHED();
  return base::unexpected(GetDeviceLocalAccountTypeError::kUnknownDomain);
}

std::string ExtractDomainName(const std::string& email_address) {
  // First canonicalize which will also verify we have proper domain part.
  std::string email = CanonicalizeEmail(email_address);
  size_t separator_pos = email.find('@');
  if (separator_pos != email.npos && separator_pos < email.length() - 1)
    return email.substr(separator_pos + 1);
  else
    NOTREACHED() << "Not a proper email address: " << email;
  return std::string();
}

}  // namespace policy
