// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef HARDWARE_VERIFIER_ENCODING_SPEC_LOADER_H_
#define HARDWARE_VERIFIER_ENCODING_SPEC_LOADER_H_

#include <memory>
#include <vector>

#include <base/files/file_path.h>

#include "hardware_verifier/hardware_verifier.pb.h"

namespace hardware_verifier {

// A loader class that loads the encoding spec payload.
class EncodingSpecLoader {
 public:
  EncodingSpecLoader() = default;
  EncodingSpecLoader(const EncodingSpecLoader&) = delete;
  EncodingSpecLoader& operator=(const EncodingSpecLoader&) = delete;

  // Loads the encoding spec.
  // Return |std::nullptr| if loading fails.
  std::unique_ptr<EncodingSpec> Load() const;

 private:
  std::vector<base::FilePath> GetPaths() const;
};

}  // namespace hardware_verifier

#endif  // HARDWARE_VERIFIER_ENCODING_SPEC_LOADER_H_
