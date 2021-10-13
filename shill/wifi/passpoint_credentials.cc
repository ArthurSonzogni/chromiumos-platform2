// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/wifi/passpoint_credentials.h"

#include <chromeos/dbus/shill/dbus-constants.h>
#include <string>
#include <vector>
#include <uuid/uuid.h>

#include "shill/eap_credentials.h"
#include "shill/error.h"
#include "shill/key_value_store.h"
#include "shill/profile.h"
#include "shill/refptr_types.h"
#include "shill/store_interface.h"

namespace shill {

// Size of an UUID string.
constexpr size_t kUUIDStringLength = 37;

PasspointCredentials::PasspointCredentials(
    const std::string& id,
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
      android_package_name_(android_package_name),
      id_(id),
      profile_(nullptr) {}

void PasspointCredentials::SetProfile(const ProfileRefPtr& profile) {
  profile_ = profile;
}

void PasspointCredentials::Load(const StoreInterface* storage) {
  CHECK(storage);
  CHECK(!id_.empty());

  storage->GetStringList(id_, kStorageDomains, &domains_);
  storage->GetString(id_, kStorageRealm, &realm_);
  storage->GetUint64List(id_, kStorageHomeOIs, &home_ois_);
  storage->GetUint64List(id_, kStorageRequiredHomeOIs, &required_home_ois_);
  storage->GetUint64List(id_, kStorageRoamingConsortia, &roaming_consortia_);
  storage->GetBool(id_, kStorageMeteredOverride, &metered_override_);
  storage->GetString(id_, kStorageAndroidPackageName, &android_package_name_);
  eap_.Load(storage, id_);
}

bool PasspointCredentials::Save(StoreInterface* storage) {
  CHECK(storage);
  CHECK(!id_.empty());

  // The credentials identifier is unique, we can use it as storage identifier.
  storage->SetString(id_, kStorageType, kTypePasspoint);
  storage->SetStringList(id_, kStorageDomains, domains_);
  storage->SetString(id_, kStorageRealm, realm_);
  storage->SetUint64List(id_, kStorageHomeOIs, home_ois_);
  storage->SetUint64List(id_, kStorageRequiredHomeOIs, required_home_ois_);
  storage->SetUint64List(id_, kStorageRoamingConsortia, roaming_consortia_);
  storage->SetBool(id_, kStorageMeteredOverride, metered_override_);
  storage->SetString(id_, kStorageAndroidPackageName, android_package_name_);
  eap_.Save(storage, id_, /*save_credentials=*/true);

  return true;
}

std::string PasspointCredentials::GenerateIdentifier() {
  uuid_t uuid_bytes;
  uuid_generate_random(uuid_bytes);
  std::string uuid(kUUIDStringLength, '\0');
  uuid_unparse(uuid_bytes, &uuid[0]);
  // Remove the null terminator from the string.
  uuid.resize(kUUIDStringLength - 1);
  return uuid;
}

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

  // Create the set of credentials with a unique identifier.
  std::string id = GenerateIdentifier();
  PasspointCredentialsRefPtr creds = new PasspointCredentials(
      id, domains, realm, home_ois, required_home_ois, roaming_consortia,
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
