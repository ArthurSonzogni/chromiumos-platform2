/*
 * Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CAMERA_HAL_ADAPTER_VENDOR_TAG_OPS_DELEGATE_H_
#define CAMERA_HAL_ADAPTER_VENDOR_TAG_OPS_DELEGATE_H_

#include <hardware/camera3.h>

#include "hal_adapter/cros_camera_mojo_utils.h"
#include "mojo/camera_common.mojom.h"

namespace cros {

class VendorTagOpsDelegate final
    : public internal::MojoBinding<mojom::VendorTagOps> {
 public:
  VendorTagOpsDelegate(scoped_refptr<base::SingleThreadTaskRunner> task_runner,
                       vendor_tag_ops_t* ops);

  ~VendorTagOpsDelegate() = default;

 private:
  void GetTagCount(const GetTagCountCallback& callback);

  void GetAllTags(const GetAllTagsCallback& callback);

  void GetSectionName(uint32_t tag, const GetSectionNameCallback& callback);

  void GetTagName(uint32_t tag, const GetTagNameCallback& callback);

  void GetTagType(uint32_t tag, const GetTagTypeCallback& callback);

  vendor_tag_ops_t* vendor_tag_ops_;

  DISALLOW_IMPLICIT_CONSTRUCTORS(VendorTagOpsDelegate);
};

}  // namespace cros

#endif  // CAMERA_HAL_ADAPTER_VENDOR_TAG_OPS_DELEGATE_H_
