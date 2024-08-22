// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "secagentd/image_cache.h"

#include <sys/stat.h>
#include <unistd.h>

#include <cinttypes>
#include <cstdint>
#include <list>
#include <memory>
#include <string>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "base/containers/lru_cache.h"
#include "base/files/file.h"
#include "base/files/file_path.h"
#include "base/logging.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_tokenizer.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/synchronization/lock.h"
#include "openssl/sha.h"
#include "secagentd/bpf/bpf_types.h"
namespace {

static const char kErrorFailedToRead[] = "Failed to read ";
static const char kErrorSslSha[] = "SSL SHA error";

using ImageCacheIfc = secagentd::ImageCacheInterface;

absl::StatusOr<ImageCacheIfc::HashValue> VerifyStatAndGenerateImageHash(
    const ImageCacheIfc::ImageCacheKeyType& image_key,
    const base::FilePath& image_path_in_current_ns) {
  auto hash =
      secagentd::ImageCache::GenerateImageHash(image_path_in_current_ns);
  if (!hash.ok()) {
    return hash.status();
  }
  base::stat_wrapper_t image_stat;
  if (base::File::Stat(image_path_in_current_ns, &image_stat) ||
      (image_stat.st_dev != image_key.inode_device_id) ||
      (image_stat.st_ino != image_key.inode) ||
      (image_stat.st_mtim.tv_sec != image_key.mtime.tv_sec) ||
      (image_stat.st_mtim.tv_nsec != image_key.mtime.tv_nsec) ||
      (image_stat.st_ctim.tv_sec != image_key.ctime.tv_sec) ||
      (image_stat.st_ctim.tv_nsec != image_key.ctime.tv_nsec)) {
    return absl::NotFoundError(
        base::StrCat({"Failed to match stat of image hashed at ",
                      image_path_in_current_ns.value()}));
  }
  return ImageCacheIfc::HashValue{.sha256 = hash.value()};
}
}  // namespace

namespace secagentd {

constexpr ImageCache::InternalImageCacheType::size_type kImageCacheMaxSize =
    256;

absl::StatusOr<std::string> ImageCache::GenerateImageHash(
    const base::FilePath& image_path_in_current_ns) {
  base::File image(image_path_in_current_ns,
                   base::File::FLAG_OPEN | base::File::FLAG_READ);
  if (!image.IsValid()) {
    return absl::NotFoundError(
        base::StrCat({kErrorFailedToRead, image_path_in_current_ns.value()}));
  }
  SHA256_CTX ctx;
  if (!SHA256_Init(&ctx)) {
    return absl::InternalError(kErrorSslSha);
  }
  std::array<char, 4096> buf;
  int bytes_read = 0;
  while ((bytes_read = image.ReadAtCurrentPos(buf.data(), buf.size())) > 0) {
    if (!SHA256_Update(&ctx, buf.data(), bytes_read)) {
      return absl::InternalError(kErrorSslSha);
    }
  }
  if (bytes_read < 0) {
    return absl::AbortedError(
        base::StrCat({kErrorFailedToRead, image_path_in_current_ns.value()}));
  }
  static_assert(sizeof(buf) >= SHA256_DIGEST_LENGTH);
  if (!SHA256_Final(reinterpret_cast<unsigned char*>(buf.data()), &ctx)) {
    return absl::InternalError(kErrorSslSha);
  }
  return base::HexEncode(buf.data(), SHA256_DIGEST_LENGTH);
}
absl::StatusOr<base::FilePath> ImageCache::SafeAppendAbsolutePath(
    const base::FilePath& path, const base::FilePath& abs_component) {
  // TODO(b/279213783): abs_component is expected to be an absolute and
  // resolved path. But that's sometimes not the case. If the path references
  // parent it likely won't resolve and possibly may attempt to escape the
  // pid_mnt_root namespace. So err on the side of safety. Similarly, if the
  // path is not absolute, it likely won't resolve because we don't have its
  // CWD.
  if (!abs_component.IsAbsolute() || abs_component.ReferencesParent()) {
    return absl::InvalidArgumentError(base::StrCat(
        {"Refusing to translate relative or parent-referencing path ",
         abs_component.value()}));
  }
  return path.Append(
      base::StrCat({base::FilePath::kCurrentDirectory, abs_component.value()}));
}

ImageCache::ImageCache(base::FilePath path)
    : root_path_(path),
      cache_(std::make_unique<InternalImageCacheType>(kImageCacheMaxSize)) {}
ImageCache::ImageCache() : ImageCache(base::FilePath("/")) {}

absl::StatusOr<ImageCacheInterface::HashValue> ImageCache::InclusiveGetImage(
    const ImageCacheKeyType& image_key,
    uint64_t pid_for_setns,
    const base::FilePath& image_path_in_pids_ns) {
  base::AutoLock lock(cache_lock_);
  auto it = cache_->Get(image_key);
  if (it != cache_->end()) {
    if (it->first.mtime.tv_sec == 0 || it->first.ctime.tv_sec == 0) {
      // Invalidate entry and force checksum if its cached ctime or mtime seems
      // missing.
      cache_->Erase(it);
      it = cache_->end();
    } else {
      return it->second;
    }
  }

  absl::StatusOr<HashValue> statusorhash;
  {
    base::AutoUnlock unlock(cache_lock_);
    // First try our own (i.e root) namespace. This will almost always work
    // because minijail mounts are 1:1. Stat will save us from false positive
    // matches.
    auto statusorpath =
        SafeAppendAbsolutePath(root_path_, image_path_in_pids_ns);
    if (statusorpath.ok()) {
      statusorhash = VerifyStatAndGenerateImageHash(image_key, *statusorpath);
    }
    // If !statusorpath.ok() then GetPathInCurrentMountNs will call
    // SafeAppendAbsolutePath with the same image_path_in_pids_ns which will
    // return the same status. No point in trying.
    if (statusorpath.ok() && !statusorhash.ok()) {
      statusorpath =
          GetPathInCurrentMountNs(pid_for_setns, image_path_in_pids_ns);
      if (statusorpath.ok()) {
        statusorhash = VerifyStatAndGenerateImageHash(image_key, *statusorpath);
      }
    }

    if (!statusorpath.ok() || !statusorhash.ok()) {
      LOG(ERROR) << "Failed to hash " << image_path_in_pids_ns
                 << " in mnt ns of pid " << pid_for_setns << ": "
                 << (!statusorpath.ok() ? statusorpath.status()
                                        : statusorhash.status());
      return absl::InternalError("Failed to hash");
    }
  }
  it = cache_->Put(image_key, std::move(*statusorhash));
  return it->second;
}

absl::StatusOr<base::FilePath> ImageCache::GetPathInCurrentMountNs(
    uint64_t pid_for_setns, const base::FilePath& image_path_in_pids_ns) const {
  const base::FilePath pid_mnt_root =
      root_path_.Append(base::StringPrintf("proc/%" PRIu64, pid_for_setns))
          .Append("root");
  return ImageCache::SafeAppendAbsolutePath(pid_mnt_root,
                                            image_path_in_pids_ns);
}
}  // namespace secagentd
