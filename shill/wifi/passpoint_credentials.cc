// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/wifi/passpoint_credentials.h"

#include <chromeos/dbus/shill/dbus-constants.h>
#include <string>
#include <vector>

#include "shill/eap_credentials.h"
#include "shill/error.h"
#include "shill/key_value_store.h"
#include "shill/refptr_types.h"

namespace shill {

PasspointCredentials::PasspointCredentials(
    const std::vector<std::string>& domains,
    const std::string& realm,
    const std::vector<uint64_t>& home_ois,
    const std::vector<uint64_t>& required_home_ois,
    const std::vector<uint64_t>& roaming_consortia,
    bool metered_override,
    const std::string& android_package_name)
    : domains_(domains),
      realm_(realm),
      home_ois_(home_ois),
      required_home_ois_(required_home_ois),
      roaming_consortia_(roaming_consortia),
      metered_override_(metered_override),
      android_package_name_(android_package_name) {}

PasspointCredentialsRefPtr PasspointCredentials::CreatePasspointCredentials(
    const KeyValueStore& args, Error* error) {
  std::vector<std::string> domains;
  std::string realm;
  std::vector<uint64_t> home_ois, required_home_ois, roaming_consortia;
  bool metered_override;
  std::string android_package_name;

  domains = args.Lookup<std::vector<std::string>>(
      kPasspointCredentialsDomainsProperty, std::vector<std::string>());
  if (domains.empty()) {
    Error::PopulateAndLog(
        FROM_HERE, error, Error::kInvalidArguments,
        "at least one FQDN is required in " +
            std::string(kPasspointCredentialsDomainsProperty));
    return nullptr;
  }
  for (const auto& domain : domains) {
    if (!EapCredentials::ValidDomainSuffixMatch(domain)) {
      Error::PopulateAndLog(FROM_HERE, error, Error::kInvalidArguments,
                            "domain '" + domain + "' is not a valid FQDN");
      return nullptr;
    }
  }

  if (!args.Contains<std::string>(kPasspointCredentialsRealmProperty)) {
    Error::PopulateAndLog(FROM_HERE, error, Error::kInvalidArguments,
                          std::string(kPasspointCredentialsRealmProperty) +
                              " property is mandatory");
    return nullptr;
  }
  realm = args.Get<std::string>(kPasspointCredentialsRealmProperty);
  if (!EapCredentials::ValidDomainSuffixMatch(realm)) {
    Error::PopulateAndLog(FROM_HERE, error, Error::kInvalidArguments,
                          "realm '" + realm + "' is not a valid FQDN");
    return nullptr;
  }

  home_ois = args.Lookup<std::vector<uint64_t>>(
      kPasspointCredentialsHomeOIsProperty, std::vector<uint64_t>());
  required_home_ois = args.Lookup<std::vector<uint64_t>>(
      kPasspointCredentialsRequiredHomeOIsProperty, std::vector<uint64_t>());
  roaming_consortia = args.Lookup<std::vector<uint64_t>>(
      kPasspointCredentialsRoamingConsortiaProperty, std::vector<uint64_t>());
  metered_override =
      args.Lookup<bool>(kPasspointCredentialsMeteredOverrideProperty, false);
  android_package_name = args.Lookup<std::string>(
      kPasspointCredentialsAndroidPackageNameProperty, std::string());

  PasspointCredentialsRefPtr creds = new PasspointCredentials(
      domains, realm, home_ois, required_home_ois, roaming_consortia,
      metered_override, android_package_name);

  // Load EAP credentials from the set of properties.
  creds->eap_.Load(args);

  // Check the set of credentials is consistent.
  if (!creds->eap().IsConnectable()) {
    Error::PopulateAndLog(FROM_HERE, error, Error::kInvalidArguments,
                          "EAP credendials not connectable");
    return nullptr;
  }

  // Our Passpoint implementation only supports EAP TLS or TTLS. SIM based EAP
  // methods are not supported on ChromeOS yet.
  std::string method = creds->eap().method();
  if (method != kEapMethodTLS && method != kEapMethodTTLS) {
    Error::PopulateAndLog(
        FROM_HERE, error, Error::kInvalidArguments,
        "EAP method '" + method + "' is not supported by Passpoint");
    return nullptr;
  }

  // The only valid inner EAP method for TTLS is MSCHAPv2
  std::string inner_method = creds->eap().inner_method();
  if (method == kEapMethodTTLS && inner_method != kEapPhase2AuthTTLSMSCHAPV2) {
    Error::PopulateAndLog(FROM_HERE, error, Error::kInvalidArguments,
                          "TTLS inner EAP method '" + inner_method +
                              "' is not supported by Passpoint");
    return nullptr;
  }

  return creds;
}

}  // namespace shill
