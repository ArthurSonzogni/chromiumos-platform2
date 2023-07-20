// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdint>
#include <iterator>
#include <memory>
#include <utility>

#include "absl/status/status.h"
#include "base/logging.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "secagentd/device_user.h"
#include "secagentd/message_sender.h"
#include "secagentd/plugins.h"
#include "secagentd/policies_features_broker.h"

namespace secagentd {

namespace pb = cros_xdr::reporting;

AuthenticationPlugin::AuthenticationPlugin(
    scoped_refptr<MessageSenderInterface> message_sender,
    scoped_refptr<PoliciesFeaturesBrokerInterface> policies_features_broker,
    scoped_refptr<DeviceUserInterface> device_user,
    uint32_t batch_interval_s)
    : weak_ptr_factory_(this),
      policies_features_broker_(policies_features_broker),
      device_user_(device_user) {
  CHECK(message_sender != nullptr);
}

std::string AuthenticationPlugin::GetName() const {
  return "Authentication";
}

absl::Status AuthenticationPlugin::Activate() {
  is_active_ = true;
  return absl::OkStatus();
}

absl::Status AuthenticationPlugin::Deactivate() {
  return absl::UnimplementedError(
      "Deactivate not implemented for AuthenticationPlugin.");
}

bool AuthenticationPlugin::IsActive() const {
  return is_active_;
}

}  // namespace secagentd
