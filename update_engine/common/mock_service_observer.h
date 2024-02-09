// Copyright 2017 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UPDATE_ENGINE_COMMON_MOCK_SERVICE_OBSERVER_H_
#define UPDATE_ENGINE_COMMON_MOCK_SERVICE_OBSERVER_H_

#include <gmock/gmock.h>
#include "update_engine/common/service_observer_interface.h"

namespace chromeos_update_engine {

class MockServiceObserver : public ServiceObserverInterface {
 public:
  MOCK_METHOD1(
      SendStatusUpdate,
      void(const update_engine::UpdateEngineStatus& update_engine_status));
};

}  // namespace chromeos_update_engine

#endif  // UPDATE_ENGINE_COMMON_MOCK_SERVICE_OBSERVER_H_
