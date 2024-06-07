#include <cassert>
#include <cstdio>
#include <exception>
#include <typeinfo>
#include <utility>

#include <unwind.h>

// Forward declare some cxxabi internals.
namespace __cxxabiv1 {
extern "C" {

// Lazily initializes global/thread local exception state.
// Returns a pointer to the global state.
// Normally returns __cxa_eh_globals*, but substituting void* is ok for us.
void* __cxa_get_globals();

// Returns a pointer to the global state without initializing it.
// Normally returns __cxa_eh_globals*, but substituting void* is ok for us.
void* __cxa_get_globals_fast();

} // end extern "C"
} // end namespace __cxxabiv1

// Custom ID used to identify exceptions from this project.
// It must be different from the IDs of other projects (language runtimes, etc.)
// TODO: Update the ID to your own.
static const uint64_t my_exception_class = 0x0123456789ABCDEFull;

// Base header for custom exceptions.
struct base_exception {
  // Initialize the header.
  explicit base_exception(const std::type_info* ty) : ty(ty) {
    header.exception_class = my_exception_class;
  }

  // Header used to speak to unwind and libcxxabi.
  _Unwind_Exception header{};  //< Zero-init, for private fields.

  // Type information to know which exception we threw.
  // TODO: Consider using your own RTTI instead of std.
  const std::type_info* ty;
};

// Wraps a type with a header as an interface for unwind and libcxxabi.
template<class T>
struct wrapped_exception : base_exception {
  // Create a wrapped exception.
  // Passes arguments to the exception this wraps.
  template<class... Args>
  explicit wrapped_exception(Args&&... args) :
    base_exception(&typeid(T)),
    body(std::forward<Args>(args)...) { }

  // Our own exception-specific data.
  T body;
};

// Throw an exception of type T.
// The object of type T is constructed using args.
template<class T, class... Args>
void my_throw(Args&&... args) {
  // Check that the thread has initialized its global exception state.
  assert(__cxxabiv1::__cxa_get_globals_fast() && "Initialize cxa globals before throwing.");

  // Allocate the exception. 
  // TODO: Customize the memory allocation.
  wrapped_exception<T>* wrapped = new wrapped_exception<T>(std::forward<Args>(args)...);

  // Set the cleanup callback.
  // Called automatically at the end of the catch block if there is no rethrow.
  wrapped->header.exception_cleanup = [](_Unwind_Reason_Code reason, _Unwind_Exception *exc) {
    // TODO: Consider handling "reason"
    (void)reason;

    // Log the cleanup to confirm it happened.
    printf("Deleting %p\n", exc);

    // Deallocate the exception.
    // TODO: Customize the memory deallocation.
    wrapped_exception<T>* wrapped = reinterpret_cast<wrapped_exception<T>*>(exc);
    delete wrapped;
  };

  // Throw the exception we prepared, using its header as a base address.
  printf("Throwing %p\n", wrapped);
  _Unwind_Reason_Code fail_reason = _Unwind_RaiseException(&wrapped->header);

  // Normally RaiseException does not return.
  // If it returns, that means that it failed.
  // TODO: Fail more gracefully.
  printf("Failed to throw (reason = %d)\n", fail_reason);
  std::terminate();
}

// Call this inside a catch block to catch a custom exception.
// Returns true if we found one of our custom exceptions.
// Returns false if the exception was not ours.
bool my_catch(const std::type_info** ty, base_exception** caught) {
  // Get the global exception state.
  void* globals = __cxxabiv1::__cxa_get_globals_fast();

  // Check for misuse.
  assert(globals && "Must call from the catch block (get_globals failed)");

  // Get the first member of the globals, "caughtExceptions".
  // It's a pointer to the currently caught exception.
  void* caughtExceptions = *reinterpret_cast<void**>(globals);

  // Check for misuse.
  assert(caughtExceptions && "Must call from the catch block (no caught exception)");

  // The last field of __cxa_exception contains the unwind header.
  // Compute the address of this last field.
  //
  // Note: We never allocated an object of type __cxa_exception.
  //       It still works because libcxxabi adjusts the address of
  //       foreign exceptions the same way as native exceptions,
  //       so the address calculation is always the same.

  // First, calculate the adddress of the end of the __cxa_exception.
  static const size_t sizeof_libcxx_cxa_exception = 0x80;  //< Depends on the ABI.
  void* exception_end = static_cast<char*>(caughtExceptions) + sizeof_libcxx_cxa_exception;

  // Go back by sizeof(_Unwind_Exception) bytes to find the start of the header.
  _Unwind_Exception* header = static_cast<_Unwind_Exception*>(exception_end) - 1;

  // Check if this is one of our exceptions.
  if (header->exception_class != my_exception_class) {
    // This was not one of our exceptions, so fail.
    return false;
  }

  // It's one of our exceptions, so cast it to our type.
  base_exception* base = reinterpret_cast<base_exception*>(header);

  // Success!
  // Let the caller decide what to do with it.
  *ty = base->ty;
  *caught = base;
  return true;
}

// Exception to test the custom throw function.
struct test_exception {
  // A message that explains the error.
  const char* what;

  // Initialize with a message that explains the error.
  explicit test_exception(const char* msg) : what(msg) {}
};

int main() {
  // Initialize global and thread local variables to manage exceptions.
  // Normally this is called automatically by __cxa_throw when you first throw.
  // However, we are throwing with custom code, so we should initialize it ourselves.
  // 
  // NOTE: Because this uses thread local variables, you must call it once on every thread.
  //
  // TODO: This function allocates global and thread local memory.
  //       You may want to customize those allocations too.
  //       If so, you should also consider handling the cleanup at thread and program exit.
  __cxxabiv1::__cxa_get_globals();

  try {
    // Test throwing a custom exception with a message.
    my_throw<test_exception>("You caught me!");
  } catch (...) {
    // Try to catch a custom exception.
    const std::type_info* ty;
    base_exception* e;
    if (!my_catch(&ty, &e)) {
      // Did not catch a custom exception.
      // Rethrow it to the next level up.
      throw;
    }

    // Match the exception's type.
    if (*ty == typeid(test_exception)) {
      // Found a match!
      auto* wrapped = static_cast<wrapped_exception<test_exception>*>(e);
      printf("Success: \"%s\"\n", wrapped->body.what);
    } else {
      // Unhandled type.
      // Rethrow it to the next level up.
      throw;
    }
  }
}
