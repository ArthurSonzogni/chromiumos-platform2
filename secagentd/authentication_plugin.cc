// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "secagentd/plugins.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <utility>
#include <variant>

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
#include "secagentd/metrics_sender.h"
#include "secagentd/policies_features_broker.h"
#include "secagentd/proto/security_xdr_events.pb.h"
#include "user_data_auth/dbus-proxies.h"

namespace secagentd {

namespace pb = cros_xdr::reporting;

namespace {
bool UpdateNumFailedAttempts(const int64_t latest_success,
                             const AuthFactorType auth_factor_type,
                             pb::UserEventAtomicVariant* event) {
  // Check that timestamp is greater than the most recent
  // success and auth
  // factor matches. If so increment number of failed attempts
  // by 1.
  if ((event->has_common() &&
       event->common().create_timestamp_us() > latest_success) &&
      (event->has_failure() && event->failure().has_authentication() &&
       event->failure().authentication().auth_factor_size() >= 1 &&
       event->failure().authentication().auth_factor()[0] ==
           auth_factor_type)) {
    event->mutable_failure()->mutable_authentication()->set_num_failed_attempts(
        event->failure().authentication().num_failed_attempts() + 1);
    return true;
  }
  return false;
}

std::optional<std::pair<metrics::EnumMetric<metrics::AuthFactor>, int>>
GetEventEnumTypeAndAuthFactor(const pb::UserEventAtomicVariant& atomic_event) {
  int auth_factor = 0;
  if (atomic_event.has_logon()) {
    if (atomic_event.logon().has_authentication() &&
        atomic_event.logon().authentication().auth_factor_size() >= 1) {
      auth_factor = atomic_event.logon().authentication().auth_factor()[0];
    }
    return std::make_pair(metrics::kLogin, auth_factor);
  } else if (atomic_event.has_unlock()) {
    if (atomic_event.unlock().has_authentication() &&
        atomic_event.unlock().authentication().auth_factor_size() >= 1) {
      auth_factor = atomic_event.unlock().authentication().auth_factor()[0];
    }
    return std::make_pair(metrics::kUnlock, auth_factor);
  }

  return std::nullopt;
}
}  // namespace

AuthenticationPlugin::AuthenticationPlugin(
    scoped_refptr<MessageSenderInterface> message_sender,
    scoped_refptr<PoliciesFeaturesBrokerInterface> policies_features_broker,
    scoped_refptr<DeviceUserInterface> device_user,
    uint32_t batch_interval_s)
    : weak_ptr_factory_(this),
      policies_features_broker_(policies_features_broker),
      device_user_(device_user) {
  batch_sender_ = std::make_unique<BatchSender<std::monostate, pb::XdrUserEvent,
                                               pb::UserEventAtomicVariant>>(
      base::BindRepeating(
          [](const cros_xdr::reporting::UserEventAtomicVariant& event)
              -> std::monostate {
            // Only the most recent event type will need to be visited
            // so monostate is used. This makes it so for each type of auth
            // variation only 1 event is tracked.
            return std::monostate();
          }),
      message_sender, reporting::Destination::CROS_SECURITY_USER,
      batch_interval_s);
  CHECK(message_sender != nullptr);
}

std::string AuthenticationPlugin::GetName() const {
  return "Authentication";
}

absl::Status AuthenticationPlugin::Activate() {
  if (is_active_) {
    return absl::OkStatus();
  }

  batch_sender_->Start();

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

void AuthenticationPlugin::HandleScreenLock() {
  auto screen_lock = std::make_unique<pb::UserEventAtomicVariant>();
  screen_lock->mutable_lock();
  screen_lock->mutable_common()->set_create_timestamp_us(
      base::Time::Now().InMillisecondsSinceUnixEpoch() *
      base::Time::kMicrosecondsPerMillisecond);
  device_user_->GetDeviceUserAsync(
      base::BindOnce(&AuthenticationPlugin::OnDeviceUserRetrieved,
                     weak_ptr_factory_.GetWeakPtr(), std::move(screen_lock)));
}

void AuthenticationPlugin::HandleScreenUnlock() {
  auto screen_unlock = std::make_unique<pb::UserEventAtomicVariant>();
  screen_unlock->mutable_common()->set_create_timestamp_us(
      base::Time::Now().InMillisecondsSinceUnixEpoch() *
      base::Time::kMicrosecondsPerMillisecond);
  latest_successful_login_timestamp_ =
      screen_unlock->common().create_timestamp_us();

  auto* authentication =
      screen_unlock->mutable_unlock()->mutable_authentication();
  if (!FillAuthFactor(authentication)) {
    authentication->clear_auth_factor();
    base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE,
        base::BindOnce(&AuthenticationPlugin::DelayedCheckForAuthSignal,
                       weak_ptr_factory_.GetWeakPtr(), std::move(screen_unlock),
                       authentication),
        kWaitForAuthFactorS);
    return;
  }

  auth_factor_type_ =
      AuthFactorType::Authentication_AuthenticationType_AUTH_TYPE_UNKNOWN;
  device_user_->GetDeviceUserAsync(
      base::BindOnce(&AuthenticationPlugin::OnDeviceUserRetrieved,
                     weak_ptr_factory_.GetWeakPtr(), std::move(screen_unlock)));
}

void AuthenticationPlugin::HandleSessionStateChange(const std::string& state) {
  auto log_event = std::make_unique<pb::UserEventAtomicVariant>();
  log_event->mutable_common()->set_create_timestamp_us(
      base::Time::Now().InMillisecondsSinceUnixEpoch() *
      base::Time::kMicrosecondsPerMillisecond);
  if (state == kStarted) {
    latest_successful_login_timestamp_ =
        log_event->common().create_timestamp_us();
    auto* authentication = log_event->mutable_logon()->mutable_authentication();
    if (!FillAuthFactor(authentication)) {
      authentication->clear_auth_factor();
      base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
          FROM_HERE,
          base::BindOnce(&AuthenticationPlugin::DelayedCheckForAuthSignal,
                         weak_ptr_factory_.GetWeakPtr(), std::move(log_event),
                         authentication),
          kWaitForAuthFactorS);
      return;
    }
    auth_factor_type_ =
        AuthFactorType::Authentication_AuthenticationType_AUTH_TYPE_UNKNOWN;
  } else if (state == kStopped) {
    log_event->mutable_logoff();
  } else if (state == kInit) {
    device_user_->GetDeviceUserAsync(
        base::BindOnce(&AuthenticationPlugin::OnFirstSessionStart,
                       weak_ptr_factory_.GetWeakPtr()));
    return;
  } else {
    return;
  }

  device_user_->GetDeviceUserAsync(
      base::BindOnce(&AuthenticationPlugin::OnDeviceUserRetrieved,
                     weak_ptr_factory_.GetWeakPtr(), std::move(log_event)));
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
  if (completed.user_creation()) {
    auth_factor_type_ =
        AuthFactorType::Authentication_AuthenticationType_AUTH_NEW_USER;
    return;
  }

  auto it = auth_factor_map_.find(completed.auth_factor_type());
  if (it == auth_factor_map_.end()) {
    LOG(ERROR) << "Unknown auth factor type " << completed.auth_factor_type();
    auth_factor_type_ =
        AuthFactorType::Authentication_AuthenticationType_AUTH_TYPE_UNKNOWN;
  } else {
    if (completed.has_error_info()) {
      // When a pin is incorrectly entered two Auth signals are sent on the
      // lockscreen. One trying the pin and one trying the password. In this
      // case ignore the password and keep the auth_factor as pin.
      // TODO(b:305093271): Update logic to handle if password is actually used.
      if (it->second ==
          AuthFactorType::Authentication_AuthenticationType_AUTH_PIN) {
        latest_pin_failure_ = base::Time::Now().InMillisecondsSinceUnixEpoch() /
                              base::Time::kMillisecondsPerSecond;
        last_auth_was_password_ = false;
      } else if (it->second ==
                 AuthFactorType::
                     Authentication_AuthenticationType_AUTH_PASSWORD) {
        if (auth_factor_type_ ==
                AuthFactorType::Authentication_AuthenticationType_AUTH_PIN &&
            !last_auth_was_password_ &&
            (base::Time::Now().InMillisecondsSinceUnixEpoch() /
             base::Time::kMillisecondsPerSecond) -
                    latest_pin_failure_ <=
                kMaxDelayForLockscreenAttemptsS) {
          last_auth_was_password_ = true;
          return;
        }
        last_auth_was_password_ = true;
      } else {
        last_auth_was_password_ = false;
      }
    }
    auth_factor_type_ = it->second;
  }

  if (completed.has_error_info()) {
    // Record auth factor for failure event.
    MetricsSender::GetInstance().SendEnumMetricToUMA(
        metrics::kFailure, static_cast<metrics::AuthFactor>(auth_factor_type_));
    if (!batch_sender_->Visit(pb::UserEventAtomicVariant::kFailure,
                              std::monostate(),
                              base::BindOnce(&UpdateNumFailedAttempts,
                                             latest_successful_login_timestamp_,
                                             auth_factor_type_))) {
      // Create new event if no matching failure event found and updated.
      auto failure_event = std::make_unique<pb::UserEventAtomicVariant>();
      failure_event->mutable_common()->set_create_timestamp_us(
          base::Time::Now().InMillisecondsSinceUnixEpoch() *
          base::Time::kMicrosecondsPerMillisecond);

      auto* authentication =
          failure_event->mutable_failure()->mutable_authentication();
      authentication->set_num_failed_attempts(1);
      FillAuthFactor(authentication);

      device_user_->GetDeviceUserAsync(base::BindOnce(
          &AuthenticationPlugin::OnDeviceUserRetrieved,
          weak_ptr_factory_.GetWeakPtr(), std::move(failure_event)));
    }
  }
}

bool AuthenticationPlugin::FillAuthFactor(pb::Authentication* proto) {
  proto->add_auth_factor(auth_factor_type_);

  return auth_factor_type_ !=
         AuthFactorType::Authentication_AuthenticationType_AUTH_TYPE_UNKNOWN;
}

void AuthenticationPlugin::DelayedCheckForAuthSignal(
    std::unique_ptr<cros_xdr::reporting::UserEventAtomicVariant> xdr_proto,
    cros_xdr::reporting::Authentication* authentication) {
  if (FillAuthFactor(authentication)) {
    // Clear auth factor after it has been set.
    auth_factor_type_ =
        AuthFactorType::Authentication_AuthenticationType_AUTH_TYPE_UNKNOWN;
  }
  device_user_->GetDeviceUserAsync(
      base::BindOnce(&AuthenticationPlugin::OnDeviceUserRetrieved,
                     weak_ptr_factory_.GetWeakPtr(), std::move(xdr_proto)));
}

void AuthenticationPlugin::OnDeviceUserRetrieved(
    std::unique_ptr<pb::UserEventAtomicVariant> atomic_event,
    const std::string& device_user) {
  atomic_event->mutable_common()->set_device_user(device_user);

  // Send metric for which auth factor is used.
  auto pair = GetEventEnumTypeAndAuthFactor(*atomic_event.get());
  if (pair.has_value()) {
    MetricsSender::GetInstance().SendEnumMetricToUMA(
        pair.value().first,
        static_cast<metrics::AuthFactor>(pair.value().second));
  }
  batch_sender_->Enqueue(std::move(atomic_event));
}

void AuthenticationPlugin::OnFirstSessionStart(const std::string& device_user) {
  // When the device_user is empty no user is signed in so do not send a login
  // event.
  // When the device_user is filled there is already a user signed in so a login
  // will be simulated.
  if (!device_user.empty()) {
    HandleSessionStateChange(kStarted);
  }
}

}  // namespace secagentd
