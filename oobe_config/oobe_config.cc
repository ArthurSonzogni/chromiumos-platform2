// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "oobe_config/oobe_config.h"

#include <optional>
#include <utility>

#include <base/check.h>
#include <base/check_op.h>
#include <base/files/file_enumerator.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <brillo/secure_blob.h>

#include "oobe_config/encryption/openssl_encryption.h"
#include "oobe_config/encryption/pstore_storage.h"
#include "oobe_config/network_exporter.h"
#include "oobe_config/rollback_data.pb.h"

namespace oobe_config {

OobeConfig::OobeConfig(FileHandler file_handler)
    : file_handler_(std::move(file_handler)) {}
OobeConfig::~OobeConfig() = default;

void OobeConfig::GetRollbackData(RollbackData* rollback_data) const {
  if (file_handler_.HasOobeCompletedFlag()) {
    // If OOBE has been completed already, we know the EULA has been accepted.
    rollback_data->set_eula_auto_accept(true);
  }

  if (file_handler_.HasMetricsReportingEnabledFlag()) {
    rollback_data->set_eula_send_statistics(true);
  }

  if (network_config_for_testing_.empty()) {
    std::optional<std::string> network_config =
        oobe_config::ExportNetworkConfig();
    if (network_config.has_value()) {
      rollback_data->set_network_config(*network_config);
    }
  } else {
    rollback_data->set_network_config(network_config_for_testing_);
  }

  return;
}

bool OobeConfig::GetSerializedRollbackData(
    std::string* serialized_rollback_data) const {
  RollbackData rollback_data;
  GetRollbackData(&rollback_data);

  if (!rollback_data.SerializeToString(serialized_rollback_data)) {
    LOG(ERROR) << "Couldn't serialize proto.";
    return false;
  }

  return true;
}

bool OobeConfig::EncryptedRollbackSave() const {
  std::string serialized_rollback_data;
  if (!GetSerializedRollbackData(&serialized_rollback_data)) {
    return false;
  }

  // Encrypt data with software and store the key in pstore.
  // TODO(crbug/1212958) add TPM based encryption.

  std::optional<EncryptedData> encrypted_rollback_data =
      Encrypt(brillo::SecureBlob(serialized_rollback_data));

  if (!encrypted_rollback_data) {
    LOG(ERROR) << "Failed to encrypt, not saving any rollback data.";
    return false;
  }

  if (!StageForPstore(encrypted_rollback_data->key.to_string(),
                      file_handler_)) {
    LOG(ERROR)
        << "Failed to prepare data for storage in the encrypted reboot vault";
    return false;
  }

  if (!file_handler_.WriteEncryptedRollbackData(
          brillo::BlobToString(encrypted_rollback_data->data))) {
    LOG(ERROR) << "Failed to write encrypted rollback data file.";
    return false;
  }

  if (!file_handler_.CreateDataSavedFlag()) {
    LOG(ERROR) << "Failed to write data saved flag.";
    return false;
  }

  return true;
}

bool OobeConfig::EncryptedRollbackRestore() const {
  LOG(INFO) << "Fetching key from pstore.";
  std::optional<std::string> key = LoadFromPstore(file_handler_);
  if (!key.has_value()) {
    LOG(ERROR) << "Failed to load key from pstore.";
    return false;
  }

  std::string encrypted_data;
  if (!file_handler_.ReadEncryptedRollbackData(&encrypted_data)) {
    return false;
  }
  std::optional<brillo::SecureBlob> decrypted_data = Decrypt(
      {brillo::BlobFromString(encrypted_data), brillo::SecureBlob(*key)});
  if (!decrypted_data.has_value()) {
    LOG(ERROR) << "Could not decrypt rollback data.";
    return false;
  }

  std::string rollback_data_str = decrypted_data->to_string();

  // Write the unencrypted data immediately to
  // kEncryptedStatefulRollbackDataPath.
  if (!file_handler_.WriteDecryptedRollbackData(rollback_data_str)) {
    return false;
  }

  RollbackData rollback_data;
  if (!rollback_data.ParseFromString(rollback_data_str)) {
    LOG(ERROR) << "Couldn't parse proto.";
    return false;
  }
  LOG(INFO) << "Parsed decrypted rollback data";

  return true;
}

}  // namespace oobe_config
