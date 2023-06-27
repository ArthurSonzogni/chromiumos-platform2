// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package proxy

import (
	"bytes"
	"testing"

	"github.com/google/go-cmp/cmp"

	"go.chromium.org/chromiumos/dbusbindings/introspect"
	"go.chromium.org/chromiumos/dbusbindings/serviceconfig"
)

func TestGenerateMockProxies(t *testing.T) {
	itf := introspect.Interface{
		Name: "fi.w1.wpa_supplicant1.Interface",
		Methods: []introspect.Method{
			{
				Name: "Scan",
				Args: []introspect.MethodArg{
					{
						Name: "args",
						Type: "ah",
					},
				},
			}, {
				Name: "PassMeProtos",
				Args: []introspect.MethodArg{
					{
						Name:      "request",
						Type:      "ay",
						Direction: "in",
						Annotation: introspect.Annotation{
							Name:  "org.chromium.DBus.Argument.ProtobufClass",
							Value: "PassMeProtosRequest",
						},
					},
				},
				Annotations: []introspect.Annotation{
					{
						Name:  "org.chromium.DBus.Method.Kind",
						Value: "async",
					},
				},
				DocString: "\n        method doc\n      ",
			},
		},
		Signals: []introspect.Signal{
			{
				Name: "BSSRemoved",
				Args: []introspect.SignalArg{
					{
						Name: "BSSDetail1",
						Type: "ay",
						Annotation: introspect.Annotation{
							Name:  "org.chromium.DBus.Argument.ProtobufClass",
							Value: "YetAnotherProto",
						},
					}, {
						Name: "BSSDetail2",
						Type: "(ih)",
					},
				},
				DocString: "\n        signal doc\n      ",
			},
		},
		Properties: []introspect.Property{
			{
				Name:      "Capabilities",
				Type:      "a{sv}",
				Access:    "read",
				DocString: "\n        property doc\n      ",
			}, {
				Name:      "Class",
				Type:      "u",
				Access:    "read",
				DocString: "\n        property doc\n      ",
				Annotation: introspect.Annotation{
					Name:  "org.chromium.DBus.Argument.VariableName",
					Value: "bluetooth_class",
				},
			},
		},
		DocString: "\n      interface doc\n    ",
	}

	emptyItf := introspect.Interface{
		Name: "EmptyInterface",
	}

	introspections := []introspect.Introspection{
		{
			Name:       "/org/chromium/Test",
			Interfaces: []introspect.Interface{itf},
		}, {
			Interfaces: []introspect.Interface{emptyItf},
		},
	}

	sc := serviceconfig.Config{
		ObjectManager: &serviceconfig.ObjectManagerConfig{
			Name: "foo.bar.ObjectManager",
		},
	}
	out := new(bytes.Buffer)
	if err := GenerateMock(introspections, out, "/tmp/mock.h", "", sc); err != nil {
		t.Fatalf("Generate got error, want nil: %v", err)
	}

	const want = `// Automatic generation of D-Bus interface mock proxies for:
//  - fi.w1.wpa_supplicant1.Interface
//  - EmptyInterface
#ifndef ____CHROMEOS_DBUS_BINDING___TMP_MOCK_H
#define ____CHROMEOS_DBUS_BINDING___TMP_MOCK_H
#include <string>
#include <vector>

#include <base/functional/callback_forward.h>
#include <base/logging.h>
#include <brillo/any.h>
#include <brillo/errors/error.h>
#include <brillo/variant_dictionary.h>
#include <gmock/gmock.h>

namespace fi {
namespace w1 {
namespace wpa_supplicant1 {

// Abstract interface proxy for fi::w1::wpa_supplicant1::Interface.
// interface doc
class InterfaceProxyInterface {
 public:
  virtual ~InterfaceProxyInterface() = default;

  virtual bool Scan(
      const std::vector<base::ScopedFD>& in_args,
      brillo::ErrorPtr* error,
      int timeout_ms = dbus::ObjectProxy::TIMEOUT_USE_DEFAULT) = 0;

  virtual void ScanAsync(
      const std::vector<base::ScopedFD>& in_args,
      base::OnceCallback<void()> success_callback,
      base::OnceCallback<void(brillo::Error*)> error_callback,
      int timeout_ms = dbus::ObjectProxy::TIMEOUT_USE_DEFAULT) = 0;

  // method doc
  virtual bool PassMeProtos(
      const PassMeProtosRequest& in_request,
      brillo::ErrorPtr* error,
      int timeout_ms = dbus::ObjectProxy::TIMEOUT_USE_DEFAULT) = 0;

  // method doc
  virtual void PassMeProtosAsync(
      const PassMeProtosRequest& in_request,
      base::OnceCallback<void()> success_callback,
      base::OnceCallback<void(brillo::Error*)> error_callback,
      int timeout_ms = dbus::ObjectProxy::TIMEOUT_USE_DEFAULT) = 0;

  virtual void RegisterBSSRemovedSignalHandler(
      const base::RepeatingCallback<void(const YetAnotherProto&,
                                         const std::tuple<int32_t, base::ScopedFD>&)>& signal_callback,
      dbus::ObjectProxy::OnConnectedCallback on_connected_callback) = 0;

  static const char* CapabilitiesName() { return "Capabilities"; }
  virtual const brillo::VariantDictionary& capabilities() const = 0;
  virtual bool is_capabilities_valid() const = 0;
  static const char* ClassName() { return "Class"; }
  virtual uint32_t bluetooth_class() const = 0;
  virtual bool is_bluetooth_class_valid() const = 0;

  virtual const dbus::ObjectPath& GetObjectPath() const = 0;
  virtual dbus::ObjectProxy* GetObjectProxy() const = 0;

  virtual void SetPropertyChangedCallback(
      const base::RepeatingCallback<void(InterfaceProxyInterface*, const std::string&)>& callback) = 0;
};

}  // namespace wpa_supplicant1
}  // namespace w1
}  // namespace fi

namespace fi {
namespace w1 {
namespace wpa_supplicant1 {

// Mock object for InterfaceProxyInterface.
class InterfaceProxyMock : public InterfaceProxyInterface {
 public:
  InterfaceProxyMock() = default;
  InterfaceProxyMock(const InterfaceProxyMock&) = delete;
  InterfaceProxyMock& operator=(const InterfaceProxyMock&) = delete;

  MOCK_METHOD(bool,
              Scan,
              (const std::vector<base::ScopedFD>& /*in_args*/,
               brillo::ErrorPtr* /*error*/,
               int /*timeout_ms*/),
              (override));
  MOCK_METHOD(void,
              ScanAsync,
              (const std::vector<base::ScopedFD>& /*in_args*/,
               base::OnceCallback<void()> /*success_callback*/,
               base::OnceCallback<void(brillo::Error*)> /*error_callback*/,
               int /*timeout_ms*/),
              (override));

  MOCK_METHOD(bool,
              PassMeProtos,
              (const PassMeProtosRequest& /*in_request*/,
               brillo::ErrorPtr* /*error*/,
               int /*timeout_ms*/),
              (override));
  MOCK_METHOD(void,
              PassMeProtosAsync,
              (const PassMeProtosRequest& /*in_request*/,
               base::OnceCallback<void()> /*success_callback*/,
               base::OnceCallback<void(brillo::Error*)> /*error_callback*/,
               int /*timeout_ms*/),
              (override));

  void RegisterBSSRemovedSignalHandler(
    const base::RepeatingCallback<void(const YetAnotherProto&,
                                       const std::tuple<int32_t, base::ScopedFD>&)>& signal_callback,
    dbus::ObjectProxy::OnConnectedCallback on_connected_callback) override {
    DoRegisterBSSRemovedSignalHandler(signal_callback, &on_connected_callback);
  }
  MOCK_METHOD(void,
              DoRegisterBSSRemovedSignalHandler,
              (const base::RepeatingCallback<void(const YetAnotherProto&,
                                                  const std::tuple<int32_t, base::ScopedFD>&)>& /*signal_callback*/,
               dbus::ObjectProxy::OnConnectedCallback* /*on_connected_callback*/));

  MOCK_METHOD(const brillo::VariantDictionary&, capabilities, (), (const, override));
  MOCK_METHOD(bool, is_capabilities_valid, (), (const, override));

  MOCK_METHOD(uint32_t, bluetooth_class, (), (const, override));
  MOCK_METHOD(bool, is_bluetooth_class_valid, (), (const, override));

  MOCK_METHOD(const dbus::ObjectPath&, GetObjectPath, (), (const, override));
  MOCK_METHOD(dbus::ObjectProxy*, GetObjectProxy, (), (const, override));

  MOCK_METHOD(void,
              SetPropertyChangedCallback,
              ((const base::RepeatingCallback<void(InterfaceProxyInterface*,
                                                   const std::string&)>&)),
              (override));
};
}  // namespace wpa_supplicant1
}  // namespace w1
}  // namespace fi


// Abstract interface proxy for EmptyInterface.
class EmptyInterfaceProxyInterface {
 public:
  virtual ~EmptyInterfaceProxyInterface() = default;

  virtual const dbus::ObjectPath& GetObjectPath() const = 0;
  virtual dbus::ObjectProxy* GetObjectProxy() const = 0;
};



// Mock object for EmptyInterfaceProxyInterface.
class EmptyInterfaceProxyMock : public EmptyInterfaceProxyInterface {
 public:
  EmptyInterfaceProxyMock() = default;
  EmptyInterfaceProxyMock(const EmptyInterfaceProxyMock&) = delete;
  EmptyInterfaceProxyMock& operator=(const EmptyInterfaceProxyMock&) = delete;

  MOCK_METHOD(const dbus::ObjectPath&, GetObjectPath, (), (const, override));
  MOCK_METHOD(dbus::ObjectProxy*, GetObjectProxy, (), (const, override));
};

#endif  // ____CHROMEOS_DBUS_BINDING___TMP_MOCK_H
`

	if diff := cmp.Diff(out.String(), want); diff != "" {
		t.Errorf("Generate failed (-got +want):\n%s", diff)
	}
}

func TestGenerateMockProxiesEmpty(t *testing.T) {
	emptyItf := introspect.Interface{
		Name: "EmptyInterface",
	}

	introspections := []introspect.Introspection{{
		Interfaces: []introspect.Interface{emptyItf},
	}}

	sc := serviceconfig.Config{}
	out := new(bytes.Buffer)
	if err := GenerateMock(introspections, out, "/tmp/mock.h", "", sc); err != nil {
		t.Fatalf("Generate got error, want nil: %v", err)
	}

	const want = `// Automatic generation of D-Bus interface mock proxies for:
//  - EmptyInterface
#ifndef ____CHROMEOS_DBUS_BINDING___TMP_MOCK_H
#define ____CHROMEOS_DBUS_BINDING___TMP_MOCK_H
#include <string>
#include <vector>

#include <base/functional/callback_forward.h>
#include <base/logging.h>
#include <brillo/any.h>
#include <brillo/errors/error.h>
#include <brillo/variant_dictionary.h>
#include <gmock/gmock.h>


// Abstract interface proxy for EmptyInterface.
class EmptyInterfaceProxyInterface {
 public:
  virtual ~EmptyInterfaceProxyInterface() = default;

  virtual const dbus::ObjectPath& GetObjectPath() const = 0;
  virtual dbus::ObjectProxy* GetObjectProxy() const = 0;
};



// Mock object for EmptyInterfaceProxyInterface.
class EmptyInterfaceProxyMock : public EmptyInterfaceProxyInterface {
 public:
  EmptyInterfaceProxyMock() = default;
  EmptyInterfaceProxyMock(const EmptyInterfaceProxyMock&) = delete;
  EmptyInterfaceProxyMock& operator=(const EmptyInterfaceProxyMock&) = delete;

  MOCK_METHOD(const dbus::ObjectPath&, GetObjectPath, (), (const, override));
  MOCK_METHOD(dbus::ObjectProxy*, GetObjectProxy, (), (const, override));
};

#endif  // ____CHROMEOS_DBUS_BINDING___TMP_MOCK_H
`

	if diff := cmp.Diff(out.String(), want); diff != "" {
		t.Errorf("Generate failed (-got +want):\n%s", diff)
	}
}

func TestGenerateMockProxiesWithProxyPath(t *testing.T) {
	emptyItf := introspect.Interface{
		Name: "EmptyInterface",
	}

	introspections := []introspect.Introspection{{
		Interfaces: []introspect.Interface{emptyItf},
	}}

	sc := serviceconfig.Config{}
	out := new(bytes.Buffer)
	if err := GenerateMock(introspections, out, "/tmp/mock.h", "../proxy.h", sc); err != nil {
		t.Fatalf("Generate got error, want nil: %v", err)
	}

	const want = `// Automatic generation of D-Bus interface mock proxies for:
//  - EmptyInterface
#ifndef ____CHROMEOS_DBUS_BINDING___TMP_MOCK_H
#define ____CHROMEOS_DBUS_BINDING___TMP_MOCK_H
#include <string>
#include <vector>

#include <base/functional/callback_forward.h>
#include <base/logging.h>
#include <brillo/any.h>
#include <brillo/errors/error.h>
#include <brillo/variant_dictionary.h>
#include <gmock/gmock.h>

#include "../proxy.h"


// Mock object for EmptyInterfaceProxyInterface.
class EmptyInterfaceProxyMock : public EmptyInterfaceProxyInterface {
 public:
  EmptyInterfaceProxyMock() = default;
  EmptyInterfaceProxyMock(const EmptyInterfaceProxyMock&) = delete;
  EmptyInterfaceProxyMock& operator=(const EmptyInterfaceProxyMock&) = delete;

  MOCK_METHOD(const dbus::ObjectPath&, GetObjectPath, (), (const, override));
  MOCK_METHOD(dbus::ObjectProxy*, GetObjectProxy, (), (const, override));
};

#endif  // ____CHROMEOS_DBUS_BINDING___TMP_MOCK_H
`

	if diff := cmp.Diff(out.String(), want); diff != "" {
		t.Errorf("Generate failed (-got +want):\n%s", diff)
	}
}

func TestGenerateMockProxiesWithMethods(t *testing.T) {
	emptyItf := introspect.Interface{
		Name: "EmptyInterface",
		Methods: []introspect.Method{{
			Name: "MethodNoArg",
			Args: []introspect.MethodArg{},
		}, {
			Name: "MethodWithInArgs",
			Args: []introspect.MethodArg{
				{Name: "iarg1", Type: "x"},
				{Name: "iarg2", Type: "ay"},
				{Name: "iarg3", Type: "(ih)"},
				{
					Name: "iprotoArg",
					Type: "ay",
					Annotation: introspect.Annotation{
						Name:  "org.chromium.DBus.Argument.ProtobufClass",
						Value: "RequestProto",
					},
				},
			},
		}, {
			Name: "MethodWithOutArgs",
			Args: []introspect.MethodArg{
				{Name: "oarg1", Type: "x", Direction: "out"},
				{Name: "oarg2", Type: "ay", Direction: "out"},
				{Name: "oarg3", Type: "(ih)", Direction: "out"},
				{
					Name:      "oprotoArg",
					Type:      "ay",
					Direction: "out",
					Annotation: introspect.Annotation{
						Name:  "org.chromium.DBus.Argument.ProtobufClass",
						Value: "ResponseProto",
					},
				},
			},
		}, {
			Name: "MethodWithBothArgs",
			Args: []introspect.MethodArg{
				{Name: "iarg1", Type: "x"},
				{Name: "iarg2", Type: "ay"},
				{Name: "oarg1", Type: "q", Direction: "out"},
				{Name: "oarg2", Type: "d", Direction: "out"},
			},
		}, {
			Name: "MethodWithMixedArgs",
			Args: []introspect.MethodArg{
				{Name: "iarg1", Type: "x"},
				{Name: "oarg1", Type: "q", Direction: "out"},
				{Name: "iarg2", Type: "ay"},
				{Name: "oarg2", Type: "d", Direction: "out"},
			},
		}, {
			Name: "MethodArity5_2",
			Args: []introspect.MethodArg{
				{Name: "iarg1", Type: "x"},
				{Name: "iarg2", Type: "x"},
				{Name: "iarg3", Type: "x"},
				{Name: "iarg4", Type: "x"},
				{Name: "iarg5", Type: "x"},
				{Name: "oarg1", Type: "x", Direction: "out"},
				{Name: "oarg2", Type: "x", Direction: "out"},
			},
		}, {
			Name: "MethodArity6_2",
			Args: []introspect.MethodArg{
				{Name: "iarg1", Type: "x"},
				{Name: "iarg2", Type: "x"},
				{Name: "iarg3", Type: "x"},
				{Name: "iarg4", Type: "x"},
				{Name: "iarg5", Type: "x"},
				{Name: "iarg6", Type: "x"},
				{Name: "oarg1", Type: "x", Direction: "out"},
				{Name: "oarg2", Type: "x", Direction: "out"},
			},
		}, {
			Name: "MethodArity7_2",
			Args: []introspect.MethodArg{
				{Name: "iarg1", Type: "x"},
				{Name: "iarg2", Type: "x"},
				{Name: "iarg3", Type: "x"},
				{Name: "iarg4", Type: "x"},
				{Name: "iarg5", Type: "x"},
				{Name: "iarg6", Type: "x"},
				{Name: "iarg7", Type: "x"},
				{Name: "oarg1", Type: "x", Direction: "out"},
				{Name: "oarg2", Type: "x", Direction: "out"},
			},
		}, {
			Name: "MethodArity8_2",
			Args: []introspect.MethodArg{
				{Name: "iarg1", Type: "x"},
				{Name: "iarg2", Type: "x"},
				{Name: "iarg3", Type: "x"},
				{Name: "iarg4", Type: "x"},
				{Name: "iarg5", Type: "x"},
				{Name: "iarg6", Type: "x"},
				{Name: "iarg7", Type: "x"},
				{Name: "iarg8", Type: "x"},
				{Name: "oarg1", Type: "x", Direction: "out"},
				{Name: "oarg2", Type: "x", Direction: "out"},
			},
		}},
	}

	introspections := []introspect.Introspection{{
		Interfaces: []introspect.Interface{emptyItf},
	}}

	sc := serviceconfig.Config{}
	out := new(bytes.Buffer)
	if err := GenerateMock(introspections, out, "/tmp/mock.h", "../proxy.h", sc); err != nil {
		t.Fatalf("Generate got error, want nil: %v", err)
	}

	const want = `// Automatic generation of D-Bus interface mock proxies for:
//  - EmptyInterface
#ifndef ____CHROMEOS_DBUS_BINDING___TMP_MOCK_H
#define ____CHROMEOS_DBUS_BINDING___TMP_MOCK_H
#include <string>
#include <vector>

#include <base/functional/callback_forward.h>
#include <base/logging.h>
#include <brillo/any.h>
#include <brillo/errors/error.h>
#include <brillo/variant_dictionary.h>
#include <gmock/gmock.h>

#include "../proxy.h"


// Mock object for EmptyInterfaceProxyInterface.
class EmptyInterfaceProxyMock : public EmptyInterfaceProxyInterface {
 public:
  EmptyInterfaceProxyMock() = default;
  EmptyInterfaceProxyMock(const EmptyInterfaceProxyMock&) = delete;
  EmptyInterfaceProxyMock& operator=(const EmptyInterfaceProxyMock&) = delete;

  MOCK_METHOD(bool,
              MethodNoArg,
              (brillo::ErrorPtr* /*error*/,
               int /*timeout_ms*/),
              (override));
  MOCK_METHOD(void,
              MethodNoArgAsync,
              (base::OnceCallback<void()> /*success_callback*/,
               base::OnceCallback<void(brillo::Error*)> /*error_callback*/,
               int /*timeout_ms*/),
              (override));

  MOCK_METHOD(bool,
              MethodWithInArgs,
              (int64_t /*in_iarg1*/,
               const std::vector<uint8_t>& /*in_iarg2*/,
               (const std::tuple<int32_t, base::ScopedFD>&) /*in_iarg3*/,
               const RequestProto& /*in_iprotoArg*/,
               brillo::ErrorPtr* /*error*/,
               int /*timeout_ms*/),
              (override));
  MOCK_METHOD(void,
              MethodWithInArgsAsync,
              (int64_t /*in_iarg1*/,
               const std::vector<uint8_t>& /*in_iarg2*/,
               (const std::tuple<int32_t, base::ScopedFD>&) /*in_iarg3*/,
               const RequestProto& /*in_iprotoArg*/,
               base::OnceCallback<void()> /*success_callback*/,
               base::OnceCallback<void(brillo::Error*)> /*error_callback*/,
               int /*timeout_ms*/),
              (override));

  MOCK_METHOD(bool,
              MethodWithOutArgs,
              (int64_t* /*out_oarg1*/,
               std::vector<uint8_t>* /*out_oarg2*/,
               (std::tuple<int32_t, base::ScopedFD>*) /*out_oarg3*/,
               ResponseProto* /*out_oprotoArg*/,
               brillo::ErrorPtr* /*error*/,
               int /*timeout_ms*/),
              (override));
  MOCK_METHOD(void,
              MethodWithOutArgsAsync,
              ((base::OnceCallback<void(int64_t /*oarg1*/, const std::vector<uint8_t>& /*oarg2*/, const std::tuple<int32_t, base::ScopedFD>& /*oarg3*/, const ResponseProto& /*oprotoArg*/)>) /*success_callback*/,
               base::OnceCallback<void(brillo::Error*)> /*error_callback*/,
               int /*timeout_ms*/),
              (override));

  MOCK_METHOD(bool,
              MethodWithBothArgs,
              (int64_t /*in_iarg1*/,
               const std::vector<uint8_t>& /*in_iarg2*/,
               uint16_t* /*out_oarg1*/,
               double* /*out_oarg2*/,
               brillo::ErrorPtr* /*error*/,
               int /*timeout_ms*/),
              (override));
  MOCK_METHOD(void,
              MethodWithBothArgsAsync,
              (int64_t /*in_iarg1*/,
               const std::vector<uint8_t>& /*in_iarg2*/,
               (base::OnceCallback<void(uint16_t /*oarg1*/, double /*oarg2*/)>) /*success_callback*/,
               base::OnceCallback<void(brillo::Error*)> /*error_callback*/,
               int /*timeout_ms*/),
              (override));

  MOCK_METHOD(bool,
              MethodWithMixedArgs,
              (int64_t /*in_iarg1*/,
               const std::vector<uint8_t>& /*in_iarg2*/,
               uint16_t* /*out_oarg1*/,
               double* /*out_oarg2*/,
               brillo::ErrorPtr* /*error*/,
               int /*timeout_ms*/),
              (override));
  MOCK_METHOD(void,
              MethodWithMixedArgsAsync,
              (int64_t /*in_iarg1*/,
               const std::vector<uint8_t>& /*in_iarg2*/,
               (base::OnceCallback<void(uint16_t /*oarg1*/, double /*oarg2*/)>) /*success_callback*/,
               base::OnceCallback<void(brillo::Error*)> /*error_callback*/,
               int /*timeout_ms*/),
              (override));

  MOCK_METHOD(bool,
              MethodArity5_2,
              (int64_t /*in_iarg1*/,
               int64_t /*in_iarg2*/,
               int64_t /*in_iarg3*/,
               int64_t /*in_iarg4*/,
               int64_t /*in_iarg5*/,
               int64_t* /*out_oarg1*/,
               int64_t* /*out_oarg2*/,
               brillo::ErrorPtr* /*error*/,
               int /*timeout_ms*/),
              (override));
  MOCK_METHOD(void,
              MethodArity5_2Async,
              (int64_t /*in_iarg1*/,
               int64_t /*in_iarg2*/,
               int64_t /*in_iarg3*/,
               int64_t /*in_iarg4*/,
               int64_t /*in_iarg5*/,
               (base::OnceCallback<void(int64_t /*oarg1*/, int64_t /*oarg2*/)>) /*success_callback*/,
               base::OnceCallback<void(brillo::Error*)> /*error_callback*/,
               int /*timeout_ms*/),
              (override));

  MOCK_METHOD(bool,
              MethodArity6_2,
              (int64_t /*in_iarg1*/,
               int64_t /*in_iarg2*/,
               int64_t /*in_iarg3*/,
               int64_t /*in_iarg4*/,
               int64_t /*in_iarg5*/,
               int64_t /*in_iarg6*/,
               int64_t* /*out_oarg1*/,
               int64_t* /*out_oarg2*/,
               brillo::ErrorPtr* /*error*/,
               int /*timeout_ms*/),
              (override));
  MOCK_METHOD(void,
              MethodArity6_2Async,
              (int64_t /*in_iarg1*/,
               int64_t /*in_iarg2*/,
               int64_t /*in_iarg3*/,
               int64_t /*in_iarg4*/,
               int64_t /*in_iarg5*/,
               int64_t /*in_iarg6*/,
               (base::OnceCallback<void(int64_t /*oarg1*/, int64_t /*oarg2*/)>) /*success_callback*/,
               base::OnceCallback<void(brillo::Error*)> /*error_callback*/,
               int /*timeout_ms*/),
              (override));

  MOCK_METHOD(bool,
              MethodArity7_2,
              (int64_t /*in_iarg1*/,
               int64_t /*in_iarg2*/,
               int64_t /*in_iarg3*/,
               int64_t /*in_iarg4*/,
               int64_t /*in_iarg5*/,
               int64_t /*in_iarg6*/,
               int64_t /*in_iarg7*/,
               int64_t* /*out_oarg1*/,
               int64_t* /*out_oarg2*/,
               brillo::ErrorPtr* /*error*/,
               int /*timeout_ms*/),
              (override));
  MOCK_METHOD(void,
              MethodArity7_2Async,
              (int64_t /*in_iarg1*/,
               int64_t /*in_iarg2*/,
               int64_t /*in_iarg3*/,
               int64_t /*in_iarg4*/,
               int64_t /*in_iarg5*/,
               int64_t /*in_iarg6*/,
               int64_t /*in_iarg7*/,
               (base::OnceCallback<void(int64_t /*oarg1*/, int64_t /*oarg2*/)>) /*success_callback*/,
               base::OnceCallback<void(brillo::Error*)> /*error_callback*/,
               int /*timeout_ms*/),
              (override));

  MOCK_METHOD(bool,
              MethodArity8_2,
              (int64_t /*in_iarg1*/,
               int64_t /*in_iarg2*/,
               int64_t /*in_iarg3*/,
               int64_t /*in_iarg4*/,
               int64_t /*in_iarg5*/,
               int64_t /*in_iarg6*/,
               int64_t /*in_iarg7*/,
               int64_t /*in_iarg8*/,
               int64_t* /*out_oarg1*/,
               int64_t* /*out_oarg2*/,
               brillo::ErrorPtr* /*error*/,
               int /*timeout_ms*/),
              (override));
  MOCK_METHOD(void,
              MethodArity8_2Async,
              (int64_t /*in_iarg1*/,
               int64_t /*in_iarg2*/,
               int64_t /*in_iarg3*/,
               int64_t /*in_iarg4*/,
               int64_t /*in_iarg5*/,
               int64_t /*in_iarg6*/,
               int64_t /*in_iarg7*/,
               int64_t /*in_iarg8*/,
               (base::OnceCallback<void(int64_t /*oarg1*/, int64_t /*oarg2*/)>) /*success_callback*/,
               base::OnceCallback<void(brillo::Error*)> /*error_callback*/,
               int /*timeout_ms*/),
              (override));

  MOCK_METHOD(const dbus::ObjectPath&, GetObjectPath, (), (const, override));
  MOCK_METHOD(dbus::ObjectProxy*, GetObjectProxy, (), (const, override));
};

#endif  // ____CHROMEOS_DBUS_BINDING___TMP_MOCK_H
`

	if diff := cmp.Diff(out.String(), want); diff != "" {
		t.Errorf("Generate failed (-got +want):\n%s", diff)
	}
}

func TestGenerateMockProxiesWithSignals(t *testing.T) {
	emptyItf := introspect.Interface{
		Name: "EmptyInterface",
		Signals: []introspect.Signal{
			{
				Name: "Signal1",
				Args: []introspect.SignalArg{
					{
						Name: "sarg1_1",
						Type: "ay",
						Annotation: introspect.Annotation{
							Name:  "org.chromium.DBus.Argument.ProtobufClass",
							Value: "YetAnotherProto",
						},
					}, {
						Name: "sarg1_2",
						Type: "(ih)",
					},
				},
				DocString: "\n        signal doc\n      ",
			},
			{
				Name: "Signal2",
				Args: []introspect.SignalArg{
					{
						Name: "sarg2_1",
						Type: "ay",
					}, {
						Name: "sarg2_2",
						Type: "i",
					},
				},
				DocString: "\n        signal doc\n      ",
			},
		},
	}

	introspections := []introspect.Introspection{{
		Interfaces: []introspect.Interface{emptyItf},
	}}

	sc := serviceconfig.Config{}
	out := new(bytes.Buffer)
	if err := GenerateMock(introspections, out, "/tmp/mock.h", "../proxy.h", sc); err != nil {
		t.Fatalf("Generate got error, want nil: %v", err)
	}

	const want = `// Automatic generation of D-Bus interface mock proxies for:
//  - EmptyInterface
#ifndef ____CHROMEOS_DBUS_BINDING___TMP_MOCK_H
#define ____CHROMEOS_DBUS_BINDING___TMP_MOCK_H
#include <string>
#include <vector>

#include <base/functional/callback_forward.h>
#include <base/logging.h>
#include <brillo/any.h>
#include <brillo/errors/error.h>
#include <brillo/variant_dictionary.h>
#include <gmock/gmock.h>

#include "../proxy.h"


// Mock object for EmptyInterfaceProxyInterface.
class EmptyInterfaceProxyMock : public EmptyInterfaceProxyInterface {
 public:
  EmptyInterfaceProxyMock() = default;
  EmptyInterfaceProxyMock(const EmptyInterfaceProxyMock&) = delete;
  EmptyInterfaceProxyMock& operator=(const EmptyInterfaceProxyMock&) = delete;

  void RegisterSignal1SignalHandler(
    const base::RepeatingCallback<void(const YetAnotherProto&,
                                       const std::tuple<int32_t, base::ScopedFD>&)>& signal_callback,
    dbus::ObjectProxy::OnConnectedCallback on_connected_callback) override {
    DoRegisterSignal1SignalHandler(signal_callback, &on_connected_callback);
  }
  MOCK_METHOD(void,
              DoRegisterSignal1SignalHandler,
              (const base::RepeatingCallback<void(const YetAnotherProto&,
                                                  const std::tuple<int32_t, base::ScopedFD>&)>& /*signal_callback*/,
               dbus::ObjectProxy::OnConnectedCallback* /*on_connected_callback*/));

  void RegisterSignal2SignalHandler(
    const base::RepeatingCallback<void(const std::vector<uint8_t>&,
                                       int32_t)>& signal_callback,
    dbus::ObjectProxy::OnConnectedCallback on_connected_callback) override {
    DoRegisterSignal2SignalHandler(signal_callback, &on_connected_callback);
  }
  MOCK_METHOD(void,
              DoRegisterSignal2SignalHandler,
              (const base::RepeatingCallback<void(const std::vector<uint8_t>&,
                                                  int32_t)>& /*signal_callback*/,
               dbus::ObjectProxy::OnConnectedCallback* /*on_connected_callback*/));

  MOCK_METHOD(const dbus::ObjectPath&, GetObjectPath, (), (const, override));
  MOCK_METHOD(dbus::ObjectProxy*, GetObjectProxy, (), (const, override));
};

#endif  // ____CHROMEOS_DBUS_BINDING___TMP_MOCK_H
`

	if diff := cmp.Diff(out.String(), want); diff != "" {
		t.Errorf("Generate failed (-got +want):\n%s", diff)
	}
}

func TestGenerateMockProxiesWithProperties(t *testing.T) {
	emptyItf := introspect.Interface{
		Name: "EmptyInterface",
		Properties: []introspect.Property{
			{
				Name:      "ReadonlyProperty",
				Type:      "a{sv}",
				Access:    "read",
				DocString: "\n        property doc\n      ",
			},
			{
				Name:      "WritableProperty",
				Type:      "a{sv}",
				Access:    "readwrite",
				DocString: "\n        property doc\n      ",
			},
		},
	}

	introspections := []introspect.Introspection{{
		Interfaces: []introspect.Interface{emptyItf},
	}}

	sc := serviceconfig.Config{}
	out := new(bytes.Buffer)
	if err := GenerateMock(introspections, out, "/tmp/mock.h", "../proxy.h", sc); err != nil {
		t.Fatalf("Generate got error, want nil: %v", err)
	}

	const want = `// Automatic generation of D-Bus interface mock proxies for:
//  - EmptyInterface
#ifndef ____CHROMEOS_DBUS_BINDING___TMP_MOCK_H
#define ____CHROMEOS_DBUS_BINDING___TMP_MOCK_H
#include <string>
#include <vector>

#include <base/functional/callback_forward.h>
#include <base/logging.h>
#include <brillo/any.h>
#include <brillo/errors/error.h>
#include <brillo/variant_dictionary.h>
#include <gmock/gmock.h>

#include "../proxy.h"


// Mock object for EmptyInterfaceProxyInterface.
class EmptyInterfaceProxyMock : public EmptyInterfaceProxyInterface {
 public:
  EmptyInterfaceProxyMock() = default;
  EmptyInterfaceProxyMock(const EmptyInterfaceProxyMock&) = delete;
  EmptyInterfaceProxyMock& operator=(const EmptyInterfaceProxyMock&) = delete;

  MOCK_METHOD(const brillo::VariantDictionary&, readonly_property, (), (const, override));
  MOCK_METHOD(bool, is_readonly_property_valid, (), (const, override));

  MOCK_METHOD(const brillo::VariantDictionary&, writable_property, (), (const, override));
  MOCK_METHOD(bool, is_writable_property_valid, (), (const, override));
  MOCK_METHOD(void,
              set_writable_property,
              (const brillo::VariantDictionary&, base::OnceCallback<void(bool)>),
              (override));

  MOCK_METHOD(const dbus::ObjectPath&, GetObjectPath, (), (const, override));
  MOCK_METHOD(dbus::ObjectProxy*, GetObjectProxy, (), (const, override));

  MOCK_METHOD(void,
              InitializeProperties,
              ((const base::RepeatingCallback<void(EmptyInterfaceProxyInterface*,
                                                   const std::string&)>&)),
              (override));
};

#endif  // ____CHROMEOS_DBUS_BINDING___TMP_MOCK_H
`

	if diff := cmp.Diff(out.String(), want); diff != "" {
		t.Errorf("Generate failed (-got +want):\n%s", diff)
	}
}

func TestGenerateMockProxiesWithPropertiesAndObjectManager(t *testing.T) {
	emptyItf := introspect.Interface{
		Name: "EmptyInterface",
		Properties: []introspect.Property{
			{
				Name:      "ReadonlyProperty",
				Type:      "a{sv}",
				Access:    "read",
				DocString: "\n        property doc\n      ",
			},
			{
				Name:      "WritableProperty",
				Type:      "a{sv}",
				Access:    "readwrite",
				DocString: "\n        property doc\n      ",
			},
		},
	}

	introspections := []introspect.Introspection{{
		Interfaces: []introspect.Interface{emptyItf},
	}}

	sc := serviceconfig.Config{
		ObjectManager: &serviceconfig.ObjectManagerConfig{
			Name: "test.ObjectManager",
		},
	}
	out := new(bytes.Buffer)
	if err := GenerateMock(introspections, out, "/tmp/mock.h", "../proxy.h", sc); err != nil {
		t.Fatalf("Generate got error, want nil: %v", err)
	}

	const want = `// Automatic generation of D-Bus interface mock proxies for:
//  - EmptyInterface
#ifndef ____CHROMEOS_DBUS_BINDING___TMP_MOCK_H
#define ____CHROMEOS_DBUS_BINDING___TMP_MOCK_H
#include <string>
#include <vector>

#include <base/functional/callback_forward.h>
#include <base/logging.h>
#include <brillo/any.h>
#include <brillo/errors/error.h>
#include <brillo/variant_dictionary.h>
#include <gmock/gmock.h>

#include "../proxy.h"


// Mock object for EmptyInterfaceProxyInterface.
class EmptyInterfaceProxyMock : public EmptyInterfaceProxyInterface {
 public:
  EmptyInterfaceProxyMock() = default;
  EmptyInterfaceProxyMock(const EmptyInterfaceProxyMock&) = delete;
  EmptyInterfaceProxyMock& operator=(const EmptyInterfaceProxyMock&) = delete;

  MOCK_METHOD(const brillo::VariantDictionary&, readonly_property, (), (const, override));
  MOCK_METHOD(bool, is_readonly_property_valid, (), (const, override));

  MOCK_METHOD(const brillo::VariantDictionary&, writable_property, (), (const, override));
  MOCK_METHOD(bool, is_writable_property_valid, (), (const, override));
  MOCK_METHOD(void,
              set_writable_property,
              (const brillo::VariantDictionary&, base::OnceCallback<void(bool)>),
              (override));

  MOCK_METHOD(const dbus::ObjectPath&, GetObjectPath, (), (const, override));
  MOCK_METHOD(dbus::ObjectProxy*, GetObjectProxy, (), (const, override));

  MOCK_METHOD(void,
              SetPropertyChangedCallback,
              ((const base::RepeatingCallback<void(EmptyInterfaceProxyInterface*,
                                                   const std::string&)>&)),
              (override));
};

#endif  // ____CHROMEOS_DBUS_BINDING___TMP_MOCK_H
`

	if diff := cmp.Diff(out.String(), want); diff != "" {
		t.Errorf("Generate failed (-got +want):\n%s", diff)
	}
}
