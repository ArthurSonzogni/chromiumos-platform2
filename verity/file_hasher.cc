// Copyright 2010 The ChromiumOS Authors
// Use of this source code is governed by the GPL v2 license that can
// be found in the LICENSE file.
//
// Implementation of FileHasher

#define __STDC_LIMIT_MACROS 1
#define __STDC_FORMAT_MACROS 1

#include <linux/fs.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <sys/stat.h>

#include <string>

#include <base/bits.h>
#include <base/check.h>
#include <base/files/file.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>

#include "verity/dm-bht.h"
#include "verity/dm_verity_table.h"
#include "verity/file_hasher.h"

namespace verity {

using std::string;

namespace {
// |base::File| doesn't have a good way of getting block device's size. So we
// have to do linux trickery here.
int64_t GetFileSize(base::File* file) {
  struct stat statbuf;
  int rc = fstat(file->GetPlatformFile(), &statbuf);
  PLOG_IF(FATAL, rc != 0) << "Failed to get file status";
  if (S_ISBLK(statbuf.st_mode)) {
    int64_t size = 0;
    rc = ioctl(file->GetPlatformFile(), BLKGETSIZE64, &size);
    PLOG_IF(FATAL, rc != 0) << "Failed to get block size of input device";
    return size;
  }
  return file->GetLength();
}
}  // namespace

FileHasher::~FileHasher() {
  if (initialized_) {
    dm_bht_destroy(&tree_);
  }
}

bool FileHasher::Initialize() {
  CHECK(!initialized_);

  if (!alg_ || !source_ || !destination_) {
    LOG(ERROR) << "Invalid arguments supplied to ctor.";
    LOG(INFO) << "s: " << source_ << " d: " << destination_;
    return false;
  }
  int64_t source_size = GetFileSize(source_.get());
  if (source_size < 0) {
    PLOG(ERROR) << "Failed to get the file size";
    return false;
  }
  if (block_limit_ > source_size / PAGE_SIZE) {
    LOG(ERROR) << block_limit_ << " blocks exceeds image size of "
               << source_size;
    return false;
  } else if (block_limit_ == 0) {
    block_limit_ = source_size / PAGE_SIZE;
    if (source_size % PAGE_SIZE) {
      LOG(ERROR) << "The source file size must be divisible by the block size, "
                 << "Size: " << source_size;
      LOG(INFO) << "Suggested size: "
                << base::bits::AlignUp(static_cast<uint64_t>(source_size),
                                       uint64_t{PAGE_SIZE});
      return false;
    }
  }

  // Now we initialize the tree
  if (dm_bht_create(&tree_, block_limit_, alg_)) {
    LOG(ERROR) << "Could not create the BH tree";
    return false;
  }

  sectors_ = dm_bht_sectors(&tree_);
  hash_data_.resize(verity_to_bytes(sectors_));

  // No reading is needed.
  dm_bht_set_read_cb(&tree_, dm_bht_zeroread_callback);
  dm_bht_set_buffer(&tree_, hash_data_.data());
  initialized_ = true;
  return true;
}

bool FileHasher::Store() {
  return destination_->WriteAtCurrentPos(hash_data_.data(),
                                         hash_data_.size()) >= 0;
}

bool FileHasher::Hash() {
  // TODO(wad) abstract size when dm-bht needs to do break from PAGE_SIZE
  uint8_t block_data[PAGE_SIZE];
  uint32_t block = 0;

  while (block < block_limit_) {
    if (source_->ReadAtCurrentPos(reinterpret_cast<char*>(block_data),
                                  PAGE_SIZE) < 0) {
      PLOG(ERROR) << "Failed to read for block: " << block;
      return false;
    }
    if (dm_bht_store_block(&tree_, block, block_data)) {
      LOG(ERROR) << "Failed to store block " << block;
      return false;
    }
    ++block;
  }
  return !dm_bht_compute(&tree_);
}

void FileHasher::set_salt(const char* salt) {
  if (!strcmp(salt, "random")) {
    salt = RandomSalt();
  }
  dm_bht_set_salt(&tree_, salt);
  salt_ = salt;
}

const char* FileHasher::RandomSalt() {
  char buf[DM_BHT_SALT_SIZE];
  base::FilePath urandom_path("/dev/urandom");
  base::File source(urandom_path,
                    base::File::FLAG_OPEN | base::File::FLAG_READ);

  LOG_IF(FATAL, !source.IsValid())
      << "Failed to open the random source: " << urandom_path;
  PLOG_IF(FATAL, source.ReadAtCurrentPos(buf, sizeof(buf)) < 0)
      << "Failed to read the random source";

  for (size_t i = 0; i < sizeof(buf); ++i) {
    // NOLINTNEXTLINE(runtime/printf)
    sprintf(&random_salt_[i * 2], "%02x", buf[i]);
  }
  random_salt_[sizeof(random_salt_) - 1] = '\0';

  return random_salt_;
}

std::string FileHasher::GetTable(const PrintArgs& args) {
  return GetRawTable(args.colocated ? DmVerityTable::HashPlacement::COLOCATED
                                    : DmVerityTable::HashPlacement::SEPARATE)
      .Print(args.vanilla ? DmVerityTable::Format::VANILLA
                          : DmVerityTable::Format::CROS)
      .value_or("");
}

DmVerityTable FileHasher::GetRawTable(
    const DmVerityTable::HashPlacement& hash_placement) {
  // Grab the digest (up to 1kbit supported)
  uint8_t digest[128] = {0};
  char hexsalt[DM_BHT_SALT_SIZE * 2 + 1] = {'\0'};
  bool have_salt;

  dm_bht_root_hexdigest(&tree_, digest, sizeof(digest));
  have_salt = dm_bht_salt(&tree_, hexsalt) == 0;

  return DmVerityTable(
      alg_, std::to_array(digest),
      have_salt ? std::make_optional(std::to_array(hexsalt)) : std::nullopt,
      /*data_dev=*/
      DmVerityTable::DevInfo{
          .dev = "ROOT_DEV",
          .block_size = PAGE_SIZE,
          .block_count = block_limit_,
      },
      /*hash_dev=*/
      DmVerityTable::DevInfo{
          .dev = "HASH_DEV",
          .block_size = PAGE_SIZE,
          .block_count =
              0,  // This value doesn't really matter for hash device.
      },
      hash_placement);
}

void FileHasher::PrintTable(const PrintArgs& args) {
  printf("%s\n", GetTable(args).c_str());
}

}  // namespace verity
