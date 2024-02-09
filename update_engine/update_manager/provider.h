// Copyright 2014 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UPDATE_ENGINE_UPDATE_MANAGER_PROVIDER_H_
#define UPDATE_ENGINE_UPDATE_MANAGER_PROVIDER_H_

namespace chromeos_update_manager {

// Abstract base class for a policy provider.
class Provider {
 public:
  Provider(const Provider&) = delete;
  Provider& operator=(const Provider&) = delete;
  virtual ~Provider() {}

 protected:
  Provider() {}
};

}  // namespace chromeos_update_manager

#endif  // UPDATE_ENGINE_UPDATE_MANAGER_PROVIDER_H_
