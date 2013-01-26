// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/bind.h"
#include "base/callback.h"

#include "glinterface.h"
#include "glinterfacetest.h"
#include "main.h"

namespace glbench {

namespace {

bool IsEven(int value) {
  return ((value % 2) == 0);
}

}  // namespace

class ContextTest : public GLInterfaceTest {
 public:
  ContextTest() {}
  virtual ~ContextTest() {}
  virtual bool TestFunc(int iter);
  virtual const char* Name() const { return "context"; }

 private:
  DISALLOW_COPY_AND_ASSIGN(ContextTest);
};

bool ContextTest::TestFunc(int iter) {
  GLInterface* interface = g_main_gl_interface.get();
  CHECK(interface);
  GLContext main_context = interface->GetMainContext();
  GLContext new_context = interface->CreateContext();
  CHECK(main_context);
  CHECK(new_context);

  for (int i = 0 ; i < iter; ++i) {
    if (!render_func_.is_null())
      render_func_.Run();
    interface->MakeCurrent(IsEven(i) ? new_context : main_context);
  }

  interface->MakeCurrent(main_context);
  interface->DeleteContext(new_context);
  return true;
}

TestBase* GetContextTest() {
  return new ContextTest;
}

} // namespace glbench
