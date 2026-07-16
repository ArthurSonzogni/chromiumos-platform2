// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "secagentd/image_cache.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <cinttypes>
#include <cstdint>
#include <list>
#include <memory>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "base/containers/lru_cache.h"
#include "base/containers/span.h"
#include "base/files/file.h"
#include "base/files/file_path.h"
#include "base/files/scoped_file.h"
#include "base/logging.h"
#include "base/posix/eintr_wrapper.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_tokenizer.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/synchronization/lock.h"
#include "brillo/secure_blob.h"
#include "openssl/sha.h"
#include "secagentd/bpf/bpf_types.h"

namespace {

constexpr char kErrorFailedToRead[] = "Error reading file ";
constexpr char kErrorSslSha[] = "SSL SHA error";
constexpr char kErrorBytesRead[] =
    "Failed to read the expected number of bytes from the file. ";

// Allow a 10 millisecond delta for nanosec.
constexpr u_int64_t kEpsilonNs = 10000000;

struct PinnedRegularFile {
  base::File file;
  struct stat statbuf;
};

// Opens a file path without blocking and pins the underlying inode so that the
// returned readable descriptor and stat metadata are guaranteed to refer to the
// exact same regular-file inode (preventing path-swap TOCTOU races):
// 1. Opens the path with O_PATH | O_CLOEXEC to pin the resolved inode in the
//    kernel without blocking on special files (e.g. FIFOs/named pipes).
// 2. Performs fstat() on the pinned O_PATH descriptor to verify S_ISREG and
//    capture the inode's device/inode numbers and timestamps.
// 3. Reopens the pinned inode directly via /proc/self/fd/N (bypassing path
//    re-resolution) with O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOCTTY.
absl::StatusOr<PinnedRegularFile> OpenAndPinRegularFile(
    const base::FilePath& path) {
  base::ScopedFD path_pinned_fd(
      HANDLE_EINTR(open(path.value().c_str(), O_PATH | O_CLOEXEC)));
  if (!path_pinned_fd.is_valid()) {
    return absl::NotFoundError(
        base::StrCat({kErrorFailedToRead, path.value()}));
  }

  struct stat statbuf;
  if (fstat(path_pinned_fd.get(), &statbuf) != 0) {
    return absl::NotFoundError(
        base::StrCat({kErrorFailedToRead, path.value()}));
  }

  if (!S_ISREG(statbuf.st_mode)) {
    return absl::InvalidArgumentError(
        base::StrCat({"Not a regular file: ", path.value()}));
  }

  // Reopen the already-pinned inode via /proc/self/fd so the path cannot be
  // swapped between fstat() above and reading below.
  std::string proc_fd_path =
      base::StringPrintf("/proc/self/fd/%d", path_pinned_fd.get());
  base::ScopedFD pinned_read_fd(HANDLE_EINTR(open(
      proc_fd_path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOCTTY)));

  if (!pinned_read_fd.is_valid()) {
    return absl::NotFoundError(
        base::StrCat({kErrorFailedToRead, path.value()}));
  }

  return PinnedRegularFile{
      .file = base::File(pinned_read_fd.release()),
      .statbuf = statbuf,
  };
}

}  // namespace

namespace secagentd {

constexpr ImageCache::InternalImageCacheType::size_type kImageCacheMaxSize =
    256;

absl::StatusOr<ImageCacheInterface::HashValue>
ImageCache::VerifyStatAndGenerateImageHash(
    const ImageCacheInterface::ImageCacheKeyType& image_key,
    bool force_full_sha256,
    const base::FilePath& image_path_in_current_ns) {
  auto pinned_file_or = OpenAndPinRegularFile(image_path_in_current_ns);
  if (!pinned_file_or.ok()) {
    return pinned_file_or.status();
  }

  PinnedRegularFile pinned_file = std::move(pinned_file_or).value();
  const struct stat& image_stat = pinned_file.statbuf;

  if ((image_stat.st_dev != image_key.inode_device_id) ||
      (image_stat.st_ino != image_key.inode) ||
      (image_stat.st_mtim.tv_sec != image_key.mtime.tv_sec) ||
      std::abs(image_stat.st_mtim.tv_nsec - image_key.mtime.tv_nsec) >
          static_cast<int64_t>(kEpsilonNs) ||
      (image_stat.st_ctim.tv_sec != image_key.ctime.tv_sec) ||
      std::abs(image_stat.st_ctim.tv_nsec - image_key.ctime.tv_nsec) >
          static_cast<int64_t>(kEpsilonNs)) {
    return absl::NotFoundError(
        base::StrCat({"Failed to match stat of image hashed at ",
                      image_path_in_current_ns.value(),
                      "\nExpected values:\n",
                      "  inode_device_id: ",
                      base::NumberToString(image_key.inode_device_id),
                      "\n  inode: ",
                      base::NumberToString(image_key.inode),
                      "\n  mtime: ",
                      base::NumberToString(image_key.mtime.tv_sec),
                      ".",
                      base::NumberToString(image_key.mtime.tv_nsec),
                      "\n  ctime: ",
                      base::NumberToString(image_key.ctime.tv_sec),
                      ".",
                      base::NumberToString(image_key.ctime.tv_nsec),
                      "\nActual values:\n",
                      "  st_dev: ",
                      base::NumberToString(image_stat.st_dev),
                      "\n  st_ino: ",
                      base::NumberToString(image_stat.st_ino),
                      "\n  st_mtime: ",
                      base::NumberToString(image_stat.st_mtim.tv_sec),
                      ".",
                      base::NumberToString(image_stat.st_mtim.tv_nsec),
                      "\n  st_ctime: ",
                      base::NumberToString(image_stat.st_ctim.tv_sec),
                      ".",
                      base::NumberToString(image_stat.st_ctim.tv_nsec)}));
  }

  // `pinned_file.file` is bound to the exact inode verified by `image_stat`
  // above (via /proc/self/fd), and ChromeOS enforces W^X at the mount level
  // (executable mounts are MS_RDONLY; writable mounts are MS_NOEXEC), so the
  // verified inode cannot be swapped or modified in-place while being hashed.
  return GenerateImageHashInternal(pinned_file.file, image_path_in_current_ns,
                                   force_full_sha256);
}

// The function determines whether to compute a full or partial hash based on
// file size and force_full_sha. For a partial hash, it divides the file into
// chunks, processes a fixed-size portion of each chunk, and handles any
// remaining bytes in the last chunk separately. For a full hash, it reads and
// processes the entire file in chunks of a specified size. It updates the hash
// with each chunk of data and finalizes the computation. The result indicates
// if the hash was for the full file or just a part.
absl::StatusOr<ImageCacheInterface::HashValue> ImageCache::GenerateImageHash(
    const base::FilePath& image_path_in_current_ns, bool force_full_sha) {
  auto pinned_file_or = OpenAndPinRegularFile(image_path_in_current_ns);
  if (!pinned_file_or.ok()) {
    return pinned_file_or.status();
  }
  return GenerateImageHashInternal(pinned_file_or->file,
                                   image_path_in_current_ns, force_full_sha);
}

absl::StatusOr<ImageCacheInterface::HashValue>
ImageCache::GenerateImageHashInternal(
    base::File& file,
    const base::FilePath& image_path_for_logging,
    bool force_full_sha) {
  base::TimeTicks start = base::TimeTicks::Now();
  SHA256_CTX ctx;
  if (!SHA256_Init(&ctx)) {
    return absl::InternalError(kErrorSslSha);
  }

  int64_t file_size = file.GetLength();
  if (file_size < 0) {
    return absl::AbortedError(base::StrCat(
        {"Could not get file length:", image_path_for_logging.value()}));
  }

  bool is_partial =
      !force_full_sha && (file_size > (max_file_size_for_full_sha_));

  size_t chunk_size;

  if (is_partial) {
    size_t chunk_count = max_file_size_for_full_sha_ / sha_chunk_size_;
    chunk_size = file_size / chunk_count;
  } else {
    chunk_size = sha_chunk_size_;
  }

  // If last chunk is less that the chunk_count, we would end up
  // computing full hash, even though partial is needed, updating is_partial
  // correctly.
  is_partial =
      is_partial &&
      (file_size > (max_file_size_for_full_sha_ +
                    ((max_file_size_for_full_sha_ / sha_chunk_size_) - 1)));

  brillo::SecureBlob buf(sha_chunk_size_);
  size_t offset = 0;

  while (offset < file_size) {
    // Read bytes from the file.
    auto bytes_read = file.Read(offset, base::as_writable_byte_span(buf));
    if (!bytes_read.has_value()) {
      return absl::AbortedError(
          base::StrCat({kErrorBytesRead, image_path_for_logging.value()}));
    }
    // Update SHA256 context with the read data.
    if (!SHA256_Update(&ctx, buf.data(), *bytes_read)) {
      return absl::InternalError(kErrorSslSha);
    }

    offset += chunk_size;  // Move to the next position.
  }

  // Finalize the SHA calculation
  std::array<unsigned char, SHA256_DIGEST_LENGTH> final_hash;
  if (!SHA256_Final(final_hash.data(), &ctx)) {
    return absl::InternalError(kErrorSslSha);
  }

  // Convert hash to a hexadecimal string and return.
  return ImageCacheInterface::HashValue{
      .sha256 = base::HexEncode(base::span(final_hash)),
      .sha256_is_partial = is_partial,
      .file_size = static_cast<size_t>(file_size),
      .compute_time = base::TimeTicks::Now() - start};
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

ImageCache::ImageCache(base::FilePath path,
                       size_t sha_chunk_size,
                       size_t max_file_size_for_full_sha)
    : root_path_(path),
      sha_chunk_size_(sha_chunk_size),
      max_file_size_for_full_sha_(max_file_size_for_full_sha),
      cache_(std::make_unique<InternalImageCacheType>(kImageCacheMaxSize)) {}
ImageCache::ImageCache() : ImageCache(base::FilePath("/")) {}

absl::StatusOr<ImageCacheInterface::HashValue> ImageCache::InclusiveGetImage(
    const ImageCacheKeyType& image_key,
    bool force_full_sha256,
    uint64_t pid_for_setns,
    const base::FilePath& image_path_in_pids_ns) {
  base::AutoLock lock(cache_lock_);
  auto it = cache_->Get(image_key);
  if (it != cache_->end()) {
    if (it->first.mtime.tv_sec == 0 || it->first.ctime.tv_sec == 0) {
      // Invalidate entry and force checksum if its cached ctime or mtime
      // seems missing.
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
      statusorhash = VerifyStatAndGenerateImageHash(
          image_key, force_full_sha256, *statusorpath);
    }
    // If !statusorpath.ok() then GetPathInCurrentMountNs will call
    // SafeAppendAbsolutePath with the same image_path_in_pids_ns which will
    // return the same status. No point in trying.
    if (statusorpath.ok() && !statusorhash.ok()) {
      statusorpath =
          GetPathInCurrentMountNs(pid_for_setns, image_path_in_pids_ns);
      if (statusorpath.ok()) {
        statusorhash = VerifyStatAndGenerateImageHash(
            image_key, force_full_sha256, *statusorpath);
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
