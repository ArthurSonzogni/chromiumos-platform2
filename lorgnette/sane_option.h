// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LORGNETTE_SANE_OPTION_H_
#define LORGNETTE_SANE_OPTION_H_

#include <optional>
#include <string>
#include <vector>

#include <lorgnette/proto_bindings/lorgnette_service.pb.h>
#include <sane/sane.h>

#include "lorgnette/sane_constraint.h"

namespace lorgnette {

// Represents a SANE_Option_Descriptor and its current value.
class SaneOption {
 public:
  SaneOption(const SANE_Option_Descriptor& opt, int index);

  bool Set(bool b);
  bool Set(double d);
  bool Set(const std::vector<double>& d);
  bool Set(int i);
  bool Set(const std::vector<int>& i);
  bool Set(const std::string& s);
  bool Set(const char* s);
  bool Set(const ScannerOption& value);

  template <typename T>
  std::optional<T> Get() const = delete;
  template <>
  std::optional<bool> Get() const;
  template <>
  std::optional<int> Get() const;
  template <>
  std::optional<std::vector<int>> Get() const;
  template <>
  std::optional<double> Get() const;
  template <>
  std::optional<std::vector<double>> Get() const;
  template <>
  std::optional<std::string> Get() const;

  // This returns a pointer to the internal storage. Care must be taken that the
  // pointer does not outlive the SaneOption.
  void* GetPointer();

  // Disable this option by marking it inactive.  This isn't needed for normal
  // operation, but can be used to block an option that causes hangs or hardware
  // problems.
  void Disable();

  // Some options are known to cause hangs or other problems with certain
  // backends or specific devices.  If this function returns true, the caller
  // should avoid attempting to retrieve or set this option's value.
  bool IsIncompatibleWithDevice(const std::string& connection_string) const;

  int GetIndex() const;
  std::string GetName() const;
  std::string DisplayValue() const;
  SANE_Value_Type GetType() const;
  size_t GetSize() const;
  std::optional<SaneConstraint> GetConstraint() const;
  bool IsActive() const;
  SANE_Action GetAction() const;

  // Wrapper functions to process the embedded constraint.
  std::optional<std::vector<std::string>> GetValidStringValues() const;
  std::optional<std::vector<uint32_t>> GetValidIntValues() const;
  std::optional<OptionRange> GetValidRange() const;

  std::optional<ScannerOption> ToScannerOption() const;

 private:
  void ParseCapabilities(SANE_Int cap);
  void ReserveValueSize(const SANE_Option_Descriptor& opt);

  std::string name_;
  std::string title_;
  std::string description_;
  int index_;
  SANE_Value_Type type_;  // The value type used by the backend for this option.
  SANE_Unit unit_;        // The unit type used by the backend for this option.
  bool detectable_;       // Capabilities contains CAP_SOFT_DETECT.
  bool sw_settable_;      // SANE_OPTION_IS_SETTABLE is true for capabilities.
  bool hw_settable_;      // Capabilities contains CAP_HARD_SELECT.
  bool auto_settable_;    // Capabilities contains CAP_AUTOMATIC.
  bool emulated_;         // Capabilities contains CAP_EMULATED.
  bool active_;           // SANE_OPTION_IS_ACTIVE is true for capabilities.
                          // Inactive options do not contain a valid value.
  bool advanced_;         // Capabilities contains CAP_ADVANCED.
  SANE_Action action_;    // The action needed to set the current value with
                          // sane_control_option().
  std::optional<SaneConstraint> constraint_;

  // Only one of these will be set, depending on type_.
  std::optional<std::vector<SANE_Int>> int_data_;
  std::optional<std::vector<SANE_Fixed>> fixed_data_;
  SANE_Bool bool_data_;
  std::optional<std::vector<SANE_Char>> string_data_;
};

}  // namespace lorgnette

#endif  // LORGNETTE_SANE_OPTION_H_
