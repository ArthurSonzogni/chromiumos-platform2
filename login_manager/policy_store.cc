// Copyright 2012 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "login_manager/policy_store.h"

#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/notreached.h>
#include <policy/policy_util.h>

#include "login_manager/system_utils_impl.h"

namespace login_manager {

namespace {

const char kPrefsFileName[] = "preferences";

}  // namespace

PolicyStore::PolicyStore(const base::FilePath& policy_path)
    : PolicyStore(policy_path, /*is_resilient=*/false) {}

PolicyStore::PolicyStore(const base::FilePath& policy_path, bool is_resilient)
    : policy_path_(policy_path), is_resilient_store_(is_resilient) {}

PolicyStore::~PolicyStore() {}

bool PolicyStore::DefunctPrefsFilePresent() {
  return base::PathExists(policy_path_.DirName().Append(kPrefsFileName));
}

bool PolicyStore::EnsureLoadedOrCreated() {
  if (load_result_ == NOT_LOADED)
    load_result_ = LoadOrCreate() ? LOAD_SUCCEEDED : LOAD_FAILED;

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
  std::string polstr;
  cached_policy_data_.clear();
  policy::LoadPolicyResult result =
      policy::LoadPolicyFromPath(policy_path, &polstr, &policy_);
  switch (result) {
    case policy::LoadPolicyResult::kSuccess:
      cached_policy_data_ = polstr;
      return true;
    case policy::LoadPolicyResult::kFileNotFound:
      if (!is_resilient_store_) {
        return true;
      }
      LOG(WARNING) << "Resilient policy file not found: "
                   << policy_path.value();
      return false;
    case policy::LoadPolicyResult::kFailedToReadFile:
      LOG(WARNING) << "Failed to read policy file: " << policy_path.value();
      return false;
    case policy::LoadPolicyResult::kEmptyFile:
      LOG(WARNING) << "Empty policy file: " << policy_path.value();
      return false;
    case policy::LoadPolicyResult::kInvalidPolicyData:
      LOG(WARNING) << "Invalid policy data: " << policy_path.value();
      base::DeleteFile(policy_path);
      policy_.Clear();
      return false;
  }

  NOTREACHED();
}

bool PolicyStore::PersistToPath(const base::FilePath& policy_path) {
  SystemUtilsImpl utils;
  std::string policy_blob;
  if (!policy_.SerializeToString(&policy_blob)) {
    LOG(ERROR) << "Could not serialize policy!";
    return false;
  }

  // Skip writing to the file if the contents of policy data haven't been
  // changed.
  if (cached_policy_data_ == policy_blob)
    return true;

  if (!utils.AtomicFileWrite(policy_path, policy_blob))
    return false;

  LOG(INFO) << "Persisted policy to disk, path: " << policy_path.value();
  cached_policy_data_ = policy_blob;
  return true;
}

void PolicyStore::Set(
    const enterprise_management::PolicyFetchResponse& policy) {
  policy_.Clear();
  // This can only fail if |policy| and |policy_| are different types.
  policy_.CheckTypeAndMergeFrom(policy);
}

bool PolicyStore::Delete() {
  if (!base::DeleteFile(policy_path_))
    return false;
  policy_.Clear();
  return true;
}

}  // namespace login_manager
