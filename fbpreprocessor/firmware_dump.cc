// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fbpreprocessor/firmware_dump.h"

#include <ostream>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <brillo/files/file_util.h>
#include <fbpreprocessor/proto_bindings/fbpreprocessor.pb.h>

namespace fbpreprocessor {

FirmwareDump::FirmwareDump(const base::FilePath& path, Type type)
    : dmp_file_(path), type_(type) {}

base::FilePath FirmwareDump::BaseName() const {
  return dmp_file_.BaseName();
}

bool FirmwareDump::Delete() const {
  if (base::PathExists(dmp_file_)) {
    bool success = brillo::DeleteFile(dmp_file_);
    if (!success) {
      LOG(ERROR) << "Failed to delete firmware dump.";
    }
    return success;
  }
  return true;
}

std::ostream& operator<<(std::ostream& os, const FirmwareDump& dump) {
  os << dump.BaseName();
  return os;
}

DebugDump::Type FirmwareDump::ConvertToDBusType(Type type) {
  switch (type) {
    case Type::kWiFi:
      return DebugDump::WIFI;
  }
}

}  // namespace fbpreprocessor
