/*
 * Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CAMERA_FEATURES_HDRNET_VENDOR_TAGS_H_
#define CAMERA_FEATURES_HDRNET_VENDOR_TAGS_H_

// TODO(jcliang): Replace this file with the vendor tag definitions provided by
// Intel.

enum {
  // int32_t
  CHROMEOS_IPU6_RGBS_STATS_GRID_WIDTH = 0x80040000,
  // int32_t
  CHROMEOS_IPU6_RGBS_STATS_GRID_HEIGHT = 0x80040001,
  // uint8_t
  CHROMEOS_IPU6_RGBS_STATS_SHADING_CORRECTION = 0x80040002,
  // (uint8_t x 5) x GRID_WIDTH x GRID_HEIGHT
  CHROMEOS_IPU6_RGBS_STATS_BLOCKS = 0x80040003,
};

#endif  // CAMERA_FEATURES_HDRNET_VENDOR_TAGS_H_
