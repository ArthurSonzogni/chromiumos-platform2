// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LORGNETTE_SCAN_PARAMETERS_H_
#define LORGNETTE_SCAN_PARAMETERS_H_

namespace lorgnette {

// The subset of SANE_Frame values that are supported.
enum FrameFormat {
  kGrayscale,
  kRGB,
};

// The subset of SANE_Parameters needed for frame formats we know how to decode.
struct ScanParameters {
  FrameFormat format;
  int bytes_per_line;
  int pixels_per_line;
  int lines;
  int depth;
};

}  // namespace lorgnette

#endif  // LORGNETTE_SCAN_PARAMETERS_H_
