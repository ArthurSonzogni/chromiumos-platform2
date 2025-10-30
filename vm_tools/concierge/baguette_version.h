// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
#define VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_

// This constant points to the image downloaded for new installations of
// Baguette.
// TODO(crbug.com/393151776): Point to luci recipe and builders that update this
// URL when new images are available.

// clang-format off
constexpr char kBaguetteVersion[] = "2025-10-30-000103_142b3d6e79adfc58d9abf821a2f640d057ef6001";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "79472715d2ac72eb56386b6dd813025267a3a41bcf539d15ea988d9991b43508";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "67f4aa5a480ad2738b9d1512d3e817a97ab3e95bcaceb203eb131b55995cdec0";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
