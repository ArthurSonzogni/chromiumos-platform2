/* Copyright 2012 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "installer/chromeos_verity.h"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <memory>
#include <string>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_file.h>
#include <base/logging.h>
#include <base/memory/aligned_memory.h>
#include <base/scoped_generic.h>
#include <base/strings/string_number_conversions.h>
#include <verity/dm-bht.h>

namespace {

constexpr uint64_t IO_BUF_SIZE = 1 * 1024 * 1024;

ssize_t WriteHash(const base::FilePath& dev,
                  const uint8_t* buf,
                  size_t size,
                  off64_t offset) {
  base::ScopedFD fd(
      HANDLE_EINTR(open(dev.value().c_str(), O_WRONLY | O_CLOEXEC)));
  if (!fd.is_valid()) {
    PLOG(WARNING) << "Cannot open " << dev << " for writing";
    return -1;
  }
  return pwrite(fd.get(), buf, size, offset);
}

struct ScopedDmBhtDestroyTraits {
  static struct verity::dm_bht* InvalidValue() { return nullptr; }
  static void Free(struct verity::dm_bht* bht) {
    if (bht != nullptr) {
      verity::dm_bht_destroy(bht);
    }
  }
};
typedef base::ScopedGeneric<struct verity::dm_bht*, ScopedDmBhtDestroyTraits>
    ScopedDmBht;

}  // namespace

int chromeos_verity(verity::DmBhtInterface* bht,
                    const std::string& alg,
                    const base::FilePath& device,
                    unsigned blocksize,
                    uint64_t fs_blocks,
                    const std::string& salt,
                    const std::string& expected,
                    bool enforce_rootfs_verification) {
  int ret;
  if ((ret = bht->Create(fs_blocks, alg))) {
    LOG(ERROR) << "dm_bht_create failed: " << ret;
    return ret;
  }

  DCHECK(IO_BUF_SIZE % blocksize == 0) << "Alignment mismatch";
  std::unique_ptr<uint8_t, base::AlignedFreeDeleter> io_buffer(
      static_cast<uint8_t*>(base::AlignedAlloc(IO_BUF_SIZE, blocksize)));
  if (!io_buffer) {
    PLOG(ERROR) << "aligned_alloc io_buffer failed";
    return errno;
  }

  // We aren't going to do any automatic reading.
  bht->SetReadCallback(verity::dm_bht_zeroread_callback);
  bht->SetSalt(salt);
  size_t hash_size = bht->Sectors() << SECTOR_SHIFT;

  DCHECK(hash_size % blocksize == 0) << "Alignment mismatch";
  std::unique_ptr<uint8_t, base::AlignedFreeDeleter> hash_buffer(
      static_cast<uint8_t*>(base::AlignedAlloc(hash_size, blocksize)));
  if (!hash_buffer) {
    PLOG(ERROR) << "aligned_alloc hash_buffer failed";
    return errno;
  }

  memset(hash_buffer.get(), 0, hash_size);
  bht->SetBuffer(hash_buffer.get());

  base::ScopedFD fd(
      HANDLE_EINTR(open(device.value().c_str(), O_RDONLY | O_CLOEXEC)));
  if (!fd.is_valid()) {
    PLOG(ERROR) << "error opening " << device;
    return errno;
  }

  uint64_t cur_block = 0;
  while (cur_block < fs_blocks) {
    unsigned int i;
    size_t count = std::min((fs_blocks - cur_block) * blocksize, IO_BUF_SIZE);

    if (!base::ReadFromFD(fd.get(), base::as_writable_chars(base::make_span(
                                        io_buffer.get(), count)))) {
      PLOG(ERROR) << "read returned error";
      return -1;
    }

    for (i = 0; i < (count / blocksize); i++) {
      ret = bht->StoreBlock(cur_block, io_buffer.get() + (i * blocksize));
      if (ret) {
        LOG(ERROR) << "dm_bht_store_block returned error: " << ret;
        return ret;
      }
      cur_block++;
    }
  }
  io_buffer.reset();
  fd.reset();

  if ((ret = bht->Compute())) {
    LOG(ERROR) << "dm_bht_compute returned error: " << ret;
    return ret;
  }

  uint8_t digest[DM_BHT_MAX_DIGEST_SIZE];
  bht->HexDigest(digest, DM_BHT_MAX_DIGEST_SIZE);

  if (memcmp(digest, expected.c_str(), bht->DigestSize())) {
    LOG(ERROR) << "Filesystem hash verification failed";
    LOG(ERROR) << "Expected " << expected << " != actual "
               << reinterpret_cast<char*>(digest);
    if (enforce_rootfs_verification) {
      return -1;
    } else {
      LOG(INFO) << "Verified Boot not enabled; ignoring.";
    }
  }

  ssize_t written =
      WriteHash(device, hash_buffer.get(), hash_size, cur_block * blocksize);
  if (written < static_cast<ssize_t>(hash_size)) {
    PLOG(ERROR) << "Writing out hash failed: written" << written
                << ", expected %d" << hash_size;
    return errno;
  }

  return 0;
}
