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
constexpr char kBaguetteVersion[] = "2025-11-21-000128_469cd79c5aa5f79b965fb0bb275cc132ceb58146";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "6edb0fb7b5bccff2ea863b978e92bb2393898d540551adbc72924b7d723aa803";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "75f3d5289a140b48a11f3db1649b5b2af7b46c323c738321f9988525a925f1e8";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
