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
constexpr char kBaguetteVersion[] = "2026-01-01-000126_0ec25f28a76baa3dc128c153609277e36537fdd2";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "e958e830c899a09ec7e3ebd509487423ea61f32d200e9f947c169e8760064075";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "5fe6051863ae2b49f58fd275a5e48cfcf182cdccdc064294a8834d897fecaf15";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
