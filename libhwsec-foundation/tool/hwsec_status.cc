// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <stdlib.h>
#include <sysexits.h>

#include <memory>
#include <tuple>
#include <utility>

#include <attestation/proto_bindings/attestation_ca.pb.h>
#include <attestation/proto_bindings/interface.pb.h>
#include <attestation-client/attestation/dbus-proxies.h>
#include <base/functional/bind.h>
#include <base/functional/callback.h>
#include <base/logging.h>
#include <base/task/single_thread_task_runner.h>
#include <base/types/always_false.h>
#include <brillo/daemons/daemon.h>
#include <brillo/syslog_logging.h>
#include <cryptohome/proto_bindings/UserDataAuth.pb.h>
#include <device_management/proto_bindings/device_management_interface.pb.h>
#include <device_management-client/device_management/dbus-proxies.h>
#include <tpm_manager/proto_bindings/tpm_manager.pb.h>
#include <tpm_manager-client/tpm_manager/dbus-proxies.h>
#include <user_data_auth-client/user_data_auth/dbus-proxies.h>

#include "libhwsec-foundation/tool/hwsec_status.pb.h"
#include "libhwsec-foundation/tool/print_hwsec_status_proto.h"

namespace {

constexpr base::TimeDelta kDefaultTimeout = base::Minutes(5);

hwsec_foundation::InstallAttributesState ConvertInstallAttributesState(
    device_management::InstallAttributesState state) {
  switch (state) {
    case device_management::InstallAttributesState::UNKNOWN:
      return hwsec_foundation::InstallAttributesState::UNKNOWN;
    case device_management::InstallAttributesState::TPM_NOT_OWNED:
      return hwsec_foundation::InstallAttributesState::TPM_NOT_OWNED;
    case device_management::InstallAttributesState::FIRST_INSTALL:
      return hwsec_foundation::InstallAttributesState::FIRST_INSTALL;
    case device_management::InstallAttributesState::VALID:
      return hwsec_foundation::InstallAttributesState::VALID;
    case device_management::InstallAttributesState::INVALID:
      return hwsec_foundation::InstallAttributesState::INVALID;
    default:
      return hwsec_foundation::InstallAttributesState::UNKNOWN;
  }
}

class ClientLoop : public brillo::Daemon {
 public:
  ClientLoop() = default;
  ClientLoop(const ClientLoop&) = delete;
  ClientLoop& operator=(const ClientLoop&) = delete;

  ~ClientLoop() override = default;

 protected:
  int OnInit() override {
    int exit_code = brillo::Daemon::OnInit();
    if (exit_code != EX_OK) {
      LOG(ERROR) << "Error initializing hwsec_status.";
      return exit_code;
    }

    dbus::Bus::Options options;
    options.bus_type = dbus::Bus::SYSTEM;
    bus_ = base::MakeRefCounted<dbus::Bus>(options);
    CHECK(bus_->Connect()) << "Failed to connect to system D-Bus";

    tpm_manager_ = std::make_unique<org::chromium::TpmManagerProxy>(bus_);
    attestation_ = std::make_unique<org::chromium::AttestationProxy>(bus_);
    device_management_ =
        std::make_unique<org::chromium::DeviceManagementProxy>(bus_);
    cryptohome_pkcs11_ =
        std::make_unique<org::chromium::CryptohomePkcs11InterfaceProxy>(bus_);
    cryptohome_misc_ =
        std::make_unique<org::chromium::CryptohomeMiscInterfaceProxy>(bus_);
    user_data_auth_ =
        std::make_unique<org::chromium::UserDataAuthInterfaceProxy>(bus_);

    InitDBusCalls();
    return EX_OK;
  }

 private:
  // Template to fill hwsec_status_ with reply protobuf.
  template <typename Reply>
  void FillHwsecStatus(const Reply& reply) {
    if constexpr (std::same_as<Reply,
                               tpm_manager::GetTpmNonsensitiveStatusReply>) {
      if (reply.has_is_enabled())
        hwsec_status_.set_is_enabled(reply.is_enabled());
      if (reply.has_is_owned())
        hwsec_status_.set_is_owned(reply.is_owned());
      if (reply.has_is_owner_password_present())
        hwsec_status_.set_is_owner_password_present(
            reply.is_owner_password_present());
      if (reply.has_has_reset_lock_permissions())
        hwsec_status_.set_has_reset_lock_permissions(
            reply.has_reset_lock_permissions());
      if (reply.has_is_srk_default_auth())
        hwsec_status_.set_is_srk_default_auth(reply.is_srk_default_auth());
    } else if constexpr (std::same_as<Reply,
                                      tpm_manager::GetSupportedFeaturesReply>) {
      if (reply.has_support_u2f())
        hwsec_status_.set_support_u2f(reply.support_u2f());
      if (reply.has_support_pinweaver())
        hwsec_status_.set_support_pinweaver(reply.support_pinweaver());
      if (reply.has_support_runtime_selection())
        hwsec_status_.set_support_runtime_selection(
            reply.support_runtime_selection());
      if (reply.has_is_allowed())
        hwsec_status_.set_is_allowed(reply.has_is_allowed());
      if (reply.has_support_clear_request())
        hwsec_status_.set_support_clear_request(reply.support_clear_request());
      if (reply.has_support_clear_without_prompt())
        hwsec_status_.set_support_clear_without_prompt(
            reply.support_clear_without_prompt());
    } else if constexpr (std::same_as<Reply,
                                      tpm_manager::GetVersionInfoReply>) {
      if (reply.has_family())
        hwsec_status_.set_family(reply.family());
      if (reply.has_spec_level())
        hwsec_status_.set_spec_level(reply.spec_level());
      if (reply.has_manufacturer())
        hwsec_status_.set_manufacturer(reply.manufacturer());
      if (reply.has_tpm_model())
        hwsec_status_.set_tpm_model(reply.tpm_model());
      if (reply.has_firmware_version())
        hwsec_status_.set_firmware_version(reply.firmware_version());
      if (reply.has_vendor_specific())
        hwsec_status_.set_vendor_specific(reply.vendor_specific());
      if (reply.has_rw_version())
        hwsec_status_.set_gsc_rw_version(reply.rw_version());
    } else if constexpr (std::same_as<
                             Reply,
                             tpm_manager::GetDictionaryAttackInfoReply>) {
      if (reply.has_dictionary_attack_counter())
        hwsec_status_.set_dictionary_attack_counter(
            reply.dictionary_attack_counter());
      if (reply.has_dictionary_attack_threshold())
        hwsec_status_.set_dictionary_attack_threshold(
            reply.dictionary_attack_threshold());
      if (reply.has_dictionary_attack_lockout_in_effect())
        hwsec_status_.set_dictionary_attack_lockout_in_effect(
            reply.dictionary_attack_lockout_in_effect());
      if (reply.has_dictionary_attack_lockout_seconds_remaining())
        hwsec_status_.set_dictionary_attack_lockout_seconds_remaining(
            reply.dictionary_attack_lockout_seconds_remaining());
    } else if constexpr (std::same_as<Reply, attestation::GetStatusReply>) {
      if (reply.has_prepared_for_enrollment())
        hwsec_status_.set_prepared_for_enrollment(
            reply.prepared_for_enrollment());
      if (reply.has_enrolled())
        hwsec_status_.set_enrolled(reply.enrolled());
      if (reply.has_verified_boot())
        hwsec_status_.set_verified_boot(reply.verified_boot());
    } else if constexpr (std::same_as<Reply,
                                      device_management::
                                          InstallAttributesGetStatusReply>) {
      hwsec_status_.set_inst_attrs_count(reply.count());
      hwsec_status_.set_inst_attrs_is_secure(reply.is_secure());
      hwsec_status_.set_inst_attrs_state(
          ConvertInstallAttributesState(reply.state()));
    } else if constexpr (std::same_as<
                             Reply, device_management::
                                        GetFirmwareManagementParametersReply>) {
      if (reply.has_fwmp())
        hwsec_status_.set_fwmp_flags(reply.fwmp().flags());
    } else if constexpr (std::same_as<
                             Reply,
                             user_data_auth::Pkcs11IsTpmTokenReadyReply>) {
      hwsec_status_.set_user_token_ready(reply.ready());
    } else if constexpr (std::same_as<Reply,
                                      user_data_auth::GetLoginStatusReply>) {
      hwsec_status_.set_owner_user_exists(reply.owner_user_exists());
      hwsec_status_.set_is_locked_to_single_user(
          reply.is_locked_to_single_user());
    } else if constexpr (std::same_as<Reply, user_data_auth::IsMountedReply>) {
      hwsec_status_.set_is_mounted(reply.is_mounted());
      hwsec_status_.set_is_ephemeral_mount(reply.is_ephemeral_mount());
    } else {
      static_assert(base::AlwaysFalse<Reply>, "Forget to handle this reply");
    }
    CallbackFinished();
  }

  void PrintError(brillo::Error* error) {
    printf("Error: %s\n", error->GetMessage().c_str());
    CallbackFinished();
  }

  void CallbackFinished() {
    async_function_count_--;
    if (async_function_count_ == 0) {
      printf("Message Reply: %s\n", GetProtoDebugString(hwsec_status_).c_str());
      // TODO(b/316968788): Send UMA.
      Quit();
    }
  }

  template <typename Reply>
  auto GenerateReplyCallbacks() {
    async_function_count_++;
    return std::make_tuple(
        base::BindOnce(&ClientLoop::FillHwsecStatus<Reply>,
                       weak_factory_.GetWeakPtr()),
        base::BindOnce(&ClientLoop::PrintError, weak_factory_.GetWeakPtr()));
  }

  template <typename Reply, typename Proxy, typename Func, typename Request>
  void ApplyDBusCall(Proxy& proxy, Func func, const Request& request) {
    std::apply(func, std::tuple_cat(
                         std::make_tuple(proxy.get(), request),
                         GenerateReplyCallbacks<Reply>(),
                         std::make_tuple(kDefaultTimeout.InMilliseconds())));
  }

  void InitDBusCalls() {
    // Consider this function(InitDBusCalls) as an async function to make sure
    // the final callback will not be triggered before we apply all dbus calls.
    async_function_count_++;

    ApplyDBusCall<tpm_manager::GetTpmNonsensitiveStatusReply>(
        tpm_manager_,
        &org::chromium::TpmManagerProxyInterface::GetTpmNonsensitiveStatusAsync,
        tpm_manager::GetTpmNonsensitiveStatusRequest{});

    ApplyDBusCall<tpm_manager::GetSupportedFeaturesReply>(
        tpm_manager_,
        &org::chromium::TpmManagerProxyInterface::GetSupportedFeaturesAsync,
        tpm_manager::GetSupportedFeaturesRequest{});

    ApplyDBusCall<tpm_manager::GetVersionInfoReply>(
        tpm_manager_,
        &org::chromium::TpmManagerProxyInterface::GetVersionInfoAsync,
        tpm_manager::GetVersionInfoRequest{});

    ApplyDBusCall<tpm_manager::GetDictionaryAttackInfoReply>(
        tpm_manager_,
        &org::chromium::TpmManagerProxyInterface::GetDictionaryAttackInfoAsync,
        tpm_manager::GetDictionaryAttackInfoRequest{});

    ApplyDBusCall<attestation::GetStatusReply>(
        attestation_, &org::chromium::AttestationProxyInterface::GetStatusAsync,
        []() {
          attestation::GetStatusRequest request;
          request.set_extended_status(true);
          return request;
        }());

    ApplyDBusCall<device_management::InstallAttributesGetStatusReply>(
        device_management_,
        &org::chromium::DeviceManagementProxyInterface::
            InstallAttributesGetStatusAsync,
        device_management::InstallAttributesGetStatusRequest{});

    ApplyDBusCall<device_management::GetFirmwareManagementParametersReply>(
        device_management_,
        &org::chromium::DeviceManagementProxyInterface::
            GetFirmwareManagementParametersAsync,
        device_management::GetFirmwareManagementParametersRequest{});

    ApplyDBusCall<user_data_auth::Pkcs11IsTpmTokenReadyReply>(
        cryptohome_pkcs11_,
        &org::chromium::CryptohomePkcs11InterfaceProxyInterface::
            Pkcs11IsTpmTokenReadyAsync,
        user_data_auth::Pkcs11IsTpmTokenReadyRequest{});

    ApplyDBusCall<user_data_auth::GetLoginStatusReply>(
        cryptohome_misc_,
        &org::chromium::CryptohomeMiscInterfaceProxyInterface::
            GetLoginStatusAsync,
        user_data_auth::GetLoginStatusRequest{});

    ApplyDBusCall<user_data_auth::IsMountedReply>(
        user_data_auth_,
        &org::chromium::UserDataAuthInterfaceProxyInterface::IsMountedAsync,
        user_data_auth::IsMountedRequest{});

    CallbackFinished();
  }

  hwsec_foundation::HwsecStatus hwsec_status_;
  uint32_t async_function_count_ = 0;

  scoped_refptr<dbus::Bus> bus_;

  // IPC proxy interfaces.
  std::unique_ptr<org::chromium::TpmManagerProxyInterface> tpm_manager_;
  std::unique_ptr<org::chromium::AttestationProxyInterface> attestation_;
  std::unique_ptr<org::chromium::DeviceManagementProxyInterface>
      device_management_;
  std::unique_ptr<org::chromium::CryptohomePkcs11InterfaceProxyInterface>
      cryptohome_pkcs11_;
  std::unique_ptr<org::chromium::CryptohomeMiscInterfaceProxyInterface>
      cryptohome_misc_;
  std::unique_ptr<org::chromium::UserDataAuthInterfaceProxyInterface>
      user_data_auth_;

  // Declared last so that weak pointers will be destroyed first.
  base::WeakPtrFactory<ClientLoop> weak_factory_{this};
};

}  // namespace

int main(int argc, char* argv[]) {
  brillo::InitLog(brillo::kLogToStderr);
  ClientLoop loop;
  return loop.Run();
}
