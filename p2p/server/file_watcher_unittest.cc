// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "common/testutil.h"
#include "server/file_watcher.h"

#include <stdarg.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <arpa/inet.h>

#include <glib-object.h>

#include <iostream>
#include <string>
#include <cctype>
#include <cinttypes>
#include <vector>
#include <tuple>

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include <base/bind.h>
#include <base/command_line.h>
#include <base/logging.h>
#include <base/file_path.h>
#include <base/threading/simple_thread.h>
#include <base/stringprintf.h>

using std::vector;

using base::Bind;
using base::FilePath;
using base::Unretained;

using testing::_;
using testing::StrictMock;

using p2p::testutil::ExpectCommand;
using p2p::testutil::kDefaultMainLoopTimeoutMs;
using p2p::testutil::RunGMainLoopUntil;
using p2p::testutil::SetupTestDir;
using p2p::testutil::TeardownTestDir;

namespace p2p {

namespace server {

// ------------------------------------------------------------------------

class FileWatcherListener {
 public:
  explicit FileWatcherListener(FileWatcher* file_watcher) {
    file_watcher->SetChangedCallback(
        Bind(&FileWatcherListener::OnChanged, Unretained(this)));
  }

  virtual void OnChanged(const FilePath& file,
                         FileWatcher::EventType event_type) = 0;

 private:
  DISALLOW_COPY_AND_ASSIGN(FileWatcherListener);
};

class MockFileWatcherListener : public FileWatcherListener {
 public:
  explicit MockFileWatcherListener(FileWatcher* file_watcher)
      : FileWatcherListener(file_watcher),
      num_calls_(0) {
    ON_CALL(*this, OnChanged(_, _))
      .WillByDefault(testing::InvokeWithoutArgs(
          this, &MockFileWatcherListener::OnCall));
  }

  MOCK_METHOD2(OnChanged,
               void(const FilePath& file, FileWatcher::EventType event_type));

  // NumCallsReached() returns true when the number of calls to |this|
  // is at least |num_calls|. This is used to terminate the GLib main loop
  // excecution and verify the expectations.
  bool NumCallsReached(int num_calls) const { return num_calls_ >= num_calls; }

 private:
  void OnCall() { num_calls_++; }

  int num_calls_;

  DISALLOW_COPY_AND_ASSIGN(MockFileWatcherListener);
};

// ------------------------------------------------------------------------

// Check that we detect that files are added - this should result in
// two events, one for the file creation event and one for the
// change event that results in touch(1) updating the timestamp.
TEST(FileWatcher, TouchNonExisting) {
  FilePath testdir = SetupTestDir("filewatcher-touch-non-existing");

  FileWatcher* watcher = FileWatcher::Construct(testdir, ".p2p");

  {
    vector<FilePath> expected_files;
    EXPECT_EQ(watcher->files(), expected_files);
  }

  StrictMock<MockFileWatcherListener> listener(watcher);
  EXPECT_CALL(listener,
              OnChanged(testdir.Append("file.p2p"),
                        FileWatcher::EventType::kFileAdded));
  EXPECT_CALL(listener,
              OnChanged(testdir.Append("file.p2p"),
                        FileWatcher::EventType::kFileChanged));
  ExpectCommand(0, "touch %s", testdir.Append("file.p2p").value().c_str());

  // At this point, all the events should be generated, but the directory
  // watcher could be implemented using polling.
  RunGMainLoopUntil(kDefaultMainLoopTimeoutMs,
                    Bind(&MockFileWatcherListener::NumCallsReached,
                         Unretained(&listener),
                         2 /* num_calls */));

  {
    vector<FilePath> expected_files;
    expected_files.push_back(testdir.Append("file.p2p"));
    EXPECT_EQ(watcher->files(), expected_files);
  }

  delete watcher;
  TeardownTestDir(testdir);
}

// Check that we detect when a timestamp is updated on an existing
// file that we monitor - this should result in a single event.
TEST(FileWatcher, TouchExisting) {
  FilePath testdir = SetupTestDir("filewatcher-touch-existing");
  ExpectCommand(0, "touch %s", testdir.Append("existing.p2p").value().c_str());

  FileWatcher* watcher = FileWatcher::Construct(testdir, ".p2p");

  {
    vector<FilePath> expected_files;
    expected_files.push_back(testdir.Append("existing.p2p"));
    EXPECT_EQ(watcher->files(), expected_files);
  }

  StrictMock<MockFileWatcherListener> listener(watcher);
  EXPECT_CALL(listener,
              OnChanged(testdir.Append("existing.p2p"),
                        FileWatcher::EventType::kFileChanged));
  ExpectCommand(0, "touch %s", testdir.Append("existing.p2p").value().c_str());

  RunGMainLoopUntil(kDefaultMainLoopTimeoutMs,
                    Bind(&MockFileWatcherListener::NumCallsReached,
                         Unretained(&listener),
                         1 /* num_calls */));

  {
    vector<FilePath> expected_files;
    expected_files.push_back(testdir.Append("existing.p2p"));
    EXPECT_EQ(watcher->files(), expected_files);
  }

  delete watcher;
  TeardownTestDir(testdir);
}

// Check that we detect when a file has been written to.
TEST(FileWatcher, CreateFile) {
  FilePath testdir = SetupTestDir("filewatcher-create-file");

  FileWatcher* watcher = FileWatcher::Construct(testdir, ".p2p");

  {
    vector<FilePath> expected_files;
    EXPECT_EQ(watcher->files(), expected_files);
  }

  StrictMock<MockFileWatcherListener> listener(watcher);
  EXPECT_CALL(listener,
              OnChanged(testdir.Append("new-file.p2p"),
                        FileWatcher::EventType::kFileAdded));
  EXPECT_CALL(listener,
              OnChanged(testdir.Append("new-file.p2p"),
                        FileWatcher::EventType::kFileChanged));
  ExpectCommand(0,
                "dd if=/dev/zero of=%s bs=1000 count=1",
                testdir.Append("new-file.p2p").value().c_str());

  RunGMainLoopUntil(kDefaultMainLoopTimeoutMs,
                    Bind(&MockFileWatcherListener::NumCallsReached,
                         Unretained(&listener),
                         2 /* num_calls */));

  {
    vector<FilePath> expected_files;
    expected_files.push_back(testdir.Append("new-file.p2p"));
    EXPECT_EQ(watcher->files(), expected_files);
  }

  delete watcher;
  TeardownTestDir(testdir);
}

// Check that we detect when data is appended to a file.
TEST(FileWatcher, AppendToFile) {
  FilePath testdir = SetupTestDir("filewatcher-append-to-file");
  ExpectCommand(0, "touch %s", testdir.Append("existing.p2p").value().c_str());

  FileWatcher* watcher = FileWatcher::Construct(testdir, ".p2p");

  {
    vector<FilePath> expected_files;
    expected_files.push_back(testdir.Append("existing.p2p"));
    EXPECT_EQ(watcher->files(), expected_files);
  }

  StrictMock<MockFileWatcherListener> listener(watcher);
  EXPECT_CALL(listener,
              OnChanged(testdir.Append("existing.p2p"),
                        FileWatcher::EventType::kFileChanged));
  ExpectCommand(
      0, "echo -n xyz >> %s", testdir.Append("existing.p2p").value().c_str());

  RunGMainLoopUntil(kDefaultMainLoopTimeoutMs,
                    Bind(&MockFileWatcherListener::NumCallsReached,
                         Unretained(&listener),
                         1 /* num_calls */));

  {
    vector<FilePath> expected_files;
    expected_files.push_back(testdir.Append("existing.p2p"));
    EXPECT_EQ(watcher->files(), expected_files);
  }

  delete watcher;
  TeardownTestDir(testdir);
}

// Check that we detect when a file is removed - this should result
// in a single event.
TEST(FileWatcher, RemoveFile) {
  FilePath testdir = SetupTestDir("filewatcher-remove-file");
  ExpectCommand(0, "touch %s", testdir.Append("file.p2p").value().c_str());

  FileWatcher* watcher = FileWatcher::Construct(testdir, ".p2p");

  {
    vector<FilePath> expected_files;
    expected_files.push_back(testdir.Append("file.p2p"));
    EXPECT_EQ(watcher->files(), expected_files);
  }

  StrictMock<MockFileWatcherListener> listener(watcher);
  EXPECT_CALL(listener,
              OnChanged(testdir.Append("file.p2p"),
                        FileWatcher::EventType::kFileRemoved));
  ExpectCommand(0, "rm -f %s", testdir.Append("file.p2p").value().c_str());

  RunGMainLoopUntil(kDefaultMainLoopTimeoutMs,
                    Bind(&MockFileWatcherListener::NumCallsReached,
                         Unretained(&listener),
                         1 /* num_calls */));

  {
    vector<FilePath> expected_files;
    EXPECT_EQ(watcher->files(), expected_files);
  }

  delete watcher;
  TeardownTestDir(testdir);
}

// Check that we detect when a file is renamed into what we match - this
// should result in just a single event
TEST(FileWatcher, RenameInto) {
  FilePath testdir = SetupTestDir("filewatcher-rename-into");

  ExpectCommand(0, "touch %s", testdir.Append("bar.p2p.tmp").value().c_str());

  FileWatcher* watcher = FileWatcher::Construct(testdir, ".p2p");

  {
    vector<FilePath> expected_files;
    EXPECT_EQ(watcher->files(), expected_files);
  }

  StrictMock<MockFileWatcherListener> listener(watcher);
  EXPECT_CALL(listener,
              OnChanged(testdir.Append("bar.p2p"),
                        FileWatcher::EventType::kFileAdded));
  ExpectCommand(0,
                "dd if=/dev/zero of=%s bs=100 count=10",
                testdir.Append("bar.p2p.tmp").value().c_str());
  int rc = rename(testdir.Append("bar.p2p.tmp").value().c_str(),
                  testdir.Append("bar.p2p").value().c_str());
  EXPECT_EQ(rc, 0);

  RunGMainLoopUntil(kDefaultMainLoopTimeoutMs,
                    Bind(&MockFileWatcherListener::NumCallsReached,
                         Unretained(&listener),
                         1 /* num_calls */));

  {
    vector<FilePath> expected_files;
    expected_files.push_back(testdir.Append("bar.p2p"));
    EXPECT_EQ(watcher->files(), expected_files);
  }

  delete watcher;
  TeardownTestDir(testdir);
}

// Check that we get a Removed event when a file is renamed away
// from what we match
TEST(FileWatcher, RenameAway) {
  FilePath testdir = SetupTestDir("filewatcher-rename-away");

  ExpectCommand(0, "touch %s", testdir.Append("foo.p2p").value().c_str());

  FileWatcher* watcher = FileWatcher::Construct(testdir, ".p2p");

  {
    vector<FilePath> expected_files;
    expected_files.push_back(testdir.Append("foo.p2p"));
    EXPECT_EQ(watcher->files(), expected_files);
  }

  StrictMock<MockFileWatcherListener> listener(watcher);
  EXPECT_CALL(listener,
              OnChanged(testdir.Append("foo.p2p"),
                        FileWatcher::EventType::kFileRemoved));
  int rc = rename(testdir.Append("foo.p2p").value().c_str(),
                  testdir.Append("foo.p2p.tmp").value().c_str());
  EXPECT_EQ(rc, 0);
  RunGMainLoopUntil(kDefaultMainLoopTimeoutMs,
                    Bind(&MockFileWatcherListener::NumCallsReached,
                         Unretained(&listener),
                         1 /* num_calls */));

  {
    vector<FilePath> expected_files;
    EXPECT_EQ(watcher->files(), expected_files);
  }

  delete watcher;
  TeardownTestDir(testdir);
}

// Check that it monitoring works even when there are existing files.
TEST(FileWatcher, ExistingFiles) {
  FilePath testdir = SetupTestDir("filewatcher-existing-files");
  ExpectCommand(0, "touch %s", testdir.Append("1.p2p").value().c_str());
  ExpectCommand(0, "touch %s", testdir.Append("2.p2p").value().c_str());
  ExpectCommand(0, "touch %s", testdir.Append("3.p2p").value().c_str());

  FileWatcher* watcher = FileWatcher::Construct(testdir, ".p2p");

  {
    vector<FilePath> expected_files;
    expected_files.push_back(testdir.Append("1.p2p"));
    expected_files.push_back(testdir.Append("2.p2p"));
    expected_files.push_back(testdir.Append("3.p2p"));
    EXPECT_EQ(watcher->files(), expected_files);
  }

  StrictMock<MockFileWatcherListener> listener(watcher);
  EXPECT_CALL(listener,
              OnChanged(testdir.Append("4.p2p"),
                        FileWatcher::EventType::kFileAdded));
  EXPECT_CALL(listener,
              OnChanged(testdir.Append("4.p2p"),
                        FileWatcher::EventType::kFileChanged));
  ExpectCommand(0, "touch %s", testdir.Append("4.p2p").value().c_str());

  RunGMainLoopUntil(kDefaultMainLoopTimeoutMs,
                    Bind(&MockFileWatcherListener::NumCallsReached,
                         Unretained(&listener),
                         2 /* num_calls */));

  {
    vector<FilePath> expected_files;
    expected_files.push_back(testdir.Append("1.p2p"));
    expected_files.push_back(testdir.Append("2.p2p"));
    expected_files.push_back(testdir.Append("3.p2p"));
    expected_files.push_back(testdir.Append("4.p2p"));
    EXPECT_EQ(watcher->files(), expected_files);
  }

  delete watcher;
  TeardownTestDir(testdir);
}

// Check that activity on non-matching files does not cause any events.
TEST(FileWatcher, ActivityOnNonMatchingFiles) {
  FilePath testdir = SetupTestDir("filewatcher-activity-non-matching");

  FileWatcher* watcher = FileWatcher::Construct(testdir, ".p2p");
  StrictMock<MockFileWatcherListener> listener(watcher);
  ExpectCommand(0, "touch %s", testdir.Append("non-match.boo").value().c_str());

  // We use a second file to flag the test completion and ensure the event
  // from the non-match.boo file was processed and properly ignored.
  EXPECT_CALL(listener,
              OnChanged(testdir.Append("match.p2p"),
                        FileWatcher::EventType::kFileAdded));
  EXPECT_CALL(listener,
              OnChanged(testdir.Append("match.p2p"),
                        FileWatcher::EventType::kFileChanged));
  ExpectCommand(0, "touch %s", testdir.Append("match.p2p").value().c_str());

  RunGMainLoopUntil(kDefaultMainLoopTimeoutMs,
                    Bind(&MockFileWatcherListener::NumCallsReached,
                         Unretained(&listener),
                         2 /* num_calls */));
  delete watcher;
  TeardownTestDir(testdir);
}

// ------------------------------------------------------------------------

}  // namespace server

}  // namespace p2p
