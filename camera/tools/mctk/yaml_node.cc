/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "tools/mctk/yaml_tree.h"

#include "tools/mctk/debug.h"

bool YamlNode::IsEmpty() {
  YamlEmpty* empty = dynamic_cast<YamlEmpty*>(this);
  return !!empty;
}
