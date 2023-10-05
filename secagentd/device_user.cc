// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "secagentd/device_user.h"

#include <list>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/important_file_writer.h"
#include "base/functional/bind.h"
#include "base/functional/callback_forward.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/time/time.h"
#include "base/types/expected.h"
#include "base/uuid.h"
#include "bindings/chrome_device_policy.pb.h"
#include "bindings/device_management_backend.pb.h"
#include "brillo/errors/error.h"
#include "policy/device_local_account_policy_util.h"

namespace secagentd {

DeviceUser::DeviceUser(
    std::unique_ptr<org::chromium::SessionManagerInterfaceProxyInterface>
        session_manager)
    : DeviceUser(std::move(session_manager), base::FilePath("/")) {}

DeviceUser::DeviceUser(
    std::unique_ptr<org::chromium::SessionManagerInterfaceProxyInterface>
        session_manager,
    const base::FilePath& root_path)
    : weak_ptr_factory_(this),
      session_manager_(std::move(session_manager)),
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
                base::BindOnce(&DeviceUser::HandleRegistrationResult,
                               weak_ptr));
            session_manager->GetObjectProxy()->SetNameOwnerChangedCallback(
                base::BindRepeating(&DeviceUser::OnSessionManagerNameChange,
                                    weak_ptr));
          },
          session_manager_.get(), weak_ptr_factory_.GetWeakPtr()));
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
  device_user_ = "";
}

std::string DeviceUser::GetDeviceUser() {
  return device_user_;
}

std::list<std::string> DeviceUser::GetUsernamesForRedaction() {
  return redacted_usernames_;
}

void DeviceUser::HandleRegistrationResult(const std::string& interface,
                                          const std::string& signal,
                                          bool success) {
  if (!success) {
    LOG(ERROR) << "Callback registration failed for dbus signal: " << signal
               << " on interface: " << interface;
    device_user_ = "Unknown";
  } else {
    UpdateDeviceId();
    UpdateDeviceUser();
  }
}

void DeviceUser::OnSessionStateChange(const std::string& state) {
  if (state == kStarted) {
    UpdateDeviceId();
    if (!UpdateDeviceUser()) {
      return;
    }
  } else if (state == kStopping) {
    device_user_ = "";
  } else if (state == kStopped) {
    device_user_ = "";
  }

  for (auto cb : session_change_listeners_) {
    cb.Run(state);
  }
}

void DeviceUser::UpdateDeviceId() {
  if (device_id_ != "") {
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
    device_user_ = "Unknown";
    // Do not exit method because possible that it is user session.
    LOG(ERROR) << "Failed to deterimine if guest session "
               << error->GetMessage();
  } else if (is_guest) {
    device_user_ = "GuestUser";
    return true;
  }

  // Retrieve the device username.
  std::string username;
  std::string sanitized;
  if (!session_manager_->RetrievePrimarySession(&username, &sanitized,
                                                &error) ||
      error.get()) {
    device_user_ = "Unknown";
    LOG(ERROR) << "Failed to retrieve primary session " << error->GetMessage();
    return true;
  } else {
    // No active session.
    if (username.empty()) {
      // Only set as empty when Guest session retrieval succeeds.
      if (device_user_ != "Unknown") {
        device_user_ = "";
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

    // Check if username file exists in daemon-store.
    std::string username_file_path =
        "run/daemon-store/secagentd/" + sanitized + "/username";
    base::FilePath username_file = root_path_.Append(username_file_path);
    if (base::PathExists(username_file)) {
      int64_t file_size;
      if (!base::GetFileSize(username_file, &file_size) || file_size == 0) {
        LOG(ERROR)
            << "Failed to get username file size. Checking policy instead";
      } else if (!base::ReadFileToString(username_file, &username) ||
                 username.empty()) {
        LOG(ERROR) << "Failed to read username. Checking policy instead";
      } else {
        device_user_ = username;
        return true;
      }
    }

    // When a user logs in for the first time there is a delay for their
    // ID to be added. Add a slight delay so the ID can appear.
    base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE,
        base::BindOnce(&DeviceUser::HandleUserPolicyAndNotifyListeners,
                       weak_ptr_factory_.GetWeakPtr(), username, username_file),
        base::Seconds(2));
    return false;
  }
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
    device_user_ = "Unknown";
  } else {
    device_user_ = it->second;
  }

  return true;
}

void DeviceUser::HandleUserPolicyAndNotifyListeners(
    std::string username, base::FilePath username_file) {
  // Retrieve user policy information.
  auto response = RetrievePolicy(login_manager::ACCOUNT_TYPE_USER, username);
  if (!response.ok()) {
    device_user_ = "Unknown";
    LOG(ERROR) << response.status();
    return;
  }
  auto policy_data = response.value();

  // Fill in device_user if user is affiliated.
  if (IsAffiliated(policy_data)) {
    device_user_ = username;
  } else {
    device_user_ = base::Uuid::GenerateRandomV4().AsLowercaseString();
  }
  if (!base::ImportantFileWriter::WriteFileAtomically(username_file,
                                                      device_user_)) {
    LOG(ERROR) << "Failed to write username to file";
  }

  // Notify listeners.
  for (auto cb : session_change_listeners_) {
    cb.Run(kStarted);
  }
}

}  // namespace secagentd
