// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ODML_MANTIS_LIB_API_H_
#define ODML_MANTIS_LIB_API_H_

#include <string>

namespace mantis {

// Opaque handle to a Processor.
using ProcessorPtr = uintptr_t;
// Opaque handle to an instance of a Segmenter.
using SegmenterPtr = uintptr_t;

struct MantisComponent {
  ProcessorPtr processor;
  SegmenterPtr segmenter;
};

// Table of C API functions defined within the library.
struct MantisAPI {
  // Initializes the Processor and Segmenter.
  MantisComponent (*Initialize)(std::string assets_path_dir);

  void (*DestroyMantisComponent)(MantisComponent component);
};

}  // namespace mantis

#endif  // ODML_MANTIS_LIB_API_H_
