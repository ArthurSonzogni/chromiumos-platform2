// Copyright 2012 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "login_manager/policy_store.h"

#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/notreached.h>
#include <brillo/files/file_util.h>
#include <policy/policy_util.h>

#include "login_manager/system_utils_impl.h"

namespace login_manager {

PolicyStore::PolicyStore(SystemUtils* system_utils,
                         const base::FilePath& policy_path)
    : PolicyStore(system_utils, policy_path, /*is_resilient=*/false) {}

PolicyStore::PolicyStore(SystemUtils* system_utils,
                         const base::FilePath& policy_path,
                         bool is_resilient)
    : policy_path_(policy_path),
      system_utils_(system_utils),
      is_resilient_store_(is_resilient) {}

PolicyStore::~PolicyStore() {}

bool PolicyStore::EnsureLoadedOrCreated() {
  if (load_result_ == NOT_LOADED) {
    load_result_ = LoadOrCreate() ? LOAD_SUCCEEDED : LOAD_FAILED;
  }

  return load_result_ == LOAD_SUCCEEDED;
}

const enterprise_management::PolicyFetchResponse& PolicyStore::Get() const {
  return policy_;
}

bool PolicyStore::Persist() {
  return PersistToPath(policy_path_);
}

bool PolicyStore::LoadOrCreate() {
  return LoadOrCreateFromPath(policy_path_);
}

bool PolicyStore::LoadOrCreateFromPath(const base::FilePath& policy_path) {
  DCHECK(!is_resilient_store_);
  std::string polstr;
  policy::LoadPolicyResult result =
      system_utils_->LoadPolicyFromPath(policy_path, &polstr, &policy_);
  LOG(INFO) << "Policy loaded, result: " << static_cast<int>(result)
            << ", path: " << policy_path.value();
  switch (result) {
    case policy::LoadPolicyResult::kSuccess:
      return true;
    case policy::LoadPolicyResult::kFileNotFound:
      return true;
    case policy::LoadPolicyResult::kFailedToReadFile:
      LOG(WARNING) << "Failed to read policy file: " << policy_path.value();
      return false;
    case policy::LoadPolicyResult::kEmptyFile:
      LOG(WARNING) << "Empty policy file: " << policy_path.value();
      return false;
    case policy::LoadPolicyResult::kInvalidPolicyData:
      LOG(WARNING) << "Invalid policy data: " << policy_path.value();
      system_utils_->RemoveFile(policy_path);
      policy_.Clear();
      return false;
  }

  NOTREACHED_IN_MIGRATION();
}

bool PolicyStore::PersistToPath(const base::FilePath& policy_path) {
  // Skip if there's no change in policy data.
  if (!explicit_update_persist_pending_) {
    return true;
  }

  std::string policy_blob;
  if (!policy_.SerializeToString(&policy_blob)) {
    LOG(ERROR) << "Could not serialize policy!";
    return false;
  }

  // Policy file is owned by root.
  if (!system_utils_->WriteFileAtomically(
          policy_path, base::as_byte_span(policy_blob),
          S_IRUSR | S_IWUSR | S_IROTH, {.uid = 0, .gid = 0})) {
    return false;
  }

  LOG(INFO) << "Persisted policy to disk, path: " << policy_path.value();
  explicit_update_persist_pending_ = false;
  return true;
}

void PolicyStore::Set(
    const enterprise_management::PolicyFetchResponse& policy) {
  policy_.Clear();
  // This can only fail if |policy| and |policy_| are different types.
  policy_.CheckTypeAndMergeFrom(policy);
  explicit_update_persist_pending_ = true;
}

}  // namespace login_manager
