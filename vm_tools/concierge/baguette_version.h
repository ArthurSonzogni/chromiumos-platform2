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
constexpr char kBaguetteVersion[] = "2026-09-05-000105_63dca3b6b8503425a45d4c74f4dd8469de80fd88";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "308ff3959f87a7889d614c78151c5ffd96cef5ea02073c9bec2584b73e73493a";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "957046c8262abc63d8893a9a45639c555e8e6d79a4fd5f1fffd788f963d53462";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
