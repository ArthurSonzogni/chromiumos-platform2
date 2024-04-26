// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package dlclib_test

import (
	"os"
	"path"
	"testing"

	"go.chromium.org/chromiumos/dlc/dlclib"
)

func TestSquashfs(t *testing.T) {
	tests := []struct {
		Name string
		Func func(string, string) error
	}{
		{
			Name: "CreateSquashfs",
			Func: dlclib.Filesystem.CreateSquashfs,
		},
		{
			Name: "CreateSquashfsNoCompression",
			Func: dlclib.Filesystem.CreateSquashfsNoCompression,
		},
	}
	for _, test := range tests {
		t.Run(test.Name, func(t *testing.T) {
			src := path.Join(t.TempDir(), "src")
			dst := path.Join(t.TempDir(), "dst")
			ext := path.Join(t.TempDir(), "ext")

			for _, p := range []string{src, dst, ext} {
				if err := os.Mkdir(p, 0755); err != nil {
					t.Fatalf("Failed to create directory (%s): %v", p, err)
				}
			}

			if err := os.WriteFile(path.Join(src, "file"), []byte("foobar"), 0444); err != nil {
				t.Fatalf("Failed to write file: %v", err)
			}

			dstImg := path.Join(dst, "squashfs.img")
			if err := test.Func(src, dstImg); err != nil {
				t.Fatalf("Failed to create squashfs: %v", err)
			}

			if exists, err := dlclib.PathExists(dstImg); err != nil {
				t.Fatalf("Failed path exists check: %v", err)
			} else if !exists {
				t.Fatal("File should exist")
			}

			if err := dlclib.Filesystem.ExtractSquashfs(dstImg, ext); err != nil {
				t.Fatalf("Failed to extract the squashfs: %v", err)
			}
		})
	}
}
