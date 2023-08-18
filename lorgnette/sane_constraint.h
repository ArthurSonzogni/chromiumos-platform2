// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LORGNETTE_SANE_CONSTRAINT_H_
#define LORGNETTE_SANE_CONSTRAINT_H_

#include <optional>
#include <string>
#include <vector>

#include <brillo/errors/error.h>
#include <lorgnette/proto_bindings/lorgnette_service.pb.h>
#include <sane/sane.h>

namespace lorgnette {

// Represents the possible values for an option.
struct OptionRange {
  double start;
  double size;
};

// Represents a SANE_Constraint_Type and the associated constraint rules.
class SaneConstraint {
 public:
  static std::optional<SaneConstraint> Create(
      const SANE_Option_Descriptor& opt);

  SANE_Constraint_Type GetType() const;

  std::optional<std::vector<std::string>> GetValidStringOptionValues() const;
  std::optional<std::vector<uint32_t>> GetValidIntOptionValues() const;
  std::optional<OptionRange> GetOptionRange() const;

  std::optional<OptionConstraint> ToOptionConstraint() const;

 private:
  SaneConstraint(SANE_Constraint_Type constraint_type,
                 SANE_Value_Type value_type);

  SANE_Constraint_Type constraint_type_;
  SANE_Value_Type value_type_;
  std::optional<std::vector<std::string>> string_list_;
  std::optional<std::vector<SANE_Word>> word_list_;
  std::optional<SANE_Range> range_;
};

}  // namespace lorgnette

#endif  // LORGNETTE_SANE_CONSTRAINT_H_
