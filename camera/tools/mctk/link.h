/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Wrapper class capturing a snapshot of a media-ctl "link".
 *
 * Setter functions primarily update the state in the class.
 * If fd_mc_ is set to an fd to the media-ctl device, the matching ioctl()s
 * are sent to the kernel, programming the updated values into the driver.
 *
 * If fd_mc_ is set, this class DOES NOT own it and will NOT close it.
 */

#ifndef CAMERA_TOOLS_MCTK_LINK_H_
#define CAMERA_TOOLS_MCTK_LINK_H_

#include <linux/media.h>

#include <memory>
#include <optional>

#include "tools/mctk/yaml_tree.h"

class V4lMcPad;

class V4lMcLink {
 public:
  /* Public functions */
  V4lMcLink() : desc_({}) {}
  explicit V4lMcLink(int fd) : desc_({}), fd_mc_(fd) {}

  /* Factory functions */
  static std::unique_ptr<V4lMcLink> CreateFromYamlNode(YamlNode& node_link,
                                                       V4lMcPad& src_pad);

  /* Getters for link flags (for convenience) */
  bool IsDataLink();
  bool IsImmutable();
  bool IsEnabled();

  /* Setters for link flags */
  bool SetEnable(bool enable);

  /* Public variables */

  /* Link description, as per MEDIA_IOC_ENUM_LINKS */
  struct media_link_desc desc_;

  /* Convenience pointers */
  V4lMcPad* src_;
  V4lMcPad* sink_;

 private:
  /* Private variables */

  /* Optional fd to V4L2 media-ctl this link is a part of.
   * If this is set, setters will additionally call ioctl() on this fd.
   */
  std::optional<int> fd_mc_;
};

#endif /* CAMERA_TOOLS_MCTK_LINK_H_ */
