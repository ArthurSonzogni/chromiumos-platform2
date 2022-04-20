// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package proxy

import (
	"bytes"
	"testing"

	"chromiumos/dbusbindings/introspect"
	"chromiumos/dbusbindings/serviceconfig"

	"github.com/google/go-cmp/cmp"
)

func TestGenerateProxies(t *testing.T) {
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
		ObjectManager: serviceconfig.ObjectManagerConfig{
			Name: "foo.bar.ObjectManager",
		},
	}
	out := new(bytes.Buffer)
	if err := Generate(introspections, out, "/tmp/proxy.h", sc); err != nil {
		t.Fatalf("Generate got error, want nil: %v", err)
	}

	const want = `// Automatic generation of D-Bus interfaces:
//  - fi.w1.wpa_supplicant1.Interface
//  - EmptyInterface
#ifndef ____CHROMEOS_DBUS_BINDING___TMP_PROXY_H
#define ____CHROMEOS_DBUS_BINDING___TMP_PROXY_H
#include <memory>
#include <string>
#include <vector>

#include <base/bind.h>
#include <base/callback.h>
#include <base/files/scoped_file.h>
#include <base/logging.h>
#include <base/memory/ref_counted.h>
#include <brillo/any.h>
#include <brillo/dbus/dbus_method_invoker.h>
#include <brillo/dbus/dbus_property.h>
#include <brillo/dbus/dbus_signal_handler.h>
#include <brillo/dbus/file_descriptor.h>
#include <brillo/errors/error.h>
#include <brillo/variant_dictionary.h>
#include <dbus/bus.h>
#include <dbus/message.h>
#include <dbus/object_manager.h>
#include <dbus/object_path.h>
#include <dbus/object_proxy.h>

namespace foo {
namespace bar {
class ObjectManagerProxy;
}  // namespace bar
}  // namespace foo

namespace fi {
namespace w1 {
namespace wpa_supplicant1 {

// Abstract interface proxy for fi::w1::wpa_supplicant1::Interface.
// interface doc
class InterfaceProxyInterface {
 public:
  virtual ~InterfaceProxyInterface() = default;

  virtual const dbus::ObjectPath& GetObjectPath() const = 0;
  virtual dbus::ObjectProxy* GetObjectProxy() const = 0;
};

}  // namespace wpa_supplicant1
}  // namespace w1
}  // namespace fi

namespace fi {
namespace w1 {
namespace wpa_supplicant1 {

// Interface proxy for fi::w1::wpa_supplicant1::Interface.
// interface doc
class InterfaceProxy final : public InterfaceProxyInterface {
 public:
  InterfaceProxy(const InterfaceProxy&) = delete;
  InterfaceProxy& operator=(const InterfaceProxy&) = delete;

  ~InterfaceProxy() override {
  }
  void ReleaseObjectProxy(base::OnceClosure callback) {
    bus_->RemoveObjectProxy(service_name_, object_path_, std::move(callback));
  }

  const dbus::ObjectPath& GetObjectPath() const override {
    return object_path_;
  }

  dbus::ObjectProxy* GetObjectProxy() const override {
    return dbus_object_proxy_;
  }

 private:
  scoped_refptr<dbus::Bus> bus_;
  std::string service_name_;
  const dbus::ObjectPath object_path_{"/org/chromium/Test"};
  PropertySet* property_set_;
  base::RepeatingCallback<void(InterfaceProxyInterface*, const std::string&)> on_property_changed_;
  dbus::ObjectProxy* dbus_object_proxy_;
  friend class foo::bar::ObjectManagerProxy;

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



// Interface proxy for EmptyInterface.
class EmptyInterfaceProxy final : public EmptyInterfaceProxyInterface {
 public:
  EmptyInterfaceProxy(const EmptyInterfaceProxy&) = delete;
  EmptyInterfaceProxy& operator=(const EmptyInterfaceProxy&) = delete;

  ~EmptyInterfaceProxy() override {
  }
  void ReleaseObjectProxy(base::OnceClosure callback) {
    bus_->RemoveObjectProxy(service_name_, object_path_, std::move(callback));
  }

  const dbus::ObjectPath& GetObjectPath() const override {
    return object_path_;
  }

  dbus::ObjectProxy* GetObjectProxy() const override {
    return dbus_object_proxy_;
  }

 private:
  scoped_refptr<dbus::Bus> bus_;
  std::string service_name_;
  dbus::ObjectPath object_path_;
  dbus::ObjectProxy* dbus_object_proxy_;

};


#endif  // ____CHROMEOS_DBUS_BINDING___TMP_PROXY_H
`

	if diff := cmp.Diff(out.String(), want); diff != "" {
		t.Errorf("Generate failed (-got +want):\n%s", diff)
	}
}

func TestGenerateProxiesEmpty(t *testing.T) {
	emptyItf := introspect.Interface{
		Name: "test.EmptyInterface",
	}

	introspections := []introspect.Introspection{{
		Interfaces: []introspect.Interface{emptyItf},
	}}

	sc := serviceconfig.Config{}
	out := new(bytes.Buffer)
	if err := Generate(introspections, out, "/tmp/proxy.h", sc); err != nil {
		t.Fatalf("Generate got error, want nil: %v", err)
	}

	const want = `// Automatic generation of D-Bus interfaces:
//  - test.EmptyInterface
#ifndef ____CHROMEOS_DBUS_BINDING___TMP_PROXY_H
#define ____CHROMEOS_DBUS_BINDING___TMP_PROXY_H
#include <memory>
#include <string>
#include <vector>

#include <base/bind.h>
#include <base/callback.h>
#include <base/files/scoped_file.h>
#include <base/logging.h>
#include <base/memory/ref_counted.h>
#include <brillo/any.h>
#include <brillo/dbus/dbus_method_invoker.h>
#include <brillo/dbus/dbus_property.h>
#include <brillo/dbus/dbus_signal_handler.h>
#include <brillo/dbus/file_descriptor.h>
#include <brillo/errors/error.h>
#include <brillo/variant_dictionary.h>
#include <dbus/bus.h>
#include <dbus/message.h>
#include <dbus/object_manager.h>
#include <dbus/object_path.h>
#include <dbus/object_proxy.h>

namespace test {

// Abstract interface proxy for test::EmptyInterface.
class EmptyInterfaceProxyInterface {
 public:
  virtual ~EmptyInterfaceProxyInterface() = default;

  virtual const dbus::ObjectPath& GetObjectPath() const = 0;
  virtual dbus::ObjectProxy* GetObjectProxy() const = 0;
};

}  // namespace test

namespace test {

// Interface proxy for test::EmptyInterface.
class EmptyInterfaceProxy final : public EmptyInterfaceProxyInterface {
 public:
  EmptyInterfaceProxy(const EmptyInterfaceProxy&) = delete;
  EmptyInterfaceProxy& operator=(const EmptyInterfaceProxy&) = delete;

  ~EmptyInterfaceProxy() override {
  }
  void ReleaseObjectProxy(base::OnceClosure callback) {
    bus_->RemoveObjectProxy(service_name_, object_path_, std::move(callback));
  }

  const dbus::ObjectPath& GetObjectPath() const override {
    return object_path_;
  }

  dbus::ObjectProxy* GetObjectProxy() const override {
    return dbus_object_proxy_;
  }

 private:
  scoped_refptr<dbus::Bus> bus_;
  std::string service_name_;
  dbus::ObjectPath object_path_;
  dbus::ObjectProxy* dbus_object_proxy_;

};

}  // namespace test

#endif  // ____CHROMEOS_DBUS_BINDING___TMP_PROXY_H
`

	if diff := cmp.Diff(out.String(), want); diff != "" {
		t.Errorf("Generate failed (-got +want):\n%s", diff)
	}
}

func TestGenerateProxiesWithServiceName(t *testing.T) {
	emptyItf := introspect.Interface{
		Name: "test.EmptyInterface",
	}

	introspections := []introspect.Introspection{{
		Interfaces: []introspect.Interface{emptyItf},
	}}

	sc := serviceconfig.Config{
		ServiceName: "test.ServiceName",
	}
	out := new(bytes.Buffer)
	if err := Generate(introspections, out, "/tmp/proxy.h", sc); err != nil {
		t.Fatalf("Generate got error, want nil: %v", err)
	}

	const want = `// Automatic generation of D-Bus interfaces:
//  - test.EmptyInterface
#ifndef ____CHROMEOS_DBUS_BINDING___TMP_PROXY_H
#define ____CHROMEOS_DBUS_BINDING___TMP_PROXY_H
#include <memory>
#include <string>
#include <vector>

#include <base/bind.h>
#include <base/callback.h>
#include <base/files/scoped_file.h>
#include <base/logging.h>
#include <base/memory/ref_counted.h>
#include <brillo/any.h>
#include <brillo/dbus/dbus_method_invoker.h>
#include <brillo/dbus/dbus_property.h>
#include <brillo/dbus/dbus_signal_handler.h>
#include <brillo/dbus/file_descriptor.h>
#include <brillo/errors/error.h>
#include <brillo/variant_dictionary.h>
#include <dbus/bus.h>
#include <dbus/message.h>
#include <dbus/object_manager.h>
#include <dbus/object_path.h>
#include <dbus/object_proxy.h>

namespace test {

// Abstract interface proxy for test::EmptyInterface.
class EmptyInterfaceProxyInterface {
 public:
  virtual ~EmptyInterfaceProxyInterface() = default;

  virtual const dbus::ObjectPath& GetObjectPath() const = 0;
  virtual dbus::ObjectProxy* GetObjectProxy() const = 0;
};

}  // namespace test

namespace test {

// Interface proxy for test::EmptyInterface.
class EmptyInterfaceProxy final : public EmptyInterfaceProxyInterface {
 public:
  EmptyInterfaceProxy(const EmptyInterfaceProxy&) = delete;
  EmptyInterfaceProxy& operator=(const EmptyInterfaceProxy&) = delete;

  ~EmptyInterfaceProxy() override {
  }
  void ReleaseObjectProxy(base::OnceClosure callback) {
    bus_->RemoveObjectProxy(service_name_, object_path_, std::move(callback));
  }

  const dbus::ObjectPath& GetObjectPath() const override {
    return object_path_;
  }

  dbus::ObjectProxy* GetObjectProxy() const override {
    return dbus_object_proxy_;
  }

 private:
  scoped_refptr<dbus::Bus> bus_;
  const std::string service_name_{"test.ServiceName"};
  dbus::ObjectPath object_path_;
  dbus::ObjectProxy* dbus_object_proxy_;

};

}  // namespace test

#endif  // ____CHROMEOS_DBUS_BINDING___TMP_PROXY_H
`

	if diff := cmp.Diff(out.String(), want); diff != "" {
		t.Errorf("Generate failed (-got +want):\n%s", diff)
	}
}

func TestGenerateProxiesWithNodeName(t *testing.T) {
	emptyItf := introspect.Interface{
		Name: "test.EmptyInterface",
	}

	introspections := []introspect.Introspection{{
		Name:       "test.node.Name",
		Interfaces: []introspect.Interface{emptyItf},
	}}

	sc := serviceconfig.Config{}
	out := new(bytes.Buffer)
	if err := Generate(introspections, out, "/tmp/proxy.h", sc); err != nil {
		t.Fatalf("Generate got error, want nil: %v", err)
	}

	const want = `// Automatic generation of D-Bus interfaces:
//  - test.EmptyInterface
#ifndef ____CHROMEOS_DBUS_BINDING___TMP_PROXY_H
#define ____CHROMEOS_DBUS_BINDING___TMP_PROXY_H
#include <memory>
#include <string>
#include <vector>

#include <base/bind.h>
#include <base/callback.h>
#include <base/files/scoped_file.h>
#include <base/logging.h>
#include <base/memory/ref_counted.h>
#include <brillo/any.h>
#include <brillo/dbus/dbus_method_invoker.h>
#include <brillo/dbus/dbus_property.h>
#include <brillo/dbus/dbus_signal_handler.h>
#include <brillo/dbus/file_descriptor.h>
#include <brillo/errors/error.h>
#include <brillo/variant_dictionary.h>
#include <dbus/bus.h>
#include <dbus/message.h>
#include <dbus/object_manager.h>
#include <dbus/object_path.h>
#include <dbus/object_proxy.h>

namespace test {

// Abstract interface proxy for test::EmptyInterface.
class EmptyInterfaceProxyInterface {
 public:
  virtual ~EmptyInterfaceProxyInterface() = default;

  virtual const dbus::ObjectPath& GetObjectPath() const = 0;
  virtual dbus::ObjectProxy* GetObjectProxy() const = 0;
};

}  // namespace test

namespace test {

// Interface proxy for test::EmptyInterface.
class EmptyInterfaceProxy final : public EmptyInterfaceProxyInterface {
 public:
  EmptyInterfaceProxy(const EmptyInterfaceProxy&) = delete;
  EmptyInterfaceProxy& operator=(const EmptyInterfaceProxy&) = delete;

  ~EmptyInterfaceProxy() override {
  }
  void ReleaseObjectProxy(base::OnceClosure callback) {
    bus_->RemoveObjectProxy(service_name_, object_path_, std::move(callback));
  }

  const dbus::ObjectPath& GetObjectPath() const override {
    return object_path_;
  }

  dbus::ObjectProxy* GetObjectProxy() const override {
    return dbus_object_proxy_;
  }

 private:
  scoped_refptr<dbus::Bus> bus_;
  std::string service_name_;
  const dbus::ObjectPath object_path_{"test.node.Name"};
  dbus::ObjectProxy* dbus_object_proxy_;

};

}  // namespace test

#endif  // ____CHROMEOS_DBUS_BINDING___TMP_PROXY_H
`

	if diff := cmp.Diff(out.String(), want); diff != "" {
		t.Errorf("Generate failed (-got +want):\n%s", diff)
	}
}

func TestGenerateProxiesWithProperties(t *testing.T) {
	emptyItf := introspect.Interface{
		Name: "test.EmptyInterface",
		Properties: []introspect.Property{
			{
				Name:      "Capabilities",
				Type:      "a{sv}",
				Access:    "read",
				DocString: "\n        property doc\n      ",
			},
		},
	}

	introspections := []introspect.Introspection{{
		Interfaces: []introspect.Interface{emptyItf},
	}}

	sc := serviceconfig.Config{}
	out := new(bytes.Buffer)
	if err := Generate(introspections, out, "/tmp/proxy.h", sc); err != nil {
		t.Fatalf("Generate got error, want nil: %v", err)
	}

	const want = `// Automatic generation of D-Bus interfaces:
//  - test.EmptyInterface
#ifndef ____CHROMEOS_DBUS_BINDING___TMP_PROXY_H
#define ____CHROMEOS_DBUS_BINDING___TMP_PROXY_H
#include <memory>
#include <string>
#include <vector>

#include <base/bind.h>
#include <base/callback.h>
#include <base/files/scoped_file.h>
#include <base/logging.h>
#include <base/memory/ref_counted.h>
#include <brillo/any.h>
#include <brillo/dbus/dbus_method_invoker.h>
#include <brillo/dbus/dbus_property.h>
#include <brillo/dbus/dbus_signal_handler.h>
#include <brillo/dbus/file_descriptor.h>
#include <brillo/errors/error.h>
#include <brillo/variant_dictionary.h>
#include <dbus/bus.h>
#include <dbus/message.h>
#include <dbus/object_manager.h>
#include <dbus/object_path.h>
#include <dbus/object_proxy.h>

namespace test {

// Abstract interface proxy for test::EmptyInterface.
class EmptyInterfaceProxyInterface {
 public:
  virtual ~EmptyInterfaceProxyInterface() = default;

  virtual const dbus::ObjectPath& GetObjectPath() const = 0;
  virtual dbus::ObjectProxy* GetObjectProxy() const = 0;
};

}  // namespace test

namespace test {

// Interface proxy for test::EmptyInterface.
class EmptyInterfaceProxy final : public EmptyInterfaceProxyInterface {
 public:
  EmptyInterfaceProxy(const EmptyInterfaceProxy&) = delete;
  EmptyInterfaceProxy& operator=(const EmptyInterfaceProxy&) = delete;

  ~EmptyInterfaceProxy() override {
  }
  void ReleaseObjectProxy(base::OnceClosure callback) {
    bus_->RemoveObjectProxy(service_name_, object_path_, std::move(callback));
  }

  const dbus::ObjectPath& GetObjectPath() const override {
    return object_path_;
  }

  dbus::ObjectProxy* GetObjectProxy() const override {
    return dbus_object_proxy_;
  }

 private:
  scoped_refptr<dbus::Bus> bus_;
  std::string service_name_;
  dbus::ObjectPath object_path_;
  dbus::ObjectProxy* dbus_object_proxy_;
  std::unique_ptr<PropertySet> property_set_;

};

}  // namespace test

#endif  // ____CHROMEOS_DBUS_BINDING___TMP_PROXY_H
`

	if diff := cmp.Diff(out.String(), want); diff != "" {
		t.Errorf("Generate failed (-got +want):\n%s", diff)
	}
}

func TestGenerateProxiesWithObjectManager(t *testing.T) {
	emptyItf := introspect.Interface{
		Name: "test.EmptyInterface",
	}

	introspections := []introspect.Introspection{{
		Interfaces: []introspect.Interface{emptyItf},
	}}

	sc := serviceconfig.Config{
		ObjectManager: serviceconfig.ObjectManagerConfig{
			Name: "test.ObjectManager",
		},
	}
	out := new(bytes.Buffer)
	if err := Generate(introspections, out, "/tmp/proxy.h", sc); err != nil {
		t.Fatalf("Generate got error, want nil: %v", err)
	}

	const want = `// Automatic generation of D-Bus interfaces:
//  - test.EmptyInterface
#ifndef ____CHROMEOS_DBUS_BINDING___TMP_PROXY_H
#define ____CHROMEOS_DBUS_BINDING___TMP_PROXY_H
#include <memory>
#include <string>
#include <vector>

#include <base/bind.h>
#include <base/callback.h>
#include <base/files/scoped_file.h>
#include <base/logging.h>
#include <base/memory/ref_counted.h>
#include <brillo/any.h>
#include <brillo/dbus/dbus_method_invoker.h>
#include <brillo/dbus/dbus_property.h>
#include <brillo/dbus/dbus_signal_handler.h>
#include <brillo/dbus/file_descriptor.h>
#include <brillo/errors/error.h>
#include <brillo/variant_dictionary.h>
#include <dbus/bus.h>
#include <dbus/message.h>
#include <dbus/object_manager.h>
#include <dbus/object_path.h>
#include <dbus/object_proxy.h>

namespace test {
class ObjectManagerProxy;
}  // namespace test

namespace test {

// Abstract interface proxy for test::EmptyInterface.
class EmptyInterfaceProxyInterface {
 public:
  virtual ~EmptyInterfaceProxyInterface() = default;

  virtual const dbus::ObjectPath& GetObjectPath() const = 0;
  virtual dbus::ObjectProxy* GetObjectProxy() const = 0;
};

}  // namespace test

namespace test {

// Interface proxy for test::EmptyInterface.
class EmptyInterfaceProxy final : public EmptyInterfaceProxyInterface {
 public:
  EmptyInterfaceProxy(const EmptyInterfaceProxy&) = delete;
  EmptyInterfaceProxy& operator=(const EmptyInterfaceProxy&) = delete;

  ~EmptyInterfaceProxy() override {
  }
  void ReleaseObjectProxy(base::OnceClosure callback) {
    bus_->RemoveObjectProxy(service_name_, object_path_, std::move(callback));
  }

  const dbus::ObjectPath& GetObjectPath() const override {
    return object_path_;
  }

  dbus::ObjectProxy* GetObjectProxy() const override {
    return dbus_object_proxy_;
  }

 private:
  scoped_refptr<dbus::Bus> bus_;
  std::string service_name_;
  dbus::ObjectPath object_path_;
  dbus::ObjectProxy* dbus_object_proxy_;

};

}  // namespace test

#endif  // ____CHROMEOS_DBUS_BINDING___TMP_PROXY_H
`

	if diff := cmp.Diff(out.String(), want); diff != "" {
		t.Errorf("Generate failed (-got +want):\n%s", diff)
	}
}

func TestGenerateProxiesWithPropertiesAndObjectManager(t *testing.T) {
	emptyItf := introspect.Interface{
		Name: "test.EmptyInterface",
		Properties: []introspect.Property{
			{
				Name:      "Capabilities",
				Type:      "a{sv}",
				Access:    "read",
				DocString: "\n        property doc\n      ",
			},
		},
	}

	introspections := []introspect.Introspection{{
		Interfaces: []introspect.Interface{emptyItf},
	}}

	sc := serviceconfig.Config{
		ObjectManager: serviceconfig.ObjectManagerConfig{
			Name: "test.ObjectManager",
		},
	}
	out := new(bytes.Buffer)
	if err := Generate(introspections, out, "/tmp/proxy.h", sc); err != nil {
		t.Fatalf("Generate got error, want nil: %v", err)
	}

	const want = `// Automatic generation of D-Bus interfaces:
//  - test.EmptyInterface
#ifndef ____CHROMEOS_DBUS_BINDING___TMP_PROXY_H
#define ____CHROMEOS_DBUS_BINDING___TMP_PROXY_H
#include <memory>
#include <string>
#include <vector>

#include <base/bind.h>
#include <base/callback.h>
#include <base/files/scoped_file.h>
#include <base/logging.h>
#include <base/memory/ref_counted.h>
#include <brillo/any.h>
#include <brillo/dbus/dbus_method_invoker.h>
#include <brillo/dbus/dbus_property.h>
#include <brillo/dbus/dbus_signal_handler.h>
#include <brillo/dbus/file_descriptor.h>
#include <brillo/errors/error.h>
#include <brillo/variant_dictionary.h>
#include <dbus/bus.h>
#include <dbus/message.h>
#include <dbus/object_manager.h>
#include <dbus/object_path.h>
#include <dbus/object_proxy.h>

namespace test {
class ObjectManagerProxy;
}  // namespace test

namespace test {

// Abstract interface proxy for test::EmptyInterface.
class EmptyInterfaceProxyInterface {
 public:
  virtual ~EmptyInterfaceProxyInterface() = default;

  virtual const dbus::ObjectPath& GetObjectPath() const = 0;
  virtual dbus::ObjectProxy* GetObjectProxy() const = 0;
};

}  // namespace test

namespace test {

// Interface proxy for test::EmptyInterface.
class EmptyInterfaceProxy final : public EmptyInterfaceProxyInterface {
 public:
  EmptyInterfaceProxy(const EmptyInterfaceProxy&) = delete;
  EmptyInterfaceProxy& operator=(const EmptyInterfaceProxy&) = delete;

  ~EmptyInterfaceProxy() override {
  }
  void ReleaseObjectProxy(base::OnceClosure callback) {
    bus_->RemoveObjectProxy(service_name_, object_path_, std::move(callback));
  }

  const dbus::ObjectPath& GetObjectPath() const override {
    return object_path_;
  }

  dbus::ObjectProxy* GetObjectProxy() const override {
    return dbus_object_proxy_;
  }

 private:
  scoped_refptr<dbus::Bus> bus_;
  std::string service_name_;
  dbus::ObjectPath object_path_;
  PropertySet* property_set_;
  base::RepeatingCallback<void(EmptyInterfaceProxyInterface*, const std::string&)> on_property_changed_;
  dbus::ObjectProxy* dbus_object_proxy_;
  friend class test::ObjectManagerProxy;

};

}  // namespace test

#endif  // ____CHROMEOS_DBUS_BINDING___TMP_PROXY_H
`

	if diff := cmp.Diff(out.String(), want); diff != "" {
		t.Errorf("Generate failed (-got +want):\n%s", diff)
	}
}
