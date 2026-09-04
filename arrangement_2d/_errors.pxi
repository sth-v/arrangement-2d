# -*- coding: utf-8 -*-
# Part of arrangement_2d._core (textual include; see _core.pyx).
#
# Re-imports the exception classes of arrangement_2d.errors into the extension module's
# namespace so that `arrangement_2d._core.PreconditionError` works and the other .pxi
# parts can raise them by bare name.
#
# The classes live in a pure-Python module because the C++ translation layer
# (_exc_bridge.hpp, arr2d_translate_exception) imports them with PyImport_ImportModule
# while THIS module is still initialising -- a compiled-in class object would not be
# reachable at that point.

from arrangement_2d.errors import (
    CGALError,
    PreconditionError,
    PostconditionError,
    CGALAssertionError,
    CGALWarning,
    InvalidHandleError,
    KindMismatchError,
    NotXMonotoneError,
    NotRepresentableError,
    UnsupportedError,
    CallbackError,
)

# Force the lazy import inside _exc_bridge.hpp to happen now (and to succeed), so that a
# failure to translate exceptions shows up at import time rather than at the first error.
# Raising and translating a cheap arr2d::Error is the only way to trigger it.
cdef _prime_exception_translation():
    try:
        # ops() on a kind that is never registered throws Error(Unsupported).
        ops(Kind.NumKinds)
    except BaseException:
        pass

_prime_exception_translation()
