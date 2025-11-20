// Copyright 2026 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef POWER_MANAGER_POWERD_SYSTEM_THERMAL_EC_FAN_READER_H_
#define POWER_MANAGER_POWERD_SYSTEM_THERMAL_EC_FAN_READER_H_

#include <optional>

#include <chromeos/ec/ec_commands.h>
#include <libec/ec_command_factory.h>

namespace power_manager::system {

class EcFanReaderInterface {
 public:
  EcFanReaderInterface() = default;
  EcFanReaderInterface(const EcFanReaderInterface&) = delete;
  EcFanReaderInterface& operator=(const EcFanReaderInterface&) = delete;

  virtual ~EcFanReaderInterface() = default;

  virtual void Init(const base::FilePath& cros_ec_path,
                    ec::EcCommandFactoryInterface* ec_command_factory) = 0;
  // Returns the current highest fan speed from the EC
  virtual std::optional<uint16_t> GetCurrentHighestFanSpeed() = 0;
};

class EcFanReader : public EcFanReaderInterface {
 public:
  EcFanReader() = default;
  EcFanReader(const EcFanReader&) = delete;
  EcFanReader& operator=(const EcFanReader&) = delete;

  void Init(const base::FilePath& cros_ec_path,
            ec::EcCommandFactoryInterface* ec_command_factory) override;
  std::optional<uint16_t> GetCurrentHighestFanSpeed() override;

 private:
  // File for communicating with the Embedded Controller (EC).
  base::FilePath cros_ec_path_;

  ec::EcCommandFactoryInterface* ec_command_factory_ = nullptr;  // non-owned
};

}  // namespace power_manager::system

#endif  // POWER_MANAGER_POWERD_SYSTEM_THERMAL_EC_FAN_READER_H_
