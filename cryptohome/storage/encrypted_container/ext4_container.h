// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_STORAGE_ENCRYPTED_CONTAINER_EXT4_CONTAINER_H_
#define CRYPTOHOME_STORAGE_ENCRYPTED_CONTAINER_EXT4_CONTAINER_H_

#include "cryptohome/storage/encrypted_container/encrypted_container.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <metrics/metrics_library.h>

#include "cryptohome/platform.h"
#include "cryptohome/storage/encrypted_container/filesystem_key.h"
#include "cryptohome/storage/keyring/keyring.h"

namespace cryptohome {

// `DmcryptContainer` is a block-level encrypted container, complete with its
// own filesystem (by default ext4). The backing storage for the dm-crypt
// container is currently a loopback device over a sparse file.
class Ext4Container : public EncryptedContainer {
 public:
  Ext4Container(const Ext4FileSystemConfig& config,
                std::unique_ptr<EncryptedContainer> backing_container,
                Platform* platform,
                MetricsLibraryInterface* metrics);

  ~Ext4Container() {}

  bool Purge() override;

  bool Setup(const FileSystemKey& encryption_key) override;

  bool EvictKey() override { return backing_container_->EvictKey(); }

  bool RestoreKey(const FileSystemKey& encryption_key) override {
    return backing_container_->RestoreKey(encryption_key);
  }

  bool Teardown() override { return backing_container_->Teardown(); }

  bool Exists() override;

  EncryptedContainerType GetType() const override {
    // Filesystem is not important since this layer is not encrypted.
    return backing_container_->GetType();
  }

  bool Reset() override {
    LOG(ERROR) << "Attempted to reset a filesystem container is not allowed.";
    return false;
  }

  bool SetLazyTeardownWhenUnused() override {
    return backing_container_->SetLazyTeardownWhenUnused();
  }

  bool IsLazyTeardownSupported() const override {
    return backing_container_->IsLazyTeardownSupported();
  }

  // Same location has the backing device.
  base::FilePath GetPath() const override { return GetBackingLocation(); }
  base::FilePath GetBackingLocation() const override {
    return backing_container_->GetPath();
  }

 private:
  // Configuration for the ext4 filesystem.
  const std::vector<std::string> mkfs_opts_;
  const std::vector<std::string> tune2fs_opts_;
  RecoveryType recovery_;

  // Backing device for the file system container.
  std::unique_ptr<EncryptedContainer> backing_container_;

  Platform* platform_;

  MetricsLibraryInterface* metrics_;

  // Store the prefix to use for filesystem metrics, if the container is
  // tracked on UMA, an empty string otherwise.
  std::string metrics_prefix_;

  // To send data to UMA. Since templated methods must be implemented in the
  // headers, implements all the needed functions here.

  // Sends a regular (exponential) histogram sample to Chrome for
  // transport to UMA. See MetricsLibrary::SendToUMA in
  // metrics_library.h for a description of the arguments.
  void SendSample(
      const std::string& name, int sample, int min, int max, int nbuckets) {
    if (metrics_prefix_.empty() || !metrics_)
      return;

    metrics_->SendToUMA(metrics_prefix_ + name, sample, min, max, nbuckets);
  }

  // Sends a bool to Chrome for transport to UMA. See
  // MetricsLibrary::SendBoolToUMA in metrics_library.h for a description of the
  // arguments.
  void SendBool(const std::string& name, int sample) {
    if (metrics_prefix_.empty() || !metrics_)
      return;

    metrics_->SendBoolToUMA(metrics_prefix_ + name, sample);
  }

  // Sends an enum to Chrome for transport to UMA. See
  // MetricsLibrary::SendEnumToUMA in metrics_library.h for a description of the
  // arguments.
  template <typename T>
  void SendEnum(const std::string& name, T sample) {
    if (metrics_prefix_.empty() || !metrics_)
      return;

    metrics_->SendEnumToUMA(metrics_prefix_ + name, sample);
  }
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_STORAGE_ENCRYPTED_CONTAINER_EXT4_CONTAINER_H_
