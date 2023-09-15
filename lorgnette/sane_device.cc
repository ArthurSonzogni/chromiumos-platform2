// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>
#include <utility>

#include <base/bits.h>
#include <base/logging.h>
#include <dbus/lorgnette/dbus-constants.h>

#include "lorgnette/constants.h"
#include "lorgnette/image_readers/jpeg_reader.h"
#include "lorgnette/image_readers/png_reader.h"
#include "lorgnette/sane_device.h"
#include "lorgnette/uuid_util.h"

namespace lorgnette {

std::vector<std::string> SaneDevice::GetSupportedFormats() const {
  // TODO(bmgordon): When device pass-through is available, add a hook for
  // subclasses to add additional formats.
  return {kJpegMimeType, kPngMimeType};
}

std::optional<std::string> SaneDevice::GetCurrentJob() const {
  return current_job_;
}

void SaneDevice::StartJob() {
  current_job_ = GenerateUUID();
  image_reader_.reset();
  scan_params_.reset();
  completed_lines_ = 0;
}

void SaneDevice::EndJob() {
  current_job_.reset();
}

SANE_Status SaneDevice::PrepareImageReader(brillo::ErrorPtr* error,
                                           ImageFormat format,
                                           FILE* out_file,
                                           size_t* expected_lines) {
  if (!GetCurrentJob().has_value()) {
    brillo::Error::AddTo(error, FROM_HERE, kDbusDomain, kManagerServiceError,
                         "No scan job in progress");
    return SANE_STATUS_INVAL;
  }

  ScanParameters params;
  SANE_Status status = GetScanParameters(error, &params);
  if (status != SANE_STATUS_GOOD) {
    return status;  // brillo::Error::AddTo already called.
  }
  if (params.lines < 1) {
    brillo::Error::AddToPrintf(
        error, FROM_HERE, kDbusDomain, kManagerServiceError,
        "Cannot scan an image with invalid height (%d)", params.lines);
    return SANE_STATUS_INVAL;
  }

  // Get resolution value in DPI so that we can record it in the image.
  brillo::ErrorPtr resolution_error;
  std::optional<int> resolution = GetScanResolution(&resolution_error);
  if (!resolution.has_value()) {
    LOG(WARNING) << __func__ << ": Failed to get scan resolution: "
                 << resolution_error->GetMessage();
  }

  switch (format) {
    case IMAGE_FORMAT_PNG: {
      image_reader_ = PngReader::Create(error, params, resolution, out_file);
      break;
    }
    case IMAGE_FORMAT_JPEG: {
      image_reader_ = JpegReader::Create(error, params, resolution, out_file);
      break;
    }
    default: {
      brillo::Error::AddToPrintf(
          error, FROM_HERE, kDbusDomain, kManagerServiceError,
          "Unrecognized image format: %s", ImageFormat_Name(format).c_str());
      return SANE_STATUS_INVAL;
    }
  }
  if (!image_reader_) {
    brillo::Error::AddToPrintf(error, FROM_HERE, kDbusDomain,
                               kManagerServiceError,
                               "Failed to create image reader for format: %s",
                               ImageFormat_Name(format).c_str());
    return SANE_STATUS_NO_MEM;
  }

  // Allocate a buffer to hold pixel data received from the scanner:
  //   1. At minimum, enough to hold a full line no matter what, because
  //      otherwise the image encoders can't make progress.
  //   2. Big enough to hold approximately 2.5% of the expected lines, within
  //      the range of 128KB-1024KB.  This allows the caller to transmit
  //      reasonably granular updates to the d-bus client without requiring an
  //      overwhelming number of d-bus calls or excessively huge responses.
  //   3. After the base size is picked, rounded to hold an exact multiple of
  //      lines to minimize the number of times a partial leftover line will
  //      happen.
  const size_t kMinSize = params.bytes_per_line;
  const size_t kMinPreferredSize = 128 * 1024;
  const size_t kMaxPreferredSize = 1024 * 1024;
  const size_t kPreferredSize =
      (params.lines + 39) / 40 * params.bytes_per_line;

  // Clamp kPreferredSize into [kMinPreferredSize, kMaxPreferredSize].
  size_t buffer_length =
      std::min(kMaxPreferredSize, std::max(kMinPreferredSize, kPreferredSize));

  // Round up to the nearest page size, since this is the granularity the system
  // is going to allocate anyway.
  buffer_length = base::bits::AlignUp(buffer_length, static_cast<size_t>(4096));

  // Ensure buffer is big enough for at least one full line.
  buffer_length = std::max(buffer_length, kMinSize);

  // Round size to an even multiple of lines.
  buffer_length =
      (buffer_length / params.bytes_per_line) * params.bytes_per_line;

  LOG(INFO) << "Buffer size " << buffer_length << " chosen to hold "
            << (buffer_length / params.bytes_per_line) << " lines for "
            << params.lines << " total lines of size " << params.bytes_per_line;
  image_buffer_.resize(buffer_length);
  buffer_used_ = 0;

  if (expected_lines) {
    *expected_lines = params.lines;
  }
  scan_params_ = std::move(params);
  return SANE_STATUS_GOOD;
}

SANE_Status SaneDevice::ReadEncodedData(brillo::ErrorPtr* error,
                                        size_t* encoded_bytes,
                                        size_t* lines_read) {
  // This function maintains the invariant that image_buffer_ indices
  // [0, buffer_used_) hold previously read data at the start of each call.

  DCHECK(encoded_bytes);
  DCHECK(lines_read);

  *encoded_bytes = 0;
  *lines_read = 0;

  if (!scan_params_.has_value()) {
    brillo::Error::AddTo(error, FROM_HERE, kDbusDomain, kManagerServiceError,
                         "Scan parameters missing");
    return SANE_STATUS_IO_ERROR;
  }

  size_t read = 0;
  SANE_Status status = ReadScanData(error, image_buffer_.data() + buffer_used_,
                                    image_buffer_.size() - buffer_used_, &read);
  if (status == SANE_STATUS_CANCELLED) {
    brillo::Error::AddTo(error, FROM_HERE, kDbusDomain, kManagerServiceError,
                         "Job was cancelled");
    return status;
  }
  if (status == SANE_STATUS_EOF) {
    if (!image_reader_->Finalize(error)) {
      // brillo::Error::AddTo already called.
      return SANE_STATUS_IO_ERROR;
    }
    image_buffer_.resize(0);  // No more data to read until the next StartScan.
    buffer_used_ = 0;
    return status;
  }
  if (status != SANE_STATUS_GOOD) {
    LOG(ERROR) << __func__ << ": Failed to read scan data from device: "
               << sane_strstatus(status);
    brillo::Error::AddToPrintf(
        error, FROM_HERE, kDbusDomain, kManagerServiceError,
        "Failed to read scan data from device: %s", sane_strstatus(status));
    return status;
  }

  LOG(INFO) << __func__ << ": Read " << read << " bytes from device";
  if (read == 0) {
    return SANE_STATUS_GOOD;
  }

  // Write as many lines of the image as possible with the data received so far.
  // Indices [buffer_used_, buffer_used_ + read) hold the data that was just
  // read, plus data at [0, buffer_used_) that was read by previous calls.
  size_t bytes_available = buffer_used_ + read;
  size_t bytes_converted = 0;
  size_t lines_written = 0;
  while (bytes_available - bytes_converted >= scan_params_->bytes_per_line &&
         completed_lines_ < scan_params_->lines) {
    if (!image_reader_->ReadRow(error,
                                image_buffer_.data() + bytes_converted)) {
      LOG(ERROR) << __func__ << ": Failed to convert image line";
      return SANE_STATUS_IO_ERROR;
    }
    bytes_converted += scan_params_->bytes_per_line;
    lines_written++;
  }
  completed_lines_ += lines_written;
  *encoded_bytes = bytes_converted;
  *lines_read = lines_written;

  // Shift any unconverted data in image_buffer_ to the start of image_buffer_
  // to maintain the invariant for the next call.
  size_t remaining_bytes = bytes_available - bytes_converted;
  memmove(image_buffer_.data(), image_buffer_.data() + bytes_converted,
          remaining_bytes);
  buffer_used_ = remaining_bytes;

  LOG(INFO) << __func__ << ": Wrote " << lines_written
            << " lines to buffer.  remaining_bytes=" << remaining_bytes;

  if (completed_lines_ >= scan_params_->lines && remaining_bytes > 0) {
    LOG(ERROR) << __func__ << ": " << remaining_bytes
               << " bytes left over after reading all " << completed_lines_
               << " lines";
    brillo::Error::AddToPrintf(
        error, FROM_HERE, kDbusDomain, kManagerServiceError,
        "%zu bytes left over after reading all %zu lines", remaining_bytes,
        completed_lines_);
    return SANE_STATUS_IO_ERROR;
  }

  return SANE_STATUS_GOOD;
}

}  // namespace lorgnette
