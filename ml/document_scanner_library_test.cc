// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "ml/document_scanner_library.h"
#include "ml/util.h"

namespace ml {

TEST(DocumentScannerLibraryTest, CanLoadLibrary) {
  auto* const library = DocumentScannerLibrary::GetInstance();
  if (IsAsan()) {
    EXPECT_FALSE(DocumentScannerLibrary::IsSupported());
    return;
  }

  if (DocumentScannerLibrary::IsSupported()) {
    EXPECT_EQ(library->Initialize(),
              DocumentScannerLibrary::InitializeResult::kOk);
  }
}

TEST(DocumentScannerLibraryTest, GetScanner) {
  if (!DocumentScannerLibrary::IsSupported()) {
    // No need to test the behavior on an unsupported device.
    return;
  }

  auto* const library = DocumentScannerLibrary::GetInstance();
  ASSERT_EQ(library->Initialize(),
            DocumentScannerLibrary::InitializeResult::kOk);

  auto scanner = library->CreateDocumentScanner();
  EXPECT_NE(scanner, nullptr);
}

}  // namespace ml
