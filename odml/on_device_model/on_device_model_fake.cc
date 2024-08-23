// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "odml/on_device_model/on_device_model_fake.h"

#include <memory>

#include <base/no_destructor.h>
#include <base/strings/string_number_conversions.h>
#include <mojo/public/cpp/bindings/remote.h>
#include <testing/gmock/include/gmock/gmock.h>
#include <testing/gtest/include/gtest/gtest.h>

#include "odml/on_device_model/fake/fake_chrome_ml_api.h"
#include "odml/on_device_model/ml/on_device_model_internal.h"

struct DawnProcTable {};

namespace {
using ::testing::Return;

using DawnNativeProcsGetter = const DawnProcTable* (*)();

const DawnProcTable* GetFakeDawnProcTable() {
  static const DawnProcTable fake;
  return &fake;
}

}  // namespace

namespace on_device_model {

std::unique_ptr<const ml::OnDeviceModelInternalImpl> GetOnDeviceModelFakeImpl(
    raw_ref<MetricsLibraryInterface> metrics,
    raw_ref<odml::OdmlShimLoaderMock> shim_loader) {
  EXPECT_CALL(*shim_loader, GetFunctionPointer("GetChromeMLAPI"))
      .WillRepeatedly(Return(reinterpret_cast<void*>(ChromeMLAPIGetter(
          []() -> const ChromeMLAPI* { return fake_ml::GetFakeMlApi(); }))));

  EXPECT_CALL(*shim_loader, GetFunctionPointer("GetDawnNativeProcs"))
      .WillRepeatedly(Return(reinterpret_cast<void*>(DawnNativeProcsGetter(
          []() -> const DawnProcTable* { return GetFakeDawnProcTable(); }))));

  return std::make_unique<ml::OnDeviceModelInternalImpl>(
      metrics, shim_loader, ml::GpuBlocklist{.skip_for_testing = true});
}

}  // namespace on_device_model
