// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ODML_MANTIS_LIB_API_H_
#define ODML_MANTIS_LIB_API_H_

#include <string>
#include <vector>

namespace mantis {

// Opaque handle to a Processor.
using ProcessorPtr = uintptr_t;
// Opaque handle to an instance of a Segmenter.
using SegmenterPtr = uintptr_t;

struct MantisComponent {
  ProcessorPtr processor;
  SegmenterPtr segmenter;
};

enum class MantisStatus : int {
  kOk,
  kProcessorNotInitialized,
  kInputError,
  kProcessFailed,
  kMissingSegmenter,
};

struct InpaintingResult {
  MantisStatus status;
  std::vector<uint8_t> image;
  std::vector<uint8_t> generated_region;
};

struct OutpaintingResult {
  MantisStatus status;
  std::vector<uint8_t> image;
  std::vector<uint8_t> generated_region;
};

struct GenerativeFillResult {
  MantisStatus status;
  std::vector<uint8_t> image;
  std::vector<uint8_t> generated_region;
};

struct SegmentationResult {
  MantisStatus status;
  std::vector<uint8_t> image;
};

// Table of C API functions defined within the library.
struct MantisAPI {
  // Initializes the Processor and Segmenter.
  MantisComponent (*Initialize)(const std::string& assets_path_dir);

  // Runs inpainting on the given image and mask.
  InpaintingResult (*Inpainting)(ProcessorPtr processor_ptr,
                                 const std::vector<uint8_t>& image,
                                 const std::vector<uint8_t>& mask,
                                 int seed);

  // Runs outpainting on the given image and mask.
  OutpaintingResult (*Outpainting)(ProcessorPtr processor_ptr,
                                   const std::vector<uint8_t>& image,
                                   const std::vector<uint8_t>& mask,
                                   int seed);

  // Runs generative fill on the given image and mask.
  GenerativeFillResult (*GenerativeFill)(ProcessorPtr processor_ptr,
                                         const std::vector<uint8_t>& image,
                                         const std::vector<uint8_t>& mask,
                                         int seed,
                                         const std::string& text_prompt);

  // Runs segmentation on the given image and prior.
  SegmentationResult (*Segmentation)(SegmenterPtr segmenter_ptr,
                                     const std::vector<uint8_t>& image,
                                     const std::vector<uint8_t>& prior);

  void (*DestroyMantisComponent)(MantisComponent component);
};

}  // namespace mantis

#endif  // ODML_MANTIS_LIB_API_H_
