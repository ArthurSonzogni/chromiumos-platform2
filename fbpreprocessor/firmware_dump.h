// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FBPREPROCESSOR_FIRMWARE_DUMP_H_
#define FBPREPROCESSOR_FIRMWARE_DUMP_H_

#include <ostream>

#include <base/files/file_path.h>
#include <fbpreprocessor/proto_bindings/fbpreprocessor.pb.h>

namespace fbpreprocessor {

class FirmwareDump {
 public:
  enum class Type {
    kWiFi,
    kBluetooth,
  };

  // The |path| argument is the absolute path to the directory where the
  // firmware dump files are stored, with the base of the file name appended.
  // Example:
  // If |path| is
  // /run/daemon-store/fbpreprocessord/<user_hash>/iwlwifi_${timestamp}, the
  // .dmp file will be called:
  // /run/daemon-store/fbpreprocessord/<user_hash>/iwlwifi_${timestamp}.dmp
  explicit FirmwareDump(const base::FilePath& path, Type type);

  // Returns the name of the file that holds the content of the firmware dump.
  // It's typically something like
  // /run/daemon-store/fbpreprocessord/<user_hash>/iwlwifi_${timestamp}.dmp
  base::FilePath DumpFile() const { return dmp_file_; }

  // Returns the basename, excluding extension. Typical example:
  // iwlwifi_${timestamp}
  base::FilePath BaseName() const;

  // Delete .dmp file from disk. Returns true on success, false otherwise.
  // If the file did not exist in the first place it's considered successful.
  bool Delete() const;

  Type type() const { return type_; }

  // The firmware type is exchanged over D-Bus as a protobuf enum. This utility
  // function converts the internal scoped enum type to the D-Bus type.
  static DebugDump::Type ConvertToDBusType(Type type);

 private:
  base::FilePath dmp_file_;

  Type type_;
};

std::ostream& operator<<(std::ostream& os, const FirmwareDump& dump);

}  // namespace fbpreprocessor

#endif  // FBPREPROCESSOR_FIRMWARE_DUMP_H_
