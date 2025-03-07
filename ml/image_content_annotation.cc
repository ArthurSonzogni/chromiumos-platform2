// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ml/image_content_annotation.h"

#include <string>

#include <base/files/file_path.h>
#include <base/logging.h>
#include <base/native_library.h>
#include <ml_core/raid_interface.h>

#include "chrome/knowledge/raid/raid.pb.h"

namespace ml {

ImageContentAnnotationLibrary::ImageContentAnnotationLibrary(
    const base::FilePath& dso_path) {
  // Load the library with an option preferring own symbols. Otherwise the
  // library will try to call, e.g., external tflite, which leads to crash.
  base::NativeLibraryOptions native_library_options;
  native_library_options.prefer_own_symbols = true;
  base::NativeLibraryLoadError error;
  library_.reset(base::LoadNativeLibraryWithOptions(
      dso_path, native_library_options, &error));
  if (!library_.is_valid()) {
    LOG(ERROR) << "Error loading library: " << error.ToString();
    status_ = Status::kLoadLibraryFailed;
    return;
  }

#define ML_ICA_LOOKUP_FUNCTION(function_ptr, name)                    \
  function_ptr =                                                      \
      reinterpret_cast<name##Fn>(library_.GetFunctionPointer(#name)); \
  if (function_ptr == NULL) {                                         \
    status_ = Status::kFunctionLookupFailed;                          \
    return;                                                           \
  }
  // Look up the function pointers.
  ML_ICA_LOOKUP_FUNCTION(create_image_annotator_,
                         cros_ml_raid_CreateImageAnnotator);
  ML_ICA_LOOKUP_FUNCTION(destroy_image_annotator_,
                         cros_ml_raid_DestroyImageAnnotator);
  ML_ICA_LOOKUP_FUNCTION(init_image_annotator_,
                         cros_ml_raid_InitImageAnnotator);
  ML_ICA_LOOKUP_FUNCTION(detect_, cros_ml_raid_Detect);
  ML_ICA_LOOKUP_FUNCTION(detect_encoded_image_,
                         cros_ml_raid_DetectEncodedImage);
  ML_ICA_LOOKUP_FUNCTION(delete_detect_image_result_,
                         cros_ml_raid_DeleteDetectImageResult);

  status_ = Status::kOk;
  return;
}

ImageContentAnnotationLibrary* ImageContentAnnotationLibrary::GetInstance(
    const base::FilePath& dso_path) {
  static base::NoDestructor<ImageContentAnnotationLibrary> instance(dso_path);
  return instance.get();
}

ImageContentAnnotationLibrary::Status ImageContentAnnotationLibrary::GetStatus()
    const {
  return status_;
}

RaidV2ImageAnnotator* ImageContentAnnotationLibrary::CreateImageAnnotator() {
  DCHECK(status_ == Status::kOk);
  return (*create_image_annotator_)();
}

void ImageContentAnnotationLibrary::DestroyImageAnnotator(
    RaidV2ImageAnnotator* annotator) {
  DCHECK(status_ == Status::kOk);
  (*destroy_image_annotator_)(annotator);
}

bool ImageContentAnnotationLibrary::InitImageAnnotator(
    RaidV2ImageAnnotator* annotator) {
  DCHECK(status_ == Status::kOk);
  return (*init_image_annotator_)(annotator);
}

bool ImageContentAnnotationLibrary::Detect(
    RaidV2ImageAnnotator* annotator,
    const uint8_t* rgb_bytes,
    int width,
    int height,
    chrome_knowledge::DetectionResultList* result) {
  DCHECK(status_ == Status::kOk);
  uint8_t* result_data = nullptr;
  int32_t result_size = 0;
  bool successful = (*detect_)(annotator, rgb_bytes, width, height,
                               &result_data, &result_size);
  if (successful) {
    result->Clear();
    const bool parse_result_status =
        result->ParseFromArray(result_data, result_size);
    DCHECK(parse_result_status);
    (*delete_detect_image_result_)(result_data);
  }
  return successful;
}

bool ImageContentAnnotationLibrary::DetectEncodedImage(
    RaidV2ImageAnnotator* annotator,
    const uint8_t* encoded_bytes,
    int num_bytes,
    chrome_knowledge::DetectionResultList* result) {
  DCHECK(status_ == Status::kOk);
  uint8_t* result_data = nullptr;
  int32_t result_size = 0;
  bool successful = (*detect_encoded_image_)(
      annotator, encoded_bytes, num_bytes, &result_data, &result_size);
  if (successful) {
    result->Clear();
    const bool parse_result_status =
        result->ParseFromArray(result_data, result_size);
    DCHECK(parse_result_status);
    (*delete_detect_image_result_)(result_data);
  }
  return successful;
}

}  // namespace ml
