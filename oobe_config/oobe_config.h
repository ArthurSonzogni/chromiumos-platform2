// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef OOBE_CONFIG_OOBE_CONFIG_H_
#define OOBE_CONFIG_OOBE_CONFIG_H_

#include <string>

#include <base/files/file_path.h>

#include "oobe_config/filesystem/file_handler.h"

namespace oobe_config {

class RollbackData;

// Helper class for saving and restoring rollback data.
class OobeConfig {
 public:
  explicit OobeConfig(FileHandler file_handler = FileHandler());
  OobeConfig(const OobeConfig&) = delete;
  OobeConfig& operator=(const OobeConfig&) = delete;

  ~OobeConfig();

  // Saves the rollback data into an encrypted file.
  bool EncryptedRollbackSave() const;

  // Restores the rollback data from an encrypted file.
  bool EncryptedRollbackRestore() const;

  // Sets a network config which is used instead of requesting network
  // configuration via mojo from Chrome.
  void set_network_config_for_testing(const std::string& config) {
    network_config_for_testing_ = config;
  }

 private:
  // Gets the files needed for rollback and stores them in a |RollbackData|
  // proto, then returns the serialized proto |serialized_rollback_data|.
  bool GetSerializedRollbackData(std::string* serialized_rollback_data) const;

  // Gets the files needed for rollback and returns them in |rollback_data|.
  void GetRollbackData(RollbackData* rollback_data) const;

  FileHandler file_handler_;

  // Network configuration to be used in unit tests instead of requesting
  // network configuration from Chrome.
  std::string network_config_for_testing_;
};

}  // namespace oobe_config

#endif  // OOBE_CONFIG_OOBE_CONFIG_H_
