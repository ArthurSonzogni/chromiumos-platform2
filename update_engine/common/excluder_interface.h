// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UPDATE_ENGINE_COMMON_EXCLUDER_INTERFACE_H_
#define UPDATE_ENGINE_COMMON_EXCLUDER_INTERFACE_H_

#include <memory>
#include <string>

namespace chromeos_update_engine {

class PrefsInterface;

// TODO(b/171829801): Move this interface to 'cros' directory. 'aosp' in no way
// is using this. Not even the stub implementation.
class ExcluderInterface {
 public:
  virtual ~ExcluderInterface() = default;

  // Returns true on successfuly excluding |name|, otherwise false. On a
  // successful |Exclude()| the passed in |name| will be considered excluded
  // and calls to |IsExcluded()| will return true. The exclusions are persisted.
  virtual bool Exclude(const std::string& name) = 0;

  // Returns true if |name| reached the exclusion limit, otherwise false.
  virtual bool IsExcluded(const std::string& name) = 0;

  // Returns true on sucessfully reseting the entire exclusion state, otherwise
  // false. On a successful |Reset()| there will be no excluded |name| in the
  // exclusion state.
  virtual bool Reset() = 0;

  // Not copyable or movable
  ExcluderInterface(const ExcluderInterface&) = delete;
  ExcluderInterface& operator=(const ExcluderInterface&) = delete;
  ExcluderInterface(ExcluderInterface&&) = delete;
  ExcluderInterface& operator=(ExcluderInterface&&) = delete;

 protected:
  ExcluderInterface() = default;
};

std::unique_ptr<ExcluderInterface> CreateExcluder();

}  // namespace chromeos_update_engine

#endif  // UPDATE_ENGINE_COMMON_EXCLUDER_INTERFACE_H_
