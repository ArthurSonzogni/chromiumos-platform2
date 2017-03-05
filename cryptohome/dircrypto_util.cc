// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/dircrypto_util.h"

#include <string>

#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>

extern "C" {
#include <ext2fs/ext2_fs.h>
#include <keyutils.h>
}

#include <base/files/file_path.h>
#include <base/files/scoped_file.h>
#include <base/posix/eintr_wrapper.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <brillo/secure_blob.h>

// Add missing crypto ioctls
#define EXT4_IOC_SET_ENCRYPTION_POLICY  \
  _IOR('f', 19, struct ext4_encryption_policy)
#define EXT4_IOC_GET_ENCRYPTION_POLICY  \
  _IOW('f', 21, struct ext4_encryption_policy)

namespace dircrypto {

namespace {

constexpr char kKeyType[] = "logon";
constexpr char kKeyNamePrefix[] = "ext4:";
constexpr char kKeyringName[] = "dircrypt";

}  // namespace

bool SetDirectoryKey(const base::FilePath& dir,
                     const brillo::SecureBlob& key_descriptor) {
  DCHECK_EQ(static_cast<size_t>(EXT4_KEY_DESCRIPTOR_SIZE),
            key_descriptor.size());
  base::ScopedFD fd(HANDLE_EINTR(open(dir.value().c_str(),
                                      O_RDONLY | O_DIRECTORY)));
  if (!fd.is_valid()) {
    PLOG(ERROR) << "Ext4: Invalid directory" << dir.value();
    return false;
  }
  ext4_encryption_policy policy = {};
  policy.version = 0;
  policy.contents_encryption_mode = EXT4_ENCRYPTION_MODE_AES_256_XTS;
  policy.filenames_encryption_mode = EXT4_ENCRYPTION_MODE_AES_256_CTS;
  policy.flags = 0;
  memcpy(policy.master_key_descriptor, key_descriptor.data(),
         EXT4_KEY_DESCRIPTOR_SIZE);
  if (ioctl(fd.get(), EXT4_IOC_SET_ENCRYPTION_POLICY, &policy) < 0) {
    PLOG(ERROR) << "Failed to set the encryption policy of " << dir.value();
    return false;
  }
  return true;
}

KeyState GetDirectoryKeyState(const base::FilePath& dir) {
  base::ScopedFD fd(HANDLE_EINTR(open(dir.value().c_str(),
                                      O_RDONLY | O_DIRECTORY)));
  if (!fd.is_valid()) {
    PLOG(ERROR) << "Ext4: Invalid directory" << dir.value();
    return KeyState::UNKNOWN;
  }
  ext4_encryption_policy policy = {};
  if (ioctl(fd.get(), EXT4_IOC_GET_ENCRYPTION_POLICY, &policy) < 0) {
    switch (errno) {
      case ENODATA:
      case ENOENT:
        return KeyState::NO_KEY;
      case ENOTTY:
      case EOPNOTSUPP:
        return KeyState::NOT_SUPPORTED;
      default:
        PLOG(ERROR) << "Failed to get the encryption policy of " << dir.value();
        return KeyState::UNKNOWN;
    }
  }
  return KeyState::ENCRYPTED;
}

key_serial_t AddKeyToKeyring(const brillo::SecureBlob& key,
                             const brillo::SecureBlob& key_descriptor) {
  if (key.size() > EXT4_MAX_KEY_SIZE ||
      key_descriptor.size() != EXT4_KEY_DESCRIPTOR_SIZE) {
    LOG(ERROR) << "Invalid arguments: key.size() = " << key.size()
               << "key_descriptor.size() = " << key_descriptor.size();
    return kInvalidKeySerial;
  }
  key_serial_t keyring = keyctl_search(
      KEY_SPEC_SESSION_KEYRING, "keyring", kKeyringName, 0);
  if (keyring == kInvalidKeySerial) {
    PLOG(ERROR) << "keyctl_search failed";
    return kInvalidKeySerial;
  }
  ext4_encryption_key ext4_key = {};
  ext4_key.mode = EXT4_ENCRYPTION_MODE_AES_256_XTS;
  memcpy(ext4_key.raw, key.char_data(), key.size());
  ext4_key.size = key.size();
  std::string key_name = kKeyNamePrefix + base::ToLowerASCII(
      base::HexEncode(key_descriptor.data(), key_descriptor.size()));
  key_serial_t key_serial = add_key(kKeyType, key_name.c_str(), &ext4_key,
                                    sizeof(ext4_key), keyring);
  if (key_serial == kInvalidKeySerial) {
    PLOG(ERROR) << "Failed to insert key into keyring";
    return kInvalidKeySerial;
  }
  return key_serial;
}

bool UnlinkKey(key_serial_t key) {
  key_serial_t keyring = keyctl_search(
      KEY_SPEC_SESSION_KEYRING, "keyring", kKeyringName, 0);
  if (keyring == kInvalidKeySerial) {
    PLOG(ERROR) << "keyctl_search failed";
    return false;
  }
  if (keyctl_unlink(key, keyring) == -1) {
    PLOG(ERROR) << "Failed to unlink the key.";
    return false;
  }
  return true;
}

}  // namespace dircrypto
