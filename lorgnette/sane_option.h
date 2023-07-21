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
  bool Set(int i);
  bool Set(const std::string& s);
  bool Set(const char* s);

  template <typename T>
  std::optional<T> Get() const = delete;
  template <>
  std::optional<bool> Get() const;
  template <>
  std::optional<int> Get() const;
  template <>
  std::optional<double> Get() const;
  template <>
  std::optional<std::string> Get() const;

  // This returns a pointer to the internal storage. Care must be taken that the
  // pointer does not outlive the SaneOption.
  void* GetPointer();

  int GetIndex() const;
  std::string GetName() const;
  std::string DisplayValue() const;
  SANE_Value_Type GetType() const;

 private:
  std::string name_;
  int index_;
  SANE_Value_Type type_;  // The type that the backend uses for the option.
  bool active_;           // Inactive options don't contain a value.

  // The integer data, if this is an int option.
  union {
    SANE_Int i;
    SANE_Word b;
    SANE_Fixed f;
  } int_data_;

  // The buffer containing string data, if this is a string option.
  std::vector<char> string_data_;
};

// Represents the possible values for an option.
struct OptionRange {
  double start;
  double size;
};

}  // namespace lorgnette

#endif  // LORGNETTE_SANE_OPTION_H_
