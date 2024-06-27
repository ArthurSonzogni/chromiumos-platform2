// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ODML_ON_DEVICE_MODEL_PUBLIC_CPP_MODEL_ASSETS_H_
#define ODML_ON_DEVICE_MODEL_PUBLIC_CPP_MODEL_ASSETS_H_

#include <base/files/file.h>
#include <base/files/file_path.h>

namespace on_device_model {

// A bundle of file paths to use for execution.
struct ModelAssetPaths {
  ModelAssetPaths();
  ModelAssetPaths(const ModelAssetPaths&);
  ~ModelAssetPaths();

  // Returns whether the required models to determine text safety are set.
  bool HasSafetyFiles() const {
    return !ts_data.empty() && !ts_sp_model.empty();
  }

  base::FilePath weights;
  base::FilePath ts_data;
  base::FilePath ts_sp_model;
  base::FilePath language_detection_model;
};

// A bundle of opened file assets comprising model description to use for
// execution.
struct ModelAssets {
  ModelAssets();
  ModelAssets(ModelAssets&&);
  ModelAssets& operator=(ModelAssets&&);
  ~ModelAssets();

  base::File weights;
  base::File ts_data;
  base::File ts_sp_model;
  base::File language_detection_model;
};

// Helper to open files for ModelAssets given their containing paths.
ModelAssets LoadModelAssets(const ModelAssetPaths& paths);

// A bundle of file paths to use for loading an adaptation.
struct AdaptationAssetPaths {
  AdaptationAssetPaths();
  AdaptationAssetPaths(const AdaptationAssetPaths&);
  ~AdaptationAssetPaths();

  base::FilePath weights;
};

// A bundle of opened file assets comprising an adaptation description to use
// for execution.
struct AdaptationAssets {
  AdaptationAssets();
  AdaptationAssets(AdaptationAssets&&);
  AdaptationAssets& operator=(AdaptationAssets&&);
  ~AdaptationAssets();

  base::File weights;
};

// Helper to open files for AdaptationAssets given their containing paths.
AdaptationAssets LoadAdaptationAssets(const AdaptationAssetPaths& paths);

}  // namespace on_device_model

#endif  //  ODML_ON_DEVICE_MODEL_PUBLIC_CPP_MODEL_ASSETS_H_
