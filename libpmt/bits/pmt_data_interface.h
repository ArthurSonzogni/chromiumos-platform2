// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBPMT_BITS_PMT_DATA_INTERFACE_H_
#define LIBPMT_BITS_PMT_DATA_INTERFACE_H_

#include <vector>

#include <brillo/brillo_export.h>

#include <base/files/file_path.h>

namespace pmt {

// Intel PMT device unique identifier is a 32 bit unsigned integer.
typedef uint32_t Guid;

// Interface for retrieving PMT related data.
class BRILLO_EXPORT PmtDataInterface {
 public:
  virtual ~PmtDataInterface() = default;

  // Detect the PMT devices on the system and return their GUIDs.
  //
  // @return The vector of GUIDs of detected devices.
  virtual std::vector<Guid> DetectDevices() = 0;

  // Get the path to the PMT metadata mapping file.
  //
  // This file contains description of the metadata mappings for different PMT
  // devices. Based on those one can decode the binary telemetry data and
  // transform it into readable values. Mappings also include human-readable
  // field names.
  // @return Path to the PMT metadata mapping file or an empty path if it is
  // missing.
  virtual base::FilePath GetMetadataMappingsFile() const = 0;

  // Checks whether a given device was detected by DetectDevices().
  //
  // Until DetectDevices() is called, this function will return false.
  // @param guid Identifier of a PMT device.
  // @return True if device was detected, false otherwise.
  virtual bool IsValid(Guid guid) const = 0;

  // Get path to the telemetry data file for a given device.
  //
  // @param guid Identifier of a PMT device.
  // @return Path to the telemetry file or an empty option if there is no
  // device with a given identifier.
  virtual const std::optional<base::FilePath> GetTelemetryFile(
      Guid guid) const = 0;

  // Get the size of the telemetry data sample for a given device.
  //
  // @param guid Identifier of a PMT device.
  // @return Size of the telemetry data sample or 0 if there is no device
  // with a given identifier.
  virtual const size_t GetTelemetrySize(Guid guid) const = 0;
};

}  // namespace pmt

#endif  // LIBPMT_BITS_PMT_DATA_INTERFACE_H_
