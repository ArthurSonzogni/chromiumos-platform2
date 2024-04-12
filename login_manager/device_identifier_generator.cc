// Copyright 2014 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "login_manager/device_identifier_generator.h"

#include <map>
#include <utility>

#include <base/functional/bind.h>
#include <base/functional/callback_helpers.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>
#include <base/types/expected.h>
#include <crypto/hmac.h>
#include <crypto/sha2.h>

#include "login_manager/login_metrics.h"
#include "login_manager/system_utils.h"

namespace login_manager {

namespace {

// Characters to trim from values.
const char kTrimChars[] = "\" ";

// Keys in the tool-provided key-value pairs.
const char kGroupCodeKey[] = "gbind_attribute";
const char kSerialNumberKey[] = "serial_number";
const char kDiskSerialNumberKey[] = "root_disk_serial_number";
const char kStableDeviceSecretKey[] = "stable_device_secret_DO_NOT_SHARE";
const char kReEnrollmentKey[] = "re_enrollment_key";
const size_t kHmacInitLength = 32;

// These are the machine serial number keys that we check in order until we find
// a non-empty serial number.
//
// On older Samsung devices the VPD contains two serial numbers: "Product_S/N"
// and "serial_number" which are based on the same value except that the latter
// has a letter appended that serves as a check digit. Unfortunately, the
// sticker on the device packaging didn't include that check digit (the sticker
// on the device did though!). The former sticker was the source of the serial
// number used by device management service, so we preferred "Product_S/N" over
// "serial_number" to match the server. As an unintended consequence, older
// Samsung devices display and report a serial number that doesn't match the
// sticker on the device (the check digit is missing).
//
// "Product_S/N" is known to be used on celes, lumpy, pi, pit, snow, winky and
// some kevin devices and thus needs to be supported until AUE of these
// devices. It's known *not* to be present on caroline.
// TODO(tnagel): Remove "Product_S/N" after all devices that have it are AUE.
const char* const kMachineInfoSerialNumberKeys[] = {
    "Product_S/N",     // Samsung legacy
    kSerialNumberKey,  // VPD v2+ devices
};

// The secret to initialize the hmac instance to generate PSM device
// active secret.
const char kPsmDeviceActiveUsageContext[] = "psm_device_active_secret";

// String constant identifying the device secret usage context.
const char kDeviceSecretUsageContext[] = "server_backed_state_keys";

std::string GetMapValue(const std::map<std::string, std::string>& map,
                        const std::string& key) {
  std::map<std::string, std::string>::const_iterator entry = map.find(key);
  return entry == map.end() ? std::string() : entry->second;
}

}  // namespace

const int DeviceIdentifierGenerator::kDeviceStateKeyTimeQuantumPower;
const int DeviceIdentifierGenerator::kDeviceStateKeyFutureQuanta;

DeviceIdentifierGenerator::DeviceIdentifierGenerator(SystemUtils* system_utils,
                                                     LoginMetrics* metrics)
    : system_utils_(system_utils), metrics_(metrics) {}

DeviceIdentifierGenerator::~DeviceIdentifierGenerator() {}

// static
bool DeviceIdentifierGenerator::ParseMachineInfo(
    const std::string& data,
    const std::map<std::string, std::string>& ro_vpd,
    const std::map<std::string, std::string>& rw_vpd,
    std::map<std::string, std::string>* params) {
  params->clear();

  // Parse the name-value pairs list. The return value of
  // SplitStringIntoKeyValuePairs is deliberately ignored in order to handle
  // comment lines (those start with a #).
  base::StringPairs pairs;
  base::SplitStringIntoKeyValuePairs(data, '=', '\n', &pairs);

  for (base::StringPairs::const_iterator pair(pairs.begin());
       pair != pairs.end(); ++pair) {
    std::string name;
    base::TrimString(pair->first, kTrimChars, &name);
    if (name.empty())
      continue;

    // Use the first pair present in the input.
    if (params->find(name) != params->end())
      continue;

    std::string value;
    base::TrimString(pair->second, kTrimChars, &value);
    (*params)[name] = value;
  }

  // Process RO first, then RW. Earlier contents override later, as we don't
  // want RW (modifiable) to override RO, and we don't want RO or RW to
  // override udev data that's passed in |data|.
  for (const auto& dict : std::array{ro_vpd, rw_vpd}) {
    for (const auto& [name, value] : dict) {
      // Use the first pair present in the input.
      params->try_emplace(name, value);
    }
  }

  return !params->empty();
}

bool DeviceIdentifierGenerator::InitMachineInfo(
    const std::map<std::string, std::string>& params) {
  machine_info_available_ = true;

  for (const char* key : kMachineInfoSerialNumberKeys) {
    std::string candidate = GetMapValue(params, key);
    if (!candidate.empty()) {
      machine_serial_number_ = candidate;
      break;
    }
  }
  group_code_key_ = GetMapValue(params, kGroupCodeKey);
  disk_serial_number_ = GetMapValue(params, kDiskSerialNumberKey);
  stable_device_secret_ = GetMapValue(params, kStableDeviceSecretKey);
  re_enrollment_key_ = GetMapValue(params, kReEnrollmentKey);

  LOG_IF(INFO, re_enrollment_key_.empty()) << "Re-enrollment key missing!";
  LOG_IF(INFO, stable_device_secret_.empty())
      << "Stable device secret missing!";
  LOG_IF(INFO, machine_serial_number_.empty())
      << "Machine serial number missing!";
  LOG_IF(INFO, disk_serial_number_.empty()) << "Disk serial number missing!";

  // Fire all pending state_keys callbacks.
  const base::expected<StateKeysList, StateKeysComputationError> state_keys =
      ComputeKeys();
  std::vector<StateKeyCallback> callbacks;
  callbacks.swap(pending_callbacks_);
  for (auto& callback : callbacks) {
    std::move(callback).Run(state_keys);
  }

  // Fire all pending PSM device active secret callbacks.
  std::string derived_secret;
  DerivePsmDeviceActiveSecret(&derived_secret);
  std::vector<PsmDeviceActiveSecretCallback> psm_device_secret_callbacks;
  psm_device_secret_callbacks.swap(pending_psm_device_secret_callbacks_);
  for (auto& callback : psm_device_secret_callbacks) {
    std::move(callback).Run(derived_secret);
  }

  return !re_enrollment_key_.empty() || !stable_device_secret_.empty() ||
         (!machine_serial_number_.empty() && !disk_serial_number_.empty());
}

void DeviceIdentifierGenerator::RequestStateKeys(StateKeyCallback callback) {
  if (!machine_info_available_) {
    pending_callbacks_.push_back(std::move(callback));
    return;
  }

  std::move(callback).Run(ComputeKeys());
}

base::expected<DeviceIdentifierGenerator::StateKeysList,
               DeviceIdentifierGenerator::StateKeysComputationError>
DeviceIdentifierGenerator::ComputeKeys() {
  StateKeysList state_keys;

  // If we use a re-enrollment key, return its value if it's valid, as is.
  if (!re_enrollment_key_.empty()) {
    std::vector<uint8_t> key_bytes;
    if (!base::HexStringToBytes(re_enrollment_key_, &key_bytes) ||
        key_bytes.size() < 32) {
      metrics_->SendStateKeyGenerationStatus(
          LoginMetrics::STATE_KEY_STATUS_BAD_RE_ENROLLMENT_KEY);
      LOG(ERROR) << "Malformed re-enrollment key, length: "
                 << re_enrollment_key_.size()
                 << ". Bytes found: " << key_bytes.size() << ".";
      return base::unexpected(
          StateKeysComputationError::kMalformedReEnrollmentKey);
    }

    state_keys.push_back(key_bytes);
    metrics_->SendStateKeyGenerationStatus(
        LoginMetrics::STATE_KEY_STATUS_GENERATION_METHOD_RE_ENROLLMENT_KEY);
  } else {
    // Get the current time in quantized form.
    const int64_t quantum_size = 1 << kDeviceStateKeyTimeQuantumPower;
    int64_t quantized_time = system_utils_->time(nullptr) & ~(quantum_size - 1);

    // Compute the state keys.
    if (!stable_device_secret_.empty()) {
      crypto::HMAC hmac(crypto::HMAC::SHA256);
      std::vector<uint8_t> secret_bytes;
      if (!base::HexStringToBytes(stable_device_secret_, &secret_bytes) ||
          secret_bytes.size() < 32) {
        metrics_->SendStateKeyGenerationStatus(
            LoginMetrics::STATE_KEY_STATUS_BAD_DEVICE_SECRET);
        LOG(ERROR) << "Malformed device secret, no state keys generated.";
        return base::unexpected(
            StateKeysComputationError::kMalformedDeviceSecret);
      }
      if (!hmac.Init(secret_bytes.data(), secret_bytes.size())) {
        metrics_->SendStateKeyGenerationStatus(
            LoginMetrics::STATE_KEY_STATUS_HMAC_INIT_FAILURE);
        LOG(ERROR) << "Failed to init HMAC, no state keys generated.";
        return base::unexpected(
            StateKeysComputationError::kHmacInitializationError);
      }

      for (int i = 0; i < kDeviceStateKeyFutureQuanta; ++i) {
        state_keys.push_back(std::vector<uint8_t>(hmac.DigestLength()));
        std::string data_to_sign;
        data_to_sign.append(kDeviceSecretUsageContext);
        data_to_sign.append(1, '\0');
        data_to_sign.append(reinterpret_cast<char*>(&quantized_time),
                            sizeof(quantized_time));
        if (!hmac.Sign(data_to_sign, state_keys.back().data(),
                       state_keys.back().size())) {
          metrics_->SendStateKeyGenerationStatus(
              LoginMetrics::STATE_KEY_STATUS_HMAC_SIGN_FAILURE);
          LOG(ERROR) << "Failed to compute HMAC, no state keys generated.";
          state_keys.clear();
          return base::unexpected(
              StateKeysComputationError::kHmacComputationError);
        }
        quantized_time += quantum_size;
      }
      metrics_->SendStateKeyGenerationStatus(
          LoginMetrics::STATE_KEY_STATUS_GENERATION_METHOD_HMAC_DEVICE_SECRET);
    } else if (!machine_serial_number_.empty() &&
               !disk_serial_number_.empty()) {
      for (int i = 0; i < kDeviceStateKeyFutureQuanta; ++i) {
        state_keys.push_back(StateKey(crypto::kSHA256Length));
        crypto::SHA256HashString(
            crypto::SHA256HashString(group_code_key_) +
                crypto::SHA256HashString(disk_serial_number_) +
                crypto::SHA256HashString(machine_serial_number_) +
                crypto::SHA256HashString(base::NumberToString(quantized_time)),
            state_keys.back().data(), state_keys.back().size());
        quantized_time += quantum_size;
      }
      metrics_->SendStateKeyGenerationStatus(
          LoginMetrics::STATE_KEY_STATUS_GENERATION_METHOD_IDENTIFIER_HASH);
    } else {
      LOG(WARNING)
          << "No device identifiers available, no state keys generated";

      if (machine_serial_number_.empty() && disk_serial_number_.empty()) {
        metrics_->SendStateKeyGenerationStatus(
            LoginMetrics::STATE_KEY_STATUS_MISSING_ALL_IDENTIFIERS);
        return base::unexpected(
            StateKeysComputationError::kMissingAllDeviceIdentifiers);
      }

      if (machine_serial_number_.empty()) {
        metrics_->SendStateKeyGenerationStatus(
            LoginMetrics::STATE_KEY_STATUS_MISSING_MACHINE_SERIAL_NUMBER);
        return base::unexpected(
            StateKeysComputationError::kMissingSerialNumber);
      }

      DCHECK(disk_serial_number_.empty());
      metrics_->SendStateKeyGenerationStatus(
          LoginMetrics::STATE_KEY_STATUS_MISSING_DISK_SERIAL_NUMBER);
      return base::unexpected(
          StateKeysComputationError::kMissingDiskSerialNumber);
    }
  }

  LOG(WARNING) << "State keys successfully generated. Number of keys: "
               << state_keys.size() << ".";

  return state_keys;
}

void DeviceIdentifierGenerator::RequestPsmDeviceActiveSecret(
    PsmDeviceActiveSecretCallback callback) {
  if (!machine_info_available_) {
    pending_psm_device_secret_callbacks_.push_back(std::move(callback));
    return;
  }

  std::string derived_secret;
  DerivePsmDeviceActiveSecret(&derived_secret);
  std::move(callback).Run(derived_secret);
}

void DeviceIdentifierGenerator::DerivePsmDeviceActiveSecret(
    std::string* derived_secret) {
  if (!stable_device_secret_.empty()) {
    crypto::HMAC hmac(crypto::HMAC::SHA256);
    unsigned char secret_key[kHmacInitLength];

    bool result_status =
        hmac.Init(kPsmDeviceActiveUsageContext) &&
        hmac.Sign(stable_device_secret_, secret_key, kHmacInitLength);

    if (!result_status) {
      LOG(ERROR) << "The generation of PSM device active secret is failure.";
      return;
    }

    *derived_secret = base::HexEncode(secret_key, kHmacInitLength);
  } else {
    LOG(ERROR) << "No stable device secret available.";
  }
}

const std::vector<DeviceIdentifierGenerator::StateKeyCallback>&
DeviceIdentifierGenerator::GetPendingCallbacksForTesting() const {
  return pending_callbacks_;
}

}  // namespace login_manager
