// Copyright 2016 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LOGIN_MANAGER_VPD_PROCESS_H_
#define LOGIN_MANAGER_VPD_PROCESS_H_

#include <string>
#include <utility>
#include <vector>

#include "login_manager/policy_service.h"

namespace login_manager {

class VpdProcess {
 public:
  virtual ~VpdProcess() = default;

  using KeyValuePairs = std::vector<std::pair<std::string, std::string>>;
  using CompletionCallback = base::OnceCallback<void(bool)>;

  // Update values in RW_VPD by running the update_rw_vpd utility in a separate
  // process. Keys with empty string values are deleted. update_rw_vpd will not
  // perform unnecessary writes if the already cache matches the update.
  //
  // Takes ownership of |completion| if process starts successfully. Returns
  // whether fork() was successful.
  virtual bool RunInBackground(const KeyValuePairs& updates,
                               CompletionCallback completion) = 0;
};

}  // namespace login_manager

#endif  // LOGIN_MANAGER_VPD_PROCESS_H_
