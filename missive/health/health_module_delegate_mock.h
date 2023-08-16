// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MISSIVE_HEALTH_HEALTH_MODULE_DELEGATE_MOCK_H_
#define MISSIVE_HEALTH_HEALTH_MODULE_DELEGATE_MOCK_H_

#include <base/functional/callback.h>
#include <base/memory/weak_ptr.h>
#include <base/sequence_checker.h>
#include <base/thread_annotations.h>
#include <gmock/gmock.h>

#include "missive/health/health_module_delegate.h"
#include "missive/proto/health.pb.h"
#include "missive/util/status.h"

namespace reporting {

// Interface between the Health Module and the underlying data that needs to
// be stored. Fully implemented for production, overridden for testing.
class HealthModuleDelegateMockBase : public HealthModuleDelegate {
 public:
  HealthModuleDelegateMockBase() = default;
  HealthModuleDelegateMockBase(const HealthModuleDelegateMockBase&) = delete;
  HealthModuleDelegateMockBase& operator=(const HealthModuleDelegateMockBase&) =
      delete;

  MOCK_METHOD(Status, DoInit, (), (override));
  MOCK_METHOD(void, DoGetERPHealthData, (HealthCallback cb), (const override));
  MOCK_METHOD(void,
              DoPostHealthRecord,
              (HealthDataHistory history),
              (override));
};

using HealthModuleDelegateMock =
    ::testing::NiceMock<HealthModuleDelegateMockBase>;
using HealthModuleDelegateStrictMock =
    ::testing::StrictMock<HealthModuleDelegateMockBase>;
}  // namespace reporting

#endif  // MISSIVE_HEALTH_HEALTH_MODULE_DELEGATE_MOCK_H_
