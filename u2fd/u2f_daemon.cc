// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "u2fd/u2f_daemon.h"

#include <sysexits.h>

#include <functional>
#include <string>
#include <utility>
#include <vector>

#include <base/functional/bind.h>
#include <base/logging.h>
#include <base/synchronization/waitable_event.h>
#include <dbus/u2f/dbus-constants.h>
#include <libhwsec/factory/factory_impl.h>
#include <libhwsec/structures/u2f.h>
#include <policy/device_policy.h>
#include <policy/libpolicy.h>
#include <user_data_auth-client/user_data_auth/dbus-proxies.h>

#include "u2fd/u2f_command_processor.h"
#include "u2fd/u2f_command_processor_generic.h"
#include "u2fd/u2f_command_processor_vendor.h"
#include "u2fd/u2fhid_service_impl.h"

namespace u2f {

namespace {

using hwsec::u2f::FipsInfo;
using hwsec::u2f::FipsStatus;

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

constexpr char kU2fFipsStatusMetric[] = "Platform.U2F.FipsStatus";

bool U2fPolicyReady() {
  policy::PolicyProvider policy_provider;

  return policy_provider.Reload();
}

U2fMode ReadU2fPolicy() {
  policy::PolicyProvider policy_provider;

  if (!policy_provider.Reload()) {
    LOG(DFATAL) << "Failed to load device policy";
  }

  const policy::DevicePolicy* policy = &policy_provider.GetDevicePolicy();
  std::optional<int> mode = policy->GetSecondFactorAuthenticationMode();
  if (!mode.has_value())
    return U2fMode::kUnset;

  return static_cast<U2fMode>(*mode);
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

void OnPolicySignalConnected(const std::string& interface,
                             const std::string& signal,
                             bool success) {
  if (!success) {
    LOG(FATAL) << "Could not connect to signal " << signal << " on interface "
               << interface;
  }
}

// These values are persisted to logs. Entries should not be renumbered and
// numeric values should never be reused.
enum class U2fFipsStatus {
  // Failed to determine FIPS status on this device.
  kError = 0,
  kNotActive = 1,
  kActive = 2,
  kMaxValue = kActive,
};

void SendU2fFipsStatusMetrics(MetricsLibraryInterface& metrics,
                              U2fMode u2f_mode,
                              U2fFipsStatus status) {
  std::string aggregated_metrics_name = kU2fFipsStatusMetric;
  std::string suffix_name = ".";
  switch (u2f_mode) {
    case U2fMode::kUnset:
      suffix_name += "Unset";
      break;
    case U2fMode::kDisabled:
      suffix_name += "Disabled";
      break;
    case U2fMode::kU2f:
      suffix_name += "U2f";
      break;
    case U2fMode::kU2fExtended:
      suffix_name += "U2fExtended";
      break;
  }
  metrics.SendEnumToUMA(aggregated_metrics_name, status);
  metrics.SendEnumToUMA(aggregated_metrics_name + suffix_name, status);
}

}  // namespace

U2fDaemon::U2fDaemon(bool force_u2f,
                     bool force_g2f,
                     bool enable_corp_protocol,
                     bool g2f_allowlist_data,
                     bool force_activate_fips,
                     bool legacy_kh_fallback)
    : brillo::DBusServiceDaemon(kU2FServiceName),
      force_u2f_(force_u2f),
      force_g2f_(force_g2f),
      enable_corp_protocol_(enable_corp_protocol),
      g2f_allowlist_data_(g2f_allowlist_data),
      force_activate_fips_(force_activate_fips),
      legacy_kh_fallback_(legacy_kh_fallback),
      service_started_(false),
      hwsec_factory_(hwsec::ThreadingMode::kCurrentThread) {
  auto u2f_vendor_frontend = hwsec_factory_.GetU2fVendorFrontend();
  if (u2f_vendor_frontend->IsEnabled().value_or(false)) {
    u2fhid_service_ =
        std::make_unique<U2fHidServiceImpl>(std::move(u2f_vendor_frontend));
  }
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
      base::BindRepeating(&U2fDaemon::TryStartService, base::Unretained(this)),
      base::BindOnce(&OnPolicySignalConnected));

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
    VLOG(1) << "U2F currently disabled, waiting for policy updates...";
  } else {
    VLOG(1) << "Policy not available, waiting...";
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

bool U2fDaemon::MaybeForceActivateFips(U2fMode u2f_mode) {
  auto u2f_vendor_frontend = hwsec_factory_.GetU2fVendorFrontend();
  if (!u2f_vendor_frontend->IsEnabled().value_or(false)) {
    return true;
  }
  bool should_activate = force_activate_fips_;
  if (!should_activate) {
    return true;
  }
  if (hwsec::Status status = u2f_vendor_frontend->ActivateFipsIfNotActive();
      !status.ok()) {
    LOG(ERROR) << "ActivateFips failed: " << status;
    return false;
  }
  return true;
}

FipsInfo U2fDaemon::ReportFipsStatus(U2fMode u2f_mode) {
  auto u2f_vendor_frontend = hwsec_factory_.GetU2fVendorFrontend();
  // Only need to report FIPS status on devices supporting U2F vendor command.
  if (!u2f_vendor_frontend->IsEnabled().value_or(false)) {
    return FipsInfo{.activation_status = FipsStatus::kNotActive};
  }
  hwsec::StatusOr<FipsInfo> fips_info = u2f_vendor_frontend->GetFipsInfo();
  if (!fips_info.ok()) {
    LOG(ERROR) << "GetFipsInfo failed: " << fips_info.status();
    SendU2fFipsStatusMetrics(metrics_library_, u2f_mode, U2fFipsStatus::kError);
    return FipsInfo{.activation_status = FipsStatus::kNotActive};
  }
  U2fFipsStatus status;
  switch (fips_info->activation_status) {
    case FipsStatus::kNotActive:
      status = U2fFipsStatus::kNotActive;
      LOG(INFO) << "U2F is not running in FIPS mode.";
      break;
    case FipsStatus::kActive:
      status = U2fFipsStatus::kActive;
      LOG(INFO) << "U2F is running in FIPS mode.";
      break;
  }
  SendU2fFipsStatusMetrics(metrics_library_, u2f_mode, status);
  return *fips_info;
}

int U2fDaemon::StartService() {
  U2fMode u2f_mode = GetU2fMode(force_u2f_, force_g2f_);
  if (!MaybeForceActivateFips(u2f_mode)) {
    // If FIPS mode should be forced but we failed to force it, stop u2fd.
    return EX_UNAVAILABLE;
  }
  FipsInfo fips_info = ReportFipsStatus(u2f_mode);

  // Start U2fHid service before WebAuthn because WebAuthn initialization can
  // be slow.
  int status = StartU2fHidService(fips_info);

  VLOG(1) << "Initializing WebAuthn handler.";
  // If initialize WebAuthn handler failed, it means that the whole u2fd service
  // is unavailable (it can't happen on devices we enable U2fHid service), and
  // there's no point to keep running it.
  if (!InitializeWebAuthnHandler(u2f_mode, fips_info)) {
    LOG(INFO) << "Initialize WebAuthn handler failed, quiting.";
    return EX_UNAVAILABLE;
  }

  return status;
}

int U2fDaemon::StartU2fHidService(hwsec::u2f::FipsInfo fips_info) {
  if (!u2fhid_service_) {
    // No need to start u2f HID service on this device.
    service_started_ = true;
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

  service_started_ = true;

  LOG(INFO) << "Starting U2fHid service, enable_corp_protocol: "
            << enable_corp_protocol_ << ".";

  // If g2f is enabled by policy, we always include allowlisting data.
  bool include_g2f_allowlist_data =
      g2f_allowlist_data_ || (ReadU2fPolicy() == U2fMode::kU2fExtended);

  std::function<void()> request_presence = [this]() {
    IgnorePowerButtonPress();
    SendWinkSignal();
  };

  return u2fhid_service_->CreateU2fHid(
             u2f_mode == U2fMode::kU2fExtended /* Allow G2F Attestation */,
             include_g2f_allowlist_data, enable_corp_protocol_, fips_info,
             request_presence, user_state_.get(), sm_proxy_.get(),
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

  u2f_interface->AddSimpleMethodHandler(
      kU2FIsPlatformAuthenticatorInitialized,
      base::Unretained(&webauthn_handler_),
      &WebAuthnHandler::IsPlatformAuthenticatorInitialized);

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

  u2f_interface->AddSimpleMethodHandler(kU2FGetAlgorithms,
                                        base::Unretained(&webauthn_handler_),
                                        &WebAuthnHandler::GetAlgorithms);

  u2f_interface->AddSimpleMethodHandler(kU2FGetSupportedFeatures,
                                        base::Unretained(&webauthn_handler_),
                                        &WebAuthnHandler::GetSupportedFeatures);

  dbus_object_->RegisterAsync(
      sequencer->GetHandler("Failed to register DBus Interface.", true));
}

bool U2fDaemon::InitializeWebAuthnHandler(U2fMode u2f_mode,
                                          hwsec::u2f::FipsInfo fips_info) {
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
    allowlisting_util = std::make_unique<AllowlistingUtil>(
        [this](int cert_size) {
          return u2fhid_service_->GetCertifiedG2fCert(cert_size);
        },
        fips_info);
  }

  if (auto u2f_vendor_frontend = hwsec_factory_.GetU2fVendorFrontend();
      u2f_vendor_frontend->IsEnabled().value_or(false)) {
    u2f_command_processor = std::make_unique<U2fCommandProcessorVendor>(
        std::move(u2f_vendor_frontend), request_presence);
  } else if (auto u2f_frontend = hwsec_factory_.GetU2fFrontend();
             u2f_frontend->IsEnabled().value_or(false)) {
    u2f_command_processor = std::make_unique<U2fCommandProcessorGeneric>(
        user_state_.get(),
        std::make_unique<org::chromium::UserDataAuthInterfaceProxy>(bus_.get()),
        std::move(u2f_frontend));
  } else {
    return false;
  }

  webauthn_handler_.Initialize(bus_.get(), user_state_.get(), u2f_mode,
                               std::move(u2f_command_processor),
                               std::move(allowlisting_util), &metrics_library_);
  return true;
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

U2fMode U2fDaemon::GetU2fMode(bool force_u2f, bool force_g2f) {
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

  // On devices without GSC, power button can't be used as security key so U2F
  // and G2F modes are not supported.
  if (!u2fhid_service_) {
    LOG(INFO) << "Mode: Disabled (not supported)";
    return U2fMode::kDisabled;
  }

  if (force_g2f || policy_mode == U2fMode::kU2fExtended) {
    LOG(INFO) << "Mode: U2F+extensions";
    return U2fMode::kU2fExtended;
  }

  if (force_u2f || policy_mode == U2fMode::kU2f) {
    LOG(INFO) << "Mode: U2F";
    return U2fMode::kU2f;
  }

  LOG(INFO) << "Mode: Disabled";
  return U2fMode::kDisabled;
}

}  // namespace u2f
