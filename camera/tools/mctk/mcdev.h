/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Wrapper class capturing a snapshot of the description and and child nodes
 * of a V4L2 media controller.
 *
 * If fd_ is set, this class owns it and will close it upon destruction.
 */

#ifndef CAMERA_TOOLS_MCTK_MCDEV_H_
#define CAMERA_TOOLS_MCTK_MCDEV_H_

#include <stdio.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "tools/mctk/entity.h"
#include "tools/mctk/link.h"
#include "tools/mctk/pad.h"
#include "tools/mctk/yaml_tree.h"

class V4lMcDev {
 public:
  /* Public functions */

  /* This constructor should be private, but that forces hacks to make
   * unique_ptr work. So let's keep it public, but please use the
   * factory functions instead of the constructor directly!
   */
  V4lMcDev() : info_({}) {}

  ~V4lMcDev();

  bool ResetLinks();
  V4lMcEntity* EntityById(__u32 id);
  V4lMcEntity* EntityByName(std::string name);

  static std::unique_ptr<V4lMcDev> CreateFromKernel(int fd_mc);
  static std::unique_ptr<V4lMcDev> CreateFromYamlNode(YamlNode& node_mc);

  void ToYamlFile(FILE& file);

  /* Public variables */

  struct media_device_info info_;

  std::vector<std::unique_ptr<V4lMcEntity>> entities_;
  std::vector<V4lMcPad*> all_pads_;
  std::vector<V4lMcLink*> all_links_;

 private:
  /* Private functions */

  void BuildCrosslinks();

  /* Private variables */

  std::optional<int> fd_;
};

#endif /* CAMERA_TOOLS_MCTK_MCDEV_H_ */
