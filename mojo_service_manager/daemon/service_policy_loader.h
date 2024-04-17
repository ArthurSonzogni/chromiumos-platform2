// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MOJO_SERVICE_MANAGER_DAEMON_SERVICE_POLICY_LOADER_H_
#define MOJO_SERVICE_MANAGER_DAEMON_SERVICE_POLICY_LOADER_H_

#include <optional>
#include <string>
#include <vector>

#include <base/files/file_path.h>
#include <base/values.h>

#include "mojo_service_manager/daemon/service_policy.h"

struct passwd;

namespace chromeos::mojo_service_manager {

// Note that the functions here use |base::JSONReader| which is not guaranteed
// to be memory-safe. In production environment, all the policy files must come
// from a trusted source (e.g. verified rootfs).

// Loads all the policy files under |dir|. It is guaranteed that if a file
// cannot be parsed, all the rules in that file are ignored. The results will
// be merged into to |policy_map|. Returns whether all policy files are loaded
// successfully. When rules in two files conflict (i.e. try to own the same
// service), this tries to merge them and return false.
bool LoadAllServicePolicyFileFromDirectory(const base::FilePath& dir,
                                           ServicePolicyMap* policy_map);

// Same as above but load from multiple directories.
bool LoadAllServicePolicyFileFromDirectories(
    const std::vector<base::FilePath>& dirs, ServicePolicyMap* policy_map);

// Loads a policy file. Returns |nullopt| on error.
std::optional<ServicePolicyMap> LoadServicePolicyFile(
    const base::FilePath& file);

// Parses policy from a string. Returns |nullopt| on error.
std::optional<ServicePolicyMap> ParseServicePolicyFromString(
    const std::string& str);

// Same as above but takes a |base::Value|.
std::optional<ServicePolicyMap> ParseServicePolicyFromValue(
    const base::Value::List& value);

// A delegate for overriding functions for testing.
class LoadServicePolicyDelegate {
 public:
  LoadServicePolicyDelegate();
  LoadServicePolicyDelegate(const LoadServicePolicyDelegate&) = delete;
  LoadServicePolicyDelegate& operator=(const LoadServicePolicyDelegate&) =
      delete;
  virtual ~LoadServicePolicyDelegate();

  // Calls `getpwnam`.
  virtual const struct passwd* GetPWNam(const char* name) const;
};

// Sets delegate for testing. Pass `nullptr` to reset the delegate to the
// default one.
void SetLoadServicePolicyDelegateForTest(LoadServicePolicyDelegate* delegate);

}  // namespace chromeos::mojo_service_manager

#endif  // MOJO_SERVICE_MANAGER_DAEMON_SERVICE_POLICY_LOADER_H_
