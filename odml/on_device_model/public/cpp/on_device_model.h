// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ODML_ON_DEVICE_MODEL_PUBLIC_CPP_ON_DEVICE_MODEL_H_
#define ODML_ON_DEVICE_MODEL_PUBLIC_CPP_ON_DEVICE_MODEL_H_

#include <base/types/expected.h>

#include <memory>
#include <string>

#include "odml/mojom/on_device_model.mojom.h"

namespace on_device_model {

// An interface mirroring mojom::OnDeviceModel to avoid having the internal
// library depend on the mojom interfaces directly.
class OnDeviceModel {
 public:
  virtual ~OnDeviceModel() = default;

  // An interface mirroring mojom::Session to avoid having the internal library
  // depend on the mojom interfaces directly.
  class Session {
   public:
    virtual ~Session() = default;

    virtual void AddContext(mojom::InputOptionsPtr input,
                            mojo::PendingRemote<mojom::ContextClient> client,
                            base::OnceClosure on_complete) = 0;
    virtual void Execute(
        mojom::InputOptionsPtr input,
        mojo::PendingRemote<mojom::StreamingResponder> response,
        base::OnceClosure on_complete) = 0;
    virtual void ClearContext() = 0;
    virtual void SizeInTokens(const std::string& text,
                              base::OnceCallback<void(uint32_t)> callback) = 0;
    virtual void Score(const std::string& text,
                       base::OnceCallback<void(float)> callback) = 0;
  };

  virtual std::unique_ptr<Session> CreateSession(
      std::optional<uint32_t> adaptation_id) = 0;
  virtual mojom::SafetyInfoPtr ClassifyTextSafety(const std::string& text) = 0;
  virtual mojom::LanguageDetectionResultPtr DetectLanguage(
      const std::string& text) = 0;
  virtual base::expected<uint32_t, mojom::LoadModelResult> LoadAdaptation(
      mojom::LoadAdaptationParamsPtr params) = 0;
};

}  // namespace on_device_model

#endif  //  ODML_ON_DEVICE_MODEL_PUBLIC_CPP_ON_DEVICE_MODEL_H_
