// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// The dlctool executable allows for modification of DLCs directly on the device.
package main

import (
	"flag"
	"log"
	"os"
	"os/exec"
)

const dlctoolShellPath = "/usr/local/bin/dlctool-shell"

var (
	flagID     *string = flag.String("id", "", "ID of the DLC to [un]pack.")
	flagUnpack *bool   = flag.Bool("unpack", false, "To unpack the DLC passed.")
	flagShell  *bool   = flag.Bool("shell", true, "Force shell usage.")
)

// Defined, but ignored.
var (
	flagCompress   *bool = flag.Bool("compress", true, "Compress the image. Slower to pack but creates smaller images.")
	flagNoCompress *bool = flag.Bool("nocompress", false, "Don't compress the image.")
)

func initFlags() {
	// Parse only after flag definitions.
	flag.Parse()

	// Special treatment for select flags.
	flag.Visit(func(f *flag.Flag) {
		if f.Name == "shell" && f.Value.(flag.Getter).Get().(bool) == true {
			log.Fatal("Please don't explicitly pass in `shell` flag with `true`.")
		}
	})
	if *flagID == "" {
		log.Fatal("Cannot pass empty ID.")
	}
}

func dlctoolShell(args []string) {
	cmd := &exec.Cmd{
		Path:   dlctoolShellPath,
		Args:   append([]string{dlctoolShellPath}, args...),
		Stdout: os.Stdout,
		Stderr: os.Stderr,
		Env:    os.Environ(),
	}
	if err := cmd.Run(); err != nil {
		log.Fatalf("%v failed: %v", os.Args[0], err)
	}
}

func main() {
	initFlags()

	if *flagShell {
		dlctoolShell(os.Args[1:])
		return
	}

	log.Fatal("Please use shell variant.")
}
