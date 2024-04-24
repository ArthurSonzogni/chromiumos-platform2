// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package dlclib_test

import (
	"bytes"
	"os"
	"path"
	"testing"

	"go.chromium.org/chromiumos/dlc/dlclib"
)

func TestIsWritableTrue(t *testing.T) {
	p := path.Join(t.TempDir(), "writable_file")

	if err := os.WriteFile(p, []byte("foobar"), 0644); err != nil {
		t.Fatalf("Failed to write file: %v", err)
	}

	if !dlclib.IsWritable(p) {
		t.Fatal("File should be writeable")
	}
}

func TestIsWritableFalse(t *testing.T) {
	p := path.Join(t.TempDir(), "not_writeable_file")

	if err := os.WriteFile(p, []byte("foobar"), 0444); err != nil {
		t.Fatalf("Failed to write file: %v", err)
	}

	if dlclib.IsWritable(p) {
		t.Fatal("File should not be writeable")
	}
}

func TestPathExistsTrue(t *testing.T) {
	p := path.Join(t.TempDir(), "file")
	os.OpenFile(p, os.O_RDONLY|os.O_CREATE, 0644)

	if exists, err := dlclib.PathExists(p); err != nil {
		t.Fatalf("Failed path exists check: %v", err)
	} else if !exists {
		t.Fatal("File should exist")
	}
}

func TestPathExistsFalse(t *testing.T) {
	p := path.Join(t.TempDir(), "file")

	if exists, err := dlclib.PathExists(p); err != nil {
		t.Fatalf("Failed path exists check: %v", err)
	} else if exists {
		t.Fatal("File should not exist")
	}
}

func TestGetFileSizeInBlocks(t *testing.T) {
	p := path.Join(t.TempDir(), "file")
	os.OpenFile(p, os.O_RDONLY|os.O_CREATE, 0644)

	if err := os.Truncate(p, 10*4096); err != nil {
		t.Fatalf("Failed to truncate file: %v", err)
	}

	if blocks, err := dlclib.GetFileSizeInBlocks(p); err != nil {
		t.Fatalf("Failed to get file size in blocks: %v", err)
	} else if blocks != 10 {
		t.Fatalf("Unexpected block count=%v", blocks)
	}
}

func TestGetFileSizeInBlocksRoundUp(t *testing.T) {
	p := path.Join(t.TempDir(), "file")
	os.OpenFile(p, os.O_RDONLY|os.O_CREATE, 0644)

	if err := os.Truncate(p, 10*4096-1); err != nil {
		t.Fatalf("Failed to truncate file: %v", err)
	}

	if blocks, err := dlclib.GetFileSizeInBlocks(p); err != nil {
		t.Fatalf("Failed to get file size in blocks: %v", err)
	} else if blocks != 10 {
		t.Fatalf("Unexpected block count=%v", blocks)
	}
}

func TestAppendFile(t *testing.T) {
	p1 := path.Join(t.TempDir(), "file1")
	p2 := path.Join(t.TempDir(), "file2")

	data := []byte("foobar")
	if err := os.WriteFile(p1, data, 0444); err != nil {
		t.Fatalf("Failed to write file: %v", err)
	}

	if err := dlclib.AppendFile(p1, p2); err != nil {
		t.Fatalf("Failed to append file: %v", err)
	}

	if b, err := os.ReadFile(p2); err != nil {
		t.Fatalf("Failed to read file: %v", err)
	} else if !bytes.Equal(b, data) {
		t.Fatalf("Unexpected data: %v", b)
	}

	if err := dlclib.AppendFile(p1, p2); err != nil {
		t.Fatalf("Failed to append file: %v", err)
	}

	if b, err := os.ReadFile(p2); err != nil {
		t.Fatalf("Failed to read file: %v", err)
	} else if !bytes.Equal(b, append(append([]byte{}, data...), data...)) {
		t.Fatalf("Unexpected data: %v", b)
	}
}

func TestCopyFile(t *testing.T) {
	p1 := path.Join(t.TempDir(), "file1")
	p2 := path.Join(t.TempDir(), "file2")

	data := []byte("foobar")
	if err := os.WriteFile(p1, data, 0444); err != nil {
		t.Fatalf("Failed to write file: %v", err)
	}

	if err := dlclib.CopyFile(p1, p2); err != nil {
		t.Fatalf("Failed to append file: %v", err)
	}

	if b, err := os.ReadFile(p2); err != nil {
		t.Fatalf("Failed to read file: %v", err)
	} else if !bytes.Equal(b, data) {
		t.Fatalf("Unexpected data: %v", b)
	}

	// Truncate check.
	if err := dlclib.CopyFile(p1, p2); err != nil {
		t.Fatalf("Failed to append file: %v", err)
	}

	if b, err := os.ReadFile(p2); err != nil {
		t.Fatalf("Failed to read file: %v", err)
	} else if !bytes.Equal(b, data) {
		t.Fatalf("Unexpected data: %v", b)
	}
}

func TestSha256Sum(t *testing.T) {
	p := path.Join(t.TempDir(), "file")

	if err := os.WriteFile(p, []byte("foobar"), 0444); err != nil {
		t.Fatalf("Failed to write file: %v", err)
	}

	if hash, err := dlclib.Sha256Sum(p); err != nil {
		t.Fatalf("Failed to hash: %v", err)
	} else if hash != "c3ab8ff13720e8ad9047dd39466b3c8974e592c2fa383d4a3960714caef0c4f2" {
		t.Fatalf("Unexpected hash: %v", hash)
	}
}
