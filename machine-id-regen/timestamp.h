// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MACHINE_ID_REGEN_TIMESTAMP_H_
#define MACHINE_ID_REGEN_TIMESTAMP_H_

#include <string>

#include <base/time/time.h>
#include <brillo/file_utils.h>

namespace machineidregen {
// Timestamp represents the time that records uptime, not a (wall clock)
// timestamp.
class Timestamp {
 public:
  explicit Timestamp(const base::FilePath& timestamp_path)
      : timestamp_path_(timestamp_path) {}

  Timestamp(const Timestamp&) = delete;
  Timestamp& operator=(const Timestamp&) = delete;
  Timestamp() = delete;

  std::optional<base::TimeDelta> get_last_update();
  bool update(base::TimeDelta value);
  const base::FilePath& GetPath() const { return timestamp_path_; }

 private:
  base::FilePath timestamp_path_;
};

}  // namespace machineidregen

#endif  // MACHINE_ID_REGEN_TIMESTAMP_H_
