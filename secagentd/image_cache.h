// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SECAGENTD_IMAGE_CACHE_H_
#define SECAGENTD_IMAGE_CACHE_H_
#include <list>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/status/statusor.h"
#include "base/containers/lru_cache.h"
#include "base/files/file_path.h"
#include "base/memory/ref_counted.h"
#include "base/memory/scoped_refptr.h"
#include "base/synchronization/lock.h"
#include "secagentd/bpf/bpf_types.h"
#include "secagentd/device_user.h"
#include "secagentd/metrics_sender.h"
#include "secagentd/proto/security_xdr_events.pb.h"

namespace secagentd {

namespace testing {
class ProcessCacheTestFixture;
}

class ImageCacheInterface
    : public base::RefCountedThreadSafe<ImageCacheInterface> {
 public:
  virtual ~ImageCacheInterface() = default;
  struct ImageCacheKeyType {
    uint64_t inode_device_id;
    uint64_t inode;
    bpf::cros_timespec mtime;
    bpf::cros_timespec ctime;
    bool operator<(const ImageCacheKeyType& rhs) const {
      return std::tie(inode_device_id, inode, mtime.tv_sec, mtime.tv_nsec,
                      ctime.tv_sec, ctime.tv_nsec) <
             std::tie(rhs.inode_device_id, rhs.inode, rhs.mtime.tv_sec,
                      rhs.mtime.tv_nsec, rhs.ctime.tv_sec, rhs.ctime.tv_nsec);
    }
  };
  struct HashValue {
    std::string sha256;
    bool sha256_is_partial;
  };

  // If the SHA256 for the file identified by image_key is found in the
  // cache then immediately return the result otherwise compute the
  // SHA256 using the filename and the namespace pid and update the
  // internal cache afterwards.
  virtual absl::StatusOr<HashValue> InclusiveGetImage(
      const ImageCacheKeyType& image_key,
      uint64_t pid_for_setns,
      const base::FilePath& image_path_in_ns) = 0;
  // Returns a hashable and statable path of the given image path in the current
  // (i.e init) mount namespace.
  virtual absl::StatusOr<base::FilePath> GetPathInCurrentMountNs(
      uint64_t pid_for_setns,
      const base::FilePath& image_path_in_pids_ns) const = 0;
};

class ImageCache : public ImageCacheInterface {
 public:
  ImageCache();
  using InternalImageCacheType = base::LRUCache<ImageCacheKeyType, HashValue>;

  absl::StatusOr<HashValue> InclusiveGetImage(
      const ImageCacheKeyType& image_key,
      uint64_t pid_for_setns,
      const base::FilePath& image_path_in_ns) override;
  // Returns a hashable and statable path of the given image path in the current
  // (i.e init) mount namespace.
  absl::StatusOr<base::FilePath> GetPathInCurrentMountNs(
      uint64_t pid_for_setns,
      const base::FilePath& image_path_in_pids_ns) const override;

  template <typename... Args>

  static scoped_refptr<ImageCache> CreateForTesting(Args&&... args) {
    return base::WrapRefCounted(new ImageCache(std::forward<Args>(args)...));
  }

  // Appends an absolute path to the given base path. base::FilePath has a
  // DCHECK that avoids appending such absolute paths. We absolutely do need
  // to though because /proc/pid/exe is an absolute symlink that needs to be
  // resolved and appended to /proc/pid/root or root_path_.
  static absl::StatusOr<base::FilePath> SafeAppendAbsolutePath(
      const base::FilePath& path, const base::FilePath& abs_component);

  // Bypass the image cache and generate a SHA256 directly.
  static absl::StatusOr<std::string> GenerateImageHash(
      const base::FilePath& image_path_in_current_ns);

  ImageCache(const ImageCache&) = delete;
  ImageCache& operator=(const ImageCache&) = delete;

 private:
  friend class testing::ProcessCacheTestFixture;
  explicit ImageCache(base::FilePath path);
  const base::FilePath root_path_;
  base::Lock cache_lock_;
  std::unique_ptr<InternalImageCacheType> cache_;
};
}  // namespace secagentd
#endif  // SECAGENTD_IMAGE_CACHE_H_
