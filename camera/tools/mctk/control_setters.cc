/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Setters for abstract models of V4L2 controls.
 *
 * If the model has an fd for a kernel device set, then the setters will
 * propagate the new values to the kernel.
 *
 * Return values:
 *  - on success: true
 *  - on failure: false
 */

#include "tools/mctk/control.h"

#include <linux/media.h>
#include <linux/types.h>
#include <linux/videodev2.h>
#include <stddef.h> /* size_t */
#include <sys/ioctl.h>

#include <optional>
#include <string>
#include <vector>

#include "tools/mctk/debug.h"

bool V4lMcControl::WrapIoctl(struct v4l2_ext_control& controls) {
  if (fd_ent_.has_value()) {
    struct v4l2_ext_controls ext_controls = {};
    ext_controls.which = V4L2_CTRL_WHICH_CUR_VAL;
    ext_controls.count = 1;
    ext_controls.controls = &controls;

    if (ioctl(fd_ent_.value(), VIDIOC_S_EXT_CTRLS, &ext_controls) < 0) {
      MCTK_PERROR("VIDIOC_S_EXT_CTRLS");
      MCTK_ERR("Failed setting control " + std::to_string(desc_.id));
      return false;
    }
  }

  return true;
}

bool V4lMcControl::Set(std::vector<__s32>& values_s32) {
  MCTK_ASSERT(!this->IsReadOnly());
  MCTK_ASSERT(this->desc_.type == V4L2_CTRL_TYPE_INTEGER ||
              this->desc_.type == V4L2_CTRL_TYPE_BOOLEAN ||
              this->desc_.type == V4L2_CTRL_TYPE_MENU ||
              this->desc_.type == V4L2_CTRL_TYPE_BUTTON ||
              this->desc_.type == V4L2_CTRL_TYPE_BITMASK ||
              this->desc_.type == V4L2_CTRL_TYPE_INTEGER_MENU);
  MCTK_ASSERT(values_s32.size() == this->desc_.elems);
  this->values_s32_ = values_s32;

  struct v4l2_ext_control ec = {};
  ec.id = this->desc_.id;
  ec.size = values_s32.size() * sizeof(values_s32[0]);
  ec.ptr = this->values_s32_.data();

  /* Legacy controls are set to a value rather than a pointer */
  if (!this->desc_.nr_of_dims) {
    MCTK_ASSERT(values_s32.size() == 1);
    ec.size = 0;
    ec.value = this->values_s32_[0];
  }

  return WrapIoctl(ec);
}

bool V4lMcControl::Set(std::vector<__s64>& values_s64) {
  MCTK_ASSERT(!this->IsReadOnly());
  MCTK_ASSERT(this->desc_.type == V4L2_CTRL_TYPE_INTEGER64);
  MCTK_ASSERT(values_s64.size() == this->desc_.elems);
  this->values_s64_ = values_s64;

  struct v4l2_ext_control ec = {};
  ec.id = this->desc_.id;
  ec.size = values_s64.size() * sizeof(values_s64[0]);
  ec.ptr = this->values_s64_.data();

  /* Legacy controls are set to a value rather than a pointer */
  if (!this->desc_.nr_of_dims) {
    MCTK_ASSERT(values_s64.size() == 1);
    ec.size = 0;
    ec.value = this->values_s64_[0];
  }

  return WrapIoctl(ec);
}

bool V4lMcControl::Set(std::vector<std::string>& values_string) {
  MCTK_ASSERT(!this->IsReadOnly());
  MCTK_ASSERT(this->desc_.type == V4L2_CTRL_TYPE_STRING);
  MCTK_ASSERT(this->desc_.elem_size >= 1);

  MCTK_ASSERT(values_string.size() == this->desc_.elems);

  /* Check that the new strings aren't overflowing the target.
   * Note that they have to be SHORTER than elem_size, because
   * elem_size includes the terminating '\0', whereas C++
   * std::string's .size() does not include it.
   */
  for (std::string& val : values_string) {
    MCTK_ASSERT(val.size() < this->desc_.elem_size);
  }

  this->values_string_ = values_string;

  /* Temporary buffer for uploading to kernel */
  std::vector<char> temp;
  temp.resize(this->desc_.elems * this->desc_.elem_size);

  for (size_t i = 0; i < this->desc_.elems; i++) {
    strncpy(&temp.data()[i * this->desc_.elem_size],
            this->values_string_[i].c_str(), this->desc_.elem_size - 1);
  }

  struct v4l2_ext_control ec = {};
  ec.id = this->desc_.id;
  ec.size = this->desc_.elems * this->desc_.elem_size;
  ec.string = temp.data();

  return WrapIoctl(ec);
}

bool V4lMcControl::Set(std::vector<__u8>& values_u8) {
  MCTK_ASSERT(!this->IsReadOnly());
  MCTK_ASSERT(this->desc_.type == V4L2_CTRL_TYPE_U8);
  MCTK_ASSERT(values_u8.size() == this->desc_.elems);
  this->values_u8_ = values_u8;

  struct v4l2_ext_control ec = {};
  ec.id = this->desc_.id;
  ec.size = this->desc_.elems * this->desc_.elem_size;
  ec.p_u8 = this->values_u8_.data();

  return WrapIoctl(ec);
}

bool V4lMcControl::Set(std::vector<__u16>& values_u16) {
  MCTK_ASSERT(!this->IsReadOnly());
  MCTK_ASSERT(this->desc_.type == V4L2_CTRL_TYPE_U16);
  MCTK_ASSERT(values_u16.size() == this->desc_.elems);
  this->values_u16_ = values_u16;

  struct v4l2_ext_control ec = {};
  ec.id = this->desc_.id;
  ec.size = this->desc_.elems * this->desc_.elem_size;
  ec.p_u16 = this->values_u16_.data();

  return WrapIoctl(ec);
}

bool V4lMcControl::Set(std::vector<__u32>& values_u32) {
  MCTK_ASSERT(!this->IsReadOnly());
  MCTK_ASSERT(this->desc_.type == V4L2_CTRL_TYPE_U32);
  MCTK_ASSERT(values_u32.size() == this->desc_.elems);
  this->values_u32_ = values_u32;

  struct v4l2_ext_control ec = {};
  ec.id = this->desc_.id;
  ec.size = this->desc_.elems * this->desc_.elem_size;
  ec.p_u32 = this->values_u32_.data();

  return WrapIoctl(ec);
}

#ifdef V4L2_CTRL_TYPE_AREA
bool V4lMcControl::Set(std::vector<struct v4l2_area>& values_area) {
  MCTK_ASSERT(!this->IsReadOnly());
  MCTK_ASSERT(this->desc_.type == V4L2_CTRL_TYPE_AREA);
  MCTK_ASSERT(values_area.size() == this->desc_.elems);
  this->values_area_ = values_area;

  struct v4l2_ext_control ec = {};
  ec.id = this->desc_.id;
  ec.size = this->desc_.elems * this->desc_.elem_size;
  ec.p_area = this->values_area_.data();

  return WrapIoctl(ec);
}
#endif /* V4L2_CTRL_TYPE_AREA */

template <typename T>
bool V4lMcControl::Set(T value) {
  std::vector<T> temp = {value};

  return this->Set(temp);
}

template bool V4lMcControl::Set<__s32>(__s32 value);
template bool V4lMcControl::Set<__s64>(__s64 value);
