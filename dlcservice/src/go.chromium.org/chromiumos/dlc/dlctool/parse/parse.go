// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package parse

import (
	"errors"
	"flag"
	"fmt"
	"os"
	"path"
)

var (
	// FlagID to hold parsed ID.
	FlagID *string

	// FlagUnpack to hold parsed unpack option.
	FlagUnpack *bool

	// FlagCompress to hold parsed compress option.
	FlagCompress *bool
)

// Defined, but hidden.
var (
	flagNoCompress *bool
)

// Args to parse the system arguments.
func Args(prog string, sysArgs []string) (string, error) {
	fs := flag.NewFlagSet("", flag.ContinueOnError)

	FlagID = fs.String("id", "", "ID of the DLC to [un]pack.")
	FlagUnpack = fs.Bool("unpack", false, "To unpack the DLC passed.")
	FlagCompress = fs.Bool("compress", true, "Compress the image. Slower to pack but creates smaller images.")

	flagNoCompress = fs.Bool("nocompress", false, "Don't compress the image.")

	// Usage update only after flag definitions.
	fs.Usage = func() {
		usage := `Usage of %[1]s:
  [Unpacking a DLC]
  %[1]s --unpack --id=<id> <path>
  <path> to which the DLC image will be unpacked to.

  [Packaging a DLC]
  %[1]s --id=<id> <path>
  <path> from which to create the DLC image and manifest.

`
		fmt.Fprintf(os.Stderr, usage, prog)
		fs.PrintDefaults()
	}

	// Parse only after flag definitions.
	if err := fs.Parse(sysArgs); err != nil {
		return "", fmt.Errorf("parse.Args: failed to parse: %w", err)
	}

	// Special treatment for select flags.
	var err error
	fs.Visit(func(f *flag.Flag) {
		if f.Name == "compress" {
			*FlagCompress = f.Value.(flag.Getter).Get().(bool)
		}
		// Lexicographical visiting, so "nocompress" has higher priority.
		if f.Name == "nocompress" {
			if f.Value.(flag.Getter).Get().(bool) == false {
				err = errors.New("parse.Args: please don't explicity pass in `nocompress` with `false`")
			}
			*FlagCompress = false
		}
	})
	if err != nil {
		return "", err
	}

	if *FlagID == "" {
		return "", errors.New("parse.Args: cannot pass empty ID")
	}

	args := fs.Args()
	if len(args) != 1 {
		return "", errors.New("parse.Args: please only pass in <path>")
	}

	return path.Clean(args[0]), nil
}
