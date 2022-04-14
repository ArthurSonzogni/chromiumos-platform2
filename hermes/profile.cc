// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "hermes/profile.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include <base/callback_helpers.h>
#include <base/check.h>
#include <base/logging.h>
#include <chromeos/dbus/service_constants.h>

#include "hermes/executor.h"
#include "hermes/hermes_common.h"
#include "hermes/lpa_util.h"

namespace hermes {

namespace {

const char kBasePath[] = "/org/chromium/Hermes/profile/";

std::optional<profile::State> LpaProfileStateToHermes(
    lpa::proto::ProfileState state) {
  switch (state) {
    case lpa::proto::DISABLED:
      return profile::kInactive;
    case lpa::proto::ENABLED:
      return profile::kActive;
    default:
      LOG(ERROR) << "Unrecognized lpa ProfileState: " << state;
      return std::nullopt;
  }
}

std::optional<profile::ProfileClass> LpaProfileClassToHermes(
    lpa::proto::ProfileClass cls) {
  switch (cls) {
    case lpa::proto::TESTING:
      return profile::kTesting;
    case lpa::proto::PROVISIONING:
      return profile::kProvisioning;
    case lpa::proto::OPERATIONAL:
      return profile::kOperational;
    default:
      LOG(ERROR) << "Unrecognized lpa ProfileClass: " << cls;
      return std::nullopt;
  }
}

void SendDBusError(std::shared_ptr<Profile::DBusResponse<>> response,
                   brillo::ErrorPtr lpa_error,
                   int modem_error) {
  if (modem_error != kSuccess) {
    LOG(ERROR) << "Modem finished with error code: " << modem_error;
  }
  response->ReplyWithError(lpa_error.get());
}

template <typename T>
void RunOnSuccess(base::OnceCallback<void(T)> cb, T response, int err) {
  if (err) {
    LOG(ERROR) << "Received modem error: " << err;
    auto decoded_error = LpaErrorToBrillo(FROM_HERE, err);
    if (decoded_error) {
      response->ReplyWithError(
          FROM_HERE, brillo::errors::dbus::kDomain, kErrorUnknown,
          "QMI/MBIM operation failed with code: " + std::to_string(err));
    }
    return;
  }
  std::move(cb).Run(std::move(response));
}

}  // namespace

// static
std::unique_ptr<Profile> Profile::Create(
    const lpa::proto::ProfileInfo& profile_info,
    const uint32_t physical_slot,
    const std::string& eid,
    bool is_pending,
    base::RepeatingCallback<void(const std::string&)> on_profile_enabled_cb) {
  CHECK(profile_info.has_iccid());
  auto profile = std::unique_ptr<Profile>(new Profile(
      dbus::ObjectPath(kBasePath + eid + "/" + profile_info.iccid()),
      physical_slot));
  LOG(INFO) << __func__ << " Slot:" << physical_slot << " "
            << GetObjectPathForLog(profile->object_path_);
  // Initialize properties.
  profile->SetIccid(profile_info.iccid());
  profile->SetServiceProvider(profile_info.service_provider_name());
  if (profile_info.has_profile_owner()) {
    profile->SetMccMnc(profile_info.profile_owner().mcc() +
                       profile_info.profile_owner().mnc());
  }
  profile->SetActivationCode(profile_info.activation_code());
  std::optional<profile::State> state;
  state = is_pending ? profile::kPending
                     : LpaProfileStateToHermes(profile_info.profile_state());
  if (!state.has_value()) {
    LOG(ERROR) << "Failed to create Profile for iccid " << profile_info.iccid()
               << "; invalid ProfileState " << profile_info.profile_state();
    return nullptr;
  }
  profile->SetState(state.value());
  auto cls = LpaProfileClassToHermes(profile_info.profile_class());
  if (!cls.has_value()) {
    LOG(ERROR) << "Failed to create Profile for iccid " << profile_info.iccid()
               << "; invalid ProfileClass " << profile_info.profile_class();
    return nullptr;
  }
  profile->SetProfileClass(cls.value());
  profile->SetName(profile_info.profile_name());
  profile->SetNickname(profile_info.profile_nickname());

  profile->RegisterWithDBusObject(&profile->dbus_object_);
  profile->dbus_object_.RegisterAndBlock();

  profile->on_profile_enabled_cb_ = std::move(on_profile_enabled_cb);

  LOG(INFO) << "Successfuly created Profile";
  VLOG(2) << profile_info.DebugString();
  return profile;
}

Profile::Profile(dbus::ObjectPath object_path, const uint32_t physical_slot)
    : org::chromium::Hermes::ProfileAdaptor(this),
      context_(Context::Get()),
      object_path_(std::move(object_path)),
      dbus_object_(nullptr, context_->bus(), object_path_),
      physical_slot_(physical_slot),
      weak_factory_(this) {}

void Profile::Enable(std::unique_ptr<DBusResponse<>> response) {
  LOG(INFO) << __func__ << " " << GetObjectPathForLog(object_path_);
  if (!context_->lpa()->IsLpaIdle()) {
    context_->executor()->PostDelayedTask(
        FROM_HERE,
        base::BindOnce(&Profile::Enable, weak_factory_.GetWeakPtr(),
                       std::move(response)),
        kLpaRetryDelay);
    return;
  }
  if (GetState() == profile::kPending) {
    response->ReplyWithError(FROM_HERE, brillo::errors::dbus::kDomain,
                             kErrorPendingProfile,
                             "Cannot enable a pending Profile object");
    return;
  }
  LOG(INFO) << "Enabling profile: " << GetObjectPathForLog(object_path_);
  auto enable_profile =
      base::BindOnce(&Profile::EnableProfile, weak_factory_.GetWeakPtr());
  context_->modem_control()->ProcessEuiccEvent(
      {physical_slot_, EuiccStep::START, EuiccOp::ENABLE},
      base::BindOnce(&RunOnSuccess<std::unique_ptr<DBusResponse<>>>,
                     std::move(enable_profile), std::move(response)));
}

void Profile::EnableProfile(std::unique_ptr<DBusResponse<>> response) {
  LOG(INFO) << __func__ << " " << GetObjectPathForLog(object_path_);
  context_->lpa()->EnableProfile(
      GetIccid(), context_->executor(),
      [response{std::shared_ptr<DBusResponse<>>(std::move(response))},
       weak{weak_factory_.GetWeakPtr()}](int error) mutable {
        if (!weak) {
          return;
        }
        weak->OnEnabled(error, std::move(response));
      });
}

void Profile::Disable(std::unique_ptr<DBusResponse<>> response) {
  LOG(INFO) << __func__ << " " << GetObjectPathForLog(object_path_);
  if (!context_->lpa()->IsLpaIdle()) {
    context_->executor()->PostDelayedTask(
        FROM_HERE,
        base::BindOnce(&Profile::Disable, weak_factory_.GetWeakPtr(),
                       std::move(response)),
        kLpaRetryDelay);
    return;
  }
  if (GetState() == profile::kPending) {
    response->ReplyWithError(FROM_HERE, brillo::errors::dbus::kDomain,
                             kErrorPendingProfile,
                             "Cannot disable a pending Profile object");
    return;
  }

  LOG(INFO) << "Disabling profile: " << GetObjectPathForLog(object_path_);
  auto disable_profile =
      base::BindOnce(&Profile::DisableProfile, weak_factory_.GetWeakPtr());
  context_->modem_control()->ProcessEuiccEvent(
      {physical_slot_, EuiccStep::START, EuiccOp::DISABLE},
      base::BindOnce(&RunOnSuccess<std::unique_ptr<DBusResponse<>>>,
                     std::move(disable_profile), std::move(response)));
}

void Profile::DisableProfile(std::unique_ptr<DBusResponse<>> response) {
  LOG(INFO) << __func__ << " " << GetObjectPathForLog(object_path_);
  context_->lpa()->DisableProfile(
      GetIccid(), context_->executor(),
      [response{std::shared_ptr<DBusResponse<>>(std::move(response))},
       weak{weak_factory_.GetWeakPtr()}](int error) mutable {
        if (!weak) {
          return;
        }
        weak->OnDisabled(error, std::move(response));
      });
}

void Profile::OnEnabled(int error, std::shared_ptr<DBusResponse<>> response) {
  LOG(INFO) << __func__ << " " << GetObjectPathForLog(object_path_);
  auto decoded_error = LpaErrorToBrillo(FROM_HERE, error);
  if (decoded_error) {
    LOG(ERROR) << "Failed enabling profile: " << object_path_.value()
               << " (error " << decoded_error << ")";
    context_->modem_control()->ProcessEuiccEvent(
        {physical_slot_, EuiccStep::END},
        base::BindOnce(&SendDBusError, std::move(response),
                       std::move(decoded_error)));
    return;
  }
  on_profile_enabled_cb_.Run(GetIccid());
  VLOG(2) << "Enabled profile: " << object_path_.value();
  auto send_notifs =
      base::BindOnce(&Profile::FinishProfileOpCb, weak_factory_.GetWeakPtr(),
                     std::move(response));
  context_->modem_control()->ProcessEuiccEvent(
      {physical_slot_, EuiccStep::PENDING_NOTIFICATIONS},
      std::move(send_notifs));
}

void Profile::OnDisabled(int error, std::shared_ptr<DBusResponse<>> response) {
  LOG(INFO) << __func__ << " " << GetObjectPathForLog(object_path_);
  auto decoded_error = LpaErrorToBrillo(FROM_HERE, error);
  if (decoded_error) {
    LOG(ERROR) << "Failed disabling profile: " << object_path_.value()
               << " (error " << decoded_error << ")";
    context_->modem_control()->ProcessEuiccEvent(
        {physical_slot_, EuiccStep::END},
        base::BindOnce(&SendDBusError, std::move(response),
                       std::move(decoded_error)));
    return;
  }
  LOG(INFO) << "Disabled profile: " << object_path_.value();
  SetState(profile::kInactive);

  auto send_notifs =
      base::BindOnce(&Profile::FinishProfileOpCb, weak_factory_.GetWeakPtr(),
                     std::move(response));
  context_->modem_control()->ProcessEuiccEvent(
      {physical_slot_, EuiccStep::PENDING_NOTIFICATIONS},
      std::move(send_notifs));
}

void Profile::FinishProfileOpCb(std::shared_ptr<DBusResponse<>> response,
                                int err) {
  LOG(INFO) << __func__;
  if (err) {
    LOG(WARNING) << "Could not finish profile op: " << object_path_.value();
    // Notifications are optional by the standard. Since FinishProfileOp failed,
    // it means notifications cannot be sent, but our enable/disable succeeded.
    // return success on DBus anyway since only notifications cannot be sent.
    response->Return();
    return;
  }
  context_->lpa()->SendNotifications(
      context_->executor(),
      [this, response{std::move(response)}](int /*error*/) {
        VLOG(2) << "FinishProfileOpCb: sent all notifications";
        context_->modem_control()->ProcessEuiccEvent(
            {physical_slot_, EuiccStep::END},
            base::BindOnce(
                [](std::shared_ptr<DBusResponse<>> response, int error) {
                  response->Return();
                  LOG(INFO)
                      << "FinishProfileOpCb: completed with err = " << error;
                },
                response));
      });
}

void Profile::Rename(std::unique_ptr<DBusResponse<>> response,
                     const std::string& nickname) {
  LOG(INFO) << __func__ << " Nickname: " << nickname << " "
            << GetObjectPathForLog(object_path_);
  if (!context_->lpa()->IsLpaIdle()) {
    context_->executor()->PostDelayedTask(
        FROM_HERE,
        base::BindOnce(&Profile::Rename, weak_factory_.GetWeakPtr(),
                       std::move(response), nickname),
        kLpaRetryDelay);
    return;
  }
  auto set_nickname =
      base::BindOnce(&Profile::SetNicknameMethod, weak_factory_.GetWeakPtr(),
                     std::move(nickname));
  context_->modem_control()->ProcessEuiccEvent(
      {physical_slot_, EuiccStep::START},
      base::BindOnce(&RunOnSuccess<std::unique_ptr<DBusResponse<>>>,
                     std::move(set_nickname), std::move(response)));
}

void Profile::SetProfileNickname(std::string nickname) {
  LOG(INFO) << __func__ << " " << GetObjectPathForLog(object_path_);
  if (!context_->lpa()->IsLpaIdle()) {
    context_->executor()->PostDelayedTask(
        FROM_HERE,
        base::BindOnce(&Profile::SetProfileNickname, weak_factory_.GetWeakPtr(),
                       std::move(nickname)),
        kLpaRetryDelay);
    return;
  }
  auto set_nickname_property =
      base::BindOnce(&Profile::SetNicknameProperty, weak_factory_.GetWeakPtr(),
                     std::move(nickname));
  context_->modem_control()->StoreAndSetActiveSlot(
      physical_slot_,
      base::BindOnce(&IgnoreErrorRunClosure, std::move(set_nickname_property)));
}

void Profile::SetNicknameProperty(std::string nickname) {
  context_->lpa()->SetProfileNickname(
      GetIccid(), nickname, context_->executor(), [this](int error) {
        auto decoded_error = LpaErrorToBrillo(FROM_HERE, error);
        if (decoded_error) {
          LOG(ERROR) << "Failed to set profile nickname: "
                     << decoded_error->GetMessage();
        }
        context_->modem_control()->RestoreActiveSlot(base::DoNothing());
      });
}

void Profile::SetNicknameMethod(std::string nickname,
                                std::unique_ptr<DBusResponse<>> response) {
  LOG(INFO) << __func__ << " Nickname: " << nickname << " "
            << GetObjectPathForLog(object_path_);
  context_->lpa()->SetProfileNickname(
      GetIccid(), nickname, context_->executor(),
      [this, nickname,
       response{std::shared_ptr<DBusResponse<>>(std::move(response))}](
          int error) mutable {
        auto decoded_error = LpaErrorToBrillo(FROM_HERE, error);
        if (decoded_error) {
          LOG(ERROR) << "Failed to set profile nickname: "
                     << decoded_error->GetMessage();
          response->ReplyWithError(decoded_error.get());
          return;
        }
        this->SetNickname(nickname);

        context_->modem_control()->RestoreActiveSlot(
            base::BindOnce(&Profile::OnRestoreActiveSlot,
                           weak_factory_.GetWeakPtr(), std::move(response)));
      });
}

void Profile::OnRestoreActiveSlot(std::shared_ptr<DBusResponse<>> response,
                                  int error) {
  if (error) {
    auto decoded_error = brillo::Error::Create(
        FROM_HERE, brillo::errors::dbus::kDomain, kErrorUnknown,
        "QMI/MBIM operation failed with code: " + std::to_string(error));
    LOG(ERROR) << "Failed to restore slot: " << object_path_.value() << " ("
               << decoded_error << ")";
    context_->modem_control()->ProcessEuiccEvent(
        {physical_slot_, EuiccStep::END},
        base::BindOnce(&SendDBusError, std::move(response),
                       std::move(decoded_error)));
    return;
  }
  auto return_dbus_success = base::BindOnce(
      [](std::shared_ptr<DBusResponse<>> response) { response->Return(); });
  context_->modem_control()->ProcessEuiccEvent(
      {physical_slot_, EuiccStep::END},
      base::BindOnce(&RunOnSuccess<std::shared_ptr<DBusResponse<>>>,
                     std::move(return_dbus_success), std::move(response)));
}

bool Profile::ValidateNickname(brillo::ErrorPtr* /*error*/,
                               const std::string& value) {
  SetProfileNickname(value);
  return true;
}

Profile::~Profile() {
  dbus_object_.UnregisterAsync();
}

}  // namespace hermes
