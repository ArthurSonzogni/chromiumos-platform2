// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dlcservice/system_state.h"

#include <limits.h>
#include <utility>

#include <base/files/file_util.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>

#include "dlcservice/boot/boot_device.h"

namespace dlcservice {

std::unique_ptr<SystemState> SystemState::g_instance_ = nullptr;

SystemState::SystemState(
    std::unique_ptr<org::chromium::ImageLoaderInterfaceProxyInterface>
        image_loader_proxy,
    std::unique_ptr<org::chromium::UpdateEngineInterfaceProxyInterface>
        update_engine_proxy,
    std::unique_ptr<BootSlot> boot_slot,
    const base::FilePath& manifest_dir,
    const base::FilePath& preloaded_content_dir,
    const base::FilePath& content_dir,
    const base::FilePath& metadata_dir)
    : image_loader_proxy_(std::move(image_loader_proxy)),
      update_engine_proxy_(std::move(update_engine_proxy)),
      boot_slot_(std::move(boot_slot)),
      manifest_dir_(manifest_dir),
      preloaded_content_dir_(preloaded_content_dir),
      content_dir_(content_dir),
      metadata_dir_(metadata_dir) {}

// static
void SystemState::Initialize(
    std::unique_ptr<org::chromium::ImageLoaderInterfaceProxyInterface>
        image_loader_proxy,
    std::unique_ptr<org::chromium::UpdateEngineInterfaceProxyInterface>
        update_engine_proxy,
    std::unique_ptr<BootSlot> boot_slot,
    const base::FilePath& manifest_dir,
    const base::FilePath& preloaded_content_dir,
    const base::FilePath& content_dir,
    const base::FilePath& metadata_dir,
    bool for_test) {
  if (!for_test)
    CHECK(!g_instance_) << "SystemState::Initialize() called already.";
  g_instance_.reset(new SystemState(
      std::move(image_loader_proxy), std::move(update_engine_proxy),
      std::move(boot_slot), manifest_dir, preloaded_content_dir, content_dir,
      metadata_dir));
}

// static
SystemState* SystemState::Get() {
  CHECK(g_instance_);
  return g_instance_.get();
}

org::chromium::ImageLoaderInterfaceProxyInterface* SystemState::image_loader()
    const {
  return image_loader_proxy_.get();
}

org::chromium::UpdateEngineInterfaceProxyInterface* SystemState::update_engine()
    const {
  return update_engine_proxy_.get();
}

const BootSlot& SystemState::boot_slot() const {
  return *boot_slot_;
}

const base::FilePath& SystemState::manifest_dir() const {
  return manifest_dir_;
}

const base::FilePath& SystemState::preloaded_content_dir() const {
  return preloaded_content_dir_;
}

const base::FilePath& SystemState::content_dir() const {
  return content_dir_;
}

const base::FilePath& SystemState::metadata_dir() const {
  return metadata_dir_;
}

}  // namespace dlcservice
