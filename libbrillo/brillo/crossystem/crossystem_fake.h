// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBBRILLO_BRILLO_CROSSYSTEM_CROSSYSTEM_FAKE_H_
#define LIBBRILLO_BRILLO_CROSSYSTEM_CROSSYSTEM_FAKE_H_

#include <map>
#include <set>
#include <string>

#include <brillo/brillo_export.h>
#include <brillo/crossystem/crossystem.h>

namespace brillo {

namespace fake {

// A fake implementation that simulates the manipulation of system properties
// with an in-memory table.  By default, all properties are unset so getters
// will return |base::nullopt| (or other equivalent value type).
class BRILLO_EXPORT CrossystemFake : public Crossystem {
 public:
  CrossystemFake() = default;

  base::Optional<int> VbGetSystemPropertyInt(
      const std::string& name) const override;

  bool VbSetSystemPropertyInt(const std::string& name, int value) override;

  base::Optional<std::string> VbGetSystemPropertyString(
      const std::string& name) const override;

  bool VbSetSystemPropertyString(const std::string& name,
                                 const std::string& value) override;

  // Unset the value of the specific system property.
  //
  // After the property is unset, both VbGetSystemPropertyInt() and
  // VbGetSystemPropertyString() return |base::nullopt|.
  //
  // @param name The name of the target system property.
  void UnsetSystemPropertyValue(const std::string& name);

  // Specify whether the system property is read-only or not.
  //
  // After the property is marked RO, both VbSetSystemPropertyString() and
  // VbSetSystemPropertyInt() will return |false|.
  //
  // @param name The name of the target system property.
  // @param is_readonly |true| to mark the system property read-only.
  void SetSystemPropertyReadOnlyStatus(const std::string& name,
                                       bool is_readonly);

 private:
  std::set<std::string> readonly_system_peroperty_names_;
  std::map<std::string, int> system_int_properties_;
  std::map<std::string, std::string> system_str_properties_;
};

}  // namespace fake

}  // namespace brillo

#endif  // LIBBRILLO_BRILLO_CROSSYSTEM_CROSSYSTEM_FAKE_H_
