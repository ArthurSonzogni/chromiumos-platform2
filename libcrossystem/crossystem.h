// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBCROSSYSTEM_CROSSYSTEM_H_
#define LIBCROSSYSTEM_CROSSYSTEM_H_

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include <brillo/brillo_export.h>
#include <libcrossystem/crossystem_impl.h>
#include <libcrossystem/crossystem_vboot_interface.h>

namespace crossystem {

// C++ class to access crossystem system properties.
class BRILLO_EXPORT Crossystem {
 public:
  // Default implementation uses the real crossystem (CrossystemImpl).
  Crossystem() : Crossystem(std::make_unique<CrossystemImpl>()) {}

  // Can be used to instantiate a fake implementation for testing by passing
  // CrossystemFake.
  explicit Crossystem(std::unique_ptr<CrossystemVbootInterface> impl)
      : impl_(std::move(impl)) {}

  std::optional<int> VbGetSystemPropertyInt(const std::string& name) const;

  bool VbSetSystemPropertyInt(const std::string& name, int value);

  std::optional<std::string> VbGetSystemPropertyString(
      const std::string& name) const;

  bool VbSetSystemPropertyString(const std::string& name,
                                 const std::string& value);

 private:
  std::unique_ptr<CrossystemVbootInterface> impl_;
};

}  // namespace crossystem

#endif  // LIBCROSSYSTEM_CROSSYSTEM_H_
