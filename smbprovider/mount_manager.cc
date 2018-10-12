// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "smbprovider/mount_manager.h"

#include <algorithm>

#include <base/strings/string_util.h>
#include <base/time/tick_clock.h>

#include "smbprovider/smb_credential.h"
#include "smbprovider/smbprovider_helper.h"

namespace smbprovider {

namespace {

// Returns true if |buffer_length| is large enough to contain |str|.
bool CanBufferHoldString(const std::string& str, int32_t buffer_length) {
  return static_cast<int32_t>(str.size()) + 1 <= buffer_length;
}

// Returns true if |buffer_length| is large enough to contain |password|.
bool CanBufferHoldPassword(
    const std::unique_ptr<password_provider::Password>& password,
    int32_t buffer_length) {
  DCHECK(password);

  return static_cast<int32_t>(password->size()) + 1 <= buffer_length;
}

// Sets the first element in the buffer to be a null terminator.
void SetBufferEmpty(char* buffer) {
  DCHECK(buffer);

  buffer[0] = '\0';
}

// Copies |str| to |buffer| and adds a null terminator at the end.
void CopyStringToBuffer(const std::string& str, char* buffer) {
  DCHECK(buffer);

  strncpy(buffer, str.c_str(), str.size());
  buffer[str.size()] = '\0';
}

// Copies |password| to |buffer| and adds a null terminator at the end.
void CopyPasswordToBuffer(
    const std::unique_ptr<password_provider::Password>& password,
    char* buffer) {
  DCHECK(password);
  DCHECK(buffer);

  strncpy(buffer, password->GetRaw(), password->size());
  buffer[password->size()] = '\0';
}

// Checks that the credential can be inputted given the buffer sizes. Returns
// false if the buffers are too small or if the credential is empty.
bool CanInputCredential(int32_t workgroup_length,
                        int32_t username_length,
                        int32_t password_length,
                        const SmbCredential& credential) {
  if (!CanBufferHoldString(credential.workgroup, workgroup_length) ||
      !CanBufferHoldString(credential.username, username_length)) {
    LOG(ERROR) << "Credential buffers are too small for input.";
    return false;
  }

  if (credential.password &&
      !CanBufferHoldPassword(credential.password, password_length)) {
    LOG(ERROR) << "Password buffer is too small for input.";
    return false;
  }

  return true;
}

// Populates the |credential| into the specified buffers. CanInputCredential()
// should be called first in order to verify the buffers can contain the
// credential.
void PopulateCredential(const SmbCredential& credential,
                        char* workgroup_buffer,
                        char* username_buffer,
                        char* password_buffer) {
  DCHECK(workgroup_buffer);
  DCHECK(username_buffer);
  DCHECK(password_buffer);

  CopyStringToBuffer(credential.workgroup, workgroup_buffer);
  CopyStringToBuffer(credential.username, username_buffer);

  const bool empty_password = !credential.password;
  if (empty_password) {
    SetBufferEmpty(password_buffer);
  } else {
    CopyPasswordToBuffer(credential.password, password_buffer);
  }
}

}  // namespace

std::unique_ptr<password_provider::Password> GetPassword(
    const base::ScopedFD& password_fd) {
  size_t password_length = 0;

  // Read sizeof(size_t) bytes from the file to get the password length.
  bool success = base::ReadFromFD(password_fd.get(),
                                  reinterpret_cast<char*>(&password_length),
                                  sizeof(password_length));
  if (!success) {
    LOG(ERROR) << "Could not read password from file.";
    return std::unique_ptr<password_provider::Password>();
  }

  if (password_length == 0) {
    // Return empty password since there is no password.
    return std::unique_ptr<password_provider::Password>();
  }

  return password_provider::Password::CreateFromFileDescriptor(
      password_fd.get(), password_length);
}

MountManager::MountManager(std::unique_ptr<MountTracker> mount_tracker,
                           SambaInterfaceFactory samba_interface_factory)
    : mount_tracker_(std::move(mount_tracker)),
      samba_interface_factory_(std::move(samba_interface_factory)) {
  system_samba_interface_ =
      CreateSambaInterface(MountConfig(false /* enable_ntlm */));
}

MountManager::~MountManager() = default;

bool MountManager::IsAlreadyMounted(int32_t mount_id) const {
  DCHECK_GE(mount_id, 0);

  return mount_tracker_->IsAlreadyMounted(mount_id);
}

bool MountManager::IsAlreadyMounted(const std::string& mount_root) const {
  return mount_tracker_->IsAlreadyMounted(mount_root);
}

bool MountManager::AddMount(const std::string& mount_root,
                            SmbCredential credential,
                            const MountConfig& mount_config,
                            int32_t* mount_id) {
  DCHECK(mount_id);

  const bool mount_succeeded =
      mount_tracker_->AddMount(mount_root, std::move(credential),
                               CreateSambaInterface(mount_config), mount_id);

  if (mount_succeeded) {
    // After adding a new mount, remounts are disabled. This is only used as a
    // DCHECK to ensure remounts are not called after a new mount.
    can_remount_ = false;
  }

  return mount_succeeded;
}

bool MountManager::Remount(const std::string& mount_root,
                           int32_t mount_id,
                           SmbCredential credential,
                           const MountConfig& mount_config) {
  DCHECK(can_remount_);
  DCHECK_GE(mount_id, 0);

  return mount_tracker_->AddMountWithId(mount_root, std::move(credential),
                                        CreateSambaInterface(mount_config),
                                        mount_id);
}

bool MountManager::RemoveMount(int32_t mount_id) {
  DCHECK_GE(mount_id, 0);

  return mount_tracker_->RemoveMount(mount_id);
}

bool MountManager::GetFullPath(int32_t mount_id,
                               const std::string& entry_path,
                               std::string* full_path) const {
  DCHECK(full_path);

  return mount_tracker_->GetFullPath(mount_id, entry_path, full_path);
}

bool MountManager::GetMetadataCache(int32_t mount_id,
                                    MetadataCache** cache) const {
  DCHECK(cache);

  return mount_tracker_->GetMetadataCache(mount_id, cache);
}

std::string MountManager::GetRelativePath(int32_t mount_id,
                                          const std::string& full_path) const {
  return mount_tracker_->GetRelativePath(mount_id, full_path);
}

bool MountManager::GetSambaInterface(int32_t mount_id,
                                     SambaInterface** samba_interface) const {
  DCHECK(samba_interface);

  return mount_tracker_->GetSambaInterface(mount_id, samba_interface);
}

SambaInterface* MountManager::GetSystemSambaInterface() const {
  return system_samba_interface_.get();
}

std::unique_ptr<SambaInterface> MountManager::CreateSambaInterface(
    const MountConfig& mount_config) {
  return samba_interface_factory_.Run(this, mount_config);
}

bool MountManager::GetAuthentication(
    SambaInterface::SambaInterfaceId samba_interface_id,
    const std::string& share_path,
    char* workgroup,
    int32_t workgroup_length,
    char* username,
    int32_t username_length,
    char* password,
    int32_t password_length) const {
  DCHECK_GT(workgroup_length, 0);
  DCHECK_GT(username_length, 0);
  DCHECK_GT(password_length, 0);

  if (!mount_tracker_->IsAlreadyMounted(samba_interface_id)) {
    LOG(ERROR) << "Credential not found for SambaInterfaceId: "
               << samba_interface_id;

    SetBufferEmpty(workgroup);
    SetBufferEmpty(username);
    SetBufferEmpty(password);
    return false;
  }

  const SmbCredential& credential =
      mount_tracker_->GetCredential(samba_interface_id);

  if (!CanInputCredential(workgroup_length, username_length, password_length,
                          credential)) {
    LOG(ERROR) << "Buffers cannot support a credential for SambaInterfaceId: "
               << samba_interface_id;

    SetBufferEmpty(workgroup);
    SetBufferEmpty(username);
    SetBufferEmpty(password);
    return false;
  }

  PopulateCredential(credential, workgroup, username, password);
  return true;
}

SambaInterface::SambaInterfaceId MountManager::GetSystemSambaInterfaceId() {
  return system_samba_interface_->GetSambaInterfaceId();
}

}  // namespace smbprovider
