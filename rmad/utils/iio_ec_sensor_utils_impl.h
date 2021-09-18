// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_UTILS_IIO_EC_SENSOR_UTILS_IMPL_H_
#define RMAD_UTILS_IIO_EC_SENSOR_UTILS_IMPL_H_

#include "rmad/utils/iio_ec_sensor_utils.h"

#include <string>
#include <vector>

#include <base/files/file_path.h>

namespace rmad {

class IioEcSensorUtilsImpl : public IioEcSensorUtils {
 public:
  IioEcSensorUtilsImpl(const std::string& location, const std::string& name);
  ~IioEcSensorUtilsImpl() = default;

  bool GetAvgData(const std::vector<std::string>& channels,
                  int samples,
                  std::vector<double>* avg_data) override;
  bool SetSysValues(const std::vector<std::string>& entries,
                    const std::vector<int>& values) override;

 private:
  // To find out a specific sensor and how to communicate with it, we will check
  // the value in sysfs and then get all the necessary information in the init
  // step.
  void Initialize();

  base::FilePath sysfs_path_;
  int id_;
  double frequency_;
  double scale_;
  bool initialized_;
};

}  // namespace rmad

#endif  // RMAD_UTILS_IIO_EC_SENSOR_UTILS_IMPL_H_
