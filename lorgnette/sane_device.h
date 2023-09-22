// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LORGNETTE_SANE_DEVICE_H_
#define LORGNETTE_SANE_DEVICE_H_

#include <cstdint>
#include <cstdio>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <brillo/errors/error.h>
#include <lorgnette/proto_bindings/lorgnette_service.pb.h>
#include <sane/sane.h>

#include "lorgnette/image_readers/image_reader.h"
#include "lorgnette/scan_parameters.h"

namespace lorgnette {

struct ValidOptionValues {
  std::vector<uint32_t> resolutions;
  std::vector<DocumentSource> sources;
  std::vector<std::string> color_modes;
};

// This class represents an active connection to a scanning device.
// At most 1 active connection to a particular device is allowed at once.
// This class is thread-compatible, but not thread-safe.
class SaneDevice {
 public:
  virtual ~SaneDevice() {}

  virtual std::optional<ValidOptionValues> GetValidOptionValues(
      brillo::ErrorPtr* error) = 0;

  virtual std::optional<int> GetScanResolution(brillo::ErrorPtr* error) = 0;
  virtual bool SetScanResolution(brillo::ErrorPtr* error, int resolution) = 0;
  virtual std::optional<std::string> GetDocumentSource(
      brillo::ErrorPtr* error) = 0;
  virtual bool SetDocumentSource(brillo::ErrorPtr* error,
                                 const std::string& source_name) = 0;
  virtual std::optional<ColorMode> GetColorMode(brillo::ErrorPtr* error) = 0;
  virtual bool SetColorMode(brillo::ErrorPtr* error, ColorMode color_mode) = 0;
  virtual bool SetScanRegion(brillo::ErrorPtr* error,
                             const ScanRegion& region) = 0;
  virtual std::optional<ScannerConfig> GetCurrentConfig(
      brillo::ErrorPtr* error) = 0;
  virtual SANE_Status StartScan(brillo::ErrorPtr* error) = 0;
  virtual SANE_Status GetScanParameters(brillo::ErrorPtr* error,
                                        ScanParameters* params) = 0;

  // PrepareImageReader() sets up an ImageReader to encode data read from the
  // SANE backend into `format` and write it to `out_file`.  If `expected_lines`
  // is not NULL, it will be populated with the number of lines declared by the
  // backend's GetScanParameters() result.
  //
  // This function must be called after StartScan() and before
  // ReadEncodedData().
  //
  // Returns SANE_STATUS_GOOD if this object is ready to read encoded data from
  // the scanner.
  virtual SANE_Status PrepareImageReader(brillo::ErrorPtr* error,
                                         ImageFormat format,
                                         FILE* out_file,
                                         size_t* expected_lines);

  // ReadScanData() reads the next chunk of up to `count` bytes of raw SANE
  // frame data into `buf`.  If `read_out` is not NULL, it will be set to the
  // number of bytes actually ready.  Note that `read_out` can be 0 if no data
  // was available when this function was called.  Returns the SANE status from
  // the underlying sane_read() call.
  virtual SANE_Status ReadScanData(brillo::ErrorPtr* error,
                                   uint8_t* buf,
                                   size_t count,
                                   size_t* read_out) = 0;

  // ReadEncodedData reads a chunk of data from the SANE backend into an
  // internal buffer, encodes as many complete lines as possible, and writes the
  // result to the file set up with PrepareImageReader().  `encoded_bytes` will
  // be set to the number of bytes written to the file, and `lines_read` will be
  // set to the number of complete lines encoded.  Any of the statuses returned
  // by ReadScanData() can be returned; in addition, SANE_STATUS_IO_ERROR will
  // be returned if there are errors encoding data or writing to the file.
  virtual SANE_Status ReadEncodedData(brillo::ErrorPtr* error,
                                      size_t* encoded_bytes,
                                      size_t* lines_read);

  // This function is thread-safe.
  virtual bool CancelScan(brillo::ErrorPtr* error) = 0;

  // SetOption attempts to set the value referenced by `option`.  If needed, it
  // will reload all the SANE options.  The return value will be
  // SANE_STATUS_GOOD if everything succeeded.  Otherwise SetOption can return
  // the error from setting the option or an error from reloading options.
  virtual SANE_Status SetOption(brillo::ErrorPtr* error,
                                const ScannerOption& option) = 0;

  // MIME types for image formats that can be returned from this scanner.
  std::vector<std::string> GetSupportedFormats() const;
  std::optional<std::string> GetCurrentJob() const;

 protected:
  void StartJob();
  void EndJob();

 private:
  std::optional<std::string> current_job_;
  std::optional<ScanParameters> scan_params_;
  std::unique_ptr<ImageReader> image_reader_;
  std::vector<uint8_t> image_buffer_;
  size_t buffer_used_ = 0;
  size_t completed_lines_ = 0;
};

}  // namespace lorgnette

#endif  // LORGNETTE_SANE_DEVICE_H_
