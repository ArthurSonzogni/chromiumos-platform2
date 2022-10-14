// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/cellular/apn_list.h"

#include <set>
#include <string>
#include <tuple>
#include "base/strings/string_piece_forward.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"

#include <base/containers/contains.h>
#include <base/strings/string_number_conversions.h>
#include <chromeos/dbus/service_constants.h>
#include <chromeos/dbus/shill/dbus-constants.h>

#include "shill/cellular/cellular_helpers.h"
#include "shill/logging.h"
namespace shill {

namespace Logging {
static auto kModuleLogScope = ScopeLogger::kCellular;
}  // namespace Logging

void ApnList::AddApns(const std::vector<MobileOperatorInfo::MobileAPN>& apns,
                      ApnSource source) {
  for (const auto& mobile_apn : apns)
    AddApn(mobile_apn, source);
}

ApnList::ApnIndexKey ApnList::GetKey(
    const MobileOperatorInfo::MobileAPN& mobile_apn) {
  return std::make_tuple(mobile_apn.apn, mobile_apn.username,
                         mobile_apn.password, mobile_apn.authentication);
}

void ApnList::AddApn(const MobileOperatorInfo::MobileAPN& mobile_apn,
                     ApnSource source) {
  ApnList::ApnIndexKey index = GetKey(mobile_apn);
  if (!base::Contains(apn_index_, index)) {
    apn_dict_list_.emplace_back();
    apn_index_[index] = apn_dict_list_.size() - 1;
  }

  Stringmap& props = apn_dict_list_.at(apn_index_[index]);
  if (!mobile_apn.apn.empty())
    props[kApnProperty] = mobile_apn.apn;
  if (!mobile_apn.username.empty())
    props[kApnUsernameProperty] = mobile_apn.username;
  if (!mobile_apn.password.empty())
    props[kApnPasswordProperty] = mobile_apn.password;
  if (!mobile_apn.authentication.empty())
    props[kApnAuthenticationProperty] = mobile_apn.authentication;
  if (!mobile_apn.ip_type.empty())
    props[kApnIpTypeProperty] = mobile_apn.ip_type;

  props[cellular::kApnVersionProperty] =
      base::NumberToString(cellular::kCurrentApnCacheVersion);
  // Find the first localized and non-localized name, if any.
  if (!mobile_apn.operator_name_list.empty())
    props[kApnNameProperty] = mobile_apn.operator_name_list[0].name;

  props[kApnTypesProperty] = ApnList::JoinApnTypes(
      {mobile_apn.apn_types.begin(), mobile_apn.apn_types.end()});
  switch (source) {
    case ApnSource::kModb:
      props[cellular::kApnSource] = cellular::kApnSourceMoDb;
      break;
    case ApnSource::kModem:
      props[cellular::kApnSource] = cellular::kApnSourceModem;
      break;
  }
  for (const auto& lname : mobile_apn.operator_name_list) {
    if (!lname.language.empty())
      props[kApnLocalizedNameProperty] = lname.name;
  }
}

std::string ApnList::GetApnTypeString(enum ApnType apn_type) {
  switch (apn_type) {
    case ApnType::kDefault:
      return kApnTypeDefault;
    case ApnType::kAttach:
      return kApnTypeIA;
  }
}

bool ApnList::IsApnType(const Stringmap& apn_info, enum ApnType apn_type) {
  if (!base::Contains(apn_info, kApnTypesProperty)) {
    SLOG(1) << "APN info does not contain the apn_types property. APN:"
            << GetStringmapValue(apn_info, kApnProperty);
    return false;
  }
  std::vector<std::string> types =
      base::SplitString(apn_info.at(kApnTypesProperty), ",",
                        base::WhitespaceHandling::KEEP_WHITESPACE,
                        base::SplitResult::SPLIT_WANT_NONEMPTY);
  return std::count(types.begin(), types.end(),
                    ApnList::GetApnTypeString(apn_type));
}

bool ApnList::IsAttachApn(const Stringmap& apn_info) {
  return IsApnType(apn_info, ApnType::kAttach);
}

bool ApnList::IsDefaultApn(const Stringmap& apn_info) {
  return IsApnType(apn_info, ApnType::kDefault);
}

std::string ApnList::JoinApnTypes(std::vector<std::string> apn_types) {
  std::set<std::string> types(apn_types.begin(), apn_types.end());
  std::string joined_apn_types = base::JoinString(
      std::vector<base::StringPiece>(types.begin(), types.end()), ",");
  // Validate APN types
  types.erase(kApnTypeDefault);
  types.erase(kApnTypeIA);
  if (!types.empty()) {
    LOG(ERROR) << "Invalid APN type: " << *types.begin();
    DCHECK(false);
  }

  return joined_apn_types;
}

}  // namespace shill
