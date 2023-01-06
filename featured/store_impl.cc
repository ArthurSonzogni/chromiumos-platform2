// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "featured/store_impl.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/files/file.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <featured/proto_bindings/featured.pb.h>
#include <openssl/crypto.h>
#include <sys/stat.h>

#include "bootlockbox-client/bootlockbox/boot_lockbox_client.h"
#include "featured/hmac.h"

namespace featured {

constexpr char kStorePath[] = "/var/lib/featured/store";
constexpr char kStoreHMACPath[] = "/var/lib/featured/store_hmac";
constexpr char kLockboxKey[] = "featured_early_boot_key";

constexpr mode_t kSystemFeaturedFilesMode = 0760;

namespace {

// Walks the directory tree to make sure we avoid symlinks.
// Creates |path| if it does not exist.
//
// All parent parts must already exist else we return false.
bool ValidatePathAndOpen(const base::FilePath& path,
                         int* outfd,
                         int flags = 0) {
  std::vector<std::string> components = path.GetComponents();
  if (components.empty()) {
    LOG(ERROR) << "Cannot open an empty path";
    return false;
  }

  int parentfd = AT_FDCWD;
  for (auto it = components.begin(); it != components.end(); ++it) {
    std::string component = *it;
    int fd;
    if (it == components.end() - 1) {
      // Check that the last component is a valid file and open it for reading
      // and writing.
      fd = openat(parentfd, component.c_str(),
                  O_CREAT | O_RDWR | O_NOFOLLOW | O_CLOEXEC | flags,
                  kSystemFeaturedFilesMode);
    } else {
      // Check that all components except the last are a valid directory.
      fd = openat(parentfd, component.c_str(),
                  O_NOFOLLOW | O_CLOEXEC | O_PATH | O_DIRECTORY);
    }
    if (fd < 0) {
      PLOG(ERROR) << "Unable to access path: " << path.value() << " ("
                  << component << ")";
      if (parentfd != AT_FDCWD) {
        close(parentfd);
      }
      return false;
    }
    if (parentfd != AT_FDCWD) {
      close(parentfd);
    }
    parentfd = fd;
  }
  *outfd = parentfd;
  return true;
}

// Validates |file_path| according to |ValidatePathAndOpen| and reads the
// contents into |file_content|. Creates |file_path| if it does not exist.
//
// Returns false if validating, opening, or reading |file_path| fail.
//
// NOTE: While |file_path| could be recreated if reading fails, doing so is
// risky since deletion could have unintended consequences (eg. the file is a
// symlink).
bool ValidatePathAndRead(const base::FilePath& file_path,
                         std::string& file_content) {
  int fd;
  if (!ValidatePathAndOpen(file_path, &fd)) {
    LOG(ERROR) << "Failed to validate and open " << file_path;
    return false;
  } else {
    // Constructing with |fd| instead of |file_path| to avoid potential
    // TOCTOU (time-of-check/time-of-use) vulnerabilities between calling
    // |ValidatePathAndOpen| and constructing |file|.
    base::File file(fd);
    std::vector<uint8_t> buffer(file.GetLength());
    if (!file.ReadAndCheck(/*offset=*/0, base::make_span(buffer))) {
      LOG(ERROR) << "Failed to read file contents";
      return false;
    }
    file_content = std::string(buffer.begin(), buffer.end());
  }
  return true;
}

// Writes store and hmac to disk.
bool WriteDisk(const Store& store,
               const HMAC& hmac_wrapper,
               const base::FilePath& store_path,
               const base::FilePath& hmac_path) {
  std::string serialized_store;
  bool serialized = store.SerializeToString(&serialized_store);
  if (!serialized) {
    LOG(ERROR) << "Could not serialize protobuf";
    return false;
  }

  int store_fd;
  if (!ValidatePathAndOpen(store_path, &store_fd, O_TRUNC)) {
    PLOG(ERROR) << "Could not reopen " << store_path;
    return false;
  }

  // Write store to disk.
  base::File store_file(store_fd);
  if (!store_file.WriteAtCurrentPosAndCheck(
          base::as_bytes(base::make_span(serialized_store)))) {
    PLOG(ERROR) << "Could not write new store to disk";
    return false;
  }

  // Compute store HMAC.
  std::optional<std::string> store_hmac = hmac_wrapper.Sign(serialized_store);
  if (!store_hmac.has_value()) {
    LOG(ERROR) << "Failed to sign store hmac";
    return false;
  }

  int hmac_fd;
  if (!ValidatePathAndOpen(hmac_path, &hmac_fd, O_TRUNC)) {
    LOG(ERROR) << "Could not reopen " << hmac_path;
    return false;
  }

  // Write store HMAC to disk.
  std::string store_hmac_str = store_hmac.value();
  base::File hmac_file(hmac_fd);
  if (!hmac_file.WriteAtCurrentPosAndCheck(
          base::as_bytes(base::make_span(store_hmac_str)))) {
    PLOG(ERROR) << "Could not write new store HMAC to disk";
    return false;
  }
  return true;
}
}  // namespace

StoreImpl::StoreImpl(const Store& store,
                     std::unique_ptr<HMAC> hmac_wrapper,
                     const base::FilePath& store_path,
                     const base::FilePath& hmac_path)
    : store_(store),
      hmac_wrapper_(std::move(hmac_wrapper)),
      store_path_(std::move(store_path)),
      hmac_path_(std::move(hmac_path)) {}

std::unique_ptr<StoreInterface> StoreImpl::Create() {
  std::unique_ptr<bootlockbox::BootLockboxClient> boot_lockbox_client =
      bootlockbox::BootLockboxClient::CreateBootLockboxClient();
  return Create(base::FilePath(kStorePath), base::FilePath(kStoreHMACPath),
                std::move(boot_lockbox_client));
}

std::unique_ptr<StoreInterface> StoreImpl::Create(
    base::FilePath store_path,
    base::FilePath hmac_path,
    std::unique_ptr<bootlockbox::BootLockboxClient> boot_lockbox_client) {
  // Check validity of the boot lockbox.
  if (!boot_lockbox_client) {
    LOG(ERROR) << "Invalid bootlockbox client";
    return nullptr;
  }

  // Read the store and HMAC.
  // Open store file or create if it does not exist.
  std::string store_content;
  if (!ValidatePathAndRead(store_path, store_content)) {
    LOG(ERROR) << "Failed to validate and read from " << store_path;
    return nullptr;
  }

  // Open hmac file or create if it does not exist.
  std::string hmac_content;
  if (!ValidatePathAndRead(hmac_path, hmac_content)) {
    LOG(ERROR) << "Failed to validate and read from " << hmac_path;
    return nullptr;
  }

  // Verify the HMAC, falling back to an empty proto if it fails to verify
  // (or is missing).
  std::unique_ptr<HMAC> hmac_wrapper;
  std::string hmac_key;
  bool key_exists = boot_lockbox_client->Read(kLockboxKey, &hmac_key);
  bool verified = false;
  if (key_exists) {
    hmac_wrapper = std::make_unique<HMAC>(HMAC::SHA256);
    if (!hmac_wrapper->Init(hmac_key)) {
      LOG(ERROR) << "Failed to initialize HMAC instance";
      return nullptr;
    }
    verified = hmac_wrapper->Verify(store_content, hmac_content);
    // Zero out the hmac key after verifying store and hmac.
    OPENSSL_cleanse(hmac_key.data(), hmac_key.size());
  }

  // Deserialize the proto and store it in memory.
  Store store;
  if (verified) {
    bool deserialized_store = store.ParseFromString(store_content);
    if (!deserialized_store) {
      LOG(ERROR) << "Failed to deserialize store";
      store.Clear();
    }
    // else: Use empty proto.
  }  // else: Use empty proto.

  // Generate a new key, attempt to store it in the boot lockbox, and only if
  // that succeeds, re-generate an HMAC of the serialized proto and store it to
  // disk.
  std::unique_ptr<HMAC> new_hmac_wrapper = std::make_unique<HMAC>(HMAC::SHA256);
  if (!new_hmac_wrapper->Init()) {
    LOG(ERROR) << "HMAC wrapper failed to generate new key";
    return nullptr;
  }
  std::string new_key = new_hmac_wrapper->GetKey();
  if (!boot_lockbox_client->Store(kLockboxKey, new_key)) {
    LOG(ERROR) << "Could not store new key";
    return nullptr;
  }
  // Zero out the symmetric key after storing it.
  OPENSSL_cleanse(new_key.data(), new_key.size());

  if (!WriteDisk(store, *new_hmac_wrapper, store_path, hmac_path)) {
    LOG(ERROR) << "Failed to write store and hmac to disk";
    return nullptr;
  }

  return std::unique_ptr<StoreInterface>(
      new StoreImpl(store, std::move(new_hmac_wrapper), store_path, hmac_path));
}

uint32_t StoreImpl::GetBootAttemptsSinceLastUpdate() {
  return store_.boot_attempts_since_last_seed_update();
}

bool StoreImpl::IncrementBootAttemptsSinceLastUpdate() {
  uint32_t boot_attempts = GetBootAttemptsSinceLastUpdate();
  store_.set_boot_attempts_since_last_seed_update(boot_attempts + 1);

  if (!WriteDisk(store_, *hmac_wrapper_, store_path_, hmac_path_)) {
    LOG(ERROR) << "Failed to increment boot attempts to disk.";
    return false;
  }
  return true;
}

bool StoreImpl::ClearBootAttemptsSinceLastUpdate() {
  store_.set_boot_attempts_since_last_seed_update(0);

  if (!WriteDisk(store_, *hmac_wrapper_, store_path_, hmac_path_)) {
    LOG(ERROR) << "Failed to increment boot attempts to disk.";
    return false;
  }
  return true;
}

SeedDetails StoreImpl::GetLastGoodSeed() {
  return store_.last_good_seed();
}

bool StoreImpl::SetLastGoodSeed(const SeedDetails& seed) {
  *store_.mutable_last_good_seed() = seed;
  if (!WriteDisk(store_, *hmac_wrapper_, store_path_, hmac_path_)) {
    LOG(ERROR) << "Failed to increment boot attempts to disk.";
    return false;
  }
  return true;
}

// TODO(kendraketsui): implement.
std::vector<FeatureOverride> StoreImpl::GetOverrides() {
  return std::vector<FeatureOverride>();
}
// TODO(kendraketsui): implement.
void StoreImpl::AddOverride(const FeatureOverride& override) {}
// TODO(kendraketsui): implement.
void StoreImpl::RemoveOverrideFor(const std::string& name) {}
}  // namespace featured
