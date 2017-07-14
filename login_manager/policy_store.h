// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LOGIN_MANAGER_POLICY_STORE_H_
#define LOGIN_MANAGER_POLICY_STORE_H_

#include <string>

#include <base/files/file_path.h>
#include <base/macros.h>

#include "bindings/device_management_backend.pb.h"

namespace login_manager {
class PolicyKey;

// This class holds policy settings and takes care of reading from and writing
// it to a file on disk. The policy is represented as a PolicyFetchResponse
// protobuffer, which may contain per-device or per-user policy in its payload.
//
// If there is a policy on disk at creation time, we will load it along with its
// signature.  A new policy and its attendant signature can be set at any time
// and persisted to disk on-demand.
//
// THIS CLASS DOES NO SIGNATURE VALIDATION.
class PolicyStore {
 public:
  explicit PolicyStore(const base::FilePath& policy_path);
  virtual ~PolicyStore();

  virtual bool DefunctPrefsFilePresent();

  // Load the signed policy off of disk into |policy_|.
  // Returns true unless there is a policy on disk and loading it fails.
  virtual bool LoadOrCreate();

  virtual const enterprise_management::PolicyFetchResponse& Get() const;

  // Persist |policy_| to disk at |policy_file_|
  // Returns false if there's an error while writing data.
  virtual bool Persist();

  // Clobber the stored policy with new data.
  virtual void Set(const enterprise_management::PolicyFetchResponse& policy);

 private:
  static const char kPrefsFileName[];

  // The cached policy data from |policy_path_|. It is kept up to date whenever
  // the contents in the file are updated by this object.
  std::string cached_policy_data_;

  enterprise_management::PolicyFetchResponse policy_;
  const base::FilePath policy_path_;

  DISALLOW_COPY_AND_ASSIGN(PolicyStore);
};
}  // namespace login_manager

#endif  // LOGIN_MANAGER_POLICY_STORE_H_
