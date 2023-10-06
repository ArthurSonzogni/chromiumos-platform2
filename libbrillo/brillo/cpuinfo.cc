// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "brillo/cpuinfo.h"

#include <utility>

#include <base/files/file_util.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>

#include "brillo/strings/string_utils.h"

namespace brillo {

namespace {

constexpr char kCpuInfoPath[] = "/proc/cpuinfo";

}  // namespace

CpuInfo::CpuInfo() = default;
CpuInfo::~CpuInfo() = default;

CpuInfo::CpuInfo(CpuInfo&& other) = default;
CpuInfo& CpuInfo::operator=(CpuInfo&& other) = default;

CpuInfo::CpuInfo(RecordsVec proc_records)
    : proc_records_(std::move(proc_records)) {}

std::optional<CpuInfo> CpuInfo::Create(const base::FilePath& path) {
  std::string cpuinfo;
  if (!ReadFileToString(path, &cpuinfo)) {
    return std::nullopt;
  }
  return CreateFromString(cpuinfo);
}

std::optional<CpuInfo::RecordsVec> CpuInfo::ParseFromString(
    std::string_view data) {
  std::optional<RecordsVec> recs = RecordsVec();
  Record p;

  for (std::string_view line : base::SplitStringPiece(
           data, "\n", base::TRIM_WHITESPACE, base::SPLIT_WANT_ALL)) {
    // Blank lines separate processor records.
    if (line.size() == 0) {
      if (!p.empty()) {
        // No empty records.
        recs->push_back(std::move(p));
      }
      p.clear();
      continue;
    }

    auto kvopt =
        brillo::string_utils::SplitAtFirst(line, ":", base::TRIM_WHITESPACE);
    if (!kvopt.has_value()) {
      // Must be a "key : value" pair.
      return std::nullopt;
    }
    if (kvopt.value().first.size() == 0) {
      // Must have a nonempty key.
      return std::nullopt;
    }
    p.emplace(std::move(kvopt).value());
  }

  if (!p.empty()) {
    recs->push_back(std::move(p));
  }

  return recs;
}

std::optional<CpuInfo> CpuInfo::CreateFromString(std::string_view data) {
  std::optional<RecordsVec> recs = ParseFromString(data);
  if (!recs.has_value()) {
    return std::nullopt;
  }
  return CpuInfo(std::move(recs).value());
}

size_t CpuInfo::NumProcRecords() const {
  return proc_records_.size();
}

std::optional<std::string_view> CpuInfo::LookUp(size_t proc_index,
                                                std::string_view key) const {
  if (proc_index >= proc_records_.size()) {
    return std::nullopt;
  }
  auto& rec = proc_records_[proc_index];
  auto it = rec.find(key);
  if (it == rec.end()) {
    return std::nullopt;
  }
  return std::optional<std::string_view>(it->second);
}

base::FilePath CpuInfo::DefaultPath() {
  return base::FilePath(kCpuInfoPath);
}

}  // namespace brillo
