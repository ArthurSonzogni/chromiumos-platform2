// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MIST_MOCK_CONTEXT_H_
#define MIST_MOCK_CONTEXT_H_

#include "mist/context.h"

namespace mist {

class MockConfigLoader;
class MockUdev;

// A mock context class, which replaces helper objects in the Context class with
// mocks and is used in place of the Context class in unit tests.
class MockContext : public Context {
 public:
  MockContext() = default;
  ~MockContext() override = default;

  // Initializes all helper objects with mocks in the context for unit testing.
  // Always returns true.
  bool Initialize() override;

  // Returns the MockConfigLoader object held by this context object.
  MockConfigLoader* GetMockConfigLoader() const;

  // Returns the MockUdev object held by this context object.
  MockUdev* GetMockUdev() const;

 private:
  DISALLOW_COPY_AND_ASSIGN(MockContext);
};

}  // namespace mist

#endif  // MIST_MOCK_CONTEXT_H_
