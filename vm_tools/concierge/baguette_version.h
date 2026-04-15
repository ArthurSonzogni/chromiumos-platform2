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
constexpr char kBaguetteVersion[] = "2026-04-15-000120_f7705b6c6e199242efd8f25a3808b3f4a6683435";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "6f2ff2796817f93987ea665a1568adb06423374afff194a168e424baeca33a95";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "919c9370a951468f8321d73b03184075fc5da263e0a6f91a92e264de05569d3e";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
