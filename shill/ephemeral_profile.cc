// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/ephemeral_profile.h"

#include <chromeos/dbus/service_constants.h>

#include "shill/adaptor_interfaces.h"
#include "shill/control_interface.h"
#include "shill/logging.h"
#include "shill/manager.h"

namespace shill {

namespace Logging {
static auto kModuleLogScope = ScopeLogger::kProfile;
static std::string ObjectID(EphemeralProfile* e) {
  return e->GetRpcIdentifier();
}
}

namespace {

constexpr char kFriendlyName[] = "(ephemeral)";

}  // namespace

EphemeralProfile::EphemeralProfile(ControlInterface* control_interface,
                                   Metrics* metrics,
                                   Manager* manager)
    : Profile(control_interface, metrics, manager, Identifier(),
              base::FilePath(), false) {
}

EphemeralProfile::~EphemeralProfile() = default;

std::string EphemeralProfile::GetFriendlyName() const {
  return kFriendlyName;
}

bool EphemeralProfile::AdoptService(const ServiceRefPtr& service) {
  SLOG(this, 2) << "Adding service " << service->unique_name()
                << " to ephemeral profile.";
  service->SetProfile(this);
  return true;
}

bool EphemeralProfile::AbandonService(const ServiceRefPtr& service) {
  if (service->profile() == this)
    service->SetProfile(nullptr);
  SLOG(this, 2) << "Removing service " << service->unique_name()
                << " from ephemeral profile.";
  return true;
}

bool EphemeralProfile::Save() {
  NOTREACHED();
  return false;
}

}  // namespace shill
