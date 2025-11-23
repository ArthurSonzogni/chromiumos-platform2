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
constexpr char kBaguetteVersion[] = "2025-11-23-000120_86656621c0ed9f92196ab93e176df6ee5c51c56f";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "1cf3dfe2137da1912f54ae6c49b2d830ab66631bce7492b9bfa57cf6d28f3426";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "d158f02bc5e2e50d1eeea798ced866ee701cc28b4ef540635c62fa5d3b5e8b1b";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
