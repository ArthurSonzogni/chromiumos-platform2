// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "runtime_probe/functions/audio_codec.h"

#include <string>
#include <string_view>
#include <utility>

#include <base/containers/contains.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>
#include <base/values.h>

#include "runtime_probe/system/context.h"
#include "runtime_probe/utils/file_utils.h"

namespace runtime_probe {
namespace {

constexpr size_t kCodecFileMaxSize = 65536;
constexpr char kHdaCodecPathPattern[] = "proc/asound/card*/codec*";
constexpr std::string_view kCodecKey = "Codec:";

AudioCodecFunction::DataType ProbeI2cCodecFromFile(
    const base::FilePath& asoc_path) {
  std::string asoc_content;
  if (!base::ReadFileToStringWithMaxSize(asoc_path, &asoc_content,
                                         kCodecFileMaxSize)) {
    if (asoc_content.size() == kCodecFileMaxSize) {
      LOG(ERROR) << "Cannot read " << asoc_path
                 << " because its size is greater than " << kCodecFileMaxSize;
    } else {
      PLOG(ERROR) << "Cannot read " << asoc_path;
    }
    return {};
  }

  AudioCodecFunction::DataType result{};
  for (std::string_view codec :
       base::SplitStringPiece(asoc_content, "\n", base::TRIM_WHITESPACE,
                              base::SPLIT_WANT_NONEMPTY)) {
    base::Value::Dict value;
    value.Set("name", codec);
    result.Append(std::move(value));
  }
  return result;
}

AudioCodecFunction::DataType ProbeHdaCodecFromFile(
    const base::FilePath& procfs_path) {
  std::string codec_content;
  if (!base::ReadFileToStringWithMaxSize(procfs_path, &codec_content,
                                         kCodecFileMaxSize)) {
    if (codec_content.size() == kCodecFileMaxSize) {
      LOG(ERROR) << "Cannot read " << procfs_path
                 << " because its size is greater than " << kCodecFileMaxSize;
    } else {
      PLOG(ERROR) << "Cannot read " << procfs_path;
    }
    return {};
  }

  AudioCodecFunction::DataType result{};
  for (std::string line :
       base::SplitString(codec_content, "\n", base::TRIM_WHITESPACE,
                         base::SPLIT_WANT_NONEMPTY)) {
    if (!base::StartsWith(line, kCodecKey, base::CompareCase::SENSITIVE)) {
      continue;
    }

    std::string raw_codec = line.substr(kCodecKey.size());
    auto codec =
        base::TrimWhitespaceASCII(raw_codec, base::TrimPositions::TRIM_ALL);

    base::Value::Dict value;
    value.Set("name", codec);
    result.Append(std::move(value));
  }
  return result;
}

}  // namespace

AudioCodecFunction::DataType AudioCodecFunction::EvalImpl() const {
  DataType results{};

  for (const auto& asoc_path_str : kAsocPaths) {
    base::FilePath asoc_path = GetRootedPath(asoc_path_str);
    if (!PathExists(asoc_path))
      continue;
    results = ProbeI2cCodecFromFile(asoc_path);
  }

  base::FilePath procfs_pattern =
      Context::Get()->root_dir().Append(kHdaCodecPathPattern);
  for (const auto& procfs_path : Glob(procfs_pattern)) {
    auto hda_result = ProbeHdaCodecFromFile(procfs_path);
    for (auto& res : hda_result) {
      results.Append(std::move(res));
    }
  }

  if (results.empty()) {
    LOG(ERROR) << "Cannot find any asoc files or ALSA proc files which contain "
                  "the codecs.";
  }

  return results;
}

void AudioCodecFunction::PostHelperEvalImpl(DataType* results) const {
  auto helper_results = std::move(*results);
  *results = DataType();

  for (auto& helper_result : helper_results) {
    auto* codec = helper_result.GetDict().FindString("name");
    if (codec == nullptr || base::Contains(kKnownInvalidCodecNames, *codec) ||
        codec->find("HDMI") != std::string::npos) {
      continue;
    }
    results->Append(std::move(helper_result));
  }
}

}  // namespace runtime_probe
