// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_CELLULAR_MOBILE_APN_H_
#define SHILL_CELLULAR_MOBILE_APN_H_

#include <map>
#include <set>
#include <string>
#include <tuple>
#include <vector>

namespace shill {

// Encapsulates a name and the language that name has been localized to.
// The name can be a carrier name, or the name that a cellular carrier
// prefers to show for a certain access point.
struct LocalizedName {
  // The name as it appears in the corresponding language.
  std::string name;
  // The language of this localized name. The format of a language is a two
  // letter language code, e.g. 'en' for English.
  // It is legal for an instance of LocalizedName to have an empty |language|
  // field, as sometimes the underlying database does not contain that
  // information.
  std::string language;

 private:
  auto tuple() const { return std::tie(name, language); }

 public:
  bool operator==(const LocalizedName& rhs) const {
    return tuple() == rhs.tuple();
  }
};

// Encapsulates information on a mobile access point name. This information
// is usually necessary for 3GPP networks to be able to connect to a mobile
// network.
struct MobileAPN {
  // The access point url, which is fed to the modemmanager while connecting.
  std::string apn;
  // A list of localized names for this access point. Usually there is only
  // one for each country that the associated cellular carrier operates in.
  std::vector<LocalizedName> operator_name_list;
  // The username and password fields that are required by the modemmanager.
  // Either of these values can be empty if none is present. If a MobileAPN
  // instance that is obtained from this parser contains a non-empty value
  // for username/password, this usually means that the carrier requires
  // a certain default pair.
  std::string username;
  std::string password;
  // The authentication method for sending username / password, which could
  // be one of the following values:
  // * (empty):
  //   - When no username or password is provided, no authentication method
  //     is specified.
  //   - When a username and password is provided, the default authentication
  //     method is used (which is PAP for most cases in the current
  //     implementation of ModemManager).
  // * "pap" (kApnAuthenticationPap):
  //   - Password Authentication Protocol (PAP) is used for authentication
  // * "chap" (kApnAuthenticationChap):
  //   - Challenge-Handshake Authentication Protocol (CHAP) for authentication
  std::string authentication;
  // A list of APN types.
  std::set<std::string> apn_types;
  // IP type as one of "ipv4", "ipv6", "ipv4v6" (dual-stack)
  std::string ip_type;
  // If the APN overrides all other APNs of the same type.
  bool is_required_by_carrier_spec = false;

 private:
  auto tuple() const {
    return std::tie(apn, operator_name_list, username, password, authentication,
                    apn_types, ip_type, is_required_by_carrier_spec);
  }

 public:
  bool operator==(const MobileAPN& rhs) const { return tuple() == rhs.tuple(); }
};

}  // namespace shill

#endif  // SHILL_CELLULAR_MOBILE_APN_H_
