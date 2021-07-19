// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <map>
#include <string>
#include <vector>

#include <base/base64.h>
#include <base/check.h>
#include <base/files/scoped_temp_dir.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <gtest/gtest.h>

#include "vm_tools/garcon/mime_types_parser.h"

namespace vm_tools {
namespace garcon {

namespace {

// Test mime.cache files are generated using a process such as:
// mkdir -p /tmp/mimetest/packages
// cat <<EOF >> /tmp/mimetest/packages/application-x-foobar.xml
// <?xml version="1.0" encoding="UTF-8"?>
// <mime-info xmlns="http://www.freedesktop.org/standards/shared-mime-info">
//   <mime-type type="x/no-dot"><glob pattern="~"/></mime-type>
//   <mime-type type="application/pdf"><glob pattern="*.pdf"/></mime-type>
//   <mime-type type="text/plain"><glob pattern="*.txt"/></mime-type>
//   <mime-type type="text/plain"><glob pattern="*.doc"/></mime-type>
//   <mime-type type="text/plain"><glob pattern="*.foo"/></mime-type>
//   <mime-type type="x/smile"><glob pattern="*.🙂🤩"/></mime-type>
// </mime-info>
//  EOF
//  update-mime-database /tmp/mimetest
//  base64 -270 /tmp/mimetest/mime.cache
//  See https://wiki.archlinux.org/title/XDG_MIME_Applications

static constexpr const char kTestMimeCacheB64[] =
    "AAEAAgAAAGAAAABkAAAAaAAAAHgAAAGgAAABpAAAAbAAAAG0AAABuAAAAbx0ZXh0L3BsYW"
    "luAAB+AAAAeC9uby1kb3QAAAAAYXBwbGljYXRpb24vcGRmAHgvc21pbGUAAAAAAAAAAAAA"
    "AAABAAAAOAAAADwAAAAyAAAABQAAAIAAAABjAAAAAQAAALwAAABmAAAAAQAAAMgAAABvAA"
    "AAAQAAANQAAAB0AAAAAQAAAOAAAfkpAAAAAQAAAOwAAABvAAAAAQAAAPgAAABkAAAAAQAA"
    "AQQAAABvAAAAAQAAARAAAAB4AAAAAQAAARwAAfZCAAAAAQAAASgAAABkAAAAAQAAATQAAA"
    "BwAAAAAQAAAUAAAABmAAAAAQAAAUwAAAB0AAAAAQAAAVgAAAAuAAAAAQAAAWQAAAAuAAAA"
    "AQAAAXAAAAAuAAAAAQAAAXwAAAAuAAAAAQAAAYgAAAAuAAAAAQAAAZQAAAAAAAAAWAAAAD"
    "IAAAAAAAAALAAAADIAAAAAAAAASAAAADIAAAAAAAAALAAAADIAAAAAAAAALAAAADIAAAAA"
    "AAAAAAAAAAAAAAGwAAAAAAAAAAAAAAAAAAAABAAAAAAAAAAAAAAAAAAAAAA=";

class MimeTypesParserTest : public ::testing::Test {
 public:
  MimeTypesParserTest() {
    CHECK(temp_dir_.CreateUniqueTempDir());
    mime_types_path_ = temp_dir_.GetPath().Append("mime.types");
  }
  MimeTypesParserTest(const MimeTypesParserTest&) = delete;
  MimeTypesParserTest& operator=(const MimeTypesParserTest&) = delete;

  ~MimeTypesParserTest() override = default;

  void WriteContents(const std::string& file_contents) {
    EXPECT_EQ(file_contents.size(),
              base::WriteFile(mime_types_path_, file_contents.c_str(),
                              file_contents.size()));
  }

  // Ensures that parsing fails when mime.cache file is modified such that
  // |buf[pos] = c|.
  void InvalidIf(std::string buf, int pos, char c) {
    std::string s(buf);
    s[pos] = c;
    WriteContents(s);
    MimeTypeMap map;
    EXPECT_FALSE(ParseMimeTypes(TempFilePath(), &map));
  }

  std::string TempFilePath() const { return mime_types_path_.value(); }

 private:
  base::ScopedTempDir temp_dir_;
  base::FilePath mime_types_path_;
};

}  // namespace

TEST_F(MimeTypesParserTest, NonExistentFileFails) {
  MimeTypeMap map;
  EXPECT_FALSE(ParseMimeTypes("/invalid/filepath/foo", &map));
}

TEST_F(MimeTypesParserTest, ValidResult) {
  MimeTypeMap map;
  std::string buf;
  base::Base64Decode(kTestMimeCacheB64, &buf);
  WriteContents(buf);
  EXPECT_TRUE(ParseMimeTypes(TempFilePath(), &map));
  MimeTypeMap expected = {
      {"pdf", "application/pdf"}, {"txt", "text/plain"},
      {"doc", "text/plain"},      {"foo", "text/plain"},
      {"🙂🤩", "x/smile"},
  };
  EXPECT_EQ(map, expected);
}

//  xxd /tmp/mimetest/mime.cache
// 00000000: 0001 0002 0000 0060 0000 0064 0000 0068  .......`...d...h
// 00000010: 0000 0078 0000 01a0 0000 01a4 0000 01b0  ...x............
// 00000020: 0000 01b4 0000 01b8 0000 01bc 7465 7874  ............text
// 00000030: 2f70 6c61 696e 0000 7e00 0000 782f 6e6f  /plain..~...x/no
// 00000040: 2d64 6f74 0000 0000 6170 706c 6963 6174  -dot....applicat
// 00000050: 696f 6e2f 7064 6600 782f 736d 696c 6500  ion/pdf.x/smile.
// 00000060: 0000 0000 0000 0000 0000 0001 0000 0038  ...............8
// 00000070: 0000 003c 0000 0032 0000 0005 0000 0080  ...<...2........
// 00000080: 0000 0063 0000 0001 0000 00bc 0000 0066  ...c...........f
// 00000090: 0000 0001 0000 00c8 0000 006f 0000 0001  ...........o....
// 000000a0: 0000 00d4 0000 0074 0000 0001 0000 00e0  .......t........
// 000000b0: 0001 f929 0000 0001 0000 00ec 0000 006f  ...)...........o
// 000000c0: 0000 0001 0000 00f8 0000 0064 0000 0001  ...........d....
// 000000d0: 0000 0104 0000 006f 0000 0001 0000 0110  .......o........
// 000000e0: 0000 0078 0000 0001 0000 011c 0001 f642  ...x...........B
// 000000f0: 0000 0001 0000 0128 0000 0064 0000 0001  .......(...d....
// 00000100: 0000 0134 0000 0070 0000 0001 0000 0140  ...4...p.......@
// 00000110: 0000 0066 0000 0001 0000 014c 0000 0074  ...f.......L...t
// 00000120: 0000 0001 0000 0158 0000 002e 0000 0001  .......X........
// 00000130: 0000 0164 0000 002e 0000 0001 0000 0170  ...d...........p
// 00000140: 0000 002e 0000 0001 0000 017c 0000 002e  ...........|....
// 00000150: 0000 0001 0000 0188 0000 002e 0000 0001  ................
// 00000160: 0000 0194 0000 0000 0000 0058 0000 0032  ...........X...2
// 00000170: 0000 0000 0000 002c 0000 0032 0000 0000  .......,...2....
// 00000180: 0000 0048 0000 0032 0000 0000 0000 002c  ...H...2.......,
// 00000190: 0000 0032 0000 0000 0000 002c 0000 0032  ...2.......,...2
// 000001a0: 0000 0000 0000 0000 0000 0000 0000 01b0  ................
// 000001b0: 0000 0000 0000 0000 0000 0000 0000 0004  ................
// 000001c0: 0000 0000 0000 0000 0000 0000 0000 0000  ................
TEST_F(MimeTypesParserTest, Invalid) {
  std::string buf;
  base::Base64Decode(kTestMimeCacheB64, &buf);
  // ALIAS_LIST_OFFSET is uint32 at byte 4 = 0x60.
  // Alias list offset inside header.
  InvalidIf(buf, 7, 10);
  // Alias list offset larger than file size.
  InvalidIf(buf, 6, 0xff);
  // Not null beore alias list.
  InvalidIf(buf, 0x60 - 1, 'X');
  // N_ROOTS > kMaxUnicode (0x10ffff).
  InvalidIf(buf, 0x79, 0x20);
  InvalidIf(buf, 0xc1, 0x20);
  // Node C > kMaxUnicode (0x10ffff).
  InvalidIf(buf, 0x81, 0x20);
  // Node N_CHILDREN > kMaxUnicode (0x10ffff).
  InvalidIf(buf, 0x85, 0x20);
  // Node FIRST_CHILD_OFFSET below tree offset.
  InvalidIf(buf, 0x8b, 0x10);
  InvalidIf(buf, 0xc7, 0x20);
  // Node FIRST_CHILD_OFFSET beyond file size.
  InvalidIf(buf, 0x8a, 0x20);
  InvalidIf(buf, 0xc6, 0x20);
  // Mime type offset below header.
  InvalidIf(buf, 0x177, 0x10);
  // Mime type offset above alias list.
  InvalidIf(buf, 0x177, 0x60);
}

}  // namespace garcon
}  // namespace vm_tools
