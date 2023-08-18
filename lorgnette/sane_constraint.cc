// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lorgnette/sane_constraint.h"

#include <base/logging.h>
#include <base/notreached.h>
#include <chromeos/dbus/service_constants.h>

#include "lorgnette/dbus_adaptors/org.chromium.lorgnette.Manager.h"

namespace lorgnette {

std::optional<SaneConstraint> SaneConstraint::Create(
    const SANE_Option_Descriptor& descriptor) {
  SaneConstraint constraint(descriptor.constraint_type, descriptor.type);
  switch (descriptor.constraint_type) {
    case SANE_CONSTRAINT_NONE:
      return constraint;
    case SANE_CONSTRAINT_RANGE:
      if (!descriptor.constraint.range) {
        return std::nullopt;
      }
      constraint.range_.emplace(*descriptor.constraint.range);
      return constraint;
    case SANE_CONSTRAINT_WORD_LIST: {
      if (!descriptor.constraint.word_list) {
        return std::nullopt;
      }
      SANE_Int num_words = descriptor.constraint.word_list[0];
      constraint.word_list_.emplace(
          descriptor.constraint.word_list + 1,
          descriptor.constraint.word_list + num_words + 1);
      return constraint;
    }
    case SANE_CONSTRAINT_STRING_LIST:
      if (!descriptor.constraint.string_list) {
        return std::nullopt;
      }
      constraint.string_list_.emplace();
      for (const SANE_String_Const* str = descriptor.constraint.string_list;
           *str; str++) {
        constraint.string_list_->emplace_back(*str);
      }
      return constraint;
    default:
      LOG(ERROR) << "Skipping unhandled option constraint type "
                 << descriptor.constraint_type << " in option "
                 << descriptor.name;
      return std::nullopt;
  }

  NOTREACHED();
}

SaneConstraint::SaneConstraint(SANE_Constraint_Type constraint_type,
                               SANE_Value_Type value_type)
    : constraint_type_(constraint_type), value_type_(value_type) {}

SANE_Constraint_Type SaneConstraint::GetType() const {
  return constraint_type_;
}

std::optional<OptionConstraint> SaneConstraint::ToOptionConstraint() const {
  OptionConstraint constraint;

  switch (constraint_type_) {
    case SANE_CONSTRAINT_NONE:
      break;
    case SANE_CONSTRAINT_RANGE: {
      if (!range_.has_value()) {
        LOG(ERROR) << "Missing range entry in constraint";
        return std::nullopt;
      }
      if (value_type_ == SANE_TYPE_FIXED) {
        constraint.set_constraint_type(
            OptionConstraint::CONSTRAINT_FIXED_RANGE);
        auto* range = constraint.mutable_fixed_range();
        range->set_min(SANE_UNFIX(range_->min));
        range->set_max(SANE_UNFIX(range_->max));
        range->set_quant(SANE_UNFIX(range_->quant));
      } else {
        constraint.set_constraint_type(OptionConstraint::CONSTRAINT_INT_RANGE);
        auto* range = constraint.mutable_int_range();
        range->set_min(range_->min);
        range->set_max(range_->max);
        range->set_quant(range_->quant);
      }
      break;
    }
    case SANE_CONSTRAINT_WORD_LIST: {
      if (!word_list_.has_value()) {
        LOG(ERROR) << "Missing word_list entry in constraint";
        return std::nullopt;
      }
      constraint.set_constraint_type(
          value_type_ == SANE_TYPE_FIXED
              ? OptionConstraint::CONSTRAINT_FIXED_LIST
              : OptionConstraint::CONSTRAINT_INT_LIST);
      for (SANE_Word value : word_list_.value()) {
        if (value_type_ == SANE_TYPE_FIXED) {
          constraint.add_valid_fixed(SANE_UNFIX(value));
        } else {
          constraint.add_valid_int(value);
        }
      }
      break;
    }
    case SANE_CONSTRAINT_STRING_LIST: {
      if (!string_list_.has_value()) {
        LOG(ERROR) << "Missing string_list entry in constraint";
        return std::nullopt;
      }
      constraint.set_constraint_type(OptionConstraint::CONSTRAINT_STRING_LIST);
      for (const std::string& str : string_list_.value()) {
        constraint.add_valid_string(str);
      }
      break;
    }
    default:
      LOG(ERROR) << "Skipping unhandled option constraint type "
                 << constraint_type_;
      return std::nullopt;
  }

  return constraint;
}

std::optional<std::vector<std::string>>
SaneConstraint::GetValidStringOptionValues() const {
  if (constraint_type_ != SANE_CONSTRAINT_STRING_LIST) {
    LOG(ERROR) << __func__
               << ": Invalid option constraint type for string list: "
               << constraint_type_;
    return std::nullopt;
  }

  return string_list_;
}

std::optional<std::vector<uint32_t>> SaneConstraint::GetValidIntOptionValues()
    const {
  std::vector<uint32_t> values;
  switch (constraint_type_) {
    case SANE_CONSTRAINT_WORD_LIST:
      for (SANE_Word w : *word_list_) {
        int value = value_type_ == SANE_TYPE_FIXED ? SANE_UNFIX(w) : w;
        values.push_back(value);
      }
      break;
    case SANE_CONSTRAINT_RANGE:
      for (int i = range_->min; i <= range_->max; i += range_->quant) {
        const int value = value_type_ == SANE_TYPE_FIXED ? SANE_UNFIX(i) : i;
        values.push_back(value);
      }
      break;
    default:
      LOG(ERROR) << __func__
                 << ": Invalid option constraint type for int list: "
                 << constraint_type_;
      return std::nullopt;
  }

  return values;
}

std::optional<OptionRange> SaneConstraint::GetOptionRange() const {
  if (constraint_type_ != SANE_CONSTRAINT_RANGE) {
    LOG(ERROR) << __func__ << ":Invalid option constraint type for range: "
               << constraint_type_;
    return std::nullopt;
  }

  OptionRange option_range;
  switch (value_type_) {
    case SANE_TYPE_INT:
      option_range.start = range_->min;
      option_range.size = range_->max - range_->min;
      return option_range;
    case SANE_TYPE_FIXED:
      option_range.start = SANE_UNFIX(range_->min);
      option_range.size = SANE_UNFIX(range_->max - range_->min);
      return option_range;
    default:
      LOG(ERROR) << __func__
                 << ": Unexpected option value type for range constraint: "
                 << value_type_;
      return std::nullopt;
  }
}

}  // namespace lorgnette
