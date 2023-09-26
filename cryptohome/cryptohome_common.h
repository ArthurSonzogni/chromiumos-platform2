// Copyright 2012 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_CRYPTOHOME_COMMON_H_
#define CRYPTOHOME_CRYPTOHOME_COMMON_H_

#include <stddef.h>

namespace cryptohome {

// Constants used in both service.cc and userdataauth.cc
inline constexpr char kPublicMountSaltFilePath[] = "/var/lib/public_mount_salt";

// The default symmetric key size for cryptohome is the ecryptfs default
inline constexpr size_t kCryptohomeDefaultKeySize = 64;
inline constexpr size_t kCryptohomeDefaultKeySignatureSize = 8;
inline constexpr size_t kCryptohomeDefaultKeySaltSize = 8;
inline constexpr size_t kCryptohomeAesKeyBytes = 16;
// The default salt length for the user salt
inline constexpr size_t kCryptohomeDefaultSaltLength = 16;
inline constexpr size_t kCryptohomePwnameBufLength = 1024;
inline constexpr size_t kCryptohomeChapsKeyLength = 16;  // AES block size
inline constexpr size_t kCryptohomeResetSeedLength = 32;
// Always 32 bytes per the firmware.
inline constexpr size_t kCryptohomeResetSecretLength = 32;
// UserSecretStash file system encryption keys are 512 bits.
inline constexpr size_t kCryptohomeDefault512BitKeySize = 64;

}  // namespace cryptohome

#endif  // CRYPTOHOME_CRYPTOHOME_COMMON_H_
