// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#include <base/files/dir_reader_posix.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <libpmt/pmt_impl.h>

namespace pmt {

static constexpr char kPmtSysfsPath[] = "/sys/class/intel_pmt";

std::vector<Guid> PmtSysfsData::DetectDevices() {
  base::DirReaderPosix reader(kPmtSysfsPath);
  if (!reader.IsValid()) {
    PLOG(ERROR) << "Failed to open " << kPmtSysfsPath;
    return std::vector<Guid>();
  }

  devices_.clear();
  std::vector<Guid> result;
  while (reader.Next()) {
    // Only process telem<x> directories.
    if (strncmp("telem", reader.name(), 5))
      continue;
    base::FilePath dev_path =
        base::FilePath(kPmtSysfsPath).Append(reader.name());
    base::FilePath guid_path = dev_path.Append("guid");
    base::FilePath size_path = dev_path.Append("size");
    std::string buf;
    Guid guid;
    size_t size;

    if (!base::ReadFileToString(guid_path, &buf)) {
      LOG(ERROR) << "Failed to read GUID from " << guid_path.value();
      return std::vector<Guid>();
    }
    base::TrimWhitespaceASCII(buf, base::TRIM_TRAILING, &buf);
    if (!base::HexStringToUInt(buf, &guid)) {
      LOG(ERROR) << "Failed to parse GUID '" << buf << "' from " << guid_path;
      return std::vector<Guid>();
    }

    if (!base::ReadFileToString(size_path, &buf)) {
      LOG(ERROR) << "Failed to read telemetry size from " << size_path.value();
      return std::vector<Guid>();
    }
    base::TrimWhitespaceASCII(buf, base::TRIM_TRAILING, &buf);
    if (!base::StringToSizeT(buf, &size)) {
      LOG(ERROR) << "Failed to parse telemetry size '" << buf << "' from "
                 << size_path;
      return std::vector<Guid>();
    }

    devices_[guid] = PmtDevice{
        .guid = guid,
        .size = size,
        .telem_path = dev_path.Append("telem"),
    };
    result.push_back(guid);
  }
  // Sort by GUIDs. GUIDs need to be sorted because some transformations
  // are relying on data from other devices (see the 'pkgc_block_cause'
  // transformation).
  std::sort(result.begin(), result.end());

  return result;
}

base::FilePath PmtSysfsData::GetMetadataMappingsFile() const {
  return base::FilePath("/usr/share/libpmt/metadata/pmt.xml");
}

bool PmtSysfsData::IsValid(Guid guid) const {
  return devices_.contains(guid);
}

const std::optional<base::FilePath> PmtSysfsData::GetTelemetryFile(
    Guid guid) const {
  return devices_.contains(guid) ? devices_.at(guid).telem_path
                                 : std::optional<base::FilePath>();
}

const size_t PmtSysfsData::GetTelemetrySize(Guid guid) const {
  return devices_.contains(guid) ? devices_.at(guid).size : 0;
}

}  // namespace pmt
