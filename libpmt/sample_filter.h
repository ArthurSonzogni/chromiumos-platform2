// Copyright 2026 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBPMT_SAMPLE_FILTER_H_
#define LIBPMT_SAMPLE_FILTER_H_

#include <optional>
#include <string>
#include <vector>

#include "libpmt/bits/pmt_data_interface.h"

namespace pmt {

struct Filter {
  std::optional<Guid> guid;
  std::optional<std::string> group;
  std::optional<std::string> sample;
};

std::vector<Filter> ParseFilters(const std::vector<std::string>& filter_strs);

bool IsSampleSelected(const std::vector<Filter>& filters,
                      Guid guid,
                      const std::string& group,
                      const std::string& sample);

}  // namespace pmt

#endif  // LIBPMT_SAMPLE_FILTER_H_
