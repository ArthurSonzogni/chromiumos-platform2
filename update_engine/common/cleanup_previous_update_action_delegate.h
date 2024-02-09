// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UPDATE_ENGINE_COMMON_CLEANUP_PREVIOUS_UPDATE_ACTION_DELEGETE_H_
#define UPDATE_ENGINE_COMMON_CLEANUP_PREVIOUS_UPDATE_ACTION_DELEGETE_H_

namespace chromeos_update_engine {

// Delegate interface for CleanupPreviousUpdateAction.
class CleanupPreviousUpdateActionDelegateInterface {
 public:
  virtual ~CleanupPreviousUpdateActionDelegateInterface() {}
  // |progress| is within [0, 1]
  virtual void OnCleanupProgressUpdate(double progress) = 0;
};

}  // namespace chromeos_update_engine

#endif  // UPDATE_ENGINE_COMMON_CLEANUP_PREVIOUS_UPDATE_ACTION_DELEGETE_H_
