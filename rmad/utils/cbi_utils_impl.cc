// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/utils/cbi_utils_impl.h"

#include <string>
#include <vector>

#include <base/logging.h>
#include <base/process/launch.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <re2/re2.h>

namespace rmad {

namespace {

constexpr char kEctoolCmdPath[] = "/usr/sbin/ectool";
constexpr char kEctoolIntValRegex[] = R"(As uint: (\d+))";

constexpr int kCbiTagSkuId = 2;
constexpr int kCbiTagDramPartNum = 3;

}  // namespace

bool CbiUtilsImpl::GetSku(uint64_t* sku) const {
  DCHECK(sku);

  return GetCbi(kCbiTagSkuId, sku);
}

bool CbiUtilsImpl::GetDramPartNum(std::string* dram_part_num) const {
  DCHECK(dram_part_num);

  return GetCbi(kCbiTagDramPartNum, dram_part_num);
}

bool CbiUtilsImpl::SetSku(uint64_t sku) {
  int byte_size = 0;
  uint64_t tmp = sku;

  // To tackle |sku| = 0, we use do-while to ensure that |byte_size| >= 1.
  do {
    tmp >>= 8;
    byte_size++;
  } while (tmp);

  return SetCbi(kCbiTagSkuId, sku, byte_size);
}

bool CbiUtilsImpl::SetDramPartNum(const std::string& dram_part_num) {
  return SetCbi(kCbiTagDramPartNum, dram_part_num);
}

bool CbiUtilsImpl::SetCbi(int tag, const std::string& value, int set_flag) {
  std::vector<std::string> argv{kEctoolCmdPath,
                                "cbi",
                                "set",
                                base::NumberToString(tag),
                                value,
                                "0",
                                base::NumberToString(set_flag)};
  static std::string unused_output;
  return base::GetAppOutput(argv, &unused_output);
}

bool CbiUtilsImpl::GetCbi(int tag, std::string* value, int get_flag) const {
  CHECK(value != nullptr);

  std::vector<std::string> argv{kEctoolCmdPath, "cbi", "get",
                                base::NumberToString(tag),
                                base::NumberToString(get_flag)};
  if (!base::GetAppOutput(argv, value)) {
    return false;
  }

  base::TrimWhitespaceASCII(*value, base::TRIM_TRAILING, value);
  return true;
}

bool CbiUtilsImpl::SetCbi(int tag, uint64_t value, int size, int set_flag) {
  CHECK_GE(size, 1);
  CHECK_LE(size, 8);
  CHECK(size == 8 || 1 << (size * 8) > value);

  std::vector<std::string> argv{kEctoolCmdPath,
                                "cbi",
                                "set",
                                base::NumberToString(tag),
                                base::NumberToString(value),
                                base::NumberToString(size),
                                base::NumberToString(set_flag)};
  static std::string unused_output;
  return base::GetAppOutput(argv, &unused_output);
}

bool CbiUtilsImpl::GetCbi(int tag, uint64_t* value, int get_flag) const {
  CHECK(value != nullptr);

  std::vector<std::string> argv{kEctoolCmdPath, "cbi", "get",
                                base::NumberToString(tag),
                                base::NumberToString(get_flag)};
  std::string output;
  if (!base::GetAppOutput(argv, &output)) {
    return false;
  }

  if (!re2::RE2::PartialMatch(output, kEctoolIntValRegex, value)) {
    LOG(ERROR) << "Failed to parse output from ectool";
    return false;
  }

  return true;
}

}  // namespace rmad
