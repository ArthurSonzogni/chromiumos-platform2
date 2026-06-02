// Copyright 2012 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "login_manager/user_policy_service.h"

#include <stdint.h>
#include <sys/stat.h>

#include <string>
#include <utility>
#include <vector>

#include <base/files/file_util.h>
#include <base/logging.h>
#include <chromeos/dbus/service_constants.h>

#include "bindings/device_management_backend.pb.h"
#include "login_manager/dbus_util.h"
#include "login_manager/policy_key.h"
#include "login_manager/policy_store.h"
#include "login_manager/system_utils.h"

namespace em = enterprise_management;

namespace login_manager {

UserPolicyService::UserPolicyService(
    SystemUtils* system_utils,
    const base::FilePath& policy_dir,
    std::unique_ptr<PolicyKey> policy_key,
    std::unique_ptr<PolicyKey> extension_install_policy_key,
    const base::FilePath& key_copy_path,
    const base::FilePath& extension_install_key_copy_path)
    : PolicyService(system_utils,
                    policy_dir,
                    policy_key.get(),
                    extension_install_policy_key.get(),
                    nullptr,
                    false),
      scoped_policy_key_(std::move(policy_key)),
      scoped_extension_install_policy_key_(
          std::move(extension_install_policy_key)),
      key_copy_path_(key_copy_path),
      extension_install_key_copy_path_(extension_install_key_copy_path) {}

UserPolicyService::~UserPolicyService() = default;

void UserPolicyService::CopyKeyToPath(PolicyKey* key,
                                      const base::FilePath& path) {
  if (path.empty()) {
    return;
  }
  if (key && key->IsPopulated()) {
    base::FilePath dir(path.DirName());
    base::CreateDirectory(dir);
    mode_t mode = S_IRWXU | S_IXGRP | S_IXOTH;
    chmod(dir.value().c_str(), mode);

    system_utils()->WriteFileAtomically(path, key->public_key_der(),
                                        S_IRUSR | S_IRGRP | S_IROTH);
  } else {
    // Remove the key if it has been cleared.
    system_utils()->RemoveFile(path);
  }
}

void UserPolicyService::PersistKeyCopy() {
  CopyKeyToPath(scoped_policy_key_.get(), key_copy_path_);
  CopyKeyToPath(scoped_extension_install_policy_key_.get(),
                extension_install_key_copy_path_);
}

void UserPolicyService::Store(const PolicyNamespace& ns,
                              const std::vector<uint8_t>& policy_blob,
                              int key_flags,
                              Completion completion) {
  em::PolicyFetchResponse policy;
  em::PolicyData policy_data;
  if (!policy.ParseFromArray(policy_blob.data(), policy_blob.size()) ||
      !policy.has_policy_data() ||
      !policy_data.ParseFromString(policy.policy_data())) {
    std::move(completion)
        .Run(CREATE_ERROR_AND_LOG(dbus_error::kSigDecodeFail,
                                  "Unable to parse policy protobuf."));
    return;
  }

  // Allow to switch to unmanaged state even if no signature is present.
  if (policy_data.state() == em::PolicyData::UNMANAGED &&
      !policy.has_policy_data_signature()) {
    PolicyKey* validation_key = ns.first == POLICY_DOMAIN_EXTENSION_INSTALL
                                    ? extension_install_policy_key()
                                    : key();

    // Also clear the key.
    if (validation_key && validation_key->IsPopulated()) {
      validation_key->ClobberCompromisedKey(std::vector<uint8_t>());
      PersistKey(validation_key);
    }

    GetOrCreateStore(ns)->Set(policy);
    PersistPolicy(ns, std::move(completion));
    return;
  }

  PolicyService::StorePolicy(ns, policy, key_flags, std::move(completion));
}

void UserPolicyService::OnKeyPersisted(PolicyKey* key, bool status) {
  if (status) {
    if (key == scoped_policy_key_.get()) {
      CopyKeyToPath(scoped_policy_key_.get(), key_copy_path_);
    } else if (key == scoped_extension_install_policy_key_.get()) {
      CopyKeyToPath(scoped_extension_install_policy_key_.get(),
                    extension_install_key_copy_path_);
    } else {
      LOG(FATAL) << "Unknown key persisted to disk.";
    }
  }
  // Only notify the delegate after writing the copy, so that chrome can find
  // the file after being notified that the key is ready.
  PolicyService::OnKeyPersisted(key, status);
}

}  // namespace login_manager
