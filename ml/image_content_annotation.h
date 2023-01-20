// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ML_IMAGE_CONTENT_ANNOTATION_H_
#define ML_IMAGE_CONTENT_ANNOTATION_H_

#include <optional>

#include <base/no_destructor.h>
#include <base/scoped_native_library.h>
#include <chromeos/libica/interface.h>

#include "chrome/knowledge/ica/ica.pb.h"
#include "ml/util.h"

namespace ml {

// A singleton proxy class for the Image Content Annotation DSO.
// Usage:
//   auto* const library = ImageContentAnnotationLibrary::GetInstance();
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

  // Returns whether Image Content Annotation is supported.
  static constexpr bool IsImageContentAnnotationSupported() {
    return IsUseImageContentAnnotationEnabled();
  }

  // Returns bool of use.ondevice_handwriting.
  static constexpr bool IsUseImageContentAnnotationEnabled() {
    return USE_ONDEVICE_IMAGE_CONTENT_ANNOTATION;
  }

  static ImageContentAnnotationLibrary* GetInstance();

  // Get whether the library is successfully initialized.
  // Initially, the status is `Status::kUninitialized` (this value should never
  // be returned).
  // If libica.so can not be loaded, return `kLoadLibraryFailed`. This
  // usually means on-device image content annotation is not supported.
  // If the functions can not be successfully looked up, return
  // `kFunctionLookupFailed`.
  // Return `Status::kOk` if everything works fine.
  virtual Status GetStatus() const;

  virtual ImageContentAnnotator* CreateImageContentAnnotator();
  virtual void DestroyImageContentAnnotator(ImageContentAnnotator* annotator);

  virtual bool InitImageContentAnnotator(ImageContentAnnotator* annotator,
                                         const char* locale);
  virtual bool AnnotateImage(ImageContentAnnotator* annotator,
                             uint8_t* rgb_bytes,
                             int width,
                             int height,
                             int line_stride,
                             chrome_knowledge::AnnotationScoreList* result);

 protected:
  ImageContentAnnotationLibrary();
  virtual ~ImageContentAnnotationLibrary() = default;

 private:
  friend class base::NoDestructor<ImageContentAnnotationLibrary>;

  base::ScopedNativeLibrary library_;
  Status status_ = Status::kUninitialized;
  CreateImageContentAnnotatorFn create_image_content_annotator_ = nullptr;
  DestroyImageContentAnnotatorFn destroy_image_content_annotator_ = nullptr;
  InitImageContentAnnotatorFn init_image_content_annotator_ = nullptr;
  AnnotateImageFn annotate_image_ = nullptr;
  DeleteAnnoteImageResultFn delete_annotate_image_result_ = nullptr;
};

}  // namespace ml

#endif  // ML_IMAGE_CONTENT_ANNOTATION_H_
