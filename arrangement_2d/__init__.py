"""Python bindings for the CGAL 6.1 *2D Arrangements* package.

``arrangement_2d`` builds exact arrangements of curves in the plane (and of geodesic arcs
on the sphere), with full curve history, extended DCEL user data, observers, overlay,
point location, vertical decomposition and 2D Boolean set operations.

Seven geometry :class:`Kind` s are supported, each one CGAL traits instantiation:

===================  ==========================================================
``SEGMENT``          line segments
``LINEAR``           segments, rays and lines (unbounded planar topology)
``CIRCLE_SEGMENT``   circular arcs and line segments
``POLYLINE``         polylines of segments
``BEZIER``           polynomial Bezier curves with rational control points
``CONIC``            conic arcs (ellipse, parabola, hyperbola, segment)
``SPHERE``           geodesic arcs on the unit sphere
===================  ==========================================================

Quick start::

    import arrangement_2d as a2

    arr = a2.Arrangement("segment")
    arr.insert([a2.Segment((0, 0), (4, 4)),
                a2.Segment((0, 4), (4, 0))])
    print(arr)                       # Arrangement(kind='segment', vertices=5, ...)
    print(arr.locate((1, 2)))        # the face containing the point

All coordinates are exact: integers, ``float`` (its exact binary value),
:class:`fractions.Fraction`, :class:`decimal.Decimal` and numeric strings go in, and
:class:`fractions.Fraction`, :class:`SqrtExtension` or :class:`Algebraic` come back out of
``exact()``.  ``.approx`` always gives plain floats.

Two optional submodules are imported lazily: ``arrangement_2d.regions`` (higher level
region helpers) and ``arrangement_2d.plot`` (matplotlib helpers).
"""

from __future__ import annotations

import importlib
from typing import TYPE_CHECKING, Any

from ._core import (
    # ---- kinds & module info -------------------------------------------------
    Kind,
    __version__,
    available_kinds,
    build_info,
    cgal_version,
    kind_available,
    point_location_strategies,
    # ---- exact numbers -------------------------------------------------------
    Algebraic,
    SqrtExtension,
    # ---- geometry ------------------------------------------------------------
    BezierCurve,
    Circle,
    CircleSegment,
    CircularArc,
    ConicArc,
    Curve,
    GeodesicArc,
    GeodesicArc as SphericalArc,
    Line,
    LinearCurve,
    Point,
    Polygon,
    PolygonWithHoles,
    Polyline,
    Ray,
    Segment,
    conic_allow_hyperbolic,
    line_from_coefficients,
    # ---- arrangement ---------------------------------------------------------
    Arrangement,
    CurveHandle,
    Face,
    Halfedge,
    Observer,
    OverlayCallbacks,
    Traits,
    Vertex,
    traits,
    # ---- Boolean set operations ---------------------------------------------
    PolygonSet,
    complement,
    difference,
    do_intersect,
    intersection,
    join,
    oriented_side,
    symmetric_difference,
    is_valid_polygon,
    orientation,
)
from .errors import (
    CallbackError,
    CGALAssertionError,
    CGALError,
    CGALWarning,
    InvalidHandleError,
    KindMismatchError,
    NotRepresentableError,
    NotXMonotoneError,
    PostconditionError,
    PreconditionError,
    UnsupportedError,
)

if TYPE_CHECKING:  # pragma: no cover - for type checkers only
    from . import cleanup, plot, regions

_LAZY_SUBMODULES = ("regions", "plot", "cleanup")


def __getattr__(name: str) -> Any:
    """Import ``arrangement_2d.regions`` / ``arrangement_2d.plot`` on first access (PEP 562).

    Both are optional add-ons that may not be installed; a missing one raises
    :class:`AttributeError` (not :class:`ModuleNotFoundError`) so that ``hasattr`` and
    ``from arrangement_2d import *`` behave.
    """
    if name in _LAZY_SUBMODULES:
        try:
            module = importlib.import_module("." + name, __name__)
        except ModuleNotFoundError as exc:  # the optional submodule is not installed
            raise AttributeError(
                f"module {__name__!r} has no attribute {name!r} "
                f"(the optional submodule arrangement_2d.{name} is not installed)"
            ) from exc
        globals()[name] = module
        return module
    raise AttributeError(f"module {__name__!r} has no attribute {name!r}")


def __dir__() -> list[str]:
    return sorted(set(globals()) | set(_LAZY_SUBMODULES))


__all__ = [
    # kinds & module info
    "Kind",
    "available_kinds",
    "build_info",
    "cgal_version",
    "kind_available",
    "point_location_strategies",
    "__version__",
    # numbers
    "Algebraic",
    "SqrtExtension",
    # geometry
    "Point",
    "Curve",
    "Segment",
    "LinearCurve",
    "Line",
    "Ray",
    "CircleSegment",
    "Circle",
    "CircularArc",
    "Polyline",
    "BezierCurve",
    "ConicArc",
    "GeodesicArc",
    "SphericalArc",
    "Polygon",
    "PolygonWithHoles",
    "conic_allow_hyperbolic",
    "line_from_coefficients",
    # arrangement
    "Arrangement",
    "Vertex",
    "Halfedge",
    "Face",
    "CurveHandle",
    "Observer",
    "OverlayCallbacks",
    "Traits",
    "traits",
    # Boolean set operations
    "PolygonSet",
    "join",
    "intersection",
    "difference",
    "symmetric_difference",
    "complement",
    "do_intersect",
    "oriented_side",
    "is_valid_polygon",
    "orientation",
    # errors
    "CGALError",
    "PreconditionError",
    "PostconditionError",
    "CGALAssertionError",
    "CGALWarning",
    "InvalidHandleError",
    "KindMismatchError",
    "NotXMonotoneError",
    "NotRepresentableError",
    "UnsupportedError",
    "CallbackError",
]

# ``regions`` and ``plot`` are optional lazy submodules reachable as attributes (see
# __getattr__/__dir__); they are deliberately NOT in __all__, so ``from arrangement_2d
# import *`` does not fail when they are not installed.
