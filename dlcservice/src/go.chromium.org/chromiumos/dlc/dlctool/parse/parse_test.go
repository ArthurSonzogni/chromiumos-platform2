// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package parse_test

import (
	"strings"
	"testing"

	"go.chromium.org/chromiumos/dlc/dlctool/parse"
)

func TestStrictDefaultValues(t *testing.T) {
	_, err := parse.Args(
		"dlctool",
		[]string{"--id", "sample-dlc", "/path"},
	)
	if err != nil {
		t.Fatalf("Unexpected error: %v", err)
	}
	if *parse.FlagUnpack {
		t.Fatal("Flag unpack is not default false")
	}
}

func TestBadFlag(t *testing.T) {
	_, err := parse.Args(
		"dlctool",
		[]string{"--unsupported-flag"},
	)
	if err == nil {
		t.Fatal("Parsing bad flag did not fail")
	}
	if !strings.Contains(err.Error(), "parse.Args: failed to parse:") {
		t.Fatalf("Unexpected error: %v", err)
	}
}

func TestEmptyIdFlag(t *testing.T) {
	_, err := parse.Args(
		"dlctool",
		[]string{"--id", ""},
	)
	if err == nil {
		t.Fatal("Parsing empty ID flag did not fail")
	}
	if !strings.Contains(err.Error(), "parse.Args: cannot pass empty ID") {
		t.Fatalf("Unexpected error: %v", err)
	}
}

func TestMissingPath(t *testing.T) {
	_, err := parse.Args(
		"dlctool",
		[]string{"--id", "sample-dlc"},
	)
	if err == nil {
		t.Fatal("Parsing missing path did not fail")
	}
	if !strings.Contains(err.Error(), "parse.Args: please only pass in <path>") {
		t.Fatalf("Unexpected error: %v", err)
	}
}

func TestNoCompressOption(t *testing.T) {
	_, err := parse.Args(
		"dlctool",
		[]string{"--nocompress", "--id", "sample-dlc", "/path"},
	)
	if err != nil {
		t.Fatalf("Parsing no compress option failed: %v", err)
	}
	if *parse.FlagCompress != false {
		t.Errorf("Unexpected compress value")
	}
}

func TestDoubleNoCompressOption(t *testing.T) {
	_, err := parse.Args(
		"dlctool",
		[]string{"--nocompress=false", "--id", "sample-dlc", "/path"},
	)
	if err == nil {
		t.Error("Parsing double no compress option did not fail")
	}
	if !strings.Contains(err.Error(), "parse.Args: please don't explicity pass in `nocompress` with `false`") {
		t.Errorf("Unexpected error: %v", err)
	}
}

func TestCompressOption(t *testing.T) {
	_, err := parse.Args(
		"dlctool",
		[]string{"--compress", "--id", "sample-dlc", "/path"},
	)
	if err != nil {
		t.Fatalf("Parsing compress option failed: %v", err)
	}
	if *parse.FlagCompress != true {
		t.Errorf("Unexpected compress value")
	}
}

func TestPathCleanCheck(t *testing.T) {
	p, err := parse.Args(
		"dlctool",
		[]string{"--id", "sample-dlc", "/path/foo/../bar"},
	)
	if err != nil {
		t.Fatalf("Parsing failed: %v", err)
	}
	if p != "/path/bar" {
		t.Fatalf("Unexpected path parsed: %s", p)
	}
}

func TestInvalidFsType(t *testing.T) {
	_, err := parse.Args(
		"dlctool",
		[]string{"--id", "sample-dlc", "--fs-type", "foobarfs", "/path"},
	)
	if err == nil {
		t.Fatalf("Parsing invalid fs-type did not fail")
	}
	if !strings.Contains(err.Error(), "parse.Args: invalid fs-type given:") {
		t.Fatalf("Unexpected error: %v", err)
	}
}

func TestValidFsType(t *testing.T) {
	fsTypes := []string{"squashfs", "ext2", "ext4"}
	for _, fsType := range fsTypes {
		t.Run(fsType, func(t *testing.T) {
			_, err := parse.Args(
				"dlctool",
				[]string{"--id", "sample-dlc", "--fs-type", fsType, "/path"},
			)
			if err != nil {
				t.Fatalf("Unexpected error: %v", err)
			}
		})
	}
}
