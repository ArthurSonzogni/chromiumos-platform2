// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// This is an example of a tool. A tool is the implementation of one or more of
// debugd's dbus methods. The main DebugDaemon class creates a single instance
// of each tool and calls it to answer methods.

#include "example_tool.h"

#include "process_with_output.h"

using base::StringPrintf;

namespace debugd {

ExampleTool::ExampleTool() { }

ExampleTool::~ExampleTool() { }

// Tool methods have the same signature as the generated DBus adaptors. Most
// pertinently, this means they take their DBus::Error argument as a non-const
// reference (hence the NOLINT). Tool methods are generally written in
// can't-fail style, since their output is usually going to be displayed to the
// user; instead of returning a DBus exception, we tend to return a string
// indicating what went wrong.
std::string ExampleTool::GetExample(DBus::Error& error) { // NOLINT
  std::string path;
  if (!SandboxedProcess::GetHelperPath("example", &path))
    return "<path too long>";
  // This whole method is synchronous, so we create a subprocess, let it run to
  // completion, then gather up its output to return it.
  ProcessWithOutput process;
  if (!process.Init())
    return "<process init failed>";
  // If you're going to add switches to a command, have a look at the Process
  // interface; there's support for adding options specifically.
  process.AddArg(path);
  process.AddArg("hello");
  // Run the process to completion. If the process might take a while, you may
  // have to make this asynchronous using .Start().
  if (process.Run() != 0)
    return "<process exited with nonzero status>";
  std::string output;
  process.GetOutput(&output);
  return output;
}

};  // namespace debugd
