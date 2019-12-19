// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MEMS_SETUP_TEST_HELPER_H_
#define MEMS_SETUP_TEST_HELPER_H_

#include <initializer_list>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <libmems/iio_context.h>
#include <libmems/iio_device.h>
#include <libmems/test_fakes.h>
#include "mems_setup/configuration.h"
#include "mems_setup/delegate.h"
#include "mems_setup/sensor_kind.h"
#include "mems_setup/test_fakes.h"

namespace mems_setup {
namespace testing {

class FakeSysfsTrigger : public libmems::fakes::FakeIioDevice {
 public:
  FakeSysfsTrigger(libmems::fakes::FakeIioContext* ctx,
                   libmems::fakes::FakeIioDevice* trigger0)
      : FakeIioDevice(ctx, "iio_sysfs_trigger", -1),
        mock_context_(ctx),
        mock_trigger0_(trigger0) {}

  bool WriteNumberAttribute(const std::string& name, int64_t value) override;

 private:
  libmems::fakes::FakeIioContext* mock_context_;
  libmems::fakes::FakeIioDevice* mock_trigger0_;
};

class SensorTestBase : public ::testing::Test {
 public:
  Configuration* GetConfiguration();

 protected:
  std::unique_ptr<libmems::fakes::FakeIioContext> mock_context_;
  std::unique_ptr<mems_setup::fakes::FakeDelegate> mock_delegate_;
  std::unique_ptr<libmems::fakes::FakeIioDevice> mock_device_;

  std::unique_ptr<libmems::fakes::FakeIioDevice> mock_trigger0_;
  std::unique_ptr<FakeSysfsTrigger> mock_sysfs_trigger_;

  std::unique_ptr<Configuration> config_;

  SensorKind sensor_kind_;

  SensorTestBase(const char* name, int id, SensorKind kind);

  void SetSingleSensor(const char* location);
  void SetSharedSensor();

  void ConfigureVpd(
      std::initializer_list<std::pair<const char*, const char*>> values);

  std::vector<std::unique_ptr<libmems::fakes::FakeIioChannel>> channels_;
};

}  // namespace testing
}  // namespace mems_setup

#endif  // MEMS_SETUP_TEST_HELPER_H_
