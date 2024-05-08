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

#include "libtouchraw/crop.h"
#include "libtouchraw/defragmenter.h"
#include "libtouchraw/parser.h"
#include "libtouchraw/reader.h"
#include "libtouchraw/reshaper.h"

namespace touchraw {

std::unique_ptr<TouchrawInterface> TouchrawInterface::Create(
    const base::FilePath& path,
    std::unique_ptr<HeatmapConsumerInterface> consumer,
    const Crop crop) {
  base::ScopedFD fd(open(path.value().c_str(), O_RDONLY | O_CLOEXEC));
  if (!fd.is_valid()) {
    LOG(ERROR) << "Invalid file descriptor for device " << path.value();
    return nullptr;
  }

  if (crop.bottom_crop || crop.left_crop || crop.right_crop || crop.top_crop) {
    std::unique_ptr<HeatmapConsumerInterface> tmp;
    tmp.swap(consumer);
    std::unique_ptr<HeatmapConsumerInterface> reshaper =
        std::make_unique<Reshaper>(crop, std::move(tmp));
    if (!reshaper) {
      LOG(ERROR) << "Failed to create reshaper.";
      return nullptr;
    }
    consumer.swap(reshaper);
    DVLOG(1) << "Reshaper added. Will crop top by: "
             << static_cast<int>(crop.top_crop)
             << ", crop right by: " << static_cast<int>(crop.right_crop)
             << ", crop bottom by: " << static_cast<int>(crop.bottom_crop)
             << ", crop left by: " << static_cast<int>(crop.left_crop);
  }

  auto df = std::make_unique<Defragmenter>(std::move(consumer));
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
