// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBPMT_PMT_IMPL_H_
#define LIBPMT_PMT_IMPL_H_

#include <map>
#include <vector>

#include <brillo/brillo_export.h>

#include "bits/pmt_data_interface.h"

namespace pmt {

struct PmtDevice {
  // Device GUID.
  Guid guid;
  // Size of device's PMT data.
  size_t size;
  // Path to the device's /telem file.
  base::FilePath telem_path;
};

// C++ class for processing Intel PMT data.
class BRILLO_EXPORT PmtSysfsData : public PmtDataInterface {
 public:
  // Default implementation using the real filesystem to gather data.
  PmtSysfsData() = default;
  ~PmtSysfsData() = default;

  // Detect the PMT devices on the system and return their GUIDs.
  //
  // @retval A list consisting of /sys/class/intel_pmt/telem*/guid contents.
  std::vector<Guid> DetectDevices() final;

  // Get the path to the PMT metadata mapping file.
  //
  // @retval /usr/local/share/libpmt/metadata/pmt.xml
  base::FilePath GetMetadataMappingsFile() const final;

  // Checks whether a given device is in devices_.
  bool IsValid(Guid guid) const final;

  // Get path to the telemetry data file for a given device.
  //
  // @retval /sys/class/intel_pmt/telem<x>/telem corresponding to the given
  // GUID.
  const std::optional<base::FilePath> GetTelemetryFile(Guid guid) const final;

  // Get the size of the telemetry data sample for a given device.
  //
  // @retval /sys/class/intel_pmt/telem<x>/size corresponding to the given
  // GUID.
  const size_t GetTelemetrySize(Guid guid) const final;

 private:
  std::map<Guid, PmtDevice> devices_;
};

}  // namespace pmt

#endif  // LIBPMT_PMT_IMPL_H_
