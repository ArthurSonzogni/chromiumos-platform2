// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_WIFI_MOCK_CELLULAR_SERVICE_PROVIDER_H_
#define SHILL_WIFI_MOCK_CELLULAR_SERVICE_PROVIDER_H_

#include "shill/cellular/cellular_service_provider.h"

#include <gmock/gmock.h>

namespace shill {

class MockCellularServiceProvider : public CellularServiceProvider {
 public:
  MockCellularServiceProvider(Manager* manager)
      : CellularServiceProvider(manager) {}
  ~MockCellularServiceProvider() override = default;

  MOCK_METHOD(
      void,
      AcquireTetheringNetwork,
      (base::OnceCallback<void(TetheringManager::SetEnabledResult, Network*)>),
      ());
  MOCK_METHOD(void,
              ReleaseTetheringNetwork,
              (Network*, base::OnceCallback<void(bool is_success)>),
              ());
};

}  // namespace shill

#endif  // SHILL_WIFI_MOCK_CELLULAR_SERVICE_PROVIDER_H_
