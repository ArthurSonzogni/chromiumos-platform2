// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ODML_ON_DEVICE_MODEL_MOCK_ON_DEVICE_MODEL_SERVICE_H_
#define ODML_ON_DEVICE_MODEL_MOCK_ON_DEVICE_MODEL_SERVICE_H_

#include <string>

#include <base/containers/flat_map.h>
#include <base/uuid.h>
#include <gmock/gmock.h>
#include <mojo/public/cpp/bindings/receiver.h>

#include "odml/mojom/on_device_model.mojom.h"
#include "odml/mojom/on_device_model_service.mojom.h"

namespace on_device_model {

class MockOnDeviceModelService : public mojom::OnDeviceModelPlatformService {
 public:
  MockOnDeviceModelService() = default;

  MOCK_METHOD(void,
              LoadPlatformModel,
              (const base::Uuid& uuid,
               mojo::PendingReceiver<mojom::OnDeviceModel> model,
               mojo::PendingRemote<mojom::PlatformModelProgressObserver>
                   progress_observer,
               LoadPlatformModelCallback callback),
              (override));

  MOCK_METHOD(void,
              GetPlatformModelState,
              (const base::Uuid& uuid, GetPlatformModelStateCallback callback),
              (override));

  MOCK_METHOD(void,
              GetEstimatedPerformanceClass,
              (GetEstimatedPerformanceClassCallback callback),
              (override));

  MOCK_METHOD(void,
              FormatInput,
              (const base::Uuid& uuid,
               mojom::FormatFeature feature,
               (const base::flat_map<std::string, std::string>& fields),
               FormatInputCallback callback),
              (override));

  MOCK_METHOD(void,
              ValidateSafetyResult,
              (mojom::SafetyFeature safety_feature,
               const std::string& text,
               mojom::SafetyInfoPtr safety_info,
               ValidateSafetyResultCallback callback),
              (override));
};

}  // namespace on_device_model

#endif  // ODML_ON_DEVICE_MODEL_MOCK_ON_DEVICE_MODEL_SERVICE_H_
