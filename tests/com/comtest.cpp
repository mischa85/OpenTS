/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// Exercises in-process COM activation: CoRegisterClassObject, CoGetClassObject,
// CoCreateInstance, CoRevokeClassObject, and the smart pointer that construction from a
// class identifier goes through. The engine is its own COM server -- startup.cpp publishes
// a class factory for every persistent game class and the game then creates those objects
// by class identifier -- so these are the calls a scenario load depends on.
//
// The harness supplies its own interfaces, class factory, and object. It registers nothing
// with the operating system, reads no game data, and revokes what it registers.
//
// It is written against COM rather than against the WebAssembly target's substitute for
// it, and builds on both. On Windows it establishes what OLE actually does; on WebAssembly
// it holds win32compat.cpp to that same account. A check that would pass against the
// substitute but not against Windows is worth nothing here.
//
// The object implements two interfaces by separate inheritance, as LocomotionClass does,
// so a request that returns the wrong sub-object address fails a check instead of being
// discovered later as a call through a mismatched vtable.

#if defined(__EMSCRIPTEN__)
#include "win32compat.h"
#else
#include <windows.h>
#include <objbase.h>
#include <comdef.h>
#endif

#include <cstdint>
#include <cstdio>


static int Failures = 0;
static int Checks = 0;


static void Check(char const * name, bool condition)
{
	Checks++;
	if (condition) return;

	Failures++;
	printf("FAIL %s\n", name);
}


static void Check_Result(char const * name, HRESULT actual, HRESULT expected)
{
	Checks++;
	if (actual == expected) return;

	Failures++;
	printf("FAIL %s: got HRESULT 0x%08lX, expected 0x%08lX\n",
		name, (unsigned long)actual, (unsigned long)expected);
}


static void Check_Count(char const * name, unsigned long actual, unsigned long expected)
{
	Checks++;
	if (actual == expected) return;

	Failures++;
	printf("FAIL %s: got %lu, expected %lu\n", name, actual, expected);
}


/*
** The harness's own identifiers. They name nothing outside this file and are never written
** to the registry, so they exist only for the duration of the run.
*/
static GUID const IID_ITestObject =
	{0x6F1B9C40, 0x0E2A, 0x4B7D, {0x9A, 0x31, 0x4C, 0x6D, 0x5E, 0x8F, 0x00, 0x11}};
static GUID const IID_ITestExtra =
	{0x6F1B9C41, 0x0E2A, 0x4B7D, {0x9A, 0x31, 0x4C, 0x6D, 0x5E, 0x8F, 0x00, 0x11}};
static GUID const IID_ITestAbsent =
	{0x6F1B9C42, 0x0E2A, 0x4B7D, {0x9A, 0x31, 0x4C, 0x6D, 0x5E, 0x8F, 0x00, 0x11}};
static GUID const CLSID_TestObject =
	{0x6F1B9C43, 0x0E2A, 0x4B7D, {0x9A, 0x31, 0x4C, 0x6D, 0x5E, 0x8F, 0x00, 0x11}};
static GUID const CLSID_TestAbsent =
	{0x6F1B9C44, 0x0E2A, 0x4B7D, {0x9A, 0x31, 0x4C, 0x6D, 0x5E, 0x8F, 0x00, 0x11}};


struct ITestObject : public IUnknown
{
	virtual int STDMETHODCALLTYPE Identify(void) = 0;
};

struct ITestExtra : public IUnknown
{
	virtual int STDMETHODCALLTYPE Extra(void) = 0;
};


/*
** How many objects the factory has made and not yet seen destroyed. A leak and a premature
** destruction both show up here rather than as a fault somewhere later.
*/
static int LiveObjects = 0;


class TestObject : public ITestObject, public ITestExtra
{
	public:
		TestObject(void) : RefCount(0) { LiveObjects++; }
		virtual ~TestObject(void) { LiveObjects--; }

		virtual HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void ** object) override
		{
			if (object == nullptr) {
				return(E_POINTER);
			}

			*object = nullptr;

			if (IsEqualIID(riid, IID_IUnknown)) {
				*object = (void *)(IUnknown *)(ITestObject *)this;
			} else if (IsEqualIID(riid, IID_ITestObject)) {
				*object = (void *)(ITestObject *)this;
			} else if (IsEqualIID(riid, IID_ITestExtra)) {
				*object = (void *)(ITestExtra *)this;
			}

			if (*object == nullptr) {
				return(E_NOINTERFACE);
			}

			AddRef();
			return(S_OK);
		}

		virtual ULONG STDMETHODCALLTYPE AddRef(void) override
		{
			RefCount++;
			return((ULONG)RefCount);
		}

		virtual ULONG STDMETHODCALLTYPE Release(void) override
		{
			RefCount--;

			long count = RefCount;
			if (count == 0) {
				delete this;
			}
			return((ULONG)count);
		}

		virtual int STDMETHODCALLTYPE Identify(void) override { return(0x0BEC7); }
		virtual int STDMETHODCALLTYPE Extra(void) override { return(0xE7A); }

		long Count(void) const { return(RefCount); }

	private:
		long RefCount;
};


class TestFactory : public IClassFactory
{
	public:
		TestFactory(void) : RefCount(1), Locks(0) {}
		virtual ~TestFactory(void) {}

		virtual HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void ** object) override
		{
			if (object == nullptr) {
				return(E_POINTER);
			}

			*object = nullptr;

			if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IClassFactory)) {
				*object = (void *)(IClassFactory *)this;
			}

			if (*object == nullptr) {
				return(E_NOINTERFACE);
			}

			AddRef();
			return(S_OK);
		}

		virtual ULONG STDMETHODCALLTYPE AddRef(void) override
		{
			RefCount++;
			return((ULONG)RefCount);
		}

		virtual ULONG STDMETHODCALLTYPE Release(void) override
		{
			RefCount--;

			long count = RefCount;
			if (count == 0) {
				delete this;
			}
			return((ULONG)count);
		}

		virtual HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown * outer, REFIID riid, void ** object) override
		{
			if (object == nullptr) {
				return(E_POINTER);
			}

			*object = nullptr;

			if (outer != nullptr) {
				return(CLASS_E_NOAGGREGATION);
			}

			TestObject * made = new TestObject();

			HRESULT result = made->QueryInterface(riid, object);
			if (FAILED(result)) {
				delete made;
			}
			return(result);
		}

		virtual HRESULT STDMETHODCALLTYPE LockServer(BOOL lock) override
		{
			Locks += (lock != FALSE) ? 1 : -1;
			return(S_OK);
		}

		long Count(void) const { return(RefCount); }
		long Lock_Count(void) const { return(Locks); }

	private:
		long RefCount;
		long Locks;
};


_COM_SMARTPTR_TYPEDEF(ITestObject, IID_ITestObject);


/*
** Activation resolves the class identifier to the registered factory and hands back the
** interface that was asked for, carrying one reference.
*/
static void Test_Create_Instance(void)
{
	ITestObject * object = nullptr;
	HRESULT result = CoCreateInstance(CLSID_TestObject, NULL, CLSCTX_INPROC_SERVER,
		IID_ITestObject, (void **)&object);

	Check_Result("CoCreateInstance for a registered class succeeds", result, S_OK);
	Check("CoCreateInstance yields an object", object != nullptr);
	if (object == nullptr) return;

	Check_Count("a new object carries one reference", (unsigned long)((TestObject *)object)->Count(), 1);
	Check_Count("one object is alive", (unsigned long)LiveObjects, 1);
	Check("the interface handed back is the one asked for", object->Identify() == 0x0BEC7);

	ITestExtra * extra = nullptr;
	result = object->QueryInterface(IID_ITestExtra, (void **)&extra);
	Check_Result("QueryInterface reaches the second interface", result, S_OK);
	Check("QueryInterface yields the second interface", extra != nullptr);
	Check_Count("QueryInterface adds a reference", (unsigned long)((TestObject *)object)->Count(), 2);

	if (extra != nullptr) {
		Check("the second interface answers on its own vtable", extra->Extra() == 0xE7A);
		Check("a separately inherited interface has its own address",
			(void *)extra != (void *)object);
	}

	IUnknown * unknown = nullptr;
	result = object->QueryInterface(IID_IUnknown, (void **)&unknown);
	Check_Result("QueryInterface reaches IUnknown", result, S_OK);
	Check("IUnknown is the identity the object was created through", (void *)unknown == (void *)object);
	if (unknown != nullptr) unknown->Release();

	void * absent = (void *)(intptr_t)-1;
	result = object->QueryInterface(IID_ITestAbsent, &absent);
	Check_Result("an interface the object lacks is refused", result, E_NOINTERFACE);
	Check("a refused QueryInterface clears the pointer it was given", absent == nullptr);

	if (extra != nullptr) extra->Release();
	Check_Count("releasing the second interface drops one reference",
		(unsigned long)((TestObject *)object)->Count(), 1);

	object->Release();
	Check_Count("the last release destroys the object", (unsigned long)LiveObjects, 0);
}


/*
** An interface the class does not implement fails the activation, and the object the
** factory built along the way does not survive it.
*/
static void Test_Unsupported_Interface(void)
{
	void * object = (void *)(intptr_t)-1;
	HRESULT result = CoCreateInstance(CLSID_TestObject, NULL, CLSCTX_INPROC_SERVER,
		IID_ITestAbsent, &object);

	Check_Result("activating an interface the class lacks fails", result, E_NOINTERFACE);
	Check("a failed activation clears the pointer it was given", object == nullptr);
	Check_Count("a failed activation leaves no object behind", (unsigned long)LiveObjects, 0);
}


/*
** A class identifier nobody registered fails with the code Windows uses for it, rather
** than succeeding with nothing in the pointer.
*/
static void Test_Unregistered_Class(void)
{
	void * object = (void *)(intptr_t)-1;
	HRESULT result = CoCreateInstance(CLSID_TestAbsent, NULL, CLSCTX_INPROC_SERVER,
		IID_ITestObject, &object);

	Check_Result("an unregistered class identifier is refused", result, REGDB_E_CLASSNOTREG);
	Check("a refused activation clears the pointer it was given", object == nullptr);

	object = (void *)(intptr_t)-1;
	result = CoGetClassObject(CLSID_TestAbsent, CLSCTX_INPROC_SERVER, NULL,
		IID_IClassFactory, &object);

	Check_Result("an unregistered class has no class object", result, REGDB_E_CLASSNOTREG);
	Check("a refused CoGetClassObject clears the pointer it was given", object == nullptr);
}


/*
** The class object itself is reachable through the same table, and the reference it hands
** out is the caller's to give back.
*/
static void Test_Class_Object(TestFactory * factory)
{
	long before = factory->Count();

	IClassFactory * fetched = nullptr;
	HRESULT result = CoGetClassObject(CLSID_TestObject, CLSCTX_INPROC_SERVER, NULL,
		IID_IClassFactory, (void **)&fetched);

	Check_Result("CoGetClassObject reaches the registered factory", result, S_OK);
	Check("CoGetClassObject yields the factory that was registered", fetched == (IClassFactory *)factory);
	Check("CoGetClassObject hands out a reference", factory->Count() > before);

	if (fetched != nullptr) fetched->Release();
	Check_Count("releasing the class object gives its reference back",
		(unsigned long)factory->Count(), (unsigned long)before);
}


/*
** The smart pointer construction that unit.cpp uses to attach a locomotor: activation by
** class identifier, through a pointer that knows which interface it wants.
*/
static void Test_Smart_Pointer(void)
{
	{
		ITestObjectPtr pointer(CLSID_TestObject);

		Check("the smart pointer activated an object", !!pointer);
		Check_Count("the smart pointer holds one reference",
			(unsigned long)((TestObject *)pointer.GetInterfacePtr())->Count(), 1);
		Check_Count("the smart pointer's object is alive", (unsigned long)LiveObjects, 1);
		if (!!pointer) {
			Check("the smart pointer holds the interface it was declared for",
				pointer->Identify() == 0x0BEC7);
		}
	}

	Check_Count("the smart pointer released its object", (unsigned long)LiveObjects, 0);
}


int main(void)
{
	HRESULT result = CoInitialize(NULL);
	Check("CoInitialize succeeds", SUCCEEDED(result));

	TestFactory * factory = new TestFactory();
	long baseline = factory->Count();

	DWORD registration = 0;
	result = CoRegisterClassObject(CLSID_TestObject, (IUnknown *)(IClassFactory *)factory,
		CLSCTX_INPROC_SERVER, REGCLS_MULTIPLEUSE, &registration);

	Check_Result("CoRegisterClassObject succeeds", result, S_OK);
	Check("CoRegisterClassObject yields a registration", registration != 0);
	Check("registration takes a reference on the class object", factory->Count() > baseline);

	Test_Create_Instance();
	Test_Unsupported_Interface();
	Test_Unregistered_Class();
	Test_Class_Object(factory);
	Test_Smart_Pointer();

	result = CoRevokeClassObject(registration);
	Check_Result("CoRevokeClassObject succeeds", result, S_OK);
	Check_Count("revoking gives the class object's reference back",
		(unsigned long)factory->Count(), (unsigned long)baseline);

	Check("revoking an unknown registration fails", FAILED(CoRevokeClassObject(registration)));

	void * object = (void *)(intptr_t)-1;
	result = CoCreateInstance(CLSID_TestObject, NULL, CLSCTX_INPROC_SERVER,
		IID_ITestObject, &object);
	Check_Result("a revoked class can no longer be activated", result, REGDB_E_CLASSNOTREG);
	Check("a refused activation clears the pointer it was given", object == nullptr);

	factory->Release();
	Check_Count("no object outlives the run", (unsigned long)LiveObjects, 0);

	CoUninitialize();

	printf("%d checks, %d failures\n", Checks, Failures);
	return(Failures == 0 ? 0 : 1);
}
