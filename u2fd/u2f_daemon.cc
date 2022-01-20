// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "u2fd/u2f_daemon.h"

#include <sysexits.h>

#include <functional>
#include <string>
#include <utility>
#include <vector>

#include <base/bind.h>
#include <base/logging.h>
#include <base/optional.h>
#include <base/synchronization/waitable_event.h>
#include <dbus/u2f/dbus-constants.h>
#include <policy/device_policy.h>
#include <policy/libpolicy.h>

#include "u2fd/u2f_command_processor.h"
#if USE_GSC
#include "u2fd/u2f_command_processor_gsc.h"
#include "u2fd/u2fhid_service_impl.h"
#elif USE_TPM1
#include "u2fd/u2f_command_processor_generic.h"
#endif

namespace u2f {

namespace {

constexpr int kWinkSignalMinIntervalMs = 1000;
constexpr base::TimeDelta kRequestPresenceDelay = base::Milliseconds(500);

// The U2F counter stored in cr50 is stored in a format resistant to
// rollbacks, and that guarantees monotonicity even in the presence of partial
// writes. See //platform/ec/include/nvcounter.h
//
// The counter is stored across 2 pages of flash - a high page and a low page,
// with each page containing 512 4-byte words. The counter increments using
// 'strikes', with each strike occupying 4 bits. The high page can represent
// numbers 0-2048, and the low page can represent numbers 0-4096.
// The pages are interpreted as two digits of a base-4097 number, giving us
// the maximum value below.
// See //platform/ec/common/nvcounter.c for more details.
constexpr uint32_t kMaxCr50U2fCounterValue = (2048 * 4097) + 4096;
// If we are supporting legacy key handles, we initialize the counter such
// that it is always larger than the maximum possible value cr50 could have
// returned, and therefore guarantee that we provide a monotonically
// increasing counter value for migrated key handles.
constexpr uint32_t kLegacyKhCounterMin = kMaxCr50U2fCounterValue + 1;

bool U2fPolicyReady() {
  policy::PolicyProvider policy_provider;

  return policy_provider.Reload();
}

U2fMode ReadU2fPolicy() {
  policy::PolicyProvider policy_provider;

  if (!policy_provider.Reload()) {
    LOG(DFATAL) << "Failed to load device policy";
  }

  int mode = 0;
  const policy::DevicePolicy* policy = &policy_provider.GetDevicePolicy();
  if (!policy->GetSecondFactorAuthenticationMode(&mode))
    return U2fMode::kUnset;

  return static_cast<U2fMode>(mode);
}

const char* U2fModeToString(U2fMode mode) {
  switch (mode) {
    case U2fMode::kUnset:
      return "unset";
    case U2fMode::kDisabled:
      return "disabled";
    case U2fMode::kU2f:
      return "U2F";
    case U2fMode::kU2fExtended:
      return "U2F+extensions";
  }
  return "unknown";
}

U2fMode GetU2fMode(bool force_u2f, bool force_g2f) {
  U2fMode policy_mode = ReadU2fPolicy();

  LOG(INFO) << "Requested Mode: Policy[" << U2fModeToString(policy_mode)
            << "], force_u2f[" << force_u2f << "], force_g2f[" << force_g2f
            << "]";

  // Always honor the administrator request to disable even if given
  // contradictory override flags.
  if (policy_mode == U2fMode::kDisabled) {
    LOG(INFO) << "Mode: Disabled (explicitly by policy)";
    return U2fMode::kDisabled;
  }

  if (force_g2f || policy_mode == U2fMode::kU2fExtended) {
    LOG(INFO) << "Mode: U2F+extensions";
    return U2fMode::kU2fExtended;
  }

  if (force_u2f || policy_mode == U2fMode::kU2f) {
    LOG(INFO) << "Mode:U2F";
    return U2fMode::kU2f;
  }

  LOG(INFO) << "Mode: Disabled";
  return U2fMode::kDisabled;
}

void OnPolicySignalConnected(const std::string& interface,
                             const std::string& signal,
                             bool success) {
  if (!success) {
    LOG(FATAL) << "Could not connect to signal " << signal << " on interface "
               << interface;
  }
}

}  // namespace

U2fDaemon::U2fDaemon(bool force_u2f,
                     bool force_g2f,
                     bool g2f_allowlist_data,
                     bool legacy_kh_fallback,
                     uint32_t vendor_id,
                     uint32_t product_id)
    : brillo::DBusServiceDaemon(kU2FServiceName),
      force_u2f_(force_u2f),
      force_g2f_(force_g2f),
      g2f_allowlist_data_(g2f_allowlist_data),
      legacy_kh_fallback_(legacy_kh_fallback),
      service_started_(false) {
#if USE_GSC
  u2fhid_service_ = std::make_unique<U2fHidServiceImpl>(legacy_kh_fallback,
                                                        vendor_id, product_id);
#endif  // USE_GSC
}

int U2fDaemon::OnInit() {
  int rc = brillo::DBusServiceDaemon::OnInit();
  if (rc != EX_OK)
    return rc;

  if (!InitializeDBusProxies()) {
    return EX_IOERR;
  }

  user_state_ = std::make_unique<UserState>(
      sm_proxy_.get(), legacy_kh_fallback_ ? kLegacyKhCounterMin : 0);

  sm_proxy_->RegisterPropertyChangeCompleteSignalHandler(
      base::Bind(&U2fDaemon::TryStartService, base::Unretained(this)),
      base::Bind(&OnPolicySignalConnected));

  bool policy_ready = U2fPolicyReady();

  if (policy_ready) {
    int status = StartService();

    // If U2F is not currently enabled, we'll wait for policy updates
    // that may enable it. We don't ever disable U2F on policy updates.
    // TODO(louiscollard): Fix the above.
    if (status != EX_CONFIG)
      return status;
  }

  if (policy_ready) {
    LOG(INFO) << "U2F currently disabled, waiting for policy updates...";
  } else {
    LOG(INFO) << "Policy not available, waiting...";
  }

  return EX_OK;
}

void U2fDaemon::TryStartService(
    const std::string& /* unused dbus signal status */) {
  if (service_started_)
    return;

  if (!U2fPolicyReady())
    return;

  int status = StartService();

  if (status != EX_OK && status != EX_CONFIG) {
    // Something went wrong.
    exit(status);
  }
}

int U2fDaemon::StartService() {
  // Start U2fHid service before WebAuthn because WebAuthn initialization can
  // be slow.
  int status = StartU2fHidService();

  U2fMode u2f_mode = GetU2fMode(force_u2f_, force_g2f_);
  LOG(INFO) << "Initializing WebAuthn handler.";
  InitializeWebAuthnHandler(u2f_mode);

  return status;
}

int U2fDaemon::StartU2fHidService() {
  if (!u2fhid_service_) {
    // No need to start u2f HID service on this device.
    return EX_OK;
  }

  if (service_started_) {
    // Any failures in previous calls to this function would have caused the
    // program to terminate, so we can assume we have successfully started.
    return EX_OK;
  }

  U2fMode u2f_mode = GetU2fMode(force_u2f_, force_g2f_);
  if (u2f_mode == U2fMode::kDisabled) {
    return EX_CONFIG;
  }

  LOG(INFO) << "Starting U2fHid service.";

  // If g2f is enabled by policy, we always include allowlisting data.
  bool include_g2f_allowlist_data =
      g2f_allowlist_data_ || (ReadU2fPolicy() == U2fMode::kU2fExtended);

  std::function<void()> request_presence = [this]() {
    IgnorePowerButtonPress();
    SendWinkSignal();
  };

  service_started_ = true;

  return u2fhid_service_->CreateU2fHid(
             u2f_mode == U2fMode::kU2fExtended /* Allow G2F Attestation */,
             include_g2f_allowlist_data, request_presence, user_state_.get(),
             &metrics_library_)
             ? EX_OK
             : EX_PROTOCOL;
}

bool U2fDaemon::InitializeDBusProxies() {
  if (u2fhid_service_) {
    u2fhid_service_->InitializeDBusProxies(bus_.get());
  }

  pm_proxy_ = std::make_unique<org::chromium::PowerManagerProxy>(bus_.get());
  sm_proxy_ =
      std::make_unique<org::chromium::SessionManagerInterfaceProxy>(bus_.get());

  return true;
}

void U2fDaemon::RegisterDBusObjectsAsync(
    brillo::dbus_utils::AsyncEventSequencer* sequencer) {
  dbus_object_.reset(new brillo::dbus_utils::DBusObject(
      nullptr, bus_, dbus::ObjectPath(kU2FServicePath)));

  auto u2f_interface = dbus_object_->AddOrGetInterface(kU2FInterface);

  wink_signal_ = u2f_interface->RegisterSignal<UserNotification>(
      kU2FUserNotificationSignal);

  // Handlers for the WebAuthn DBus API.
  u2f_interface->AddMethodHandler(kU2FMakeCredential,
                                  base::Unretained(&webauthn_handler_),
                                  &WebAuthnHandler::MakeCredential);

  u2f_interface->AddMethodHandler(kU2FGetAssertion,
                                  base::Unretained(&webauthn_handler_),
                                  &WebAuthnHandler::GetAssertion);

  u2f_interface->AddSimpleMethodHandler(kU2FHasCredentials,
                                        base::Unretained(&webauthn_handler_),
                                        &WebAuthnHandler::HasCredentials);

  u2f_interface->AddSimpleMethodHandler(kU2FHasLegacyCredentials,
                                        base::Unretained(&webauthn_handler_),
                                        &WebAuthnHandler::HasLegacyCredentials);

  u2f_interface->AddSimpleMethodHandler(kU2FCancelWebAuthnFlow,
                                        base::Unretained(&webauthn_handler_),
                                        &WebAuthnHandler::Cancel);

  u2f_interface->AddMethodHandler(kU2FIsUvpaa,
                                  base::Unretained(&webauthn_handler_),
                                  &WebAuthnHandler::IsUvpaa);

  u2f_interface->AddSimpleMethodHandler(kU2FIsU2fEnabled,
                                        base::Unretained(&webauthn_handler_),
                                        &WebAuthnHandler::IsU2fEnabled);

  u2f_interface->AddSimpleMethodHandler(
      kU2FCountCredentialsInTimeRange, base::Unretained(&webauthn_handler_),
      &WebAuthnHandler::CountCredentialsInTimeRange);

  u2f_interface->AddSimpleMethodHandler(
      kU2FDeleteCredentialsInTimeRange, base::Unretained(&webauthn_handler_),
      &WebAuthnHandler::DeleteCredentialsInTimeRange);

  dbus_object_->RegisterAsync(
      sequencer->GetHandler("Failed to register DBus Interface.", true));
}

void U2fDaemon::InitializeWebAuthnHandler(U2fMode u2f_mode) {
  std::function<void()> request_presence = [this]() {
    IgnorePowerButtonPress();
    SendWinkSignal();
    base::PlatformThread::Sleep(kRequestPresenceDelay);
  };

  std::unique_ptr<AllowlistingUtil> allowlisting_util;
  std::unique_ptr<U2fCommandProcessor> u2f_command_processor;

  // If g2f is enabled by policy, we always include allowlisting data.
  if (u2fhid_service_ &&
      (g2f_allowlist_data_ || (ReadU2fPolicy() == U2fMode::kU2fExtended))) {
    allowlisting_util =
        std::make_unique<AllowlistingUtil>([this](int cert_size) {
          return u2fhid_service_->GetCertifiedG2fCert(cert_size);
        });
  }

#if USE_GSC
  u2f_command_processor = std::make_unique<U2fCommandProcessorGsc>(
      u2fhid_service_->tpm_proxy(), request_presence);
#elif USE_TPM1
  u2f_command_processor = std::make_unique<U2fCommandProcessorGeneric>(
      user_state_.get(), bus_.get());
#endif

  webauthn_handler_.Initialize(bus_.get(), user_state_.get(), u2f_mode,
                               std::move(u2f_command_processor),
                               std::move(allowlisting_util), &metrics_library_);
}

void U2fDaemon::SendWinkSignal() {
  static base::TimeTicks last_sent;
  base::TimeDelta elapsed = base::TimeTicks::Now() - last_sent;

  if (elapsed.InMilliseconds() > kWinkSignalMinIntervalMs) {
    UserNotification notification;
    notification.set_event_type(UserNotification::TOUCH_NEEDED);

    wink_signal_.lock()->Send(notification);

    last_sent = base::TimeTicks::Now();
  }
}

void U2fDaemon::IgnorePowerButtonPress() {
  // Duration of the user presence persistence on the firmware side.
  const base::TimeDelta kPresenceTimeout = base::Seconds(10);

  brillo::ErrorPtr err;
  // Mask the next power button press for the UI
  pm_proxy_->IgnoreNextPowerButtonPress(kPresenceTimeout.ToInternalValue(),
                                        &err, -1);
}

}  // namespace u2f
