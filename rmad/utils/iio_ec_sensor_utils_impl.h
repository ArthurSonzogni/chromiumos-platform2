// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_UTILS_IIO_EC_SENSOR_UTILS_IMPL_H_
#define RMAD_UTILS_IIO_EC_SENSOR_UTILS_IMPL_H_

#include "rmad/utils/iio_ec_sensor_utils.h"

#include <memory>
#include <string>
#include <vector>

#include <base/files/file_path.h>

#include "rmad/utils/cmd_utils.h"

namespace rmad {

class IioEcSensorUtilsImpl : public IioEcSensorUtils {
 public:
  explicit IioEcSensorUtilsImpl(const std::string& location,
                                const std::string& name);
  // Used to inject |sysfs_prefix| and |cmd_utils| for testing.
  explicit IioEcSensorUtilsImpl(const std::string& location,
                                const std::string& name,
                                const std::string& sysfs_prefix,
                                std::unique_ptr<CmdUtils> cmd_utils);
  ~IioEcSensorUtilsImpl() = default;

  bool GetAvgData(const std::vector<std::string>& channels,
                  int samples,
                  std::vector<double>* avg_data,
                  std::vector<double>* variance = nullptr) const override;
  bool GetSysValues(const std::vector<std::string>& entries,
                    std::vector<double>* values) const override;

  bool IsInitialized() const { return initialized_; }

 private:
  void Initialize();
  // To find out a specific sensor and how to communicate with it, we will check
  // the value in sysfs and then get all the necessary information in the init
  // step.
  bool InitializeFromSysfsPath(const base::FilePath& sysfs_path);

  std::string sysfs_prefix_;
  base::FilePath sysfs_path_;
  int id_;
  double frequency_;
  double scale_;
  bool initialized_;

  // External utility.
  std::unique_ptr<CmdUtils> cmd_utils_;
};

}  // namespace rmad

#endif  // RMAD_UTILS_IIO_EC_SENSOR_UTILS_IMPL_H_
