// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ODML_ON_DEVICE_MODEL_PUBLIC_CPP_TEXT_SAFETY_ASSETS_H_
#define ODML_ON_DEVICE_MODEL_PUBLIC_CPP_TEXT_SAFETY_ASSETS_H_

#include <base/files/file.h>
#include <base/files/file_path.h>
#include <mojo/public/cpp/bindings/receiver.h>
#include <mojo/public/cpp/bindings/remote.h>

#include "odml/mojom/on_device_model.mojom.h"
#include "odml/mojom/on_device_model_service.mojom.h"

namespace on_device_model {

// A bundle of file paths to use for loading an adaptation.
struct TextSafetyAssetPaths {
  TextSafetyAssetPaths();
  TextSafetyAssetPaths(const TextSafetyAssetPaths&);
  ~TextSafetyAssetPaths();

  base::FilePath data;
  base::FilePath sp_model;
};

// A bundle of file paths to use for loading an adaptation.
struct LanguageDetectionAssetPaths {
  LanguageDetectionAssetPaths();
  LanguageDetectionAssetPaths(const LanguageDetectionAssetPaths&);
  ~LanguageDetectionAssetPaths();

  base::FilePath model;
};

struct TextSafetyLoaderParams {
  TextSafetyLoaderParams();
  TextSafetyLoaderParams(const TextSafetyLoaderParams&);
  ~TextSafetyLoaderParams();

  std::optional<TextSafetyAssetPaths> ts_paths;
  std::optional<LanguageDetectionAssetPaths> language_paths;
};

// Load assets for text safety model.
mojom::TextSafetyModelParamsPtr LoadTextSafetyParams(
    TextSafetyLoaderParams params);

}  // namespace on_device_model

#endif  // ODML_ON_DEVICE_MODEL_PUBLIC_CPP_TEXT_SAFETY_ASSETS_H_
