// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_STORAGE_ENCRYPTED_CONTAINER_FAKE_ENCRYPTED_CONTAINER_H_
#define CRYPTOHOME_STORAGE_ENCRYPTED_CONTAINER_FAKE_ENCRYPTED_CONTAINER_H_

#include "cryptohome/storage/encrypted_container/backing_device.h"

#include <memory>
#include <optional>

#include <gmock/gmock.h>

#include "cryptohome/storage/encrypted_container/encrypted_container_factory.h"

namespace cryptohome {

class FakeEncryptedContainer : public EncryptedContainer {
 public:
  FakeEncryptedContainer(EncryptedContainerType type,
                         const base::FilePath& device_path)
      : exists_(false), type_(type), backing_device_path_(device_path) {}

  ~FakeEncryptedContainer() {}

  bool Purge() override {
    if (!exists_) {
      return false;
    }
    exists_ = false;
    return true;
  }

  bool Setup(const FileSystemKey& encryption_key) override {
    exists_ = true;
    return true;
  }

  bool Teardown() override {
    if (!exists_) {
      return false;
    }
    exists_ = false;
    return true;
  }

  bool EvictKey() override { return type_ != EncryptedContainerType::kDmcrypt; }
  bool RestoreKey(const FileSystemKey& encryption_key) override {
    return type_ != EncryptedContainerType::kDmcrypt;
  }

  bool Exists() override { return exists_; }

  EncryptedContainerType GetType() const override { return type_; }

  bool Reset() override {
    if (!exists_) {
      return false;
    }
    return true;
  }

  base::FilePath GetPath() const override { return GetBackingLocation(); }
  base::FilePath GetBackingLocation() const override {
    return backing_device_path_;
  }

 private:
  bool exists_;
  EncryptedContainerType type_;
  base::FilePath backing_device_path_;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_STORAGE_ENCRYPTED_CONTAINER_FAKE_ENCRYPTED_CONTAINER_H_
