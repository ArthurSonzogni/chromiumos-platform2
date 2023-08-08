// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdint>
#include <memory>
#include <optional>
#include <utility>

#include "absl/status/status.h"
#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/time/time.h"
#include "cryptohome/proto_bindings/UserDataAuth.pb.h"
#include "dbus/bus.h"
#include "missive/proto/record_constants.pb.h"
#include "secagentd/common.h"
#include "secagentd/device_user.h"
#include "secagentd/message_sender.h"
#include "secagentd/plugins.h"
#include "secagentd/policies_features_broker.h"
#include "secagentd/proto/security_xdr_events.pb.h"
#include "user_data_auth/dbus-proxies.h"

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

  // Register cryptohome proxy for authentication result.
  if (!cryptohome_proxy_) {
    cryptohome_proxy_ =
        std::make_unique<org::chromium::UserDataAuthInterfaceProxy>(
            common::GetDBus());
  }
  cryptohome_proxy_->GetObjectProxy()->WaitForServiceToBeAvailable(
      base::BindOnce(
          [](org::chromium::UserDataAuthInterfaceProxyInterface*
                 cryptohome_proxy,
             base::RepeatingCallback<void(
                 const user_data_auth::AuthenticateAuthFactorCompleted&)>
                 signal_callback,
             dbus::ObjectProxy::OnConnectedCallback on_connected_callback,
             bool available) {
            if (!available) {
              LOG(ERROR) << "Failed to register for "
                            "AuthenticateAuthFactorCompleted signal";
              return;
            }
            cryptohome_proxy
                ->RegisterAuthenticateAuthFactorCompletedSignalHandler(
                    std::move(signal_callback),
                    std::move(on_connected_callback));
          },
          cryptohome_proxy_.get(),
          base::BindRepeating(
              &AuthenticationPlugin::HandleAuthenticateAuthFactorCompleted,
              weak_ptr_factory_.GetWeakPtr()),
          base::BindOnce(&AuthenticationPlugin::HandleRegistrationResult,
                         weak_ptr_factory_.GetWeakPtr())));

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

  // Register for login/out signal.
  device_user_->RegisterSessionChangeListener(
      base::BindRepeating(&AuthenticationPlugin::HandleSessionStateChange,
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

  SendAuthenticationEvent(std::move(xdr_proto));
}

void AuthenticationPlugin::HandleScreenUnlock() {
  auto xdr_proto = std::make_unique<pb::XdrAuthenticateEvent>();
  auto screen_lock = xdr_proto->add_batched_events();
  FillCommon(screen_lock);

  auto* authentication =
      screen_lock->mutable_unlock()->mutable_authentication();
  if (!FillAuthFactor(authentication)) {
    base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE,
        base::BindOnce(&AuthenticationPlugin::DelayedCheckForAuthSignal,
                       weak_ptr_factory_.GetWeakPtr(), std::move(xdr_proto),
                       authentication),
        kWaitForAuthFactorS);
    return;
  }

  SendAuthenticationEvent(std::move(xdr_proto));
}

void AuthenticationPlugin::HandleSessionStateChange(const std::string& state) {
  auto xdr_proto = std::make_unique<pb::XdrAuthenticateEvent>();
  auto log_event = xdr_proto->add_batched_events();
  FillCommon(log_event);

  if (state == kStarted) {
    auto* authentication = log_event->mutable_logon()->mutable_authentication();
    if (!FillAuthFactor(authentication)) {
      base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
          FROM_HERE,
          base::BindOnce(&AuthenticationPlugin::DelayedCheckForAuthSignal,
                         weak_ptr_factory_.GetWeakPtr(), std::move(xdr_proto),
                         authentication),
          kWaitForAuthFactorS);
      return;
    }
  } else if (state == kStopped) {
    log_event->mutable_logoff();
  } else {
    return;
  }

  SendAuthenticationEvent(std::move(xdr_proto));
}

void AuthenticationPlugin::HandleRegistrationResult(
    const std::string& interface, const std::string& signal, bool success) {
  if (!success) {
    LOG(ERROR) << "Callback registration failed for dbus signal: " << signal
               << " on interface: " << interface;
  }
}

void AuthenticationPlugin::HandleAuthenticateAuthFactorCompleted(
    const user_data_auth::AuthenticateAuthFactorCompleted& completed) {
  if (completed.has_success()) {
    auto it = auth_factor_map_.find(completed.success().auth_factor_type());
    if (it == auth_factor_map_.end()) {
      LOG(ERROR) << "Unknown auth factor type "
                 << completed.success().auth_factor_type();
      auth_factor_type_ =
          AuthFactorType::Authentication_AuthenticationType_AUTH_TYPE_UNKNOWN;
    } else {
      auth_factor_type_ = it->second;
    }
  }
}

bool AuthenticationPlugin::FillAuthFactor(pb::Authentication* proto) {
  if (auth_factor_type_ !=
      AuthFactorType::Authentication_AuthenticationType_AUTH_TYPE_UNKNOWN) {
    proto->add_auth_factor(auth_factor_type_);
    auth_factor_type_ =
        AuthFactorType::Authentication_AuthenticationType_AUTH_TYPE_UNKNOWN;
    return true;
  }

  return false;
}

void AuthenticationPlugin::SendAuthenticationEvent(
    std::unique_ptr<pb::XdrAuthenticateEvent> proto) {
  message_sender_->SendMessage(reporting::CROS_SECURITY_USER,
                               proto->mutable_common(), std::move(proto),
                               std::nullopt);
}

void AuthenticationPlugin::DelayedCheckForAuthSignal(
    std::unique_ptr<cros_xdr::reporting::XdrAuthenticateEvent> xdr_proto,
    cros_xdr::reporting::Authentication* authentication) {
  if (!FillAuthFactor(authentication)) {
    authentication->add_auth_factor(
        AuthFactorType::Authentication_AuthenticationType_AUTH_TYPE_UNKNOWN);
  }
  SendAuthenticationEvent(std::move(xdr_proto));
}

}  // namespace secagentd
