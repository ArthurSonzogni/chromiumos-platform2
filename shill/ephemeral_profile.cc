// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/ephemeral_profile.h"

#include <chromeos/dbus/service_constants.h>

#include "shill/adaptor_interfaces.h"
#include "shill/control_interface.h"
#include "shill/logging.h"
#include "shill/manager.h"

namespace shill {

EphemeralProfile::EphemeralProfile(ControlInterface *control_interface,
                                   Manager *manager)
    : Profile(control_interface, manager, Identifier(), "", false) {
}

EphemeralProfile::~EphemeralProfile() {}

bool EphemeralProfile::AdoptService(const ServiceRefPtr &service) {
  SLOG(Profile, 2) << "Adding service " << service->unique_name()
                   << " to ephemeral profile.";
  service->SetProfile(this);
  return true;
}

bool EphemeralProfile::AbandonService(const ServiceRefPtr &service) {
  if (service->profile() == this)
    service->SetProfile(NULL);
  SLOG(Profile, 2) << "Removing service " << service->unique_name()
                   << " from ephemeral profile.";
  return true;
}

bool EphemeralProfile::Save() {
  NOTREACHED();
  return false;
}

}  // namespace shill
