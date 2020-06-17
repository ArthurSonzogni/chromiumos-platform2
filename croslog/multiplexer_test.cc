// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "croslog/multiplexer.h"

#include <base/files/file_path.h>
#include <gtest/gtest.h>

#include "croslog/log_parser_syslog.h"

namespace croslog {

class MultiplexerTest : public ::testing::Test {
 public:
  MultiplexerTest() = default;

 private:
  DISALLOW_COPY_AND_ASSIGN(MultiplexerTest);
};

TEST_F(MultiplexerTest, Forward) {
  Multiplexer Multiplexer;
  Multiplexer.AddSource(base::FilePath("./testdata/TEST_NORMAL_LOG1"),
                        std::make_unique<LogParserSyslog>(), false);
  Multiplexer.AddSource(base::FilePath("./testdata/TEST_NORMAL_LOG2"),
                        std::make_unique<LogParserSyslog>(), false);

  {
    MaybeLogEntry e = Multiplexer.Forward();
    EXPECT_TRUE(e.has_value());
    EXPECT_EQ('3', e->entire_line()[46]);
  }

  {
    MaybeLogEntry e = Multiplexer.Forward();
    EXPECT_TRUE(e.has_value());
    EXPECT_EQ('4', e->entire_line()[46]);
  }

  {
    MaybeLogEntry e = Multiplexer.Forward();
    EXPECT_TRUE(e.has_value());
    EXPECT_EQ('5', e->entire_line()[46]);
  }

  {
    MaybeLogEntry e = Multiplexer.Forward();
    EXPECT_TRUE(e.has_value());
    EXPECT_EQ('6', e->entire_line()[46]);
  }

  EXPECT_FALSE(Multiplexer.Forward().has_value());
}

TEST_F(MultiplexerTest, BackwardFromLast) {
  Multiplexer Multiplexer;
  Multiplexer.AddSource(base::FilePath("./testdata/TEST_NORMAL_LOG1"),
                        std::make_unique<LogParserSyslog>(), false);
  Multiplexer.AddSource(base::FilePath("./testdata/TEST_NORMAL_LOG2"),
                        std::make_unique<LogParserSyslog>(), false);
  Multiplexer.SetLinesFromLast(0);

  {
    MaybeLogEntry e = Multiplexer.Backward();
    EXPECT_TRUE(e.has_value());
    EXPECT_EQ('6', e->entire_line()[46]);
  }

  {
    MaybeLogEntry e = Multiplexer.Backward();
    EXPECT_TRUE(e.has_value());
    EXPECT_EQ('5', e->entire_line()[46]);
  }

  {
    MaybeLogEntry e = Multiplexer.Backward();
    EXPECT_TRUE(e.has_value());
    EXPECT_EQ('4', e->entire_line()[46]);
  }

  {
    MaybeLogEntry e = Multiplexer.Backward();
    EXPECT_TRUE(e.has_value());
    EXPECT_EQ('3', e->entire_line()[46]);
  }

  EXPECT_FALSE(Multiplexer.Backward().has_value());
}

TEST_F(MultiplexerTest, InterleaveForwardAndBackward1) {
  Multiplexer Multiplexer;
  Multiplexer.AddSource(base::FilePath("./testdata/TEST_NORMAL_LOG1"),
                        std::make_unique<LogParserSyslog>(), false);
  Multiplexer.AddSource(base::FilePath("./testdata/TEST_NORMAL_LOG2"),
                        std::make_unique<LogParserSyslog>(), false);

  {
    MaybeLogEntry e = Multiplexer.Forward();
    EXPECT_TRUE(e.has_value());
    EXPECT_EQ('3', e->entire_line()[46]);
  }

  {
    MaybeLogEntry e = Multiplexer.Backward();
    EXPECT_TRUE(e.has_value());
    EXPECT_EQ('3', e->entire_line()[46]);
  }

  {
    MaybeLogEntry e = Multiplexer.Forward();
    EXPECT_TRUE(e.has_value());
    EXPECT_EQ('3', e->entire_line()[46]);
  }

  {
    MaybeLogEntry e = Multiplexer.Backward();
    EXPECT_TRUE(e.has_value());
    EXPECT_EQ('3', e->entire_line()[46]);
  }
}

TEST_F(MultiplexerTest, InterleaveForwardAndBackward2) {
  Multiplexer Multiplexer;
  Multiplexer.AddSource(base::FilePath("./testdata/TEST_NORMAL_LOG1"),
                        std::make_unique<LogParserSyslog>(), false);
  Multiplexer.AddSource(base::FilePath("./testdata/TEST_NORMAL_LOG2"),
                        std::make_unique<LogParserSyslog>(), false);
  Multiplexer.SetLinesFromLast(0);

  {
    MaybeLogEntry e = Multiplexer.Backward();
    EXPECT_TRUE(e.has_value());
    EXPECT_EQ('6', e->entire_line()[46]);
  }

  {
    MaybeLogEntry e = Multiplexer.Forward();
    EXPECT_TRUE(e.has_value());
    EXPECT_EQ('6', e->entire_line()[46]);
  }

  {
    MaybeLogEntry e = Multiplexer.Backward();
    EXPECT_TRUE(e.has_value());
    EXPECT_EQ('6', e->entire_line()[46]);
  }

  {
    MaybeLogEntry e = Multiplexer.Forward();
    EXPECT_TRUE(e.has_value());
    EXPECT_EQ('6', e->entire_line()[46]);
  }
}

}  // namespace croslog
