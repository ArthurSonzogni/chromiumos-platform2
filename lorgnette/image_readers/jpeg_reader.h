// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LORGNETTE_IMAGE_READERS_JPEG_READER_H_
#define LORGNETTE_IMAGE_READERS_JPEG_READER_H_

#include <stdio.h>  // Needed by jpeglib.h.

#include <memory>

#include <jerror.h>
#include <jpeglib.h>

#include "lorgnette/image_readers/image_reader.h"

namespace lorgnette {

// This class is responsible for reading data from a bitmap and producing a JPEG
// image.
class JpegReader final : public ImageReader {
 public:
  static std::unique_ptr<ImageReader> Create(
      brillo::ErrorPtr* error,
      const ScanParameters& params,
      const base::Optional<int>& resolution,
      base::ScopedFILE out_file);
  ~JpegReader();

  bool ReadRow(brillo::ErrorPtr* error, uint8_t* data) override;
  bool Finalize(brillo::ErrorPtr* error) override;

 private:
  JpegReader(const ScanParameters& params, base::ScopedFILE out_file);
  bool ValidateParams(brillo::ErrorPtr* error) override;
  bool Initialize(brillo::ErrorPtr* error,
                  const base::Optional<int>& resolution) override;

  // Whether or not the libjpeg objects owned by JpegReader have been
  // initialized.
  bool initialized_ = false;

  // Whether or not the JpegReader is in a valid state.
  bool valid_ = false;

  jpeg_compress_struct cinfo_ = {0};
  jpeg_error_mgr jerr_ = {0};
};

}  // namespace lorgnette

#endif  // LORGNETTE_IMAGE_READERS_JPEG_READER_H_
