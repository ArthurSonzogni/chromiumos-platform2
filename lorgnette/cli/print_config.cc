// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lorgnette/cli/print_config.h"

#include <algorithm>
#include <limits>
#include <optional>
#include <string>
#include <vector>

#include <base/containers/contains.h>
#include <base/notreached.h>
#include <base/strings/strcat.h>
#include <base/strings/string_util.h>

namespace lorgnette::cli {

namespace {

// std::to_string doesn't have a specialization for std::string, so define one
// here and use ADL in the functions below.
using std::to_string;
std::string to_string(const std::string& s) {
  return s;
}

void PrintSaneFlags(const lorgnette::ScannerOption& option, std::ostream& out) {
  out << (option.active() ? "active" : "inactive");
  if (!option.detectable()) {
    out << " !detectable";
  } else if (!option.sw_settable()) {
    // Options are normally detectable.  Only flag it explicitly if they aren't
    // settable.
    out << " detectable";
  }
  if (option.hw_settable()) {
    out << " hw_settable";
  }
  if (!option.sw_settable()) {
    out << " !sw_settable";
  }
  if (option.auto_settable()) {
    out << " auto_capable";
  }
  if (option.emulated()) {
    out << " emulated";
  }
  if (option.advanced()) {
    out << " advanced";
  }
}

std::string UnitName(const lorgnette::OptionUnit unit) {
  switch (unit) {
    case lorgnette::OptionUnit::UNIT_NONE:
      return "";
    case lorgnette::OptionUnit::UNIT_PIXEL:
      return "px";
    case lorgnette::OptionUnit::UNIT_BIT:
      return "-bit";
    case lorgnette::OptionUnit::UNIT_MM:
      return "mm";
    case lorgnette::OptionUnit::UNIT_DPI:
      return "dpi";
    case lorgnette::OptionUnit::UNIT_PERCENT:
      return "%";
    case lorgnette::OptionUnit::UNIT_MICROSECOND:
      return "μs";
    default:
      NOTREACHED();
      return "";
  }
}

void PrintSaneValue(const lorgnette::ScannerOption& option, std::ostream& out) {
  if (!option.active()) {
    out << "[unset]";
    return;
  }

  switch (option.option_type()) {
    case lorgnette::OptionType::TYPE_BOOL:
      // SANE_TYPE_BOOL values normally don't have constraints, but they can
      // implicitly only accept 0 and 1.
      out << (option.bool_value() ? "0 | [1]" : "[0] | 1");
      break;
    case lorgnette::OptionType::TYPE_INT: {
      std::vector<std::string> values(option.int_value().value_size());
      std::transform(option.int_value().value().begin(),
                     option.int_value().value().end(), values.begin(),
                     [](int32_t i) { return to_string(i); });
      out << base::JoinString(values, ",");
      break;
    }
    case lorgnette::OptionType::TYPE_FIXED: {
      std::vector<std::string> values(option.fixed_value().value_size());
      std::transform(option.fixed_value().value().begin(),
                     option.fixed_value().value().end(), values.begin(),
                     [](double i) { return to_string(i); });
      out << base::JoinString(values, ",");
      break;
    }
    case lorgnette::OptionType::TYPE_STRING:
      out << option.string_value();
      break;
    case lorgnette::OptionType::TYPE_BUTTON:
    case lorgnette::OptionType::TYPE_GROUP:
      // No value.
      break;
    default:
      NOTREACHED();
      break;
  }

  out << UnitName(option.unit());
}

template <class V, class R>
void PrintConstraintList(const lorgnette::ScannerOption& option,
                         const V& setvals,
                         const R& allowed,
                         std::ostream& out) {
  std::vector<std::string> values;
  values.reserve(allowed.size());
  for (const auto& val : allowed) {
    if (base::Contains(setvals, val)) {
      values.push_back(base::StrCat({"[", to_string(val), "]"}));
    } else {
      values.push_back(to_string(val));
    }
  }
  if (option.auto_settable()) {
    values.insert(values.begin(), "auto");
  }
  out << base::JoinString(values, " | ");
  if (option.unit() != lorgnette::OptionUnit::UNIT_NONE) {
    out << " " << UnitName(option.unit());
  }
}

template <class T, class R>
void PrintConstraintRange(std::optional<T> val,
                          const R& range,
                          lorgnette::OptionUnit unit,
                          std::ostream& out) {
  // Print one of
  // min..max (if no value is available)
  // [x]..max
  // min..[x]..max
  // min..[x]
  if (!val.has_value()) {
    out << range.min() << ".." << range.max();
  } else if (val.value() <= range.min()) {
    out << "[" << range.min() << "]"
        << ".." << range.max();
  } else if (val.value() >= range.max()) {
    out << range.min() << ".."
        << "[" << range.max() << "]";
  } else {
    out << range.min() << ".."
        << "[" << val.value() << "]"
        << ".." << range.max();
  }
  out << UnitName(unit);
  if (range.quant() != 0.0 && range.quant() != 1.0) {
    out << " in steps of " << range.quant() << UnitName(unit);
  }
  if (!val.has_value()) {
    out << " [unset]";
  }
}

void PrintSaneConstraint(const lorgnette::ScannerOption& option,
                         std::ostream& out) {
  if (!option.has_constraint()) {
    return;
  }

  const auto& constraint = option.constraint();
  switch (constraint.constraint_type()) {
    case lorgnette::OptionConstraint::CONSTRAINT_STRING_LIST:
      PrintConstraintList(option,
                          std::vector<std::string>{option.string_value()},
                          constraint.valid_string(), out);
      break;
    case lorgnette::OptionConstraint::CONSTRAINT_INT_LIST:
      PrintConstraintList(option, option.int_value().value(),
                          constraint.valid_int(), out);
      break;
    case lorgnette::OptionConstraint::CONSTRAINT_FIXED_LIST:
      PrintConstraintList(option, option.fixed_value().value(),
                          constraint.valid_fixed(), out);
      break;
    case lorgnette::OptionConstraint::CONSTRAINT_FIXED_RANGE:
      PrintConstraintRange(
          option.has_fixed_value()
              ? std::make_optional<double>(option.fixed_value().value(0))
              : std::nullopt,
          constraint.fixed_range(), option.unit(), out);
      break;
    case lorgnette::OptionConstraint::CONSTRAINT_INT_RANGE:
      PrintConstraintRange(
          option.has_int_value()
              ? std::make_optional<int32_t>(option.int_value().value(0))
              : std::nullopt,
          constraint.int_range(), option.unit(), out);
      break;
    default:
      NOTREACHED();
      break;
  }
}

void PrintSaneOption(const lorgnette::ScannerOption& option,
                     std::ostream& out) {
  // Option name on the first row.
  out << "  " << option.name() << ":  " << option.title() << std::endl;

  // Indented description line(s).
  // TODO(b/275043885): Consider wrapping these descriptions for readability.
  std::string description = option.description();
  base::TrimWhitespaceASCII(description, base::TRIM_ALL, &description);
  base::ReplaceSubstringsAfterOffset(&description, 0, "\n", "\n    ");
  out << "    " << description << std::endl;

  // Value and constraints on a row.
  out << "    "
      << "Value: ";
  if (option.has_constraint()) {
    PrintSaneConstraint(option, out);
  } else {
    PrintSaneValue(option, out);
  }
  out << std::endl;

  // Flags on a row.
  out << "    "
      << "Flags: ";
  PrintSaneFlags(option, out);
  out << std::endl;
}

}  // namespace

void PrintScannerConfig(const lorgnette::ScannerConfig& config,
                        bool show_inactive,
                        bool show_advanced,
                        std::ostream& out) {
  out << "--- Scanner Config ---" << std::endl;

  bool first_group = true;
  for (const auto& group : config.option_groups()) {
    if (!first_group) {
      out << std::endl;
    }
    bool header_shown = false;
    for (const auto& opt_name : group.members()) {
      const auto& option = config.options().at(opt_name);
      if (!show_inactive && !option.active()) {
        continue;
      }
      if (!show_advanced && option.advanced()) {
        continue;
      }
      if (!header_shown) {
        out << group.title() << " group:\n";
        header_shown = true;
        first_group = false;
      }
      PrintSaneOption(config.options().at(opt_name), out);
    }
  }
  out << "--- End Scanner Config ---" << std::endl;
}

}  // namespace lorgnette::cli
