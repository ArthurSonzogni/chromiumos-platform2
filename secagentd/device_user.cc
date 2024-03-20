// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "secagentd/device_user.h"

#include <unistd.h>

#include <list>
#include <memory>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/important_file_writer.h"
#include "base/functional/bind.h"
#include "base/functional/callback_forward.h"
#include "base/memory/weak_ptr.h"
#include "base/time/time.h"
#include "base/types/expected.h"
#include "base/uuid.h"
#include "bindings/device_management_backend.pb.h"
#include "brillo/errors/error.h"
#include "brillo/files/file_util.h"
#include "cryptohome/proto_bindings/UserDataAuth.pb.h"
#include "policy/device_local_account_policy_util.h"
#include "secagentd/common.h"

namespace secagentd {

DeviceUser::DeviceUser(
    std::unique_ptr<org::chromium::SessionManagerInterfaceProxyInterface>
        session_manager)
    : DeviceUser(std::move(session_manager),
                 std::make_unique<org::chromium::UserDataAuthInterfaceProxy>(
                     common::GetDBus()),
                 base::FilePath("/")) {}

DeviceUser::DeviceUser(
    std::unique_ptr<org::chromium::SessionManagerInterfaceProxyInterface>
        session_manager,
    std::unique_ptr<org::chromium::UserDataAuthInterfaceProxyInterface>
        cryptohome_proxy,
    const base::FilePath& root_path)
    : weak_ptr_factory_(this),
      session_manager_(std::move(session_manager)),
      cryptohome_proxy_(std::move(cryptohome_proxy)),
      root_path_(root_path) {}

void DeviceUser::RegisterSessionChangeHandler() {
  session_manager_->GetObjectProxy()->WaitForServiceToBeAvailable(
      base::BindOnce(
          [](org::chromium::SessionManagerInterfaceProxyInterface*
                 session_manager,
             base::WeakPtr<DeviceUser> weak_ptr, bool available) {
            if (!available) {
              LOG(ERROR) << "Failed to register for session_manager's session "
                            "change signal";
              return;
            }
            session_manager->RegisterSessionStateChangedSignalHandler(
                base::BindRepeating(&DeviceUser::OnSessionStateChange,
                                    weak_ptr),
                base::BindOnce(&DeviceUser::OnRegistrationResult, weak_ptr));
            session_manager->GetObjectProxy()->SetNameOwnerChangedCallback(
                base::BindRepeating(&DeviceUser::OnSessionManagerNameChange,
                                    weak_ptr));
          },
          session_manager_.get(), weak_ptr_factory_.GetWeakPtr()));
}

void DeviceUser::RegisterRemoveCompletedHandler() {
  cryptohome_proxy_->GetObjectProxy()->WaitForServiceToBeAvailable(
      base::BindOnce(
          [](org::chromium::UserDataAuthInterfaceProxyInterface*
                 cryptohome_proxy,
             base::RepeatingCallback<void(
                 const user_data_auth::RemoveCompleted&)> signal_callback,
             dbus::ObjectProxy::OnConnectedCallback on_connected_callback,
             bool available) {
            if (!available) {
              LOG(ERROR) << "Failed to register for RemoveCompleted signal";
              return;
            }
            cryptohome_proxy->RegisterRemoveCompletedSignalHandler(
                std::move(signal_callback), std::move(on_connected_callback));
          },
          cryptohome_proxy_.get(),
          base::BindRepeating(&DeviceUser::OnRemoveCompleted,
                              weak_ptr_factory_.GetWeakPtr()),
          base::BindOnce(&DeviceUser::OnRegistrationResult,
                         weak_ptr_factory_.GetWeakPtr())));
}

void DeviceUser::RegisterScreenLockedHandler(
    base::RepeatingClosure signal_callback,
    dbus::ObjectProxy::OnConnectedCallback on_connected_callback) {
  session_manager_->GetObjectProxy()->WaitForServiceToBeAvailable(
      base::BindOnce(
          [](org::chromium::SessionManagerInterfaceProxyInterface*
                 session_manager,
             base::RepeatingClosure signal_callback,
             dbus::ObjectProxy::OnConnectedCallback on_connected_callback,
             bool available) {
            if (!available) {
              LOG(ERROR) << "Failed to register for session_manager's screen "
                            "locked signal";
              return;
            }
            session_manager->RegisterScreenIsLockedSignalHandler(
                std::move(signal_callback), std::move(on_connected_callback));
          },
          session_manager_.get(), std::move(signal_callback),
          std::move(on_connected_callback)));
}

void DeviceUser::RegisterScreenUnlockedHandler(
    base::RepeatingClosure signal_callback,
    dbus::ObjectProxy::OnConnectedCallback on_connected_callback) {
  session_manager_->GetObjectProxy()->WaitForServiceToBeAvailable(
      base::BindOnce(
          [](org::chromium::SessionManagerInterfaceProxyInterface*
                 session_manager,
             base::RepeatingClosure signal_callback,
             dbus::ObjectProxy::OnConnectedCallback on_connected_callback,
             bool available) {
            if (!available) {
              LOG(ERROR) << "Failed to register for session_manager's screen "
                            "unlocked signal";
              return;
            }
            session_manager->RegisterScreenIsUnlockedSignalHandler(
                std::move(signal_callback), std::move(on_connected_callback));
          },
          session_manager_.get(), std::move(signal_callback),
          std::move(on_connected_callback)));
}

void DeviceUser::RegisterSessionChangeListener(
    base::RepeatingCallback<void(const std::string&)> cb) {
  session_change_listeners_.push_back(std::move(cb));
}

void DeviceUser::OnSessionManagerNameChange(const std::string& old_owner,
                                            const std::string& new_owner) {
  // When session manager crashes it logs user out.
  device_user_ = device_user::kEmpty;
}

void DeviceUser::GetDeviceUserAsync(
    base::OnceCallback<void(const std::string& device_user)> cb) {
  if (device_user_ready_) {
    std::move(cb).Run(device_user_);
  } else {
    on_device_user_ready_cbs_.push_back(std::move(cb));
  }
}

std::list<std::string> DeviceUser::GetUsernamesForRedaction() {
  return redacted_usernames_;
}

void DeviceUser::OnRegistrationResult(const std::string& interface,
                                      const std::string& signal,
                                      bool success) {
  if (!success) {
    LOG(ERROR) << "Callback registration failed for dbus signal: " << signal
               << " on interface: " << interface;
    device_user_ = device_user::kUnknown;
  } else {
    OnSessionStateChange(kInit);
  }
}

void DeviceUser::OnRemoveCompleted(
    const user_data_auth::RemoveCompleted& remove_completed) {
  if (remove_completed.sanitized_username().empty()) {
    LOG(ERROR) << "RemoveCompleted signal has no username";
    return;
  }

  auto remove_directory = root_path_.Append(kSecagentdDirectory)
                              .Append(remove_completed.sanitized_username());
  if (!brillo::DeletePathRecursively(remove_directory)) {
    LOG(ERROR) << "Failed to delete removed user's affiliation file";
  }
}

void DeviceUser::OnSessionStateChange(const std::string& state) {
  device_user_ready_ = false;
  if (state == kStarted || state == kInit) {
    flush_cb_.Run();
    UpdateDeviceId();
    if (!UpdateDeviceUser()) {
      return;
    }
  } else if (state == kStopping) {
    device_user_ = device_user::kEmpty;
  } else if (state == kStopped) {
    device_user_ = device_user::kEmpty;
  }

  device_user_ready_ = true;
  for (auto& cb : on_device_user_ready_cbs_) {
    std::move(cb).Run(device_user_);
  }

  for (auto cb : session_change_listeners_) {
    cb.Run(state);
  }
}

void DeviceUser::UpdateDeviceId() {
  if (device_id_ != device_user::kEmpty) {
    return;
  }

  auto response = RetrievePolicy(login_manager::ACCOUNT_TYPE_DEVICE, "");
  if (!response.ok()) {
    LOG(ERROR) << response.status();
    return;
  }

  auto device_policy = response.value();
  if (device_policy.device_affiliation_ids_size() >= 1) {
    device_id_ = device_policy.device_affiliation_ids()[0];
    if (device_policy.user_affiliation_ids_size() > 1) {
      // There should only be 1 ID in the list.
      LOG(ERROR) << "Greater than 1 Device ID. Count = "
                 << device_policy.user_affiliation_ids_size();
    }
  }
}

bool DeviceUser::UpdateDeviceUser() {
  // Check if guest session is active.
  bool is_guest = false;
  brillo::ErrorPtr error;
  if (!session_manager_->IsGuestSessionActive(&is_guest, &error) ||
      error.get()) {
    device_user_ = device_user::kUnknown;
    // Do not exit method because possible that it is user session.
    LOG(ERROR) << "Failed to deterimine if guest session "
               << error->GetMessage();
  } else if (is_guest) {
    device_user_ = device_user::kGuest;
    return true;
  }

  // Retrieve the device username.
  std::string username;
  std::string sanitized;
  if (!session_manager_->RetrievePrimarySession(&username, &sanitized,
                                                &error) ||
      error.get()) {
    device_user_ = device_user::kUnknown;
    LOG(ERROR) << "Failed to retrieve primary session " << error->GetMessage();
    return true;
  } else {
    // No active session.
    if (username.empty()) {
      // Only set as empty when Guest session retrieval succeeds.
      if (device_user_ != device_user::kUnknown) {
        device_user_ = device_user::kEmpty;
      }
      return true;
    }

    // Set the username for redaction.
    if (std::find(redacted_usernames_.begin(), redacted_usernames_.end(),
                  username) == redacted_usernames_.end()) {
      redacted_usernames_.push_front(username);
    }

    if (SetDeviceUserIfLocalAccount(username)) {
      return true;
    }

    // Check if sanitzed username directory exists.
    base::FilePath directory_path =
        root_path_.Append(kSecagentdDirectory).Append(sanitized);
    if (base::DirectoryExists(directory_path)) {
      int64_t file_size;
      std::string uuid;
      if (base::PathExists(directory_path.Append("affiliated"))) {
        device_user_ = username;
        return true;
      } else if (!base::GetFileSize(directory_path.Append("unaffiliated"),
                                    &file_size) ||
                 file_size == 0) {
        LOG(ERROR)
            << "Failed to get username file size. Checking policy instead";
      } else if (!base::ReadFileToString(directory_path.Append("unaffiliated"),
                                         &uuid) ||
                 username.empty()) {
        LOG(ERROR) << "Failed to read uuid. Checking policy instead";
      } else {
        device_user_ = uuid;
        return true;
      }
    }

    // When a user logs in for the first time there is a delay for their
    // ID to be added. Add a slight delay so the ID can appear.
    base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE,
        base::BindOnce(&DeviceUser::HandleUserPolicyAndNotifyListeners,
                       weak_ptr_factory_.GetWeakPtr(), username,
                       directory_path),
        base::Seconds(2));
  }

  return false;
}

absl::StatusOr<enterprise_management::PolicyData> DeviceUser::RetrievePolicy(
    login_manager::PolicyAccountType account_type,
    const std::string& account_id) {
  login_manager::PolicyDescriptor descriptor;
  descriptor.set_account_type(account_type);
  descriptor.set_account_id(account_id);
  descriptor.set_domain(login_manager::POLICY_DOMAIN_CHROME);
  std::string account_type_string =
      account_type == login_manager::PolicyAccountType::ACCOUNT_TYPE_DEVICE
          ? "device"
          : "user";

  brillo::ErrorPtr error;
  std::vector<uint8_t> out_blob;
  std::string descriptor_string = descriptor.SerializeAsString();
  if (!session_manager_->RetrievePolicyEx(
          std::vector<uint8_t>(descriptor_string.begin(),
                               descriptor_string.end()),
          &out_blob, &error) ||
      error.get()) {
    return absl::InternalError("Failed to retrieve " + account_type_string +
                               " policy " + error->GetMessage());
  }
  enterprise_management::PolicyFetchResponse response;
  if (!response.ParseFromArray(out_blob.data(), out_blob.size())) {
    return absl::InternalError("Failed to parse policy response for " +
                               account_type_string);
  }

  enterprise_management::PolicyData policy_data;
  if (!policy_data.ParseFromArray(response.policy_data().data(),
                                  response.policy_data().size())) {
    return absl::InternalError("Failed to parse policy data for " +
                               account_type_string);
  }

  return policy_data;
}

bool DeviceUser::IsAffiliated(
    const enterprise_management::PolicyData& user_policy) {
  std::string user_id = "unset";
  if (user_policy.user_affiliation_ids_size() >= 1) {
    user_id = user_policy.user_affiliation_ids()[0];
    if (user_policy.user_affiliation_ids_size() > 1) {
      // There should only be 1 ID in the list.
      LOG(ERROR) << "Greater than 1 User ID. Count = "
                 << user_policy.user_affiliation_ids_size();
    }
  }

  return user_id == device_id_;
}

bool DeviceUser::SetDeviceUserIfLocalAccount(std::string& username) {
  base::expected<DeviceAccountType, policy::GetDeviceLocalAccountTypeError>
      account_type = policy::GetDeviceLocalAccountType(&username);

  if (!account_type.has_value()) {
    return false;
  }

  auto it = local_account_map_.find(account_type.value());
  if (it == local_account_map_.end()) {
    LOG(ERROR) << "Unrecognized local account " << account_type.value();
    device_user_ = device_user::kUnknown;
  } else {
    device_user_ = it->second;
  }

  return true;
}

void DeviceUser::HandleUserPolicyAndNotifyListeners(
    std::string username, base::FilePath user_directory) {
  bool directory_exists = false;
  if (base::DirectoryExists(user_directory) ||
      base::CreateDirectory(user_directory)) {
    directory_exists = true;
  } else {
    LOG(ERROR)
        << "Failed to create user directory. Not saving affiliation status.";
  }

  // Retrieve user policy information.
  auto response = RetrievePolicy(login_manager::ACCOUNT_TYPE_USER, username);
  if (!response.ok()) {
    device_user_ = device_user::kUnknown;
    LOG(ERROR) << response.status();
  } else {
    auto policy_data = response.value();

    // Fill in device_user if user is affiliated.
    if (IsAffiliated(policy_data)) {
      device_user_ = username;
      user_directory = user_directory.Append("affiliated");
      // Do not store the real name on the device, just mark as affiliated.
      if (directory_exists &&
          !base::ImportantFileWriter::WriteFileAtomically(user_directory, "")) {
        LOG(ERROR) << "Failed to write username to file";
      }
    } else {
      device_user_ = device_user::kUnaffiliatedPrefix +
                     base::Uuid::GenerateRandomV4().AsLowercaseString();
      user_directory = user_directory.Append("unaffiliated");
      if (directory_exists && !base::ImportantFileWriter::WriteFileAtomically(
                                  user_directory, device_user_)) {
        LOG(ERROR) << "Failed to write username to file";
      }
    }
  }

  device_user_ready_ = true;
  for (auto& cb : on_device_user_ready_cbs_) {
    std::move(cb).Run(device_user_);
  }
  on_device_user_ready_cbs_.clear();

  // Notify listeners.
  for (auto cb : session_change_listeners_) {
    cb.Run(kStarted);
  }
}

bool DeviceUser::GetIsUnaffiliated() {
  // If there is no device user or it is one of the managed local accounts then
  // it is considered affiliated.
  const std::unordered_set<std::string> reporting_values = {
      device_user::kEmpty, device_user::kManagedGuest, device_user::kKioskApp,
      device_user::kKioskAndroidApp};
  // If the user is unaffiliated their name will be a UUID. If they are
  // affiliated it will be their email which contains the @ symbol.
  return (!reporting_values.contains(device_user_) &&
          device_user_.find("@") == std::string::npos);
}

std::string DeviceUser::GetUsernameBasedOnAffiliation(
    const std::string& username, const std::string& sanitized_username) {
  base::FilePath directory_path =
      root_path_.Append(kSecagentdDirectory).Append(sanitized_username);

  if (base::DirectoryExists(directory_path)) {
    int64_t file_size;
    std::string uuid;
    if (base::PathExists(directory_path.Append("affiliated"))) {
      return username;
    } else if (!base::GetFileSize(directory_path.Append("unaffiliated"),
                                  &file_size) ||
               file_size == 0) {
      LOG(ERROR) << "Failed to get username file size.";
      return device_user::kUnknown;
    } else if (!base::ReadFileToString(directory_path.Append("unaffiliated"),
                                       &uuid) ||
               username.empty()) {
      LOG(ERROR) << "Failed to read uuid.";
      return device_user::kUnknown;
    } else {
      return uuid;
    }
  }

  return device_user::kUnknown;
}

void DeviceUser::SetFlushCallback(base::RepeatingCallback<void()> cb) {
  flush_cb_ = std::move(cb);
}
}  // namespace secagentd
