# Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Pull in chromium os defaults
# See the header of common.mk for details on its origin
include common.mk

PKG_CONFIG ?= pkg-config

BASE_VER ?= 180609
PC_DEPS = dbus-1 dbus-glib-1 glib-2.0 nss \
	libchrome-$(BASE_VER) libchromeos-$(BASE_VER)
PC_CFLAGS := $(shell $(PKG_CONFIG) --cflags $(PC_DEPS))
PC_LIBS := $(shell $(PKG_CONFIG) --libs $(PC_DEPS))

CPPFLAGS += -I$(SRC)/.. -I$(OUT) -DOS_CHROMEOS $(PC_CFLAGS)
LDLIBS += -lbootstat -lchrome_crypto $(PC_LIBS) -lprotobuf-lite -lmetrics

# Special logic for generating dbus service bindings.
DBUS_SOURCE = $(SRC)/session_manager.xml
DBUS_SERVER = server.h
$(DBUS_SERVER): $(DBUS_SOURCE)
	dbus-binding-tool --mode=glib-server \
	 --prefix=`basename $(DBUS_SOURCE) .xml` $(DBUS_SOURCE) > $(DBUS_SERVER)
clean: CLEAN($(DBUS_SERVER))

# Special logic for generating protobuffer bindings.
PROTO_PATH = $(SYSROOT)/usr/include/proto
PROTO_BINDINGS = chrome_device_policy.pb.cc device_management_backend.pb.cc
PROTO_HEADERS = $(patsubst %.cc,%.h,$(PROTO_BINDINGS))
PROTO_OBJS = chrome_device_policy.pb.o device_management_backend.pb.o
$(PROTO_HEADERS): %.h: %.cc ;
$(PROTO_BINDINGS): %.pb.cc: $(PROTO_PATH)/%.proto
	protoc --proto_path=$(PROTO_PATH) --cpp_out=$(OUT) $<
clean: CLEAN($(PROTO_BINDINGS))
clean: CLEAN($(PROTO_HEADERS))
clean: CLEAN($(PROTO_OBJS))
# Add rules for compiling generated protobuffer code, as the CXX_OBJECTS list
# is built before these source files exists and, as such, does not contain them.
$(eval $(call add_object_rules,$(PROTO_OBJS),CXX,cc))

# The binaries we build and the objects they depend on
KEYGEN_BIN = keygen
KEYGEN_OBJS = keygen.o keygen_worker.o nss_util.o policy_key.o \
	scoped_dbus_pending_call.o system_utils.o
SESSION_BIN = session_manager
SESSION_OBJS = $(PROTO_OBJS) \
  $(filter-out keygen%.o mock_%.o %_testrunner.o %_unittest.o,$(CXX_OBJECTS))
TEST_BIN = session_manager_unittest
TEST_OBJS = $(PROTO_OBJS) $(filter-out %main.o keygen.o,$(CXX_OBJECTS))

# Require proto and dbus bindings to be generated first.
$(patsubst %.o,%.o.depends,$(filter-out keygen.o,$(CXX_OBJECTS))): \
	$(PROTO_HEADERS)
# Ensure dbus service bindings are generated when we need them.
session_manager_service.o.depends: $(DBUS_SERVER)

CXX_BINARY($(KEYGEN_BIN)): $(KEYGEN_OBJS)
clean: CLEAN($(KEYGEN_BIN))

CXX_BINARY($(SESSION_BIN)): $(SESSION_OBJS)
clean: CLEAN($(SESSION_BIN))

CXX_BINARY($(TEST_BIN)): $(TEST_OBJS) | CXX_BINARY($(KEYGEN_BIN))
CXX_BINARY($(TEST_BIN)): CPPFLAGS += -DUNIT_TEST
CXX_BINARY($(TEST_BIN)): LDLIBS += -lgtest -lgmock
clean: CLEAN(CXX_BINARY($(TEST_BIN)))
tests: TEST(CXX_BINARY($(TEST_BIN)))

CC_BINARY(cros-xauth): cros-xauth.o
CC_BINARY(cros-xauth): LDLIBS =
clean: CC_BINARY(cros-xauth)

all: login_manager CC_BINARY(cros-xauth)
login_manager: \
  CXX_BINARY($(KEYGEN_BIN)) CXX_BINARY($(SESSION_BIN))
