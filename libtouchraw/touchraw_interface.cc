// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libtouchraw/touchraw_interface.h"

#include <fcntl.h>
#include <memory>
#include <utility>

#include <base/files/scoped_file.h>
#include <base/logging.h>
#include <base/memory/ptr_util.h>

#include "libtouchraw/defragmenter.h"
#include "libtouchraw/parser.h"
#include "libtouchraw/reader.h"

namespace touchraw {

std::unique_ptr<TouchrawInterface> TouchrawInterface::Create(
    const base::FilePath& path, std::unique_ptr<HeatmapConsumerInterface> q) {
  base::ScopedFD fd(open(path.value().c_str(), O_RDONLY | O_CLOEXEC));
  if (!fd.is_valid()) {
    LOG(ERROR) << "Invalid file descriptor for device " << path.value();
    return nullptr;
  }

  auto df = std::make_unique<Defragmenter>(std::move(q));
  if (!df) {
    return nullptr;
  }
  std::unique_ptr<Parser> parser = Parser::Create(fd.get(), std::move(df));
  if (!parser) {
    return nullptr;
  }
  auto reader = std::make_unique<Reader>(std::move(fd), std::move(parser));
  if (!reader) {
    return nullptr;
  }
  // Using `new` to access a non-public constructor.
  return base::WrapUnique(new TouchrawInterface(std::move(reader)));
}

TouchrawInterface::TouchrawInterface(std::unique_ptr<Reader> reader)
    : reader_(std::move(reader)) {}

absl::Status TouchrawInterface::StartWatching() {
  return reader_->Start();
}

void TouchrawInterface::StopWatching() {
  reader_->Stop();
}

}  // namespace touchraw
