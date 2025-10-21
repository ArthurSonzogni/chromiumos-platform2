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
constexpr char kBaguetteVersion[] = "2025-10-21-000539_4fca5290e4bb74472bdda6c520528ff244a85693";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "249594b8f4e6c4e0acb8dca9feb1e8e52e074e629e457bceb21f8d18787ca307";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "a23fe4e4d2147c1532c40616bb6bf09290d8f86c7d0fa17ea7d45d354c41307f";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
