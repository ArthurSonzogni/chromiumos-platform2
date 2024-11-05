// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ML_IMAGE_CONTENT_ANNOTATION_H_
#define ML_IMAGE_CONTENT_ANNOTATION_H_

#include <optional>

#include <base/files/file_path.h>
#include <base/no_destructor.h>
#include <base/scoped_native_library.h>
#include <ml_core/raid_interface.h>

#include "chrome/knowledge/raid/raid.pb.h"
#include "ml/util.h"

namespace ml {

// A singleton proxy class for the Image Content Annotation Dynamic Shared
// Object (ICA DSO). Used by ImageContentAnnotatorImpl to call into the DSO.
//
// Usage:
//   auto* const library = ImageContentAnnotationLibrary::GetInstance(dso_path);
//   if (library->GetStatus() == ImageContentAnnotationLibrary::kOk) {
//     annotator = library->CreateImageContentAnnotator();
//     ...
//   } else {
//     ...
//   }
class ImageContentAnnotationLibrary {
 public:
  enum class Status {
    kOk = 0,
    kUninitialized = 1,
    kLoadLibraryFailed = 2,
    kFunctionLookupFailed = 3,
    kNotSupported = 4,
  };

  static ImageContentAnnotationLibrary* GetInstance(
      const base::FilePath& dso_path);

  // Get whether the library is successfully initialized.
  // Initially, the status is `Status::kUninitialized` (this value should never
  // be returned).
  // If libica.so can not be loaded, return `kLoadLibraryFailed`. This
  // usually means on-device image content annotation is not supported.
  // If the functions can not be successfully looked up, return
  // `kFunctionLookupFailed`.
  // Return `Status::kOk` if everything works fine.
  virtual Status GetStatus() const;

  virtual RaidV2ImageAnnotator* CreateImageAnnotator();
  virtual void DestroyImageAnnotator(RaidV2ImageAnnotator* annotator);

  bool InitImageAnnotator(RaidV2ImageAnnotator* annotator);
  virtual bool Detect(RaidV2ImageAnnotator* annotator,
                      const uint8_t* rgb_bytes,
                      int width,
                      int height,
                      chrome_knowledge::DetectionResultList* result);
  virtual bool DetectEncodedImage(
      RaidV2ImageAnnotator* annotator,
      const uint8_t* encoded_bytes,
      int num_bytes,
      chrome_knowledge::DetectionResultList* result);

 protected:
  explicit ImageContentAnnotationLibrary(const base::FilePath& dso_path);
  virtual ~ImageContentAnnotationLibrary() = default;

 private:
  friend class base::NoDestructor<ImageContentAnnotationLibrary>;

  base::ScopedNativeLibrary library_;
  Status status_ = Status::kUninitialized;
  cros_ml_raid_CreateImageAnnotatorFn create_image_annotator_ = nullptr;
  cros_ml_raid_DestroyImageAnnotatorFn destroy_image_annotator_ = nullptr;
  cros_ml_raid_InitImageAnnotatorFn init_image_annotator_ = nullptr;
  cros_ml_raid_DetectFn detect_ = nullptr;
  cros_ml_raid_DetectEncodedImageFn detect_encoded_image_ = nullptr;
  cros_ml_raid_DeleteDetectImageResultFn delete_detect_image_result_ = nullptr;
};

}  // namespace ml

#endif  // ML_IMAGE_CONTENT_ANNOTATION_H_
