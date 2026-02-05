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
constexpr char kBaguetteVersion[] = "2026-02-05-000123_3356b69435692b950fbebe9c3577ac1ac925c679";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "4746b8df1def2fd58310bc76ef6fce97607ccd863ae2cda3ce2bcf221b8b80cd";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "9507253bb92218472813a43033614e941693709b4c7089fd86b14a9a55b239f6";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
