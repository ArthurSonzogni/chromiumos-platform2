// Copyright 2026 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"context"
	"flag"
	"log"
	"net"
	"time"

	"github.com/mdlayher/vsock"
	"google.golang.org/grpc"
	"google.golang.org/grpc/credentials/insecure"

	pb "vm_install_reporter/vm_rpc"
)

var (
	status        = flag.String("status", "", "Status: IN_PROGRESS, SUCCEEDED, FAILED")
	step          = flag.String("step", "", "Step: install_fetch_image, install_configure, etc.")
	percent       = flag.Int64("percent", 0, "Progress percent")
	failureReason = flag.String("failure_reason", "", "Failure reason")
	port          = flag.Int("port", 7777, "Startup listener port")
	cid           = flag.Int("cid", 2, "Host CID")
)

func main() {
	flag.Parse()

	dialer := func(ctx context.Context, addr string) (net.Conn, error) {
		return vsock.Dial(uint32(*cid), uint32(*port), nil)
	}

	conn, err := grpc.Dial("vsock",
		grpc.WithContextDialer(dialer),
		grpc.WithTransportCredentials(insecure.NewCredentials()))
	if err != nil {
		log.Fatalf("did not connect: %v", err)
	}
	defer conn.Close()

	c := pb.NewStartupListenerClient(conn)

	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()

	s, ok := pb.VmInstallState_State_value[*status]
	if !ok {
		s = int32(pb.VmInstallState_UNKNOWN)
	}

	st, ok := pb.VmInstallState_Step_value[*step]
	if !ok {
		st = int32(pb.VmInstallState_unknown)
	}

	req := &pb.VmInstallState{
		State:             pb.VmInstallState_State(s),
		InProgressStep:    pb.VmInstallState_Step(st),
		InProgressPercent: *percent,
		FailedReason:      *failureReason,
	}

	_, err = c.VmInstallStatus(ctx, req)
	if err != nil {
		log.Fatalf("could not report status: %v", err)
	}
}
