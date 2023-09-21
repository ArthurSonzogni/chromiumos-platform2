// Copyright 2012 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "login_manager/policy_service.h"

#include <stdint.h>

#include <string>
#include <utility>

#include <base/check.h>
#include <base/check_op.h>
#include <base/functional/bind.h>
#include <base/functional/callback.h>
#include <base/location.h>
#include <base/logging.h>
#include <base/notreached.h>
#include <base/synchronization/waitable_event.h>
#include <brillo/message_loops/message_loop.h>
#include <chromeos/dbus/service_constants.h>

#include "bindings/device_management_backend.pb.h"
#include "crypto/signature_verifier.h"
#include "login_manager/blob_util.h"
#include "login_manager/dbus_util.h"
#include "login_manager/nss_util.h"
#include "login_manager/policy_key.h"
#include "login_manager/policy_store.h"
#include "login_manager/resilient_policy_store.h"
#include "login_manager/system_utils.h"
#include "login_manager/validator_utils.h"

namespace em = enterprise_management;

namespace login_manager {

PolicyNamespace MakeChromePolicyNamespace() {
  return std::make_pair(POLICY_DOMAIN_CHROME, std::string());
}

// Returns true if the domain, when part of a PolicyNamespace, expects a
// non-empty |component_id()|.
bool IsComponentDomain(PolicyDomain domain) {
  switch (domain) {
    case POLICY_DOMAIN_CHROME:
      return false;
    case POLICY_DOMAIN_EXTENSIONS:
    case POLICY_DOMAIN_SIGNIN_EXTENSIONS:
      return true;
  }
  NOTREACHED();
  return false;
}

constexpr char PolicyService::kChromePolicyFileName[] = "policy";
constexpr char PolicyService::kExtensionsPolicyFileNamePrefix[] =
    "policy_extension_id_";
constexpr char PolicyService::kSignInExtensionsPolicyFileNamePrefix[] =
    "policy_signin_extension_id_";

PolicyService::PolicyService(const base::FilePath& policy_dir,
                             PolicyKey* policy_key,
                             LoginMetrics* metrics,
                             bool resilient_chrome_policy_store)
    : metrics_(metrics),
      policy_dir_(policy_dir),
      policy_key_(policy_key),
      resilient_chrome_policy_store_(resilient_chrome_policy_store),
      delegate_(nullptr),
      weak_ptr_factory_(this) {}

PolicyService::~PolicyService() = default;

void PolicyService::Store(const PolicyNamespace& ns,
                          const std::vector<uint8_t>& policy_blob,
                          int key_flags,
                          Completion completion) {
  em::PolicyFetchResponse policy;
  if (!policy.ParseFromArray(policy_blob.data(), policy_blob.size()) ||
      !policy.has_policy_data()) {
    std::move(completion)
        .Run(CREATE_ERROR_AND_LOG(dbus_error::kSigDecodeFail,
                                  "Unable to parse policy protobuf."));
    return;
  }

  StorePolicy(ns, policy, key_flags, std::move(completion));
}

bool PolicyService::Retrieve(const PolicyNamespace& ns,
                             std::vector<uint8_t>* policy_blob) {
  *policy_blob = SerializeAsBlob(GetOrCreateStore(ns)->Get());
  return true;
}

void PolicyService::PersistPolicy(const PolicyNamespace& ns,
                                  Completion completion) {
  const bool success = GetOrCreateStore(ns)->Persist();
  OnPolicyPersisted(std::move(completion),
                    success ? dbus_error::kNone : dbus_error::kSigEncodeFail);
}

PolicyStore* PolicyService::GetOrCreateStore(const PolicyNamespace& ns) {
  PolicyStoreMap::const_iterator iter = policy_stores_.find(ns);
  if (iter != policy_stores_.end())
    return iter->second.get();

  bool resilient =
      (ns == MakeChromePolicyNamespace() && resilient_chrome_policy_store_);
  std::unique_ptr<PolicyStore> store;
  if (resilient)
    store = std::make_unique<ResilientPolicyStore>(GetPolicyPath(ns), metrics_);
  else
    store = std::make_unique<PolicyStore>(GetPolicyPath(ns));

  store->EnsureLoadedOrCreated();
  PolicyStore* policy_store_ptr = store.get();
  policy_stores_[ns] = std::move(store);
  return policy_store_ptr;
}

void PolicyService::SetStoreForTesting(const PolicyNamespace& ns,
                                       std::unique_ptr<PolicyStore> store) {
  policy_stores_[ns] = std::move(store);
}

void PolicyService::StorePolicy(const PolicyNamespace& ns,
                                const em::PolicyFetchResponse& policy,
                                int key_flags,
                                Completion completion) {
  // Determine if the policy has pushed a new owner key and, if so, set it.
  if (policy.has_new_public_key() && !key()->Equals(policy.new_public_key())) {
    // The policy contains a new key, and it is different from |key_|.
    std::vector<uint8_t> der = StringToBlob(policy.new_public_key());

    bool installed = false;
    if (key()->IsPopulated()) {
      if (policy.has_new_public_key_signature() && (key_flags & KEY_ROTATE)) {
        // Graceful key rotation.
        LOG(INFO) << "Attempting policy key rotation.";
        installed =
            key()->Rotate(der, StringToBlob(policy.new_public_key_signature()),
                          crypto::SignatureVerifier::RSA_PKCS1_SHA1);
      }
    } else if (key_flags & KEY_INSTALL_NEW) {
      LOG(INFO) << "Attempting to install new policy key.";
      installed = key()->PopulateFromBuffer(der);
    }
    if (!installed && (key_flags & KEY_CLOBBER)) {
      LOG(INFO) << "Clobbering existing policy key.";
      installed = key()->ClobberCompromisedKey(der);
    }

    if (!installed) {
      std::move(completion)
          .Run(CREATE_ERROR_AND_LOG(dbus_error::kPubkeySetIllegal,
                                    "Failed to install policy key!"));
      return;
    }

    // If here, need to persist the key just loaded into memory to disk.
    PersistKey();
  }

  // Validate signature on policy and persist to disk.
  if (!key()->Verify(StringToBlob(policy.policy_data()),
                     StringToBlob(policy.policy_data_signature()),
                     crypto::SignatureVerifier::RSA_PKCS1_SHA1)) {
    std::move(completion)
        .Run(CREATE_ERROR_AND_LOG(dbus_error::kVerifyFail,
                                  "Signature could not be verified."));
    return;
  }

  GetOrCreateStore(ns)->Set(policy);
  PersistPolicy(ns, std::move(completion));
}

void PolicyService::OnKeyPersisted(bool status) {
  if (status)
    LOG(INFO) << "Persisted policy key to disk.";
  else
    LOG(ERROR) << "Failed to persist policy key to disk.";
  if (delegate_)
    delegate_->OnKeyPersisted(status);
}

void PolicyService::OnPolicyPersisted(Completion completion,
                                      const std::string& dbus_error_code) {
  if (completion.is_null()) {
    LOG(INFO) << "Policy persisted with no completion, result: "
              << dbus_error_code;
  } else {
    brillo::ErrorPtr error;
    if (dbus_error_code != dbus_error::kNone) {
      constexpr char kMessage[] = "Failed to persist policy to disk.";
      LOG(ERROR) << kMessage << ": " << dbus_error_code;
      error = CreateError(dbus_error_code, kMessage);
    }

    std::move(completion).Run(std::move(error));
  }

  if (delegate_)
    delegate_->OnPolicyPersisted(dbus_error_code == dbus_error::kNone);
}

void PolicyService::PersistKey() {
  OnKeyPersisted(key()->Persist());
}

base::FilePath PolicyService::GetPolicyPath(const PolicyNamespace& ns) {
  // If the store has already been already created, return the store's path.
  PolicyStoreMap::const_iterator iter = policy_stores_.find(ns);
  if (iter != policy_stores_.end())
    return iter->second->policy_path();

  const PolicyDomain& domain = ns.first;
  const std::string& component_id = ns.second;
  switch (domain) {
    case POLICY_DOMAIN_CHROME:
      return policy_dir_.AppendASCII(kChromePolicyFileName);
    case POLICY_DOMAIN_EXTENSIONS:
      // Double-check extension ID (should have already been checked before).
      CHECK(ValidateExtensionId(component_id));
      return policy_dir_.AppendASCII(kExtensionsPolicyFileNamePrefix +
                                     component_id);
    case POLICY_DOMAIN_SIGNIN_EXTENSIONS:
      // Double-check extension ID (should have already been checked before).
      CHECK(ValidateExtensionId(component_id));
      return policy_dir_.AppendASCII(kSignInExtensionsPolicyFileNamePrefix +
                                     component_id);
  }
}

}  // namespace login_manager
