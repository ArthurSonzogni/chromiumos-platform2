// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lorgnette/sane_option.h"

#include <algorithm>
#include <optional>
#include <math.h>

#include <absl/strings/str_join.h>
#include <base/logging.h>
#include <base/notreached.h>
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

// JoinFixed is a version of StrJoin that uses our custom SANE_Fixed formatting
// instead of the %g equivalent that StrJoin normally uses.
std::string JoinFixed(const std::vector<double>& fs,
                      const std::string& delimiter) {
  if (fs.empty()) {
    return "";
  }

  std::vector<std::string> strs;
  strs.reserve(fs.size());
  for (auto f : fs) {
    strs.push_back(ShortestStringForSaneFixed(f));
  }
  return absl::StrJoin(strs, delimiter);
}

}  // namespace

SaneOption::SaneOption(const SANE_Option_Descriptor& opt, int index) {
  name_ = opt.name ? opt.name : "";
  title_ = opt.title ? opt.title : "";
  description_ = opt.desc ? opt.desc : "";
  index_ = index;
  type_ = opt.type;
  unit_ = opt.unit;
  constraint_ = SaneConstraint::Create(opt);

  ParseCapabilities(opt.cap);
  ReserveValueSize(opt);
}

void SaneOption::ParseCapabilities(SANE_Int cap) {
  detectable_ = cap & SANE_CAP_SOFT_DETECT;
  sw_settable_ = SANE_OPTION_IS_SETTABLE(cap);
  hw_settable_ = cap & SANE_CAP_HARD_SELECT;
  auto_settable_ = cap & SANE_CAP_AUTOMATIC;
  emulated_ = cap & SANE_CAP_EMULATED;
  active_ = SANE_OPTION_IS_ACTIVE(cap);
  advanced_ = cap & SANE_CAP_ADVANCED;
  if (cap & SANE_Int(~0x7f)) {
    LOG(WARNING) << "Option " << name_ << " at index " << index_
                 << " has unrecognized bits in capabilities: "
                 << base::StringPrintf("0x%x", cap);
  }
}

void SaneOption::ReserveValueSize(const SANE_Option_Descriptor& opt) {
  size_t size = 0;
  switch (type_) {
    case SANE_TYPE_BOOL:
      // opt.size must be set to sizeof(SANE_Word) and always represents a
      // single Boolean value.
      if (opt.size != sizeof(SANE_Word)) {
        LOG(WARNING) << "Boolean option " << name_ << " has invalid size "
                     << opt.size;
      }
      size = 1;
      break;

    case SANE_TYPE_INT:
    case SANE_TYPE_FIXED:
      // opt.size is a multiple of sizeof(SANE_Word).  The number of elements
      // can be found by dividing it back out.
      if (opt.size % sizeof(SANE_Word)) {
        LOG(WARNING) << "Numeric option " << name_ << " has size " << opt.size
                     << " that is not a multiple of " << sizeof(SANE_Word);
      }
      size = opt.size / sizeof(SANE_Word);  // Truncates if needed as specified
                                            // in the upstream formula.
      if (!size) {
        LOG(WARNING) << "Numeric option " << name_ << " has size 0";
      }
      break;

    case SANE_TYPE_STRING:
      // opt.size is the maximum size of the string option, including the null
      // terminator (which is mandatory).
      size = opt.size / sizeof(SANE_Char);
      if (!size) {
        LOG(WARNING) << "String option " << name_ << " has size 0";
      }
      break;

    case SANE_TYPE_BUTTON:
    case SANE_TYPE_GROUP:
      // These contain no value.  The size is ignored.
      if (opt.size != 0) {
        LOG(WARNING) << "Non-value option " << name_
                     << " has non-zero size that will be ignored";
      }
      break;

    default:
      NOTREACHED();
  }

  switch (type_) {
    case SANE_TYPE_STRING:
      string_data_.emplace(size);
      break;
    case SANE_TYPE_INT:
      int_data_.emplace(size, 0);
      break;
    case SANE_TYPE_FIXED:
      fixed_data_.emplace(size, 0);
      break;
    default:
      // No data setup needed.
      break;
  }
}

bool SaneOption::Set(bool b) {
  if (!active_) {
    return false;
  }
  if (type_ != SANE_TYPE_BOOL) {
    return false;
  }

  bool_data_ = b ? SANE_TRUE : SANE_FALSE;
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
      bool_data_ = i == SANE_TRUE ? SANE_TRUE : SANE_FALSE;
      return true;
    case SANE_TYPE_INT:
      if (GetSize() > 0) {
        int_data_->at(0) = i;
        return true;
      }
      return false;
    case SANE_TYPE_FIXED:
      if (GetSize() > 0) {
        fixed_data_->at(0) = SANE_FIX(static_cast<double>(i));
        return true;
      }
      return false;
    default:
      return false;
  }
}

bool SaneOption::Set(const std::vector<int>& i) {
  if (!active_) {
    return false;
  }
  if (type_ != SANE_TYPE_INT) {
    return false;
  }
  if (i.size() != GetSize()) {
    return false;
  }

  std::copy(i.begin(), i.end(), int_data_->begin());
  return true;
}

bool SaneOption::Set(double d) {
  if (!active_) {
    return false;
  }

  switch (type_) {
    case SANE_TYPE_INT:
      if (GetSize() > 0) {
        int_data_->at(0) = static_cast<int>(d);
        return true;
      }
      return false;
    case SANE_TYPE_FIXED:
      if (GetSize() > 0) {
        fixed_data_->at(0) = SANE_FIX(d);
        return true;
      }
      return false;
    default:
      return false;
  }
}

bool SaneOption::Set(const std::vector<double>& d) {
  if (!active_) {
    return false;
  }
  if (type_ != SANE_TYPE_FIXED) {
    return false;
  }
  if (d.size() != GetSize()) {
    return false;
  }

  for (int i = 0; i < d.size(); i++) {
    fixed_data_->at(i) = SANE_FIX(d[i]);
  }
  return true;
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
  if (size_with_null > string_data_->size()) {
    LOG(ERROR) << "String size " << size_with_null
               << " exceeds maximum option size " << string_data_->size();
    return false;
  }

  memcpy(string_data_->data(), s.c_str(), size_with_null);
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
      if (GetSize() > 0) {
        return int_data_->at(0);
      }
      return std::nullopt;
    case SANE_TYPE_FIXED:
      if (GetSize() > 0) {
        return static_cast<int>(SANE_UNFIX(fixed_data_->at(0)));
      }
      return std::nullopt;
    case SANE_TYPE_BOOL:
      return bool_data_ == SANE_TRUE ? SANE_TRUE : SANE_FALSE;
    default:
      LOG(ERROR) << "Requested int from option type " << type_;
      return std::nullopt;
  }
}

template <>
std::optional<std::vector<int>> SaneOption::Get() const {
  if (!active_) {
    return std::nullopt;
  }

  if (type_ != SANE_TYPE_INT) {
    LOG(ERROR) << "Requested list of SANE_Int from option type " << type_;
    return std::nullopt;
  }

  return int_data_;
}

template <>
std::optional<double> SaneOption::Get() const {
  if (!active_)
    return std::nullopt;

  switch (type_) {
    case SANE_TYPE_INT:
      if (GetSize() > 0) {
        return int_data_->at(0);
      }
      return std::nullopt;
    case SANE_TYPE_FIXED:
      if (GetSize() > 0) {
        return SANE_UNFIX(fixed_data_->at(0));
      }
      return std::nullopt;
    default:
      LOG(ERROR) << "Requested double from option type " << type_;
      return std::nullopt;
  }
}

template <>
std::optional<std::vector<double>> SaneOption::Get() const {
  if (!active_) {
    return std::nullopt;
  }

  if (type_ != SANE_TYPE_FIXED) {
    LOG(ERROR) << "Requested list of SANE_Fixed from option type " << type_;
    return std::nullopt;
  }

  std::vector<double> result;
  result.reserve(fixed_data_->size());
  for (auto f : fixed_data_.value()) {
    result.push_back(SANE_UNFIX(f));
  }
  return result;
}

template <>
std::optional<bool> SaneOption::Get() const {
  if (!active_)
    return std::nullopt;

  if (type_ != SANE_TYPE_BOOL) {
    LOG(ERROR) << "Requested bool from option type " << type_;
    return std::nullopt;
  }

  return bool_data_ == SANE_TRUE;
}

template <>
std::optional<std::string> SaneOption::Get() const {
  if (!active_)
    return std::nullopt;

  if (type_ != SANE_TYPE_STRING) {
    LOG(ERROR) << "Requested string from option type " << type_;
    return std::nullopt;
  }

  return std::string(string_data_->data());
}

void* SaneOption::GetPointer() {
  if (type_ == SANE_TYPE_STRING)
    return string_data_->data();
  else if (type_ == SANE_TYPE_INT)
    return int_data_->data();
  else if (type_ == SANE_TYPE_FIXED)
    return fixed_data_->data();
  else if (type_ == SANE_TYPE_BOOL)
    return &bool_data_;
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

size_t SaneOption::GetSize() const {
  switch (type_) {
    case SANE_TYPE_BOOL:
      return 1;
    case SANE_TYPE_INT:
      return int_data_->size();
    case SANE_TYPE_FIXED:
      return fixed_data_->size();
    case SANE_TYPE_STRING:
      return string_data_->size();
    case SANE_TYPE_BUTTON:
    case SANE_TYPE_GROUP:
      return 0;
    default:
      NOTREACHED();
  }
}

bool SaneOption::IsActive() const {
  return active_;
}

std::string SaneOption::DisplayValue() const {
  if (!active_) {
    return "[inactive]";
  }

  switch (type_) {
    case SANE_TYPE_BOOL:
      return Get<bool>().value() ? "true" : "false";
    case SANE_TYPE_INT:
      if (GetSize() > 0) {
        return absl::StrJoin(Get<std::vector<int>>().value(), ", ");
      }
      return "[no value]";
    case SANE_TYPE_FIXED:
      if (GetSize() > 0) {
        return JoinFixed(Get<std::vector<double>>().value(), ", ");
      }
      return "[no value]";
    case SANE_TYPE_STRING:
      return Get<std::string>().value();
    default:
      return "[invalid]";
  }
}

std::optional<std::vector<std::string>> SaneOption::GetValidStringValues()
    const {
  if (!constraint_.has_value()) {
    LOG(ERROR) << __func__ << ": No valid constraint in option " << name_
               << " at index " << index_;
    return std::nullopt;
  }
  return constraint_->GetValidStringOptionValues();
}

std::optional<std::vector<uint32_t>> SaneOption::GetValidIntValues() const {
  if (!constraint_.has_value()) {
    LOG(ERROR) << __func__ << ": No valid constraint in option " << name_
               << " at index " << index_;
    return std::nullopt;
  }
  return constraint_->GetValidIntOptionValues();
}

std::optional<OptionRange> SaneOption::GetValidRange() const {
  if (!constraint_.has_value()) {
    LOG(ERROR) << __func__ << ": No valid constraint in option " << name_
               << " at index " << index_;
    return std::nullopt;
  }
  return constraint_->GetOptionRange();
}

std::optional<ScannerOption> SaneOption::ToScannerOption() const {
  ScannerOption option;
  option.set_name(name_);
  option.set_title(title_);
  option.set_description(description_);

  switch (type_) {
    case SANE_TYPE_BOOL:
      option.set_option_type(TYPE_BOOL);
      if (Get<bool>().has_value()) {
        option.set_bool_value(Get<bool>().value());
      }
      break;
    case SANE_TYPE_INT: {
      option.set_option_type(TYPE_INT);
      auto int_list = Get<std::vector<int>>();
      if (int_list.has_value()) {
        for (int value : int_list.value()) {
          option.mutable_int_value()->add_value(value);
        }
      }
      break;
    }
    case SANE_TYPE_FIXED: {
      option.set_option_type(TYPE_FIXED);
      auto fixed_list = Get<std::vector<double>>();
      if (fixed_list.has_value()) {
        for (double value : fixed_list.value()) {
          option.mutable_fixed_value()->add_value(value);
        }
      }
      break;
    }
    case SANE_TYPE_STRING:
      option.set_option_type(TYPE_STRING);
      if (Get<std::string>().has_value()) {
        option.set_string_value(Get<std::string>().value());
      }
      break;
    case SANE_TYPE_BUTTON:
      option.set_option_type(TYPE_BUTTON);
      break;
    case SANE_TYPE_GROUP:
      option.set_option_type(TYPE_GROUP);
      return option;  // No additional fields are valid for a group.
    default:
      LOG(ERROR) << "Skipping unhandled option type " << type_ << " in option "
                 << name_;
      return std::nullopt;
  }

  switch (unit_) {
    case SANE_UNIT_NONE:
      option.set_unit(UNIT_NONE);
      break;
    case SANE_UNIT_PIXEL:
      option.set_unit(UNIT_PIXEL);
      break;
    case SANE_UNIT_BIT:
      option.set_unit(UNIT_BIT);
      break;
    case SANE_UNIT_MM:
      option.set_unit(UNIT_MM);
      break;
    case SANE_UNIT_DPI:
      option.set_unit(UNIT_DPI);
      break;
    case SANE_UNIT_PERCENT:
      option.set_unit(UNIT_PERCENT);
      break;
    case SANE_UNIT_MICROSECOND:
      option.set_unit(UNIT_MICROSECOND);
      break;
    default:
      LOG(ERROR) << "Skipping unhandled option unit " << unit_ << " in option "
                 << name_;
      return std::nullopt;
  }

  if (constraint_) {
    auto proto_constraint = constraint_->ToOptionConstraint();
    if (proto_constraint && proto_constraint->constraint_type() !=
                                OptionConstraint::CONSTRAINT_NONE) {
      *option.mutable_constraint() = *proto_constraint;
    }
  }

  option.set_detectable(detectable_);
  option.set_sw_settable(sw_settable_);
  option.set_hw_settable(hw_settable_);
  option.set_auto_settable(auto_settable_);
  option.set_emulated(emulated_);
  option.set_active(active_);
  option.set_advanced(advanced_);

  return option;
}

}  // namespace lorgnette
