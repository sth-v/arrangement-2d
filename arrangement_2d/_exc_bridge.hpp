// arrangement_2d — C++ -> Python exception translation for the Cython layer.
//
// `arr2d_translate_exception()` is used as the `except +<handler>` function of every C++
// declaration in `_core.pxd`.  Cython calls it from inside a `catch (...)` block, with the
// GIL held and the exception still in flight, so the implementation simply rethrows and
// dispatches on the concrete type.
//
// It maps
//   arr2d::Error                          -> arrangement_2d.errors.* (by ErrorCode)
//   CGAL::Precondition_exception          -> PreconditionError
//   CGAL::Postcondition_exception         -> PostconditionError
//   CGAL::Assertion_exception             -> CGALAssertionError
//   CGAL::Warning_exception               -> CGALWarning
//   any other CGAL::Failure_exception     -> CGALError
//   CGAL::Uncertain_conversion_exception  -> CGALError          (a std::range_error!)
//   std::bad_alloc                        -> MemoryError
//   std::invalid_argument                 -> ValueError
//   std::out_of_range                     -> IndexError
//   any other std::exception              -> RuntimeError(what())
//   anything else                         -> RuntimeError
//
// The arrangement_2d.errors classes are imported lazily (the extension module cannot be
// imported while it is still initialising) and cached for the life of the process.  If the
// import fails for any reason we fall back to plain builtins so that an error is never lost.
//
// `arr2d_install_cgal_handlers()` installs silent CGAL error/warning handlers.  CGAL 6.1's
// default handler prints the whole "CGAL error: ... violation!" block to std::cerr *and then*
// throws (the "skip printing when throwing" shortcut is guarded by
// `#if defined(__GNUG__) && !defined(__llvm__)`, so it does not apply to Apple clang) —
// see docs/dev/cgal61_api/number_types_and_errors.md gotcha 5.  Silencing the handler does
// not disable the exception: the behaviour stays THROW_EXCEPTION.
#pragma once

#include <Python.h>

#include <exception>
#include <new>
#include <stdexcept>
#include <string>

#include <CGAL/assertions.h>
#include <CGAL/assertions_behaviour.h>
#include <CGAL/exceptions.h>
#include <CGAL/Uncertain.h>

#include "arr2d/common.hpp"

namespace arr2d_py {

// ---------------------------------------------------------------------------
// Cached exception classes
// ---------------------------------------------------------------------------

struct ErrorClasses {
  bool tried = false;  ///< the import was attempted (successfully or not)
  PyObject* cgal_error = nullptr;
  PyObject* precondition = nullptr;
  PyObject* postcondition = nullptr;
  PyObject* assertion = nullptr;
  PyObject* warning = nullptr;
  PyObject* invalid_handle = nullptr;
  PyObject* kind_mismatch = nullptr;
  PyObject* not_x_monotone = nullptr;
  PyObject* not_representable = nullptr;
  PyObject* unsupported = nullptr;
  PyObject* callback = nullptr;
};

inline ErrorClasses& arr2d_error_classes() {
  static ErrorClasses classes;
  return classes;
}

/// Import arrangement_2d.errors once and cache the class objects (strong references kept
/// for the life of the interpreter — they are module-level singletons anyway).
/// Never leaves a Python error set: a failed import degrades to the builtin fallbacks.
inline void arr2d_load_error_classes() {
  ErrorClasses& c = arr2d_error_classes();
  if (c.tried) return;
  c.tried = true;

  // Preserve any exception that may already be set (we are called from a catch block).
  PyObject *saved_type = nullptr, *saved_value = nullptr, *saved_tb = nullptr;
  PyErr_Fetch(&saved_type, &saved_value, &saved_tb);

  PyObject* mod = PyImport_ImportModule("arrangement_2d.errors");
  if (mod != nullptr) {
    struct Slot { const char* name; PyObject** dst; };
    const Slot slots[] = {
        {"CGALError", &c.cgal_error},
        {"PreconditionError", &c.precondition},
        {"PostconditionError", &c.postcondition},
        {"CGALAssertionError", &c.assertion},
        {"CGALWarning", &c.warning},
        {"InvalidHandleError", &c.invalid_handle},
        {"KindMismatchError", &c.kind_mismatch},
        {"NotXMonotoneError", &c.not_x_monotone},
        {"NotRepresentableError", &c.not_representable},
        {"UnsupportedError", &c.unsupported},
        {"CallbackError", &c.callback},
    };
    for (const Slot& s : slots) {
      PyObject* cls = PyObject_GetAttrString(mod, s.name);
      if (cls == nullptr) {
        PyErr_Clear();
        continue;
      }
      *s.dst = cls;  // keep the reference for the life of the process
    }
    Py_DECREF(mod);
  } else {
    PyErr_Clear();
  }

  PyErr_Restore(saved_type, saved_value, saved_tb);
}

/// `cls` if the lazy import produced it, else `fallback` (a borrowed builtin).
inline PyObject* arr2d_class_or(PyObject* cls, PyObject* fallback) {
  return (cls != nullptr) ? cls : fallback;
}

inline void arr2d_set_error(PyObject* cls, const std::string& msg) {
  PyErr_SetString(cls, msg.c_str());
}

/// "CGAL <kind>: <expression> [<library>] (<file>:<line>) <message>"
/// Accessor names are the CGAL 6.1 ones: `message()`, NOT `explanation()`
/// (docs/dev/cgal61_api/number_types_and_errors.md §8.1).
inline std::string arr2d_cgal_message(const char* kind, const CGAL::Failure_exception& e) {
  std::string expr = e.expression();
  std::string msg = e.message();
  std::string out = "CGAL ";
  out += kind;
  out += ": ";
  out += expr.empty() ? std::string("<no expression>") : expr;
  out += " [";
  out += e.library();
  out += "] (";
  out += e.filename();
  out += ":";
  out += std::to_string(e.line_number());
  out += ")";
  if (!msg.empty()) {
    out += " ";
    out += msg;
  }
  return out;
}

}  // namespace arr2d_py

// ---------------------------------------------------------------------------
// The `except +` handler
// ---------------------------------------------------------------------------

/// Rethrow the in-flight C++ exception and set the matching Python exception.
/// Must be called with the GIL held (Cython always does).
inline void arr2d_translate_exception() {
  arr2d_py::arr2d_load_error_classes();
  const arr2d_py::ErrorClasses& c = arr2d_py::arr2d_error_classes();

  try {
    throw;
  }

  // ---- our own core errors -------------------------------------------------
  catch (const arr2d::Error& e) {
    const std::string msg = e.what();
    switch (e.code) {
      case arr2d::ErrorCode::KindMismatch:
        arr2d_py::arr2d_set_error(arr2d_py::arr2d_class_or(c.kind_mismatch, PyExc_TypeError), msg);
        return;
      case arr2d::ErrorCode::InvalidHandle:
        arr2d_py::arr2d_set_error(arr2d_py::arr2d_class_or(c.invalid_handle, PyExc_ValueError), msg);
        return;
      case arr2d::ErrorCode::NotXMonotone:
        arr2d_py::arr2d_set_error(arr2d_py::arr2d_class_or(c.not_x_monotone, PyExc_ValueError), msg);
        return;
      case arr2d::ErrorCode::NotRepresentable:
        arr2d_py::arr2d_set_error(arr2d_py::arr2d_class_or(c.not_representable, PyExc_ValueError), msg);
        return;
      case arr2d::ErrorCode::Unsupported:
        arr2d_py::arr2d_set_error(arr2d_py::arr2d_class_or(c.unsupported, PyExc_NotImplementedError), msg);
        return;
      case arr2d::ErrorCode::InvalidArgument:
        arr2d_py::arr2d_set_error(PyExc_ValueError, msg);
        return;
      case arr2d::ErrorCode::CallbackFailed:
        // A Python callback raised; the Cython layer normally re-raises the recorded
        // exception itself.  Only set CallbackError when nothing is pending.
        if (!PyErr_Occurred())
          arr2d_py::arr2d_set_error(arr2d_py::arr2d_class_or(c.callback, PyExc_RuntimeError), msg);
        return;
      case arr2d::ErrorCode::Generic:
      default:
        arr2d_py::arr2d_set_error(arr2d_py::arr2d_class_or(c.cgal_error, PyExc_RuntimeError), msg);
        return;
    }
  }

  // ---- CGAL failures (most derived first: they all derive from Failure_exception) ----
  catch (const CGAL::Precondition_exception& e) {
    arr2d_py::arr2d_set_error(arr2d_py::arr2d_class_or(c.precondition, PyExc_ValueError),
                              arr2d_py::arr2d_cgal_message("precondition violation", e));
    return;
  } catch (const CGAL::Postcondition_exception& e) {
    arr2d_py::arr2d_set_error(arr2d_py::arr2d_class_or(c.postcondition, PyExc_RuntimeError),
                              arr2d_py::arr2d_cgal_message("postcondition violation", e));
    return;
  } catch (const CGAL::Assertion_exception& e) {
    arr2d_py::arr2d_set_error(arr2d_py::arr2d_class_or(c.assertion, PyExc_RuntimeError),
                              arr2d_py::arr2d_cgal_message("assertion violation", e));
    return;
  } catch (const CGAL::Warning_exception& e) {
    arr2d_py::arr2d_set_error(arr2d_py::arr2d_class_or(c.warning, PyExc_RuntimeError),
                              arr2d_py::arr2d_cgal_message("warning condition failed", e));
    return;
  } catch (const CGAL::Failure_exception& e) {
    // CGAL::Error_exception (CGAL_error / CGAL_error_msg) and CGAL::Test_exception.
    arr2d_py::arr2d_set_error(arr2d_py::arr2d_class_or(c.cgal_error, PyExc_RuntimeError),
                              arr2d_py::arr2d_cgal_message("failure", e));
    return;
  }

  // ---- filtered predicates that could not decide -------------------------------
  // CGAL::Uncertain_conversion_exception derives from std::range_error (NOT from
  // Failure_exception), so it must be caught before the std::exception fallbacks
  // (docs/dev/cgal61_api/number_types_and_errors.md gotcha, §"Uncertain").
  catch (const CGAL::Uncertain_conversion_exception& e) {
    std::string msg = "uncertain predicate: a filtered CGAL predicate could not be decided (";
    msg += e.what();
    msg += ")";
    arr2d_py::arr2d_set_error(arr2d_py::arr2d_class_or(c.cgal_error, PyExc_RuntimeError), msg);
    return;
  }

  // ---- standard library --------------------------------------------------------
  catch (const std::bad_alloc&) {
    PyErr_NoMemory();
    return;
  } catch (const std::invalid_argument& e) {
    PyErr_SetString(PyExc_ValueError, e.what());
    return;
  } catch (const std::out_of_range& e) {
    PyErr_SetString(PyExc_IndexError, e.what());
    return;
  } catch (const std::exception& e) {
    PyErr_SetString(PyExc_RuntimeError, e.what());
    return;
  } catch (...) {
    PyErr_SetString(PyExc_RuntimeError, "unknown C++ exception");
    return;
  }
}

// ---------------------------------------------------------------------------
// CGAL error/warning handlers
// ---------------------------------------------------------------------------

/// No-op failure handler: CGAL still throws, it just stops printing to std::cerr.
inline void arr2d_silent_cgal_handler(const char* /*what*/, const char* /*expr*/,
                                      const char* /*file*/, int /*line*/, const char* /*msg*/) {}

/// Called once from the Cython module's import-time initialisation.
inline void arr2d_install_cgal_handlers() {
  // Both are already the CGAL 6.1 defaults; set them explicitly so a host application that
  // changed them cannot turn a CGAL precondition into abort() inside our extension.
  CGAL::set_error_behaviour(CGAL::THROW_EXCEPTION);
  CGAL::set_warning_behaviour(CGAL::CONTINUE);
  CGAL::set_error_handler(&arr2d_silent_cgal_handler);
  CGAL::set_warning_handler(&arr2d_silent_cgal_handler);
}
