// Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by the GPL v2 license that can
// be found in the LICENSE file.
//
// Driver program for creating verity hash images.

#include <stdio.h>
#include <stdlib.h>

#include <memory>

#include <base/files/file.h>
#include <base/logging.h>
#include <brillo/syslog_logging.h>

#include "verity/file_hasher.h"

namespace {
void print_usage(const char* name) {
  // We used to advertise more algorithms, but they've never been implemented:
  // sha512 sha384 sha mdc2 ripemd160 md4 md2
  fprintf(
      stderr,
      "Usage:\n"
      "  %s <arg>=<value>...\n"
      "Options:\n"
      "  mode              One of 'create' or 'verify'\n"
      "  alg               Hash algorithm to use. Only sha256 for now\n"
      "  payload           Path to the image to hash\n"
      "  payload_blocks    Size of the image, in blocks (4096 bytes)\n"
      "  hashtree          Path to a hash tree to create or read from\n"
      "  root_hexdigest    Digest of the root node (in hex) for verification\n"
      "  salt              Salt (in hex)\n"
      "\n",
      name);
}

typedef enum { VERITY_NONE = 0, VERITY_CREATE, VERITY_VERIFY } verity_mode_t;

static unsigned int parse_blocks(const char* block_s) {
  return (unsigned int)strtoul(block_s, NULL, 0);
}
}  // namespace

static int verity_create(const char* alg,
                         const char* image_path,
                         unsigned int image_blocks,
                         const char* hash_path,
                         const char* salt);

void splitarg(char* arg, char** key, char** val) {
  char* sp = NULL;
  *key = strtok_r(arg, "=", &sp);
  *val = strtok_r(NULL, "=", &sp);
}

int main(int argc, char** argv) {
  verity_mode_t mode = VERITY_CREATE;
  const char* alg = NULL;
  const char* payload = NULL;
  const char* hashtree = NULL;
  const char* salt = NULL;
  unsigned int payload_blocks = 0;
  int i;
  char *key, *val;

  for (i = 1; i < argc; i++) {
    splitarg(argv[i], &key, &val);
    if (!key)
      continue;
    if (!val) {
      fprintf(stderr, "missing value: %s\n", key);
      print_usage(argv[0]);
      return -1;
    }
    if (!strcmp(key, "alg")) {
      alg = val;
    } else if (!strcmp(key, "payload")) {
      payload = val;
    } else if (!strcmp(key, "payload_blocks")) {
      payload_blocks = parse_blocks(val);
    } else if (!strcmp(key, "hashtree")) {
      hashtree = val;
    } else if (!strcmp(key, "root_hexdigest")) {
      // Silently drop root_hexdigest for now...
    } else if (!strcmp(key, "mode")) {
      // Silently drop the mode for now...
    } else if (!strcmp(key, "salt")) {
      salt = val;
    } else {
      fprintf(stderr, "bogus key: '%s'\n", key);
      print_usage(argv[0]);
      return -1;
    }
  }

  if (!alg || !payload || !hashtree) {
    fprintf(stderr, "missing data: %s%s%s\n", alg ? "" : "alg ",
            payload ? "" : "payload ", hashtree ? "" : "hashtree");
    print_usage(argv[0]);
    return -1;
  }

  if (mode == VERITY_CREATE) {
    return verity_create(alg, payload, payload_blocks, hashtree, salt);
  } else {
    LOG(FATAL) << "Verification not done yet";
  }
  return -1;
}

static int verity_create(const char* alg,
                         const char* image_path,
                         unsigned int image_blocks,
                         const char* hash_path,
                         const char* salt) {
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
                            image_blocks, alg);
  LOG_IF(FATAL, !hasher.Initialize()) << "Failed to initialize hasher";
  if (salt)
    hasher.set_salt(salt);
  LOG_IF(FATAL, !hasher.Hash()) << "Failed to hash hasher";
  LOG_IF(FATAL, !hasher.Store()) << "Failed to store hasher";
  hasher.PrintTable(true);
  return 0;
}
