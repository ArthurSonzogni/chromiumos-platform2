// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>
#include <utility>

#include <gtest/gtest.h>

#include "runtime_probe/functions/generic_camera.h"
#include "runtime_probe/functions/mipi_camera.h"
#include "runtime_probe/functions/usb_camera.h"
#include "runtime_probe/utils/function_test_utils.h"

namespace runtime_probe {
namespace {

class Fake1GenericCameraFunction : public GenericCameraFunction {
  using GenericCameraFunction::GenericCameraFunction;

 public:
  std::unique_ptr<UsbCameraFunction> GetUsbProber(
      const base::Value& dict_value) override {
    return CreateFakeProbeFunction<UsbCameraFunction>(R"JSON(
      [{
        "bus_type": "usb",
        "usb_product_id": "1234",
        "usb_vendor_id": "5678",
        "path": "/dev/video0"
      }]
    )JSON");
  }

  std::unique_ptr<MipiCameraFunction> GetMipiProber(
      const base::Value& dict_value) override {
    return CreateFakeProbeFunction<MipiCameraFunction>(R"JSON(
      [{
        "bus_type": "mipi",
        "mipi_module_id": "TC04d2",
        "mipi_sysfs_name": "ABC-00/ABC-1234",
        "mipi_sensor_id": "OV10e1",
        "path": "/sys/devices/XXX/nvmem"
      },
      {
        "bus_type": "mipi",
        "mipi_name": "AAAA",
        "mipi_vendor": "BBBB",
        "path": "/sys/devices/XXX/v4l-subdev0"
      }]
    )JSON");
  }
};

class Fake2GenericCameraFunction : public GenericCameraFunction {
  using GenericCameraFunction::GenericCameraFunction;

 public:
  std::unique_ptr<UsbCameraFunction> GetUsbProber(
      const base::Value& dict_value) override {
    return CreateFakeProbeFunction<UsbCameraFunction>(R"JSON(
      [{
        "bus_type": "usb",
        "usb_product_id": "1234",
        "usb_vendor_id": "5678",
        "path": "/dev/video0"
      }]
    )JSON");
  }

  std::unique_ptr<MipiCameraFunction> GetMipiProber(
      const base::Value& dict_value) override {
    // MIPI camera probe function initialization failed.
    return nullptr;
  }
};

class Fake3GenericCameraFunction : public GenericCameraFunction {
  using GenericCameraFunction::GenericCameraFunction;

 public:
  std::unique_ptr<UsbCameraFunction> GetUsbProber(
      const base::Value& dict_value) override {
    return CreateFakeProbeFunction<UsbCameraFunction>(R"JSON(
      []
    )JSON");
  }

  std::unique_ptr<MipiCameraFunction> GetMipiProber(
      const base::Value& dict_value) override {
    return CreateFakeProbeFunction<MipiCameraFunction>(R"JSON(
      []
    )JSON");
  }
};

class GenericCameraFunctionTest : public BaseFunctionTest {};

TEST_F(GenericCameraFunctionTest, ProbeGenericCamera) {
  base::Value probe_statement(base::Value::Type::DICT);
  auto probe_function =
      CreateProbeFunction<Fake1GenericCameraFunction>(probe_statement);

  auto result = probe_function->Eval();
  auto ans = CreateProbeResultFromJson(R"JSON(
    [{
      "bus_type": "usb",
      "usb_product_id": "1234",
      "usb_vendor_id": "5678",
      "path": "/dev/video0"
    },
    {
      "bus_type": "mipi",
      "mipi_module_id": "TC04d2",
      "mipi_sysfs_name": "ABC-00/ABC-1234",
      "mipi_sensor_id": "OV10e1",
      "path": "/sys/devices/XXX/nvmem"
    },
    {
      "bus_type": "mipi",
      "mipi_name": "AAAA",
      "mipi_vendor": "BBBB",
      "path": "/sys/devices/XXX/v4l-subdev0"
    }]
  )JSON");
  EXPECT_EQ(result, ans);
}

TEST_F(GenericCameraFunctionTest, ProbeEmptyResults) {
  base::Value probe_statement(base::Value::Type::DICT);
  auto probe_function =
      CreateProbeFunction<Fake3GenericCameraFunction>(probe_statement);

  auto result = probe_function->Eval();
  auto ans = CreateProbeResultFromJson(R"JSON(
    []
  )JSON");
  EXPECT_EQ(result, ans);
}

TEST_F(GenericCameraFunctionTest, ProberInitilizationFailed) {
  base::Value probe_statement(base::Value::Type::DICT);
  auto probe_function =
      CreateProbeFunction<Fake2GenericCameraFunction>(probe_statement);

  EXPECT_EQ(probe_function, nullptr);
}

}  // namespace
}  // namespace runtime_probe
