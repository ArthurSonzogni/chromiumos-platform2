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
constexpr char kBaguetteVersion[] = "2026-02-02-000119_da0a3a19712d47bd0c45072738d02724d88baf3c";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "ebba2fb93d83ab1d792353dd1260b96f725e00966aabc699eaf186a06ca4f058";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "e83679c4e2d2a03ea443c166b65c5861f7d6da7af3692d7ec3ba808901a06743";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
