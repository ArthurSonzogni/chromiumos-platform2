/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Merge:
 * This is Tool-3's heart.
 * This is where values from one model are copied into a different model.
 *
 * If the target model has access to kernel device files, then the parameters
 * will be applied to a real device as well.
 */

#ifndef CAMERA_TOOLS_MCTK_MERGE_H_
#define CAMERA_TOOLS_MCTK_MERGE_H_

#include "tools/mctk/mcdev.h"
#include "tools/mctk/remap.h"

bool V4lMcMergeMcDev(V4lMcDev& target, V4lMcDev& source, V4lMcRemap* remap);

#endif /* CAMERA_TOOLS_MCTK_MERGE_H_ */
