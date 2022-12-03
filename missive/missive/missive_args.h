// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MISSIVE_MISSIVE_MISSIVE_ARGS_H_
#define MISSIVE_MISSIVE_MISSIVE_ARGS_H_

#include <memory>
#include <string>

#include <base/strings/string_piece.h>
#include <base/time/time.h>

namespace reporting {

class MissiveArgs {
 public:
  static constexpr char kEnqueuingRecordTallierDefault[] = "3m";
  static constexpr char kCpuCollectorIntervalDefault[] = "10m";
  static constexpr char kStorageCollectorIntervalDefault[] = "1h";
  static constexpr char kMemoryCollectorIntervalDefault[] = "10m";

  MissiveArgs(base::StringPiece enqueuing_record_tallier,
              base::StringPiece cpu_collector_interval,
              base::StringPiece storage_collector_interval,
              base::StringPiece memory_collector_interval);
  MissiveArgs(const MissiveArgs&) = delete;
  MissiveArgs& operator=(const MissiveArgs&) = delete;
  ~MissiveArgs();

  base::TimeDelta enqueuing_record_tallier() const {
    return enqueuing_record_tallier_;
  }
  base::TimeDelta cpu_collector_interval() const {
    return cpu_collector_interval_;
  }
  base::TimeDelta storage_collector_interval() const {
    return storage_collector_interval_;
  }
  base::TimeDelta memory_collector_interval() const {
    return memory_collector_interval_;
  }

 private:
  const base::TimeDelta enqueuing_record_tallier_;
  const base::TimeDelta cpu_collector_interval_;
  const base::TimeDelta storage_collector_interval_;
  const base::TimeDelta memory_collector_interval_;
};
}  // namespace reporting

#endif  // MISSIVE_MISSIVE_MISSIVE_ARGS_H_
