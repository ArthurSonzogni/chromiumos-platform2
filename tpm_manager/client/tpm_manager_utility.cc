// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tpm_manager/client/tpm_manager_utility.h"

#include <string>

#include <base/bind.h>
#include <base/logging.h>
#include <base/message_loop/message_pump_type.h>

namespace {
constexpr base::TimeDelta kDefaultTimeout = base::TimeDelta::FromMinutes(5);
}  // namespace

namespace tpm_manager {

namespace {
// Singleton and lock for TpmManagerUtility.
TpmManagerUtility* singleton;
base::Lock singleton_lock;
}  // namespace

TpmManagerUtility::TpmManagerUtility(
    org::chromium::TpmManagerProxyInterface* tpm_owner,
    org::chromium::TpmNvramProxyInterface* tpm_nvram)
    : tpm_owner_(tpm_owner), tpm_nvram_(tpm_nvram) {}

bool TpmManagerUtility::Initialize() {
  if (!tpm_manager_thread_.IsRunning() &&
      !tpm_manager_thread_.StartWithOptions(base::Thread::Options(
          base::MessagePumpType::IO, 0 /* Uses default stack size. */))) {
    LOG(ERROR) << "Failed to start tpm_manager thread.";
    return false;
  }
  if (!tpm_owner_ || !tpm_nvram_) {
    base::WaitableEvent event(base::WaitableEvent::ResetPolicy::MANUAL,
                              base::WaitableEvent::InitialState::NOT_SIGNALED);
    tpm_manager_thread_.task_runner()->PostTask(
        FROM_HERE, base::Bind(&TpmManagerUtility::InitializationTask,
                              base::Unretained(this), &event));
    event.Wait();
  }
  if (!tpm_owner_ || !tpm_nvram_) {
    LOG(ERROR) << "Failed to initialize tpm_managerd clients.";
    return false;
  }

  return true;
}

bool TpmManagerUtility::TakeOwnership() {
  tpm_manager::TakeOwnershipRequest request;
  tpm_manager::TakeOwnershipReply reply;
  SendTpmOwnerRequestAndWait(
      &org::chromium::TpmManagerProxyInterface::TakeOwnershipAsync, request,
      &reply);
  if (reply.status() != tpm_manager::STATUS_SUCCESS) {
    LOG(ERROR) << __func__ << ": Failed to take ownership.";
    return false;
  }
  return true;
}

bool TpmManagerUtility::GetTpmStatus(bool* is_enabled,
                                     bool* is_owned,
                                     LocalData* local_data) {
  tpm_manager::GetTpmStatusRequest request;
  tpm_manager::GetTpmStatusReply tpm_status;
  SendTpmOwnerRequestAndWait(
      &org::chromium::TpmManagerProxyInterface::GetTpmStatusAsync, request,
      &tpm_status);
  if (tpm_status.status() != tpm_manager::STATUS_SUCCESS) {
    LOG(ERROR) << __func__ << ": Failed to read TPM state from tpm_managerd.";
    return false;
  }
  *is_enabled = tpm_status.enabled();
  *is_owned = tpm_status.owned();
  tpm_status.mutable_local_data()->Swap(local_data);
  return true;
}

bool TpmManagerUtility::GetTpmNonsensitiveStatus(
    bool* is_enabled,
    bool* is_owned,
    bool* is_owner_password_present,
    bool* has_reset_lock_permissions) {
  tpm_manager::GetTpmNonsensitiveStatusRequest request;
  tpm_manager::GetTpmNonsensitiveStatusReply tpm_status;
  SendTpmOwnerRequestAndWait(
      &org::chromium::TpmManagerProxyInterface::GetTpmNonsensitiveStatusAsync,
      request, &tpm_status);
  if (tpm_status.status() != tpm_manager::STATUS_SUCCESS) {
    LOG(ERROR) << __func__
               << ": Failed to read TPM nonsensitive state from tpm_managerd.";
    return false;
  }
  if (is_enabled) {
    *is_enabled = tpm_status.is_enabled();
  }
  if (is_owned) {
    *is_owned = tpm_status.is_owned();
  }
  if (is_owner_password_present) {
    *is_owner_password_present = tpm_status.is_owner_password_present();
  }
  if (has_reset_lock_permissions) {
    *has_reset_lock_permissions = tpm_status.has_reset_lock_permissions();
  }
  return true;
}

bool TpmManagerUtility::GetVersionInfo(uint32_t* family,
                                       uint64_t* spec_level,
                                       uint32_t* manufacturer,
                                       uint32_t* tpm_model,
                                       uint64_t* firmware_version,
                                       std::string* vendor_specific) {
  if (!family || !spec_level || !manufacturer || !tpm_model ||
      !firmware_version || !vendor_specific) {
    LOG(ERROR) << __func__ << ": some input args are not initialized.";
    return false;
  }
  tpm_manager::GetVersionInfoRequest request;
  tpm_manager::GetVersionInfoReply version_info;
  SendTpmOwnerRequestAndWait(
      &org::chromium::TpmManagerProxyInterface::GetVersionInfoAsync, request,
      &version_info);
  if (version_info.status() != tpm_manager::STATUS_SUCCESS) {
    LOG(ERROR) << __func__ << ": failed to get version info from tpm_managerd.";
    return false;
  }

  *family = version_info.family();
  *spec_level = version_info.spec_level();
  *manufacturer = version_info.manufacturer();
  *tpm_model = version_info.tpm_model();
  *firmware_version = version_info.firmware_version();
  *vendor_specific = version_info.vendor_specific();
  return true;
}

bool TpmManagerUtility::RemoveOwnerDependency(const std::string& dependency) {
  tpm_manager::RemoveOwnerDependencyRequest request;
  tpm_manager::RemoveOwnerDependencyReply reply;
  request.set_owner_dependency(dependency);
  SendTpmOwnerRequestAndWait(
      &org::chromium::TpmManagerProxyInterface::RemoveOwnerDependencyAsync,
      request, &reply);
  if (reply.status() != tpm_manager::STATUS_SUCCESS) {
    LOG(ERROR) << __func__ << ": Failed to remove the dependency of "
               << dependency << ".";
    return false;
  }
  return true;
}

bool TpmManagerUtility::ClearStoredOwnerPassword() {
  tpm_manager::ClearStoredOwnerPasswordRequest request;
  tpm_manager::ClearStoredOwnerPasswordReply reply;
  SendTpmOwnerRequestAndWait(
      &org::chromium::TpmManagerProxyInterface::ClearStoredOwnerPasswordAsync,
      request, &reply);
  if (reply.status() != tpm_manager::STATUS_SUCCESS) {
    LOG(ERROR) << __func__ << ": Failed to clear owner password. ";
    return false;
  }
  return true;
}

void TpmManagerUtility::InitializationTask(base::WaitableEvent* completion) {
  CHECK(completion);
  CHECK(tpm_manager_thread_.task_runner()->BelongsToCurrentThread());

  dbus::Bus::Options options;
  options.bus_type = dbus::Bus::SYSTEM;
  bus_ = base::MakeRefCounted<dbus::Bus>(options);
  CHECK(bus_->Connect()) << "Failed to connect to system D-Bus";

  default_tpm_owner_ = std::make_unique<org::chromium::TpmManagerProxy>(bus_);
  default_tpm_nvram_ = std::make_unique<org::chromium::TpmNvramProxy>(bus_);

  default_tpm_owner_->RegisterSignalOwnershipTakenSignalHandler(
      base::Bind(&TpmManagerUtility::OnOwnershipTaken, base::Unretained(this)),
      base::Bind(&TpmManagerUtility::OnSignalConnected,
                 base::Unretained(this)));

  tpm_owner_ = default_tpm_owner_.get();
  tpm_nvram_ = default_tpm_nvram_.get();

  completion->Signal();
}

template <typename ReplyProtoType, typename MethodType>
void TpmManagerUtility::SendTpmManagerRequestAndWait(
    const MethodType& method, ReplyProtoType* reply_proto) {
  base::WaitableEvent event(base::WaitableEvent::ResetPolicy::MANUAL,
                            base::WaitableEvent::InitialState::NOT_SIGNALED);
  auto callback = base::Bind(
      [](ReplyProtoType* target, base::WaitableEvent* completion,
         const ReplyProtoType& reply) {
        *target = reply;
        completion->Signal();
      },
      reply_proto, &event);
  auto error_callback = base::Bind(
      [](base::WaitableEvent* completion, brillo::Error* error) {
        LOG(ERROR) << "Failed to call ResetDictionaryAttackLock: "
                   << error->GetMessage();
        completion->Signal();
      },
      &event);
  tpm_manager_thread_.task_runner()->PostTask(
      FROM_HERE, base::Bind(method, callback, error_callback,
                            kDefaultTimeout.InMilliseconds()));
  event.Wait();
}

template <typename ReplyProtoType,
          typename RequestProtoType,
          typename MethodType>
void TpmManagerUtility::SendTpmOwnerRequestAndWait(
    const MethodType& method,
    const RequestProtoType& request_proto,
    ReplyProtoType* reply_proto) {
  SendTpmManagerRequestAndWait(
      base::Bind(method, base::Unretained(tpm_owner_), request_proto),
      reply_proto);
}

template <typename ReplyProtoType,
          typename RequestProtoType,
          typename MethodType>
void TpmManagerUtility::SendTpmNvramRequestAndWait(
    const MethodType& method,
    const RequestProtoType& request_proto,
    ReplyProtoType* reply_proto) {
  SendTpmManagerRequestAndWait(
      base::Bind(method, base::Unretained(tpm_nvram_), request_proto),
      reply_proto);
}

void TpmManagerUtility::ShutdownTask() {
  tpm_owner_ = nullptr;
  tpm_nvram_ = nullptr;
  default_tpm_owner_.reset(nullptr);
  default_tpm_nvram_.reset(nullptr);
}

bool TpmManagerUtility::GetDictionaryAttackInfo(int* counter,
                                                int* threshold,
                                                bool* lockout,
                                                int* seconds_remaining) {
  tpm_manager::GetDictionaryAttackInfoRequest request;
  tpm_manager::GetDictionaryAttackInfoReply reply;
  SendTpmOwnerRequestAndWait(
      &org::chromium::TpmManagerProxyInterface::GetDictionaryAttackInfoAsync,
      request, &reply);
  if (reply.status() != tpm_manager::STATUS_SUCCESS) {
    LOG(ERROR) << __func__
               << ": Failed to retreive the dictionary attack information.";
    return false;
  }

  *counter = reply.dictionary_attack_counter();
  *threshold = reply.dictionary_attack_threshold();
  *lockout = reply.dictionary_attack_lockout_in_effect();
  *seconds_remaining = reply.dictionary_attack_lockout_seconds_remaining();

  return true;
}

bool TpmManagerUtility::ResetDictionaryAttackLock() {
  tpm_manager::ResetDictionaryAttackLockRequest request;
  tpm_manager::ResetDictionaryAttackLockReply reply;
  SendTpmOwnerRequestAndWait(
      &org::chromium::TpmManagerProxyInterface::ResetDictionaryAttackLockAsync,
      request, &reply);
  if (reply.status() != tpm_manager::STATUS_SUCCESS) {
    LOG(ERROR) << __func__ << ": Failed to reset DA lock.";
    return false;
  }
  return true;
}

bool TpmManagerUtility::DefineSpace(uint32_t index,
                                    size_t size,
                                    bool write_define,
                                    bool bind_to_pcr0,
                                    bool firmware_readable) {
  tpm_manager::DefineSpaceRequest request;
  request.set_index(index);
  request.set_size(size);
  if (write_define) {
    request.add_attributes(tpm_manager::NVRAM_PERSISTENT_WRITE_LOCK);
  }
  if (bind_to_pcr0) {
    request.set_policy(tpm_manager::NVRAM_POLICY_PCR0);
  }
  if (firmware_readable) {
    request.add_attributes(tpm_manager::NVRAM_PLATFORM_READ);
  }
  tpm_manager::DefineSpaceReply reply;
  SendTpmNvramRequestAndWait(
      &org::chromium::TpmNvramProxyInterface::DefineSpaceAsync, request,
      &reply);
  if (reply.result() != tpm_manager::NVRAM_RESULT_SUCCESS) {
    LOG(ERROR) << __func__
               << ": Failed to define nvram space: " << reply.result();
    return false;
  }
  return true;
}

bool TpmManagerUtility::DestroySpace(uint32_t index) {
  tpm_manager::DestroySpaceRequest request;
  request.set_index(index);
  tpm_manager::DestroySpaceReply reply;
  SendTpmNvramRequestAndWait(
      &org::chromium::TpmNvramProxyInterface::DestroySpaceAsync, request,
      &reply);
  if (reply.result() != tpm_manager::NVRAM_RESULT_SUCCESS) {
    LOG(ERROR) << __func__
               << ": Failed to destroy nvram space: " << reply.result();
    return false;
  }
  return true;
}

bool TpmManagerUtility::WriteSpace(uint32_t index,
                                   const std::string& data,
                                   bool use_owner_auth) {
  tpm_manager::WriteSpaceRequest request;
  request.set_index(index);
  request.set_data(data);
  request.set_use_owner_authorization(use_owner_auth);
  tpm_manager::WriteSpaceReply reply;
  SendTpmNvramRequestAndWait(
      &org::chromium::TpmNvramProxyInterface::WriteSpaceAsync, request, &reply);
  if (reply.result() == tpm_manager::NVRAM_RESULT_SPACE_DOES_NOT_EXIST) {
    LOG(ERROR) << __func__ << ": NV Index [" << index << "] does not exist.";
    return false;
  }
  if (reply.result() != tpm_manager::NVRAM_RESULT_SUCCESS) {
    LOG(ERROR) << __func__ << ": Failed to write NV index [" << index << "]"
               << reply.result();
    return false;
  }
  return true;
}

bool TpmManagerUtility::ReadSpace(uint32_t index,
                                  bool use_owner_auth,
                                  std::string* output) {
  tpm_manager::ReadSpaceRequest request;
  request.set_index(index);
  request.set_use_owner_authorization(use_owner_auth);
  tpm_manager::ReadSpaceReply reply;
  SendTpmNvramRequestAndWait(
      &org::chromium::TpmNvramProxyInterface::ReadSpaceAsync, request, &reply);
  if (reply.result() == tpm_manager::NVRAM_RESULT_SPACE_DOES_NOT_EXIST) {
    LOG(ERROR) << __func__ << ": NV Index [" << index << "] does not exist.";
    return false;
  }
  if (reply.result() != tpm_manager::NVRAM_RESULT_SUCCESS) {
    LOG(ERROR) << __func__ << ": Failed to read NV index [" << index << "]"
               << reply.result();
    return false;
  }
  *output = reply.data();
  return true;
}

bool TpmManagerUtility::ListSpaces(std::vector<uint32_t>* spaces) {
  tpm_manager::ListSpacesRequest request;
  tpm_manager::ListSpacesReply reply;
  SendTpmNvramRequestAndWait(
      &org::chromium::TpmNvramProxyInterface::ListSpacesAsync, request, &reply);
  if (reply.result() != tpm_manager::NVRAM_RESULT_SUCCESS) {
    LOG(ERROR) << __func__
               << ": Failed to list nvram spaces: " << reply.result();
    return false;
  }
  spaces->clear();
  spaces->reserve(reply.index_list_size());
  for (int i = 0; i < reply.index_list_size(); ++i) {
    spaces->push_back(reply.index_list(i));
  }
  return true;
}

bool TpmManagerUtility::GetSpaceInfo(uint32_t index,
                                     uint32_t* size,
                                     bool* is_read_locked,
                                     bool* is_write_locked) {
  tpm_manager::GetSpaceInfoRequest request;
  request.set_index(index);
  tpm_manager::GetSpaceInfoReply reply;
  SendTpmNvramRequestAndWait(
      &org::chromium::TpmNvramProxyInterface::GetSpaceInfoAsync, request,
      &reply);
  if (reply.result() != tpm_manager::NVRAM_RESULT_SUCCESS) {
    LOG(ERROR) << __func__ << ": Failed to get space info for space " << index
               << ": " << reply.result();
    return false;
  }
  *size = reply.size();
  *is_read_locked = reply.is_read_locked();
  *is_write_locked = reply.is_write_locked();
  return true;
}

bool TpmManagerUtility::LockSpace(uint32_t index) {
  tpm_manager::LockSpaceRequest request;
  request.set_index(index);
  request.set_lock_write(true);
  tpm_manager::LockSpaceReply reply;
  SendTpmNvramRequestAndWait(
      &org::chromium::TpmNvramProxyInterface::LockSpaceAsync, request, &reply);
  if (reply.result() != tpm_manager::NVRAM_RESULT_SUCCESS) {
    LOG(ERROR) << __func__
               << ": Failed to lock nvram space: " << reply.result();
    return false;
  }
  return true;
}

bool TpmManagerUtility::GetOwnershipTakenSignalStatus(bool* is_successful,
                                                      bool* has_received,
                                                      LocalData* local_data) {
  base::AutoLock lock(ownership_signal_lock_);
  if (!is_connected_) {
    return false;
  }
  if (is_successful) {
    *is_successful = is_connection_successful_;
  }
  if (has_received) {
    *has_received = static_cast<bool>(ownership_taken_signal_);
  }
  // Copies |LocalData| when both the data source and destination is ready.
  if (ownership_taken_signal_ && local_data) {
    *local_data = ownership_taken_signal_->local_data();
  }
  return true;
}

void TpmManagerUtility::AddOwnershipCallback(
    OwnershipCallback ownership_callback) {
  base::AutoLock lock(ownership_callback_lock_);
  ownership_callbacks_.push_back(ownership_callback);
}

void TpmManagerUtility::OnOwnershipTaken(const OwnershipTakenSignal& signal) {
  LOG(INFO) << __func__ << ": Received |OwnershipTakenSignal|.";
  {
    base::AutoLock lock(ownership_signal_lock_);
    ownership_taken_signal_ = signal;
  }
  base::AutoLock lock(ownership_callback_lock_);
  for (auto& callback : ownership_callbacks_) {
    callback.Run();
  }
}

void TpmManagerUtility::OnSignalConnected(const std::string& /*interface_name*/,
                                          const std::string& /*signal_name*/,
                                          bool is_successful) {
  if (!is_successful) {
    LOG(ERROR) << __func__ << ": Failed to connect dbus signal.";
  } else {
    LOG(INFO) << __func__ << ": Connected dbus signal successfully.";
  }
  base::AutoLock lock(ownership_signal_lock_);
  is_connected_ = true;
  is_connection_successful_ = is_successful;
}

TpmManagerUtility* TpmManagerUtility::GetSingleton() {
  base::AutoLock lock(singleton_lock);
  if (!singleton) {
    singleton = new TpmManagerUtility();
    if (!singleton->Initialize()) {
      delete singleton;
      singleton = nullptr;
    }
  }
  return singleton;
}

}  // namespace tpm_manager
