"""Exception hierarchy of :mod:`arrangement_2d`.

Every error raised by the CGAL core is translated into one of the classes below by
``arrangement_2d/_exc_bridge.hpp`` (see ``arr2d_translate_exception``).  The classes
are plain Python so that they can be imported (and caught) without importing the
compiled extension module, and so that the C++ translation layer can look them up
lazily with ``PyImport_ImportModule("arrangement_2d.errors")``.

Two design rules:

* every error is a :class:`CGALError`, so ``except CGALError`` catches everything
  that comes out of the geometry kernel;
* where an error also *is* a classic Python error (a bad argument, a bad index, an
  unimplemented feature), the class additionally derives from the matching builtin,
  so ordinary ``except ValueError`` / ``except TypeError`` code keeps working.

Mapping (``arr2d::ErrorCode`` / CGAL exception -> Python class)::

    ErrorCode::Generic              -> CGALError
    ErrorCode::KindMismatch         -> KindMismatchError      (TypeError)
    ErrorCode::InvalidHandle        -> InvalidHandleError     (ValueError)
    ErrorCode::NotXMonotone         -> NotXMonotoneError      (ValueError)
    ErrorCode::NotRepresentable     -> NotRepresentableError  (ValueError)
    ErrorCode::Unsupported          -> UnsupportedError       (NotImplementedError)
    ErrorCode::InvalidArgument      -> ValueError             (builtin)
    ErrorCode::CallbackFailed       -> CallbackError

    CGAL::Precondition_exception    -> PreconditionError      (ValueError)
    CGAL::Postcondition_exception   -> PostconditionError
    CGAL::Assertion_exception       -> CGALAssertionError
    CGAL::Warning_exception         -> CGALWarning
    other CGAL::Failure_exception   -> CGALError
    CGAL::Uncertain_conversion_exception -> CGALError
"""

from __future__ import annotations

__all__ = [
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


class CGALError(Exception):
    """Base class of every error raised by :mod:`arrangement_2d`.

    Raised directly for a generic failure of the CGAL kernel
    (``CGAL::Error_exception`` / ``CGAL::Test_exception``, an uncertain filtered
    predicate, or ``arr2d::ErrorCode::Generic``).
    """


class PreconditionError(CGALError, ValueError):
    """A CGAL precondition was violated (``CGAL::Precondition_exception``).

    This is what you get when arguments are geometrically illegal: inserting a
    curve that intersects the arrangement through
    :meth:`~arrangement_2d.Arrangement.insert_non_intersecting`, splitting an edge
    at a point that is not in its interior, a clockwise outer polygon boundary, ...

    The message carries the failing CGAL expression and the CGAL source location::

        CGAL precondition violation: <expression> [CGAL] (<file>:<line>) <message>
    """


class PostconditionError(CGALError):
    """A CGAL postcondition was violated (``CGAL::Postcondition_exception``).

    Almost always an internal CGAL problem rather than a usage error.
    """


class CGALAssertionError(CGALError):
    """A CGAL assertion failed (``CGAL::Assertion_exception``).

    Not a subclass of the builtin :class:`AssertionError`: a failing CGAL assertion
    is a library-level error, not a failing Python ``assert``.
    """


class CGALWarning(CGALError):
    """A CGAL warning condition failed (``CGAL::Warning_exception``).

    CGAL's warning behaviour defaults to ``CONTINUE``, so this is only ever raised
    if the process changed the warning behaviour to ``THROW_EXCEPTION``.
    """


class InvalidHandleError(CGALError, ValueError):
    """A DCEL handle no longer refers to a live element.

    :class:`~arrangement_2d.Vertex`, :class:`~arrangement_2d.Halfedge`,
    :class:`~arrangement_2d.Face` and :class:`~arrangement_2d.CurveHandle` objects
    keep ``(pointer, id)`` pairs; after the element is removed (or the whole
    arrangement is cleared) the handle becomes stale and every access raises this
    error instead of crashing.  Use ``handle.is_valid`` to test without raising.
    """


class KindMismatchError(CGALError, TypeError):
    """Geometry of one :class:`~arrangement_2d.Kind` was used where another was required.

    Raised when a curve or point cannot be converted to the target kind, or when a
    handle of one arrangement is passed to another.
    """


class NotXMonotoneError(CGALError, ValueError):
    """An x-monotone curve was required but a general curve was given.

    Use ``curve.make_x_monotone()`` (or
    :meth:`~arrangement_2d.Arrangement.insert`, which subdivides automatically)
    to split the curve first.
    """


class NotRepresentableError(CGALError, ValueError):
    """An exact value cannot be represented in the requested form.

    Typical cases: asking for the rational coordinates of an algebraic point
    (Bézier / conic intersection), or converting a curve into a kind that cannot
    express it exactly.
    """


class UnsupportedError(CGALError, NotImplementedError):
    """The operation does not exist for this geometry kind or CGAL traits class.

    The message names the kind and the reason, e.g.
    ``"bezier: Construct_x_monotone_curve_2 not available"`` or
    ``"fictitious_face() is only defined for the 'linear' kind"``.
    """


class CallbackError(CGALError):
    """A Python callback (observer or overlay callback) raised.

    The original exception is re-raised after the C++ call returns; this class is
    used when the core itself reports ``arr2d::ErrorCode::CallbackFailed`` and no
    Python exception was recorded.
    """
