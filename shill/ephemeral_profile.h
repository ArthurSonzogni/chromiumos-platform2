// Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_EPHEMERAL_PROFILE_
#define SHILL_EPHEMERAL_PROFILE_

#include <string>
#include <vector>

#include <base/memory/scoped_ptr.h>

#include "shill/event_dispatcher.h"
#include "shill/profile.h"
#include "shill/property_store.h"
#include "shill/refptr_types.h"

namespace shill {

class ControlInterface;
class Manager;
class StoreInterface;

// An in-memory profile that is not persisted to disk, but allows the
// promotion of entries contained herein to the currently active profile.
class EphemeralProfile : public Profile {
 public:
  EphemeralProfile(ControlInterface *control_interface, Manager *manager);
  virtual ~EphemeralProfile();

  virtual bool AdoptService(const ServiceRefPtr &service);
  virtual bool AbandonService(const ServiceRefPtr &service);

  // Should not be called.
  virtual bool Save();

  // Leaves |path| untouched and returns false.
  virtual bool GetStoragePath(base::FilePath */*path*/) { return false; }

 private:
  DISALLOW_COPY_AND_ASSIGN(EphemeralProfile);
};

}  // namespace shill

#endif  // SHILL_EPHEMERAL_PROFILE_
