// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LORGNETTE_SANE_OPTION_H_
#define LORGNETTE_SANE_OPTION_H_

#include <optional>
#include <string>
#include <vector>

#include <sane/sane.h>

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

  int GetIndex() const;
  std::string GetName() const;
  std::string DisplayValue() const;
  SANE_Value_Type GetType() const;
  size_t GetSize() const;

 private:
  std::string name_;
  int index_;
  SANE_Value_Type type_;  // The type that the backend uses for the option.
  bool active_;           // Inactive options don't contain a value.

  // Only one of these will be set, depending on type_.
  std::optional<std::vector<SANE_Int>> int_data_;
  std::optional<std::vector<SANE_Fixed>> fixed_data_;
  SANE_Bool bool_data_;
  std::optional<std::vector<SANE_Char>> string_data_;
};

// Represents the possible values for an option.
struct OptionRange {
  double start;
  double size;
};

}  // namespace lorgnette

#endif  // LORGNETTE_SANE_OPTION_H_
