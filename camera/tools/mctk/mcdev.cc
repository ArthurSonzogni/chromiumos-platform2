/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

// NOLINTNEXTLINE(build/include)
#include "tools/mctk/mcdev.h"

#include <unistd.h>

#include <optional>
#include <string>
#include <vector>

#include "tools/mctk/debug.h"
#include "tools/mctk/yaml_tree.h"

/*
 * PUBLIC
 */

V4lMcDev::~V4lMcDev() {
  if (fd_)
    close(*fd_);
}

bool V4lMcDev::ResetLinks() {
  bool ok = true;

  for (auto link : all_links_) {
    if (!link->IsDataLink() || link->IsImmutable())
      continue;

    ok = ok && link->SetEnable(false);
  }

  return ok;
}

V4lMcEntity* V4lMcDev::EntityById(__u32 id) {
  for (auto& entity : entities_) {
    if (entity->desc_.id == id)
      return entity.get();
  }

  return nullptr;
}

V4lMcEntity* V4lMcDev::EntityByName(std::string name) {
  for (auto& entity : entities_) {
    if (std::string(entity->desc_.name) == name)
      return entity.get();
  }

  return nullptr;
}

/*
 * PRIVATE
 */

void V4lMcDev::BuildCrosslinks() {
  /* Build MC-wide lists of pads and links */
  for (auto& entity : entities_) {
    for (auto& pad : entity->pads_)
      all_pads_.push_back(pad.get());

    for (auto& link : entity->links_)
      all_links_.push_back(link.get());
  }

  /* Let links/pads point at each other */
  for (auto* link : all_links_) {
    for (auto* pad : all_pads_) {
      /* Comparing two struct media_pad_desc */
      if (!memcmp(&pad->desc_, &link->desc_.source, sizeof(pad->desc_))) {
        link->src_ = pad;
        pad->links_.push_back(link);
      }

      /* Comparing two struct media_pad_desc */
      if (!memcmp(&pad->desc_, &link->desc_.sink, sizeof(pad->desc_))) {
        link->sink_ = pad;
        /* Not doing: pad->links_.push_back(link);
         * since we only store outgoing links in entity's/pad's array
         */
      }
    }
  }
}
