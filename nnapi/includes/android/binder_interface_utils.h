// This code is taken from 
// src/aosp/frameworks/native/libs/binder/ndk/include_cpp/android/
//
// This file provides the pieces of the Binder framework
// that are referenced from within the generated AIDL code.
//
// Changes have been made to enable compilation within ChromeOS,
// look for 'NNAPI_CHROMEOS'.
//
// This is done to avoid making changes to the generated
// AIDL code so that we can easily reproduce that code
// if necessary.

#pragma once

/*
 * Copyright (C) 2018 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/**
 * @addtogroup NdkBinder
 * @{
 */

/**
 * @file binder_interface_utils.h
 * @brief This provides common C++ classes for common operations and as base classes for C++
 * interfaces.
 */

#pragma once

#include <android/binder_auto_utils.h>
#include <android/binder_ibinder.h>

#if __has_include(<android/binder_shell.h>)
#include <android/binder_shell.h>
#define HAS_BINDER_SHELL_COMMAND
#endif  //_has_include

#include <assert.h>

#include <memory>
#include <mutex>

namespace ndk {

typedef int32_t parcelable_stability_t;
enum {
    STABILITY_LOCAL,
    STABILITY_VINTF,  // corresponds to @VintfStability
};

/**
 * analog using std::shared_ptr for internally held refcount
 *
 * ref must be called at least one time during the lifetime of this object. The recommended way to
 * construct this object is with SharedRefBase::make.
 */
class SharedRefBase {
   public:
    SharedRefBase() {}
    virtual ~SharedRefBase() {
#if !defined(NNAPI_CHROMEOS)
        std::call_once(mFlagThis, [&]() {
            __assert(__FILE__, __LINE__, "SharedRefBase: no ref created during lifetime");
        });

        if (ref() != nullptr) {
            __assert(__FILE__, __LINE__,
                     "SharedRefBase: destructed but still able to lock weak_ptr. Is this object "
                     "double-owned?");
        }
#endif // NNAPI_CHROMEOS (__assert not defined)
    }

    /**
     * A shared_ptr must be held to this object when this is called. This must be called once during
     * the lifetime of this object.
     */
    std::shared_ptr<SharedRefBase> ref() {
        std::shared_ptr<SharedRefBase> thiz = mThis.lock();

        std::call_once(mFlagThis, [&]() { mThis = thiz = std::shared_ptr<SharedRefBase>(this); });

        return thiz;
    }

    /**
     * Convenience method for a ref (see above) which automatically casts to the desired child type.
     */
    template <typename CHILD>
    std::shared_ptr<CHILD> ref() {
        return std::static_pointer_cast<CHILD>(ref());
    }

    /**
     * Convenience method for making an object directly with a reference.
     */
    template <class T, class... Args>
    static std::shared_ptr<T> make(Args&&... args) {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
        T* t = new T(std::forward<Args>(args)...);
#pragma clang diagnostic pop
        // warning: Potential leak of memory pointed to by 't' [clang-analyzer-unix.Malloc]
        return t->template ref<T>();  // NOLINT(clang-analyzer-unix.Malloc)
    }

    static void operator delete(void* p) { std::free(p); }

    // Once minSdkVersion is 30, we are guaranteed to be building with the
    // Android 11 AIDL compiler which supports the SharedRefBase::make API.
    //
    // Use 'SharedRefBase::make<T>(...)' to make. SharedRefBase has implicit
    // ownership. Making this operator private to avoid double-ownership.
#if !defined(__ANDROID_API__) || __ANDROID_API__ >= 30 || defined(__ANDROID_APEX__)
   private:
#else
    [[deprecated("Prefer SharedRefBase::make<T>(...) if possible.")]]
#endif
    static void* operator new(size_t s) { return std::malloc(s); }

   private:
    std::once_flag mFlagThis;
    std::weak_ptr<SharedRefBase> mThis;
};

/**
 * wrapper analog to IInterface
 */
class ICInterface : public SharedRefBase {
   public:
    ICInterface() {}
    virtual ~ICInterface() {}

    /**
     * This either returns the single existing implementation or creates a new implementation.
     */
    virtual SpAIBinder asBinder() = 0;

    /**
     * Returns whether this interface is in a remote process. If it cannot be determined locally,
     * this will be checked using AIBinder_isRemote.
     */
    virtual bool isRemote() = 0;

    /**
     * Dumps information about the interface. By default, dumps nothing.
     */
    virtual inline binder_status_t dump(int fd, const char** args, uint32_t numArgs);

#ifdef HAS_BINDER_SHELL_COMMAND
    /**
     * Process shell commands. By default, does nothing.
     */
    virtual inline binder_status_t handleShellCommand(int in, int out, int err, const char** argv,
                                                      uint32_t argc);
#endif

    /**
     * Interprets this binder as this underlying interface if this has stored an ICInterface in the
     * binder's user data.
     *
     * This does not do type checking and should only be used when the binder is known to originate
     * from ICInterface. Most likely, you want to use I*::fromBinder.
     */
    static inline std::shared_ptr<ICInterface> asInterface(AIBinder* binder);

    /**
     * Helper method to create a class
     */
    static inline AIBinder_Class* defineClass(const char* interfaceDescriptor,
                                              AIBinder_Class_onTransact onTransact);

   private:
    class ICInterfaceData {
       public:
        std::shared_ptr<ICInterface> interface;

        static inline std::shared_ptr<ICInterface> getInterface(AIBinder* binder);

        static inline void* onCreate(void* args);
        static inline void onDestroy(void* userData);
        static inline binder_status_t onDump(AIBinder* binder, int fd, const char** args,
                                             uint32_t numArgs);

#ifdef HAS_BINDER_SHELL_COMMAND
        static inline binder_status_t handleShellCommand(AIBinder* binder, int in, int out, int err,
                                                         const char** argv, uint32_t argc);
#endif
    };
};

/**
 * implementation of IInterface for server (n = native)
 */
template <typename INTERFACE>
class BnCInterface : public INTERFACE {
   public:
    BnCInterface() {}
    virtual ~BnCInterface() {}

    SpAIBinder asBinder() override final;

    bool isRemote() override final { return false; }

   protected:
    /**
     * This function should only be called by asBinder. Otherwise, there is a possibility of
     * multiple AIBinder* objects being created for the same instance of an object.
     */
    virtual SpAIBinder createBinder() = 0;

   private:
    std::mutex mMutex;  // for asBinder
#if !defined(NNAPI_CHROMEOS)
    ScopedAIBinder_Weak mWeakBinder;
#endif //  NNAPI_CHROMEOS (removing binder references)
};

/**
 * implementation of IInterface for client (p = proxy)
 */
template <typename INTERFACE>
class BpCInterface : public INTERFACE {
   public:
#if !defined(NNAPI_CHROMEOS)
    explicit BpCInterface(const SpAIBinder& binder) : mBinder(binder) {}
#else  //  NNAPI_CHROMEOS (removing binder references)
    explicit BpCInterface(const SpAIBinder&) {}
#endif //  NNAPI_CHROMEOS (removing binder references)
    virtual ~BpCInterface() {}

    SpAIBinder asBinder() override final;

    bool isRemote() override final {
#if !defined(NNAPI_CHROMEOS)
        return AIBinder_isRemote(mBinder.get());
#else
        return false;
#endif //  NNAPI_CHROMEOS (removing binder references)
    }

    binder_status_t dump(int fd, const char** args, uint32_t numArgs) override {
        return AIBinder_dump(asBinder().get(), fd, args, numArgs);
    }

   private:
#if !defined(NNAPI_CHROMEOS)
    SpAIBinder mBinder;
#endif //  NNAPI_CHROMEOS (removing binder references)
};

// END OF CLASS DECLARATIONS

binder_status_t ICInterface::dump(int /*fd*/, const char** /*args*/, uint32_t /*numArgs*/) {
    return STATUS_OK;
}

#ifdef HAS_BINDER_SHELL_COMMAND
binder_status_t ICInterface::handleShellCommand(int /*in*/, int /*out*/, int /*err*/,
                                                const char** /*argv*/, uint32_t /*argc*/) {
    return STATUS_OK;
}
#endif

std::shared_ptr<ICInterface> ICInterface::asInterface(AIBinder* binder) {
    return ICInterfaceData::getInterface(binder);
}

AIBinder_Class* ICInterface::defineClass(const char* /* interfaceDescriptor */,
                                         AIBinder_Class_onTransact /* onTransact */) {
#if !defined(NNAPI_CHROMEOS)
    AIBinder_Class* clazz = AIBinder_Class_define(interfaceDescriptor, ICInterfaceData::onCreate,
                                                  ICInterfaceData::onDestroy, onTransact);
    if (clazz == nullptr) {
        return nullptr;
    }

    // We can't know if these methods are overridden by a subclass interface, so we must register
    // ourselves. The defaults are harmless.
    AIBinder_Class_setOnDump(clazz, ICInterfaceData::onDump);
#ifdef HAS_BINDER_SHELL_COMMAND
#ifdef __ANDROID_UNAVAILABLE_SYMBOLS_ARE_WEAK__
    if (__builtin_available(android 30, *)) {
#else
    if (__ANDROID_API__ >= 30) {
#endif
        AIBinder_Class_setHandleShellCommand(clazz, ICInterfaceData::handleShellCommand);
    }
#endif
    return clazz;
#else  //  NNAPI_CHROMEOS (removing binder references)
    return nullptr;
#endif //  NNAPI_CHROMEOS (removing binder references)
}

std::shared_ptr<ICInterface> ICInterface::ICInterfaceData::getInterface(AIBinder* binder) {
    if (binder == nullptr) return nullptr;

#if !defined(NNAPI_CHROMEOS)
    void* userData = AIBinder_getUserData(binder);
    if (userData == nullptr) return nullptr;
    return static_cast<ICInterfaceData*>(userData)->interface;
#else  //  NNAPI_CHROMEOS (removing binder references)
    return nullptr;
#endif //  NNAPI_CHROMEOS (removing binder references)
}

void* ICInterface::ICInterfaceData::onCreate(void* args) {
    std::shared_ptr<ICInterface> interface = static_cast<ICInterface*>(args)->ref<ICInterface>();
    ICInterfaceData* data = new ICInterfaceData{interface};
    return static_cast<void*>(data);
}

void ICInterface::ICInterfaceData::onDestroy(void* userData) {
    delete static_cast<ICInterfaceData*>(userData);
}

binder_status_t ICInterface::ICInterfaceData::onDump(AIBinder* binder, int fd, const char** args,
                                                     uint32_t numArgs) {
    std::shared_ptr<ICInterface> interface = getInterface(binder);
    return interface->dump(fd, args, numArgs);
}

#ifdef HAS_BINDER_SHELL_COMMAND
binder_status_t ICInterface::ICInterfaceData::handleShellCommand(AIBinder* binder, int in, int out,
                                                                 int err, const char** argv,
                                                                 uint32_t argc) {
    std::shared_ptr<ICInterface> interface = getInterface(binder);
    return interface->handleShellCommand(in, out, err, argv, argc);
}
#endif

template <typename INTERFACE>
SpAIBinder BnCInterface<INTERFACE>::asBinder() {
    std::lock_guard<std::mutex> l(mMutex);

    SpAIBinder binder;

#if !defined(NNAPI_CHROMEOS)
    if (mWeakBinder.get() != nullptr) {
        binder.set(AIBinder_Weak_promote(mWeakBinder.get()));
    }
    if (binder.get() == nullptr) {
        binder = createBinder();
        mWeakBinder.set(AIBinder_Weak_new(binder.get()));
    }
#endif  //  NNAPI_CHROMEOS (removing binder references)

    return binder;
}

template <typename INTERFACE>
SpAIBinder BpCInterface<INTERFACE>::asBinder() {
#if !defined(NNAPI_CHROMEOS)
    return mBinder;
#else
    return SpAIBinder();
#endif //  NNAPI_CHROMEOS (removing binder references)
}

}  // namespace ndk

// Once minSdkVersion is 30, we are guaranteed to be building with the
// Android 11 AIDL compiler which supports the SharedRefBase::make API.
#if !defined(__ANDROID_API__) || __ANDROID_API__ >= 30 || defined(__ANDROID_APEX__)
namespace ndk::internal {
template <typename T, typename = void>
struct is_complete_type : std::false_type {};

template <typename T>
struct is_complete_type<T, decltype(void(sizeof(T)))> : std::true_type {};
}  // namespace ndk::internal

namespace std {

// Define `SharedRefBase` specific versions of `std::make_shared` and
// `std::make_unique` to block people from using them. Using them to allocate
// `ndk::SharedRefBase` objects results in double ownership. Use
// `ndk::SharedRefBase::make<T>(...)` instead.
//
// Note: We exclude incomplete types because `std::is_base_of` is undefined in
// that case.

template <typename T, typename... Args,
          std::enable_if_t<ndk::internal::is_complete_type<T>::value, bool> = true,
          std::enable_if_t<std::is_base_of<ndk::SharedRefBase, T>::value, bool> = true>
shared_ptr<T> make_shared(Args...) {  // SEE COMMENT ABOVE.
    static_assert(!std::is_base_of<ndk::SharedRefBase, T>::value);
}

template <typename T, typename... Args,
          std::enable_if_t<ndk::internal::is_complete_type<T>::value, bool> = true,
          std::enable_if_t<std::is_base_of<ndk::SharedRefBase, T>::value, bool> = true>
unique_ptr<T> make_unique(Args...) {  // SEE COMMENT ABOVE.
    static_assert(!std::is_base_of<ndk::SharedRefBase, T>::value);
}

}  // namespace std
#endif

/** @} */
