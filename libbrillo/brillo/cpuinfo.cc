// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "brillo/cpuinfo.h"

#include <utility>

#include <base/files/file_util.h>
#include <base/strings/string_split.h>

namespace brillo {

namespace {

constexpr char kCpuInfoPath[] = "/proc/cpuinfo";

}  // namespace

CpuInfo::CpuInfo() = default;
CpuInfo::~CpuInfo() = default;

CpuInfo::CpuInfo(CpuInfo&& other) = default;
CpuInfo& CpuInfo::operator=(CpuInfo&& other) = default;

std::optional<CpuInfo> CpuInfo::Create(const base::FilePath& path) {
  std::string cpuinfo;
  if (!ReadFileToString(path, &cpuinfo))
    return std::nullopt;
  return CreateFromString(cpuinfo);
}

bool CpuInfo::LoadFromString(std::string_view data) {
  std::map<std::string, std::string, std::less<>> p;
  proc_records_.clear();
  for (std::string_view line : base::SplitStringPiece(
           data, "\n", base::TRIM_WHITESPACE, base::SPLIT_WANT_ALL)) {
    // Blank lines separate processor records.
    if (line.size() == 0) {
      if (!p.empty())
        proc_records_.push_back(std::move(p));  // no empty records
      p.clear();
      continue;
    }

    std::vector<std::string_view> kv = base::SplitStringPiece(
        line, ":", base::TRIM_WHITESPACE, base::SPLIT_WANT_ALL);
    // must be a "key : value" pair and have a nonempty key
    if (kv.size() != 2 || kv[0].size() == 0)
      return false;
    p.emplace(std::make_pair(kv[0], kv[1]));
  }

  if (!p.empty())
    proc_records_.push_back(std::move(p));

  return true;
}

std::optional<CpuInfo> CpuInfo::CreateFromString(std::string_view data) {
  std::optional<CpuInfo> c = CpuInfo();
  if (!c->LoadFromString(data))
    return std::nullopt;
  return c;
}

size_t CpuInfo::NumProcRecords() const {
  return proc_records_.size();
}

std::optional<std::string_view> CpuInfo::LookUp(size_t proc_index,
                                                std::string_view key) const {
  if (proc_index >= proc_records_.size())
    return std::nullopt;
  auto& rec = proc_records_[proc_index];
  auto it = rec.find(key);
  if (it == rec.end())
    return std::nullopt;
  return std::optional<std::string_view>(it->second);
}

base::FilePath CpuInfo::DefaultPath() {
  return base::FilePath(kCpuInfoPath);
}

}  // namespace brillo
