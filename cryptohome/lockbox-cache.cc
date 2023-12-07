// Copyright 2012 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/lockbox-cache.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>
#include <bindings/device_management_backend.pb.h>
#include <brillo/proto_bindings/install_attributes.pb.h>
#include <brillo/secure_blob.h>
#include <metrics/metrics_library.h>
#include <policy/device_local_account_policy_util.h>
#include <policy/device_policy_impl.h>

#include "cryptohome/lockbox.h"

namespace cryptohome {
namespace {
// Permissions of cache file (modulo umask).
const mode_t kCacheFilePermissions = 0644;
// Permissions of persistent file (modulo umask).
const mode_t kPersistentFilePermissions = 0644;
// An indicator to indicate that this is a device where we restored attributes.
const char kRestoredInstallAttributesFile[] =
    "/home/.shadow/install_attributes.restored";
// Record the result of the install attributes restoring process.
const char kInstallAttributesRestoreState[] =
    "Platform.DeviceManagement.InstallAttributesRestoreResult";
// The maximum length of the domain is 253, and assume the user name is
// less than 256, the total length of the username should be less than 512.
const size_t kMaxUsernameLength = 512;
// The maximum length of the device id is 36 (uuid length).
const size_t kMaxDeviceIdLength = 36;

// These values are persisted to logs. Entries should not be renumbered and
// numeric values should never be reused.
enum class RestoreResult {
  kNoDevicePolicy = 0,
  kSuccessWithEmpty = 1,
  kSuccessWtihEnterprise = 2,
  kFailed = 3,
  kMaxValue = kFailed,
};

enum class Mode {
  kConsumerDeviceMode,
  kEnterpriseDeviceMode,
  kLegacyRetailDeviceMode,
  kConsumerKioskDeviceMode,
  kDemoDeviceMode,
};

bool AddSerializedAttribute(SerializedInstallAttributes& attrs,
                            const std::string& name,
                            const std::string& value) {
  SerializedInstallAttributes::Attribute* attr = attrs.add_attributes();
  if (!attr) {
    LOG(ERROR) << "Failed to add a new attribute.";
    return false;
  }

  attr->set_name(name);
  attr->set_value(value + std::string("\0", 1));
  return true;
}

std::optional<SerializedInstallAttributes> SerializedInstallAttributesFromMode(
    Mode mode, const enterprise_management::PolicyData& policy_data) {
  if (policy_data.username().size() > kMaxUsernameLength) {
    LOG(ERROR) << "Username is too long.";
    return std::nullopt;
  }
  if (policy_data.device_id().size() > kMaxDeviceIdLength) {
    LOG(ERROR) << "Device ID is too long.";
    return std::nullopt;
  }
  SerializedInstallAttributes attrs;
  attrs.set_version(1);

  std::string kiosk_enabled;
  std::string enterprise_owned;
  std::string enterprise_mode;
  std::string domain;
  std::string device_id;
  switch (mode) {
    case Mode::kConsumerDeviceMode:
      enterprise_owned = "true";
      enterprise_mode = "consumer";
      domain = policy::ExtractDomainName(policy_data.username());
      device_id = policy_data.device_id();
      break;
    case Mode::kEnterpriseDeviceMode:
      enterprise_owned = "true";
      enterprise_mode = "enterprise";
      domain = policy::ExtractDomainName(policy_data.username());
      device_id = policy_data.device_id();
      break;
    case Mode::kLegacyRetailDeviceMode:
      enterprise_owned = "true";
      enterprise_mode = "kiosk";
      domain = policy::ExtractDomainName(policy_data.username());
      device_id = policy_data.device_id();
      break;
    case Mode::kConsumerKioskDeviceMode:
      kiosk_enabled = "true";
      enterprise_mode = "consumer_kiosk";
      break;
    case Mode::kDemoDeviceMode:
      enterprise_owned = "true";
      enterprise_mode = "demo_mode";
      domain = policy::ExtractDomainName(policy_data.username());
      device_id = policy_data.device_id();
      break;
  }

  if (!AddSerializedAttribute(attrs, "consumer.app_kiosk_enabled",
                              kiosk_enabled)) {
    return std::nullopt;
  }
  if (!AddSerializedAttribute(attrs, "enterprise.owned", enterprise_owned)) {
    return std::nullopt;
  }
  if (!AddSerializedAttribute(attrs, "enterprise.mode", enterprise_mode)) {
    return std::nullopt;
  }
  if (!AddSerializedAttribute(attrs, "enterprise.domain", domain)) {
    return std::nullopt;
  }
  if (!AddSerializedAttribute(attrs, "enterprise.realm", "")) {
    return std::nullopt;
  }
  if (!AddSerializedAttribute(attrs, "enterprise.device_id", device_id)) {
    return std::nullopt;
  }

  return attrs;
}

bool RestoreIfVerificationPasses(const base::FilePath& lockbox_path,
                                 const SerializedInstallAttributes& attrs,
                                 Platform& platform,
                                 LockboxContents& lockbox,
                                 brillo::Blob& lockbox_data) {
  lockbox_data.resize(attrs.ByteSizeLong());

  attrs.SerializeWithCachedSizesToArray(
      static_cast<google::protobuf::uint8*>(lockbox_data.data()));

  if (lockbox.Verify(lockbox_data) !=
      LockboxContents::VerificationResult::kValid) {
    return false;
  }

  // Indicate that we restored the install attributes.
  if (!platform.WriteFileAtomic(base::FilePath(kRestoredInstallAttributesFile),
                                lockbox_data, kPersistentFilePermissions)) {
    LOG(ERROR) << "Failed to write install attributes indicator";
    return false;
  }

  // Restore the install attributes file.
  if (!platform.WriteFileAtomic(lockbox_path, lockbox_data,
                                kPersistentFilePermissions)) {
    LOG(ERROR) << "Failed to write cache file";
    return false;
  }

  return true;
}

bool RestoreEmptyInstallAttributes(const base::FilePath& lockbox_path,
                                   Platform& platform,
                                   LockboxContents& lockbox,
                                   brillo::Blob& lockbox_data) {
  SerializedInstallAttributes attrs;
  attrs.set_version(1);

  /* Try to restore with empty SerializedInstallAttributes */
  if (!RestoreIfVerificationPasses(lockbox_path, attrs, platform, lockbox,
                                   lockbox_data)) {
    return false;
  }

  LOG(INFO) << "Restored with empty install attributes successfully.";
  return true;
}

bool RestoreEnterpriseInstallAttributes(
    const base::FilePath& lockbox_path,
    const enterprise_management::PolicyData& policy_data,
    Platform& platform,
    LockboxContents& lockbox,
    brillo::Blob& lockbox_data) {
  for (Mode mode : {
           Mode::kEnterpriseDeviceMode,
           Mode::kDemoDeviceMode,
           Mode::kConsumerKioskDeviceMode,
           Mode::kLegacyRetailDeviceMode,
           Mode::kConsumerDeviceMode,
       }) {
    std::optional<SerializedInstallAttributes> attrs =
        SerializedInstallAttributesFromMode(mode, policy_data);
    if (!attrs.has_value()) {
      continue;
    }
    if (RestoreIfVerificationPasses(lockbox_path, *attrs, platform, lockbox,
                                    lockbox_data)) {
      LOG(INFO) << "Restored with enterprise (mode= " << static_cast<int>(mode)
                << ") install attributes successfully.";
      return true;
    }
  }

  return false;
}

bool RestoreInstallAttributes(const base::FilePath& lockbox_path,
                              Platform& platform,
                              LockboxContents& lockbox,
                              brillo::Blob& lockbox_data) {
  MetricsLibrary metrics;

  policy::DevicePolicyImpl device_policy;
  bool policy_loaded = device_policy.LoadPolicy(/*delete_invalid_files=*/false);
  if (device_policy.get_number_of_policy_files() == 0 || !policy_loaded) {
    LOG(ERROR) << "No valid device policy.";
    metrics.SendEnumToUMA(kInstallAttributesRestoreState,
                          RestoreResult::kNoDevicePolicy);
    return false;
  }

  const enterprise_management::PolicyData& policy_data =
      device_policy.get_policy_data();

  if (RestoreEmptyInstallAttributes(lockbox_path, platform, lockbox,
                                    lockbox_data)) {
    metrics.SendEnumToUMA(kInstallAttributesRestoreState,
                          RestoreResult::kSuccessWithEmpty);
    return true;
  }
  if (RestoreEnterpriseInstallAttributes(lockbox_path, policy_data, platform,
                                         lockbox, lockbox_data)) {
    metrics.SendEnumToUMA(kInstallAttributesRestoreState,
                          RestoreResult::kSuccessWtihEnterprise);
    return true;
  }

  LOG(ERROR) << "Failed to restore install attributes.";
  metrics.SendEnumToUMA(kInstallAttributesRestoreState, RestoreResult::kFailed);
  return false;
}

}  // namespace

bool CacheLockbox(Platform* platform,
                  const base::FilePath& nvram_path,
                  const base::FilePath& lockbox_path,
                  const base::FilePath& cache_path) {
  brillo::SecureBlob nvram;
  if (!platform->ReadFileToSecureBlob(nvram_path, &nvram)) {
    LOG(INFO) << "Failed to read NVRAM contents from " << nvram_path.value();
    return false;
  }
  std::unique_ptr<LockboxContents> lockbox = LockboxContents::New();
  if (!lockbox) {
    LOG(ERROR) << "Unsupported lockbox size!";
    return false;
  }
  if (!lockbox->Decode(nvram)) {
    LOG(ERROR) << "Lockbox failed to decode NVRAM data";
    return false;
  }

  brillo::Blob lockbox_data;
  if (!platform->ReadFile(lockbox_path, &lockbox_data)) {
    LOG(INFO) << "Failed to read lockbox data from " << lockbox_path.value();
    if (!RestoreInstallAttributes(lockbox_path, *platform, *lockbox,
                                  lockbox_data)) {
      return false;
    }
  }
  if (lockbox->Verify(lockbox_data) !=
      LockboxContents::VerificationResult::kValid) {
    LOG(ERROR) << "Lockbox did not verify!";
    if (!RestoreInstallAttributes(lockbox_path, *platform, *lockbox,
                                  lockbox_data)) {
      return false;
    }
  }

  // Write atomically (not durably) because cache file resides on tmpfs.
  if (!platform->WriteFileAtomic(cache_path, lockbox_data,
                                 kCacheFilePermissions)) {
    LOG(ERROR) << "Failed to write cache file";
    return false;
  }

  return true;
}

}  // namespace cryptohome
