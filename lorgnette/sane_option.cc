// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lorgnette/sane_option.h"

#include <optional>
#include <math.h>

#include <base/logging.h>
#include <base/strings/strcat.h>
#include <base/strings/stringprintf.h>

namespace lorgnette {

namespace {

// Converts a SANE_Fixed from a double to a human-oriented string representation
// without using unecessary decimal digits.
//
// For displaying a fixed-point value, we want to make sure individual values
// are distinguishable without displaying unnecessary decimal digits.
// SANE_Fixed has a resolution of 1/65536, which is 0.0000152.  This means that
// five decimal digits is enough to distinguish any two valid values from each
// other.
//
// However, most real-world values come from physical dimensions in mm or eSCL
// units, where individual values can be distinguished with at most 3 decimal
// digits.  Even that is too many digits for cases where the number is large.
// For example, at 1200 dpi, the difference between 36mm and 36.01mm is less
// than half a pixel.  It seems unlikely that the user will ever need to
// distinguish between values that close together for scanning.
//
// This intuition is turned into something similar to how doubles themselves
// work: The returned string uses more decimal digits for numbers closer to zero
// and fewer for numbers with a large magnitude.  The cutoffs between buckets
// isn't based on anything principled, but just what generates reasonable labels
// for common ranges found on scanners.
//
// After generating the decimals, also remove any extra trailing zeros.  This
// means that things like 1.00 can be displayed as 1.0.  The last zero is left
// in place except for very large numbers, i.e. 1.0 is preferred over 1, but
// 5000 is preferred over 5000.0.
std::string ShortestStringForSaneFixed(double d) {
  double abs_d = fabs(d);

  // Anything that rounds to zero as a SANE_Fixed should display as 0.0.
  if (abs_d < 1.0 / 65536.0) {
    return "0.0";
  }

  // Ranges:
  // [5000 - 32768]: No decimal
  // [10.0 - 4999.9]: 1 decimal
  // [0.1 - 9.99]: 2 decimals
  // [0.001 - 0.099]: 3 decimals
  // [0.0 - 0.0009]: 5 decimals
  // Actual ranges are slightly lower so that the upper end doesn't round into
  // the next bucket up.
  std::string result;
  if (abs_d >= 4999.95) {
    // Directly return here because integers don't fit the decimal shortening
    // logic below.
    return base::StringPrintf("%d", static_cast<int>(d + 0.5));
  } else if (abs_d >= 9.995) {
    result = base::StringPrintf("%.1f", d);
  } else if (abs_d >= 0.095) {
    result = base::StringPrintf("%.2f", d);
  } else if (abs_d >= 0.00095) {
    result = base::StringPrintf("%.3f", d);
  } else {  // 0.0 - 0.00094
    result = base::StringPrintf("%.5f", d);
  }

  // Pop digits without checking the length because all the formats above
  // always include a decimal.
  while (result.back() == '0') {
    result.pop_back();
  }
  if (result.back() == '.') {
    result.push_back('0');
  }

  return result;
}

}  // namespace

SaneOption::SaneOption(const SANE_Option_Descriptor& opt, int index) {
  name_ = opt.name ? opt.name : "";
  index_ = index;
  type_ = opt.type;
  active_ = SANE_OPTION_IS_ACTIVE(opt.cap);
  if (type_ == SANE_TYPE_STRING) {
    // opt.size is the maximum size of the string option, including the null
    // terminator (which is mandatory).
    string_data_.resize(opt.size);
  }
}

bool SaneOption::Set(bool b) {
  if (!active_) {
    return false;
  }
  if (type_ != SANE_TYPE_BOOL) {
    return false;
  }

  int_data_.b = b ? SANE_TRUE : SANE_FALSE;
  return true;
}

bool SaneOption::Set(int i) {
  if (!active_) {
    return false;
  }

  switch (type_) {
    case SANE_TYPE_BOOL:
      if (i != SANE_TRUE && i != SANE_FALSE) {
        return false;
      }
      int_data_.b = i == SANE_TRUE ? SANE_TRUE : SANE_FALSE;
      return true;
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
  if (!active_) {
    return false;
  }

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
  if (!active_) {
    LOG(ERROR) << "Option not active";
    return false;
  }
  if (type_ != SANE_TYPE_STRING) {
    LOG(ERROR) << "type_ is not string";
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

bool SaneOption::Set(const char* s) {
  return Set(std::string(s));
}

template <>
std::optional<int> SaneOption::Get() const {
  if (!active_)
    return std::nullopt;

  switch (type_) {
    case SANE_TYPE_INT:
      return int_data_.i;
    case SANE_TYPE_FIXED:
      return static_cast<int>(SANE_UNFIX(int_data_.f));
    case SANE_TYPE_BOOL:
      return int_data_.b == SANE_TRUE ? SANE_TRUE : SANE_FALSE;
    default:
      LOG(ERROR) << "Requested int from option type " << type_;
      return std::nullopt;
  }
}

template <>
std::optional<double> SaneOption::Get() const {
  if (!active_)
    return std::nullopt;

  switch (type_) {
    case SANE_TYPE_INT:
      return int_data_.i;
    case SANE_TYPE_FIXED:
      return SANE_UNFIX(int_data_.f);
    default:
      LOG(ERROR) << "Requested double from option type " << type_;
      return std::nullopt;
  }
}

template <>
std::optional<bool> SaneOption::Get() const {
  if (!active_)
    return std::nullopt;

  if (type_ != SANE_TYPE_BOOL) {
    LOG(ERROR) << "Requested bool from option type " << type_;
    return std::nullopt;
  }

  return int_data_.b == SANE_TRUE;
}

template <>
std::optional<std::string> SaneOption::Get() const {
  if (!active_)
    return std::nullopt;

  if (type_ != SANE_TYPE_STRING) {
    LOG(ERROR) << "Requested string from option type " << type_;
    return std::nullopt;
  }

  return std::string(string_data_.data());
}

void* SaneOption::GetPointer() {
  if (type_ == SANE_TYPE_STRING)
    return string_data_.data();
  else if (type_ == SANE_TYPE_INT)
    return &int_data_.i;
  else if (type_ == SANE_TYPE_FIXED)
    return &int_data_.f;
  else if (type_ == SANE_TYPE_BOOL)
    return &int_data_.b;
  else
    return nullptr;
}

int SaneOption::GetIndex() const {
  return index_;
}

std::string SaneOption::GetName() const {
  return name_;
}

SANE_Value_Type SaneOption::GetType() const {
  return type_;
}

std::string SaneOption::DisplayValue() const {
  if (!active_) {
    return "[inactive]";
  }

  switch (type_) {
    case SANE_TYPE_BOOL:
      return Get<bool>().value() ? "true" : "false";
    case SANE_TYPE_INT:
      return std::to_string(int_data_.i);
    case SANE_TYPE_FIXED:
      return ShortestStringForSaneFixed(Get<double>().value());
    case SANE_TYPE_STRING:
      return Get<std::string>().value();
    default:
      return "[invalid]";
  }
}

}  // namespace lorgnette
