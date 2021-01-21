/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CAMERA_TOOLS_CRC_CCITT_H_
#define CAMERA_TOOLS_CRC_CCITT_H_

#include <stddef.h>
#include <stdint.h>

#include <base/containers/span.h>

uint16_t Crc16CcittFalse(base::span<const uint8_t> buf, uint16_t init);

#endif  // CAMERA_TOOLS_CRC_CCITT_H_
