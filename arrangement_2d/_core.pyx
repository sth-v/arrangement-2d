# distutils: language = c++
# cython: language_level=3
"""Cython binding of the type-erased ``arr2d`` C++ core (CGAL 6.1 2D Arrangements).

This module is built from ``_core.pyx`` plus five textual includes:

``_errors.pxi``
    re-imports the exception classes of :mod:`arrangement_2d.errors`.
``_numbers.pxi``
    exact numbers: rational conversion helpers, :class:`SqrtExtension`, :class:`Algebraic`.
``_geometry.pxi``
    points, curves and polygons for every geometry kind.
``_arrangement.pxi``
    :class:`Arrangement` and its DCEL handles, observers, overlay, point location.
``_polygon_set.pxi``
    :class:`PolygonSet` and the Boolean set operations.

Everything the C++ core exposes is declared in ``_core.pxd``; the declaration file is
applied automatically to this implementation file, so its names (``Geom``, ``Kind``,
``ArrBase``, ``ops``, ...) are visible here and in every ``.pxi`` include.

The public API is re-exported by :mod:`arrangement_2d`; import that instead.
"""

import decimal
import enum
import fractions
import numbers
import weakref

from libcpp.utility cimport move
from libc.string cimport strlen
from cpython.bytes cimport PyBytes_FromStringAndSize


# ---------------------------------------------------------------------------
# Extra entry points of our own C++ shim (the .pxd only declares the exception
# translator, which is used as the `except +` handler of every core function).
# ---------------------------------------------------------------------------
cdef extern from "_exc_bridge.hpp":
    # Installs silent CGAL error/warning handlers. CGAL 6.1's default handler prints the
    # whole "CGAL error: ... violation!" block to std::cerr *before* throwing (the
    # skip-printing shortcut is guarded by `#if defined(__GNUG__) && !defined(__llvm__)`,
    # so it never applies to Apple clang) -- number_types_and_errors.md gotcha 5.
    # Silencing the handler does not disable the exception; behaviour stays THROW_EXCEPTION.
    void arr2d_install_cgal_handlers()
    void arr2d_translate_exception()


__version__ = "0.1.0"


# ---------------------------------------------------------------------------
# Small string helpers (independent of the c_string_type/c_string_encoding
# compiler directives, so this file cythonizes the same way with or without them)
# ---------------------------------------------------------------------------

cdef inline object _from_cstr(const char* s):
    """``const char*`` (NUL terminated, UTF-8) -> :class:`str` (``None`` for NULL)."""
    if s == NULL:
        return None
    return PyBytes_FromStringAndSize(s, <Py_ssize_t>strlen(s)).decode("utf-8")


cdef inline object _from_string(const string& s):
    """``std::string`` (UTF-8) -> :class:`str`."""
    return PyBytes_FromStringAndSize(s.data(), <Py_ssize_t>s.size()).decode("utf-8")


cdef inline bytes _to_bytes(object s):
    """:class:`str` / :class:`bytes` -> ``bytes`` (for ``const std::string&`` arguments)."""
    if isinstance(s, bytes):
        return <bytes>s
    return (<str>s).encode("utf-8")


# ---------------------------------------------------------------------------
# Kind
# ---------------------------------------------------------------------------
#
# NOTE (naming workaround): `_core.pxd` declares the C++ scoped enum `arr2d::Kind` under
# the Cython name `Kind`, and that declaration file is applied automatically to this
# implementation file. A module-level `class Kind(enum.IntEnum)` would therefore be a
# redeclaration ("'Kind' is not a constant, variable or function identifier"). We define
# the Python enum under a private Cython name and publish it in the module namespace as
# "Kind" -- so `arrangement_2d._core.Kind` is the IntEnum, while `Kind` inside Cython code
# keeps meaning the C++ enum type (which is what `cdef Kind` declarations need).

class _KindEnum(enum.IntEnum):
    """Geometry kind of a point, curve, arrangement or polygon set.

    Each kind is one CGAL traits instantiation; geometry of different kinds cannot be
    mixed without an explicit (and possibly lossy) conversion.

    ==================  ====================================================
    ``SEGMENT``         line segments, ``Arr_segment_traits_2<Epeck>``
    ``LINEAR``          segments, rays and lines (unbounded planar topology)
    ``CIRCLE_SEGMENT``  circular arcs and line segments
    ``POLYLINE``        polylines of segments
    ``BEZIER``          polynomial Bezier curves (rational control points)
    ``CONIC``           conic arcs (ellipse / parabola / hyperbola / segment)
    ``SPHERE``          geodesic arcs on the unit sphere
    ==================  ====================================================

    Every API that takes a kind also accepts its name (``"segment"``, ``"circle"``,
    ``"geodesic"``, ...), its integer value, or any geometry object with a ``.kind``.
    """

    SEGMENT = 0
    LINEAR = 1
    CIRCLE_SEGMENT = 2
    POLYLINE = 3
    BEZIER = 4
    CONIC = 5
    SPHERE = 6

    def __str__(self):
        return self.name.lower()


_KindEnum.__name__ = "Kind"
_KindEnum.__qualname__ = "Kind"
_KindEnum.__module__ = "arrangement_2d"
globals()["Kind"] = _KindEnum


cdef tuple _make_kind_names():
    cdef list names = []
    cdef int i
    for i in range(<int>Kind.NumKinds):
        names.append(_from_cstr(kind_name(<Kind>i)))
    return tuple(names)


cdef tuple _KIND_NAMES = _make_kind_names()


cdef inline object _kind_enum(Kind k):
    """C++ ``arr2d::Kind`` -> the Python ``Kind`` enum member.

    ``_geometry.pxi`` defines an equivalent ``_pykind()``; this one is the copy used by
    ``_core.pyx`` / ``_arrangement.pxi`` so that neither file depends on the other's
    private helpers (two definitions of one cdef name would be a C++ redefinition).
    """
    return _KindEnum(<int>k)


cdef inline object _kind_name(Kind k):
    """C++ ``arr2d::Kind`` -> its canonical lowercase name."""
    return _KIND_NAMES[<int>k]


cdef Kind _ckind(object k) except *:
    """Any kind-like Python object -> the C++ ``arr2d::Kind``.

    Accepts a :class:`Kind` member, an ``int`` in ``range(7)``, a name (any alias
    understood by ``arr2d::kind_from_name``: ``"segment"``, ``"seg"``, ``"lines"``,
    ``"circle"``, ``"arc"``, ``"polylines"``, ``"beziers"``, ``"conics"``,
    ``"geodesic"``, ...) or any object exposing a ``.kind`` attribute (a point, curve,
    polygon, arrangement or polygon set).

    Raises :class:`ValueError` for an unknown name/value and :class:`TypeError`
    for an object that is not kind-like at all.
    """
    cdef int i
    if isinstance(k, _KindEnum):
        return <Kind>(<int>(<object>k))
    if isinstance(k, str):
        i = kind_from_name(_to_bytes((<str>k).strip().lower()))
        if i < 0:
            raise ValueError(
                "unknown geometry kind %r; valid names are %s"
                % (k, ", ".join(repr(n) for n in _KIND_NAMES))
            )
        return <Kind>i
    if isinstance(k, bytes):
        return _ckind((<bytes>k).decode("utf-8"))
    if isinstance(k, (int, numbers.Integral)) and not isinstance(k, bool):
        i = <int>k
        if i < 0 or i >= <int>Kind.NumKinds:
            raise ValueError(
                "unknown geometry kind %r; valid values are 0..%d"
                % (k, <int>Kind.NumKinds - 1)
            )
        return <Kind>i
    sub = getattr(k, "kind", None)
    if sub is not None and sub is not k:
        return _ckind(sub)
    raise TypeError(
        "expected a Kind, a kind name (%s) or an object with a .kind, got %r"
        % (", ".join(repr(n) for n in _KIND_NAMES), type(k).__name__)
    )


# ---------------------------------------------------------------------------
# Python object lifetime hooks used by arr2d::PyRef (DCEL element .data)
# ---------------------------------------------------------------------------
#
# The C++ core never includes Python.h; it increfs/decrefs the opaque `void*` it stores
# on DCEL elements through these two function pointers. The core only ever calls them
# while the GIL is held (it is only reachable from this module), so they are declared
# `nogil` -- that makes them usable from any context without Cython inserting a
# GIL acquisition of its own.

cdef extern from "Python.h" nogil:
    void _arr2d_Py_XINCREF "Py_XINCREF" (void* o)
    void _arr2d_Py_XDECREF "Py_XDECREF" (void* o)


cdef void _incref(void* o) noexcept nogil:
    _arr2d_Py_XINCREF(o)


cdef void _decref(void* o) noexcept nogil:
    _arr2d_Py_XDECREF(o)


# ---------------------------------------------------------------------------
# Module initialisation
# ---------------------------------------------------------------------------

set_pyobject_hooks(<PyObjectHook>_incref, <PyObjectHook>_decref)
arr2d_install_cgal_handlers()
init_all_kinds()


# ---------------------------------------------------------------------------
# numpy is optional: bulk exports fall back to plain lists without it
# ---------------------------------------------------------------------------

try:
    import numpy as _np
except ImportError:                                  # pragma: no cover - numpy is optional
    _np = None


# ---------------------------------------------------------------------------
# Module level introspection helpers
#
# `cgal_version`, `build_info` and `kind_available` are also the names of C functions
# declared in `_core.pxd`; a Python `def` of the same name would shadow them and make the
# C ones uncallable. They are therefore defined under private names and published into
# the module namespace (same trick as `Kind` above).
# ---------------------------------------------------------------------------

def _py_cgal_version():
    """Return the CGAL version this extension was compiled against, e.g. ``"6.1"``.

    :rtype: str
    """
    cdef string s = cgal_version()
    return _from_string(s)


def _py_build_info():
    """Return a one-line summary of the C++ build (CGAL version, exact number type, assertions).

    :rtype: str
    """
    cdef string s = build_info()
    return _from_string(s)


def _py_kind_available(kind):
    """Return whether *kind* was compiled into this build.

    :param kind: a :class:`Kind`, kind name, integer, or any object with a ``.kind``.
    :rtype: bool
    """
    return bool(kind_available(_ckind(kind)))


def available_kinds():
    """Return the tuple of :class:`Kind` members compiled into this build.

    :rtype: tuple[Kind, ...]
    """
    cdef list out = []
    cdef int i
    for i in range(<int>Kind.NumKinds):
        if kind_available(<Kind>i):
            out.append(_KindEnum(i))
    return tuple(out)


def point_location_strategies():
    """Return the names of the supported point-location strategies.

    ``("naive", "simple", "walk", "landmarks", "trapezoid", "triangulation")``.
    Not every strategy is usable on every arrangement -- see
    :meth:`Arrangement.supports_point_location`.  ``None`` / ``"default"`` selects an
    attached strategy if there is one, otherwise a walk-along-a-line query.

    :rtype: tuple[str, ...]
    """
    cdef list out = []
    cdef int i
    for i in range(<int>PL_NUM_STRATEGIES):
        out.append(_from_cstr(point_location_name(i)))
    return tuple(out)


_py_cgal_version.__name__ = "cgal_version"
_py_cgal_version.__qualname__ = "cgal_version"
_py_build_info.__name__ = "build_info"
_py_build_info.__qualname__ = "build_info"
_py_kind_available.__name__ = "kind_available"
_py_kind_available.__qualname__ = "kind_available"
globals()["cgal_version"] = _py_cgal_version
globals()["build_info"] = _py_build_info
globals()["kind_available"] = _py_kind_available


include "_errors.pxi"
include "_numbers.pxi"
include "_geometry.pxi"
include "_arrangement.pxi"
include "_polygon_set.pxi"
