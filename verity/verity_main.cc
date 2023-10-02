// Copyright 2011 The ChromiumOS Authors
// Use of this source code is governed by the GPL v2 license that can
// be found in the LICENSE file.
//
// Driver program for creating verity hash images.

#include <stdio.h>
#include <stdlib.h>

#include <memory>
#include <string>

#include <base/files/file.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <brillo/flag_helper.h>
#include <brillo/strings/string_utils.h>

#include "verity/file_hasher.h"

namespace {

typedef enum { VERITY_NONE = 0, VERITY_CREATE, VERITY_VERIFY } verity_mode_t;

int verity_create(const std::string& alg,
                  const std::string& image_path,
                  unsigned int image_blocks,
                  const std::string& hash_path,
                  const std::string& salt) {
  auto source = std::make_unique<base::File>(
      base::FilePath(image_path),
      base::File::FLAG_OPEN | base::File::FLAG_READ);
  LOG_IF(FATAL, source && !source->IsValid())
      << "Failed to open the source file: " << image_path;
  auto destination = std::make_unique<base::File>(
      base::FilePath(hash_path),
      base::File::FLAG_CREATE_ALWAYS | base::File::FLAG_WRITE);
  LOG_IF(FATAL, destination && !destination->IsValid())
      << "Failed to open destination file: " << hash_path;

  // Create the actual worker and create the hash image.
  verity::FileHasher hasher(std::move(source), std::move(destination),
                            image_blocks, alg.c_str());
  LOG_IF(FATAL, !hasher.Initialize()) << "Failed to initialize hasher";
  if (!salt.empty())
    hasher.set_salt(salt.c_str());
  LOG_IF(FATAL, !hasher.Hash()) << "Failed to hash hasher";
  LOG_IF(FATAL, !hasher.Store()) << "Failed to store hasher";
  hasher.PrintTable(true);
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  verity_mode_t mode = VERITY_CREATE;
  std::string alg = "sha256", payload = "", hashtree = "", salt = "";
  unsigned int payload_blocks = 0;

  // TODO(b/269707854): Drop the old code after adding the proper cmdline
  // options and migrating consumers by Jan 2025.
  for (int i = 1; i < argc; i++) {
    auto [key, val] = brillo::string_utils::SplitAtFirst(
        argv[i], "=", /*trim_whitespaces=*/true);
    if (key.empty())
      continue;

    if (val.empty() && !base::StartsWith(key, "--")) {
      LOG(ERROR) << "missing value: " << key;
      return -1;
    }

    if (key == "alg") {
      alg = val;
    } else if (key == "payload") {
      payload = val;
    } else if (key == "payload_blocks") {
      CHECK(base::StringToUint(val, &payload_blocks));
    } else if (key == "hashtree") {
      hashtree = val;
    } else if (key == "root_hexdigest") {
      // Silently drop root_hexdigest for now...
    } else if (key == "mode") {
      // Silently drop the mode for now...
    } else if (key == "salt") {
      salt = val;
    } else if (!base::StartsWith(key, "--")) {
      LOG(ERROR) << "bogus key: '" << key << "'";
      return -1;
    }
  }

  // Silently drop the mode for now...
  DEFINE_string(mode, "create", "Only 'create'");
  // We used to advertise more algorithms, but they've never been implemented:
  // sha512 sha384 sha mdc2 ripemd160 md4 md2
  DEFINE_string(alg, alg, "Hash algorithm to use. Only sha256 for now");
  DEFINE_string(payload, payload, "Path to the image to hash");
  DEFINE_uint32(payload_blocks, payload_blocks,
                "Size of the image, in blocks (4096 bytes)");
  DEFINE_string(hashtree, hashtree,
                "Path to a hash tree to create or read from");
  // Silently drop root_hexdigest for now...
  DEFINE_string(root_hexdigest, "",
                "Digest of the root node (in hex) for verification");
  DEFINE_string(salt, salt, "Salt (in hex)");

  brillo::FlagHelper::Init(argc, argv, "verity userspace tool");

  if (FLAGS_alg.empty() || FLAGS_payload.empty() || FLAGS_hashtree.empty()) {
    LOG(ERROR) << "missing data: " << (FLAGS_alg.empty() ? "alg " : "")
               << (FLAGS_payload.empty() ? "payload " : "")
               << (FLAGS_hashtree.empty() ? "hashtree " : "");
    return -1;
  }

  if (mode == VERITY_CREATE) {
    return verity_create(FLAGS_alg, FLAGS_payload, FLAGS_payload_blocks,
                         FLAGS_hashtree, FLAGS_salt);
  } else {
    LOG(FATAL) << "Verification not done yet";
  }
  return -1;
}
