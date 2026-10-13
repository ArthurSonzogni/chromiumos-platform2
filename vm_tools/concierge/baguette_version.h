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
constexpr char kBaguetteVersion[] = "2026-10-13-000107_cca8dee489a943bc97face1a5df71687cbbede62";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "3c5ebae88a16041ef65e465cba4cef14312e125cf9bed3d4ece73f733643ffea";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "6cc6d59fdc30927189e21d952d5968431f31b0e88fbd524d59e84b4d16f6250a";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
