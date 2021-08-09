// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBBRILLO_BRILLO_CROSSYSTEM_CROSSYSTEM_H_
#define LIBBRILLO_BRILLO_CROSSYSTEM_CROSSYSTEM_H_

#include <cstddef>

#include <string>

#include <base/optional.h>
#include <brillo/brillo_export.h>

namespace brillo {

// C++ interface to access crossystem system properties.
class BRILLO_EXPORT Crossystem {
 public:
  virtual ~Crossystem() = default;

  // Reads a system property integer.
  //
  // @param name The name of the target system property.
  // @return The property value, or |base::nullopt| if error.
  virtual base::Optional<int> VbGetSystemPropertyInt(
      const std::string& name) const = 0;

  // Sets a system property integer.
  //
  // @param name The name of the target system property.
  // @param value The integer value to set.
  // @return |true| if it succeeds; |false| if it fails.
  virtual bool VbSetSystemPropertyInt(const std::string& name, int value) = 0;

  // Reads a system property string.
  //
  // @param name The name of the target system property.
  // @return The property value, or |base::nullopt| if error.
  virtual base::Optional<std::string> VbGetSystemPropertyString(
      const std::string& name) const = 0;

  // Sets a system property string.
  //
  // @param name The name of the target system property.
  // @param value The string value to set.
  // @return |true| if it succeeds; |false| if it fails.
  virtual bool VbSetSystemPropertyString(const std::string& name,
                                         const std::string& value) = 0;
};

// The sole implementation that invokes the corresponding functions provided
// in vboot/crossystem.h .
class BRILLO_EXPORT CrossystemImpl : public Crossystem {
 public:
  base::Optional<int> VbGetSystemPropertyInt(
      const std::string& name) const override;

  bool VbSetSystemPropertyInt(const std::string& name, int value) override;

  base::Optional<std::string> VbGetSystemPropertyString(
      const std::string& name) const override;

  bool VbSetSystemPropertyString(const std::string& name,
                                 const std::string& value) override;
};

}  // namespace brillo

#endif  // LIBBRILLO_BRILLO_CROSSYSTEM_CROSSYSTEM_H_
