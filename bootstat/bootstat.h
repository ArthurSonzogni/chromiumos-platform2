// Copyright (c) 2010 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Public API for the C++ bindings to the Chromium OS
// 'bootstat' facility.  The facility is a simple timestamp
// mechanism to associate a named event with the time that it
// occurred and with other relevant statistics.

#ifndef BOOTSTAT_BOOTSTAT_H_
#define BOOTSTAT_BOOTSTAT_H_

#include <string>

namespace bootstat {

// Basic class for bootstat API interface.
class BootStat {
 public:
  BootStat() = default;
  BootStat(const BootStat&) = delete;
  BootStat& operator=(const BootStat&) = delete;
  ~BootStat() = default;

  // Logs an event.  Event names should be composed of characters drawn from
  // this subset of 7-bit ASCII:  Letters (upper- or lower-case), digits, dot
  // ('.'), dash ('-'), and underscore ('_').  Case is significant.  Behavior
  // in the presence of other characters is unspecified - Caveat Emptor!
  //
  // Applications are responsible for establishing higher-level naming
  // conventions to prevent name collisions.
  bool LogEvent(const std::string& event_name) const;

 private:
  // Returns the path representing the stats file for the root disk.
  // Returns an empty string on failure.
  std::string GetDiskStatisticsFileName() const;

  // Figures out the event output file name, and open it.
  // Returns an fd (negative on error).
  int OpenEventFile(const std::string& output_name_prefix,
                    const std::string& event_name) const;
  // Logs a disk event containing root disk statistics.
  bool LogDiskEvent(const std::string& event_name) const;
  // Logs a uptime event indicating time since boot.
  bool LogUptimeEvent(const std::string& event_name) const;
};
}  // namespace bootstat

//
// Length of the longest valid string naming an event, including the
// terminating NUL character.  Clients of bootstat_log() can use
// this value for the size of buffers to hold event names; names
// that exceed this buffer size will be truncated.
//
// This value is arbitrarily chosen, but see comments in
// bootstat_log.c regarding implementation assumptions for this
// value.
//
// TODO(drinkcat): Rename this to a kConstant in the namespace
#define BOOTSTAT_MAX_EVENT_LEN 64

// TODO(drinkcat): All the callers are C++ so we should modify them to use the
// class directly and drop this.
extern void bootstat_log(const char* event_name);

#endif  // BOOTSTAT_BOOTSTAT_H_
