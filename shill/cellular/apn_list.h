// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_CELLULAR_APN_LIST_H_
#define SHILL_CELLULAR_APN_LIST_H_

#include <map>
#include <string>
#include <tuple>
#include <vector>

#include "shill/cellular/cellular_consts.h"
#include "shill/cellular/mobile_operator_info.h"
#include "shill/data_types.h"

namespace shill {

class ApnList {
 public:
  ApnList() = default;
  ~ApnList() = default;

  enum class ApnSource { kModb, kModem };
  enum class ApnType { kDefault, kAttach };
  void AddApns(const std::vector<MobileOperatorInfo::MobileAPN>& apns,
               ApnSource source);

  const Stringmaps& GetList() { return apn_dict_list_; }

  static bool IsApnType(const Stringmap& apn_info, enum ApnType apn_type);
  static bool IsAttachApn(const Stringmap& apn_info);
  static bool IsDefaultApn(const Stringmap& apn_info);

  static std::string JoinApnTypes(std::vector<std::string> apn_types);

 private:
  using ApnIndexKey =
      std::tuple<std::string, std::string, std::string, std::string>;
  ApnIndexKey GetKey(const MobileOperatorInfo::MobileAPN& mobile_apn);

  void AddApn(const MobileOperatorInfo::MobileAPN& mobile_apn,
              ApnSource source);

  static std::string GetApnTypeString(enum ApnType apn_type);
  Stringmaps apn_dict_list_;
  std::map<ApnIndexKey, int> apn_index_;

  ApnList(const ApnList&) = delete;
  ApnList& operator=(const ApnList&) = delete;
};

}  // namespace shill

#endif  // SHILL_CELLULAR_APN_LIST_H_
