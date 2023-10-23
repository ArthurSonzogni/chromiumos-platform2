// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBBRILLO_BRILLO_CPUINFO_H_
#define LIBBRILLO_BRILLO_CPUINFO_H_

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <base/files/file_path.h>
#include "brillo/brillo_export.h"

namespace brillo {

// This class provides a bit more structure to the contents of (and only)
// /proc/cpuinfo.
//
// It breaks the data into processor information records that can be accessed
// via a proc_index argument. Each processor record contains a set of string
// keys and values that they map to. You can look up a key to obtain its value
// (if the key exists). Note that some values may be the empty string ("").
class BRILLO_EXPORT CpuInfo final {
 public:
  ~CpuInfo();

  CpuInfo(CpuInfo&& other);
  CpuInfo& operator=(CpuInfo&& other);

  // Returns a CpuInfo object based on a file pointed to by |path|, or
  // std::nullopt if |path| could not be read or if there was a parse error.
  static std::optional<CpuInfo> Create(
      const base::FilePath& path = base::FilePath("/proc/cpuinfo"));

  // Returns a CpuInfo object based on the contents of |data|
  // or std::nullopt if there was a parse error.
  static std::optional<CpuInfo> CreateFromString(std::string_view data);

  // Returns the number of processor records that were read. This is different
  // from the number of processors in the system as the ones that are not
  // online will not have their details reported.
  size_t NumProcRecords() const;

  // Returns an optional string_view with the value corresponding to the |key|
  //   for the processor entry at |proc_index|. The lifetime of the string_view
  //   that is returned is limited to that of the CpuInfo object that it came
  //   from.
  // There are four conditions you should be aware of when using this
  //   function:
  // 1. Your |proc_index| is too big: returns std::nullopt.
  // 2. Your |key| doesn't exist: returns std::nullopt.
  // 3. |key| exists, but has no associated value: returns empty string.
  // 4. |key| exists, and has an associated value: returns the value.
  std::optional<std::string_view> LookUp(size_t proc_index,
                                         std::string_view key) const;

  static base::FilePath DefaultPath();

 private:
  CpuInfo();
  using Record = std::map<std::string, std::string, std::less<>>;
  using RecordsVec = std::vector<Record>;
  explicit CpuInfo(RecordsVec proc_records);
  static std::optional<RecordsVec> ParseFromString(std::string_view data);
  RecordsVec proc_records_;
};

}  // namespace brillo

#endif  // LIBBRILLO_BRILLO_CPUINFO_H_
