// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdint>
#include <iterator>
#include <memory>
#include <optional>
#include <utility>

#include "absl/status/status.h"
#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "missive/proto/record_constants.pb.h"
#include "secagentd/device_user.h"
#include "secagentd/message_sender.h"
#include "secagentd/plugins.h"
#include "secagentd/policies_features_broker.h"
#include "secagentd/proto/security_xdr_events.pb.h"

namespace secagentd {

namespace pb = cros_xdr::reporting;

AuthenticationPlugin::AuthenticationPlugin(
    scoped_refptr<MessageSenderInterface> message_sender,
    scoped_refptr<PoliciesFeaturesBrokerInterface> policies_features_broker,
    scoped_refptr<DeviceUserInterface> device_user,
    uint32_t batch_interval_s)
    : weak_ptr_factory_(this),
      message_sender_(message_sender),
      policies_features_broker_(policies_features_broker),
      device_user_(device_user) {
  CHECK(message_sender != nullptr);
}

std::string AuthenticationPlugin::GetName() const {
  return "Authentication";
}

absl::Status AuthenticationPlugin::Activate() {
  if (is_active_) {
    return absl::OkStatus();
  }

  // Register for lock/unlock signals.
  device_user_->RegisterScreenLockedHandler(
      base::BindRepeating(&AuthenticationPlugin::HandleScreenLock,
                          weak_ptr_factory_.GetWeakPtr()),
      base::BindOnce(&AuthenticationPlugin::HandleRegistrationResult,
                     weak_ptr_factory_.GetWeakPtr()));
  device_user_->RegisterScreenUnlockedHandler(
      base::BindRepeating(&AuthenticationPlugin::HandleScreenUnlock,
                          weak_ptr_factory_.GetWeakPtr()),
      base::BindOnce(&AuthenticationPlugin::HandleRegistrationResult,
                     weak_ptr_factory_.GetWeakPtr()));

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

void AuthenticationPlugin::FillCommon(
    pb::AuthenticateEventAtomicVariant* proto) {
  proto->mutable_common()->set_create_timestamp_us(
      base::Time::Now().ToJavaTime() * base::Time::kMicrosecondsPerMillisecond);
  proto->mutable_common()->set_device_user(device_user_->GetDeviceUser());
}

void AuthenticationPlugin::HandleScreenLock() {
  auto xdr_proto = std::make_unique<pb::XdrAuthenticateEvent>();
  auto screen_lock = xdr_proto->add_batched_events();
  screen_lock->mutable_lock();
  FillCommon(screen_lock);

  message_sender_->SendMessage(reporting::CROS_SECURITY_USER,
                               xdr_proto->mutable_common(),
                               std::move(xdr_proto), std::nullopt);
}

void AuthenticationPlugin::HandleScreenUnlock() {
  auto xdr_proto = std::make_unique<pb::XdrAuthenticateEvent>();
  auto screen_lock = xdr_proto->add_batched_events();
  // TODO(b/289005503): Source auth type.
  screen_lock->mutable_unlock()->mutable_authentication()->add_auth_factor(
      pb::Authentication_AuthenticationType_AUTH_TYPE_UNKNOWN);
  FillCommon(screen_lock);

  message_sender_->SendMessage(reporting::CROS_SECURITY_USER,
                               xdr_proto->mutable_common(),
                               std::move(xdr_proto), std::nullopt);
}

void AuthenticationPlugin::HandleRegistrationResult(
    const std::string& interface, const std::string& signal, bool success) {
  if (!success) {
    LOG(ERROR) << "Callback registration failed for dbus signal: " << signal
               << " on interface: " << interface;
  }
}

}  // namespace secagentd
