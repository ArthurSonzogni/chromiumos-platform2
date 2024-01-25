// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBSTORAGE_STORAGE_CONTAINER_FAKE_STORAGE_CONTAINER_H_
#define LIBSTORAGE_STORAGE_CONTAINER_FAKE_STORAGE_CONTAINER_H_

#include "libstorage/storage_container/backing_device.h"

#include <memory>
#include <optional>

#include <gmock/gmock.h>

#include "libstorage/storage_container/storage_container_factory.h"

namespace libstorage {

class FakeStorageContainer : public StorageContainer {
 public:
  FakeStorageContainer(StorageContainerType type,
                       const base::FilePath& device_path)
      : exists_(false), type_(type), backing_device_path_(device_path) {}

  ~FakeStorageContainer() {}

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

  bool EvictKey() override { return type_ != StorageContainerType::kDmcrypt; }
  bool RestoreKey(const FileSystemKey& encryption_key) override {
    return type_ != StorageContainerType::kDmcrypt;
  }

  bool Exists() override { return exists_; }

  StorageContainerType GetType() const override { return type_; }

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
  StorageContainerType type_;
  base::FilePath backing_device_path_;
};

}  // namespace libstorage

#endif  // LIBSTORAGE_STORAGE_CONTAINER_FAKE_STORAGE_CONTAINER_H_
