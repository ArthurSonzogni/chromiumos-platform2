// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "odml/on_device_model/fake/on_device_model_fake.h"

#include <testing/gmock/include/gmock/gmock.h>
#include <testing/gtest/include/gtest/gtest.h>

#include "odml/on_device_model/fake/fake_chrome_ml_api.h"

struct DawnProcTable {};

namespace {
using ::testing::AnyNumber;
using ::testing::Return;

using DawnNativeProcsGetter = const DawnProcTable* (*)();

const DawnProcTable* GetFakeDawnProcTable() {
  static const DawnProcTable fake;
  return &fake;
}

}  // namespace

namespace fake_ml {

void SetupFakeChromeML(raw_ref<MetricsLibraryInterface> metrics,
                       raw_ref<odml::OdmlShimLoaderMock> shim_loader) {
  // A catch-all such that ON_CALL default actions can be set on shim_loader.
  EXPECT_CALL(*shim_loader, GetFunctionPointer).Times(AnyNumber());
  EXPECT_CALL(*shim_loader, GetFunctionPointer("GetChromeMLAPI"))
      .WillRepeatedly(Return(reinterpret_cast<void*>(ChromeMLAPIGetter(
          []() -> const ChromeMLAPI* { return fake_ml::GetFakeMlApi(); }))));

  EXPECT_CALL(*shim_loader, GetFunctionPointer("GetDawnNativeProcs"))
      .WillRepeatedly(Return(reinterpret_cast<void*>(DawnNativeProcsGetter(
          []() -> const DawnProcTable* { return GetFakeDawnProcTable(); }))));

  return;
}

}  // namespace fake_ml
