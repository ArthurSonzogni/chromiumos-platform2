// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/storage/mount_constants.h"

namespace cryptohome {

constexpr char kEphemeralCryptohomeDir[] = "/run/cryptohome";
constexpr char kSparseFileDir[] = "ephemeral_data";

constexpr char kDefaultSharedUser[] = "chronos";

constexpr char kCacheDir[] = "Cache";
constexpr char kDownloadsDir[] = "Downloads";
constexpr char kDownloadsBackupDir[] = "Downloads-backup";
constexpr char kMyFilesDir[] = "MyFiles";
constexpr char kGCacheDir[] = "GCache";
constexpr char kGCacheVersion1Dir[] = "v1";
constexpr char kGCacheVersion2Dir[] = "v2";
constexpr char kGCacheBlobsDir[] = "blobs";
constexpr char kGCacheTmpDir[] = "tmp";

constexpr char kUserHomeSuffix[] = "user";
constexpr char kRootHomeSuffix[] = "root";

constexpr char kEphemeralMountDir[] = "ephemeral_mount";
constexpr char kEphemeralMountType[] = "ext4";
constexpr char kEphemeralMountOptions[] = "";

constexpr char kEtcDaemonStoreBaseDir[] = "/etc/daemon-store/";
constexpr char kRunDaemonStoreBaseDir[] = "/run/daemon-store/";
constexpr char kRunDaemonStoreCacheBaseDir[] = "/run/daemon-store-cache/";

constexpr char kDaemonStoreCacheDir[] = ".cache";

// Each xattr is set to Android app internal data directory, contains
// 8-byte inode number of cache subdirectory.  See
// frameworks/base/core/java/android/app/ContextImpl.java
constexpr char kAndroidCacheInodeAttribute[] = "user.inode_cache";
constexpr char kAndroidCodeCacheInodeAttribute[] = "user.inode_code_cache";
constexpr char kTrackedDirectoryNameAttribute[] = "user.TrackedDirectoryName";
constexpr char kRemovableFileAttribute[] = "user.GCacheRemovable";

}  // namespace cryptohome
