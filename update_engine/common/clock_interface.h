// Copyright 2013 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UPDATE_ENGINE_COMMON_CLOCK_INTERFACE_H_
#define UPDATE_ENGINE_COMMON_CLOCK_INTERFACE_H_

#include <string>

#include <base/time/time.h>

namespace chromeos_update_engine {

// The clock interface allows access to various system clocks. The
// sole reason for providing this as an interface is unit testing.
// Additionally, the sole reason for the methods not being static
// is also unit testing.
class ClockInterface {
 public:
  virtual ~ClockInterface() = default;

  // Gets the current time e.g. similar to base::Time::Now().
  virtual base::Time GetWallclockTime() const = 0;

  // Returns monotonic time since some unspecified starting point. It
  // is not increased when the system is sleeping nor is it affected
  // by NTP or the user changing the time.
  //
  // (This is a simple wrapper around clock_gettime(2) / CLOCK_MONOTONIC_RAW.)
  virtual base::Time GetMonotonicTime() const = 0;

  // Returns monotonic time since some unspecified starting point. It
  // is increased when the system is sleeping but it's not affected
  // by NTP or the user changing the time.
  //
  // (This is a simple wrapper around clock_gettime(2) / CLOCK_BOOTTIME.)
  virtual base::Time GetBootTime() const = 0;
};

}  // namespace chromeos_update_engine

#endif  // UPDATE_ENGINE_COMMON_CLOCK_INTERFACE_H_
