// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/garcon/icon_finder.h"

#include <algorithm>
#include <memory>
#include <vector>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/strings/stringprintf.h>
#include <base/strings/string_split.h>
#include "vm_tools/garcon/desktop_file.h"
#include "vm_tools/garcon/icon_index_file.h"

namespace vm_tools {
namespace garcon {
namespace {

constexpr char kXdgDataDirsEnvVar[] = "XDG_DATA_DIRS";
constexpr char kXdgDataDirsDefault[] = "/usr/share/";
constexpr char kDefaultPixmapsDir[] = "/usr/share/pixmaps/";
const char* const kThemeDirs[] = {"gnome", "hicolor"};

// Returns a vector of directory paths under which an index.theme file is
// located.
std::vector<base::FilePath> GetPathsForIconIndexDirs() {
  std::vector<base::FilePath> retval;
  const char* xdg_data_dirs = getenv(kXdgDataDirsEnvVar);
  if (!xdg_data_dirs || strlen(xdg_data_dirs) == 0) {
    xdg_data_dirs = kXdgDataDirsDefault;
  }
  std::vector<base::StringPiece> dirs = base::SplitStringPiece(
      xdg_data_dirs, ":", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
  for (const char* theme_dir : kThemeDirs) {
    std::transform(
        dirs.begin(), dirs.end(), std::back_inserter(retval),
        [&theme_dir](const base::StringPiece& dir) {
          return base::FilePath(dir).Append("icons").Append(theme_dir);
        });
  }
  return retval;
}

}  // namespace

std::vector<base::FilePath> GetPathsForIcons(const base::FilePath& icon_dir,
                                             int icon_size,
                                             int scale) {
  std::unique_ptr<IconIndexFile> icon_index_file =
      IconIndexFile::ParseIconIndexFile(icon_dir);
  if (icon_index_file) {
    return icon_index_file->GetPathsForSizeAndScale(icon_size, scale);
  } else {
    return {};
  }
}

base::FilePath LocateIconFile(const std::string& desktop_file_id,
                              int icon_size,
                              int scale) {
  base::FilePath desktop_file_path =
      DesktopFile::FindFileForDesktopId(desktop_file_id);
  if (desktop_file_path.empty()) {
    LOG(ERROR) << "Failed to find desktop file for " << desktop_file_id;
    return base::FilePath();
  }
  std::unique_ptr<DesktopFile> desktop_file =
      DesktopFile::ParseDesktopFile(desktop_file_path);
  if (!desktop_file) {
    LOG(ERROR) << "Failed to parse desktop file " << desktop_file_path.value();
    return base::FilePath();
  }
  if (desktop_file->icon().empty()) {
    return base::FilePath();
  }
  const base::FilePath desktop_file_icon_filepath(desktop_file->icon());
  if (desktop_file_icon_filepath.IsAbsolute()) {
    if (desktop_file_icon_filepath.Extension() == ".png") {
      return desktop_file_icon_filepath;
    } else {
      LOG(INFO) << desktop_file_id << " icon file is not a png file";
      return base::FilePath();
    }
  }
  std::string icon_filename =
      desktop_file_icon_filepath.AddExtension("png").value();
  for (const base::FilePath& icon_dir : GetPathsForIconIndexDirs()) {
    for (const base::FilePath& curr_path :
         GetPathsForIcons(icon_dir, icon_size, scale)) {
      base::FilePath test_path = curr_path.Append(icon_filename);
      if (base::PathExists(test_path))
        return test_path;
    }
  }

  // Also check the default pixmaps dir as a last resort.
  base::FilePath test_path =
      base::FilePath(kDefaultPixmapsDir).Append(icon_filename);
  if (base::PathExists(test_path))
    return test_path;

  LOG(INFO) << "No icon file found for " << desktop_file_id;
  return base::FilePath();
}

}  // namespace garcon
}  // namespace vm_tools
