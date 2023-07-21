// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lorgnette/sane_option.h"

#include <optional>

#include <base/logging.h>

namespace lorgnette {

SaneOption::SaneOption(const SANE_Option_Descriptor& opt, int index) {
  name_ = opt.name;
  index_ = index;
  type_ = opt.type;
  if (type_ == SANE_TYPE_STRING) {
    // opt.size is the maximum size of the string option, including the null
    // terminator (which is mandatory).
    string_data_.resize(opt.size);
  }
}

bool SaneOption::Set(int i) {
  switch (type_) {
    case SANE_TYPE_INT:
      int_data_.i = i;
      return true;
    case SANE_TYPE_FIXED:
      int_data_.f = SANE_FIX(static_cast<double>(i));
      return true;
    default:
      return false;
  }
}

bool SaneOption::Set(double d) {
  switch (type_) {
    case SANE_TYPE_INT:
      int_data_.i = static_cast<int>(d);
      return true;
    case SANE_TYPE_FIXED:
      int_data_.f = SANE_FIX(d);
      return true;
    default:
      return false;
  }
}

bool SaneOption::Set(const std::string& s) {
  if (type_ != SANE_TYPE_STRING) {
    return false;
  }

  size_t size_with_null = s.size() + 1;
  if (size_with_null > string_data_.size()) {
    LOG(ERROR) << "String size " << size_with_null
               << " exceeds maximum option size " << string_data_.size();
    return false;
  }

  memcpy(string_data_.data(), s.c_str(), size_with_null);
  return true;
}

template <>
std::optional<int> SaneOption::Get() const {
  switch (type_) {
    case SANE_TYPE_INT:
      return int_data_.i;
    case SANE_TYPE_FIXED:
      return static_cast<int>(SANE_UNFIX(int_data_.f));
    default:
      return std::nullopt;
  }
}

template <>
std::optional<std::string> SaneOption::Get() const {
  if (type_ != SANE_TYPE_STRING)
    return std::nullopt;

  return std::string(string_data_.data());
}

void* SaneOption::GetPointer() {
  if (type_ == SANE_TYPE_STRING)
    return string_data_.data();
  else if (type_ == SANE_TYPE_INT)
    return &int_data_.i;
  else if (type_ == SANE_TYPE_FIXED)
    return &int_data_.f;
  else
    return nullptr;
}

int SaneOption::GetIndex() const {
  return index_;
}

std::string SaneOption::GetName() const {
  return name_;
}

std::string SaneOption::DisplayValue() const {
  switch (type_) {
    case SANE_TYPE_INT:
      return std::to_string(int_data_.i);
    case SANE_TYPE_FIXED:
      return std::to_string(static_cast<int>(SANE_UNFIX(int_data_.f)));
    case SANE_TYPE_STRING:
      return Get<std::string>().value();
    default:
      return "[invalid]";
  }
}

}  // namespace lorgnette
