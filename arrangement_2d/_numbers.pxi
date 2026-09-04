# -*- coding: utf-8 -*-
# Part of arrangement_2d._core (textual include; see _core.pyx).
#
# Exact numbers at the Python boundary.
#
#   * rational values are exchanged as fractions.Fraction (exact, arbitrary precision);
#   * circle-segment coordinates are a + b*sqrt(c) with rational a, b, c  -> SqrtExtension;
#   * Bezier / conic coordinates are CORE::Expr algebraic numbers        -> Algebraic.
#
# Python -> exact conversion is lossless: int (any size), float (its exact binary value),
# Fraction, Decimal, str, numpy scalars and rational SqrtExtension/Algebraic values.
#
# Note on to_double: CGAL's `to_double(Lazy_exact_nt)` is not correctly rounded (it works
# off the interval approximation, number_types_and_errors.md gotcha 2). The core always
# converts through the exact value, so `.approx` here is the correctly rounded double of
# the exact number.

cdef object _Fraction = fractions.Fraction
cdef object _Decimal = decimal.Decimal

# arr2d::NumberKind values (numbers.hpp). They are NOT spelled `NumberKind.NRational`
# here on purpose: Cython emits `<enum cname>::<member cython name>` for the members of a
# `cdef enum class`, ignoring the member's own cname, so `NumberKind.NRational` would
# generate the non-existent `arr2d::NumberKind::NRational`. Compare the int value instead.
cdef enum _NumberKindValue:
    _NK_RATIONAL = 0
    _NK_SQRT_EXT = 1
    _NK_ALGEBRAIC = 2


cdef object _INT64_MIN = -9223372036854775808
cdef object _INT64_MAX = 9223372036854775807


# ---------------------------------------------------------------------------
# Python -> Rational
# ---------------------------------------------------------------------------

cdef Rational _rational_from_ints(object num, object den) except *:
    """Exact rational from two Python integers (arbitrary size)."""
    if den == 0:
        raise ZeroDivisionError("rational with zero denominator")
    return rational_from_strings(_to_bytes(str(num)), _to_bytes(str(den)))


cdef Rational _rational_from_pyint(object x) except *:
    cdef long long v
    if _INT64_MIN <= x <= _INT64_MAX:
        v = <long long>x
        return rational_from_int64(<int64_t>v)
    return _rational_from_ints(x, 1)


cdef Rational _to_rational(object x) except *:
    """Convert any number-like Python object to the exact ``arr2d::Rational``.

    Accepted: ``int`` (any size), ``float`` (its *exact* binary value -- ``0.1`` becomes
    ``3602879701896397/36028797018963968``), :class:`fractions.Fraction`,
    :class:`decimal.Decimal`, ``str`` (``"3"``, ``"3/4"``, ``"0.125"``, ``"-1.25e3"``),
    any :class:`numbers.Rational` / :class:`numbers.Integral` / :class:`numbers.Real`,
    numpy scalars and 0-d arrays, and :class:`SqrtExtension` / :class:`Algebraic` values
    that are rational.

    :raises TypeError: the object is not number-like.
    :raises ValueError: a NaN/infinite float, or an unparsable string.
    :raises NotRepresentableError: an irrational :class:`SqrtExtension` / :class:`Algebraic`.
    """
    cdef double d
    # --- fast, exact paths -------------------------------------------------
    if type(x) is int:
        return _rational_from_pyint(x)
    if type(x) is float:
        d = <double>x
        return rational_from_double(d)
    if isinstance(x, _Fraction):
        return _rational_from_ints(x.numerator, x.denominator)
    # --- our own exact numbers --------------------------------------------
    if isinstance(x, SqrtExtension):
        return number_to_rational((<SqrtExtension>x).g)
    if isinstance(x, Algebraic):
        return number_to_rational((<Algebraic>x).g)
    # --- text --------------------------------------------------------------
    if isinstance(x, (str, bytes)):
        return _rational_from_text(x)
    # --- decimal -----------------------------------------------------------
    if isinstance(x, _Decimal):
        if not x.is_finite():
            raise ValueError("cannot convert a non-finite Decimal (%s) to an exact rational" % (x,))
        ratio = _Fraction(x).as_integer_ratio()
        return _rational_from_ints(ratio[0], ratio[1])
    # --- bool / subclasses of int and float --------------------------------
    if isinstance(x, bool):
        return rational_from_int64(<int64_t>1 if x else <int64_t>0)
    if isinstance(x, int):
        return _rational_from_pyint(int(x))
    if isinstance(x, float):
        d = <double>x
        return rational_from_double(d)
    # --- numpy 0-d arrays and other scalars exposing .item() ---------------
    if getattr(x, "ndim", None) == 0 and hasattr(x, "item"):
        return _to_rational(x.item())
    # --- numeric tower ------------------------------------------------------
    if isinstance(x, numbers.Rational):
        return _rational_from_ints(int(x.numerator), int(x.denominator))
    if isinstance(x, numbers.Integral):
        return _rational_from_pyint(int(x))
    if isinstance(x, numbers.Real):
        d = <double>float(x)
        return rational_from_double(d)
    # --- last resort --------------------------------------------------------
    if hasattr(x, "as_integer_ratio"):
        ratio = x.as_integer_ratio()
        return _rational_from_ints(ratio[0], ratio[1])
    if hasattr(x, "__index__"):
        return _rational_from_pyint(int(x))
    if hasattr(x, "__float__"):
        d = <double>float(x)
        return rational_from_double(d)
    raise TypeError(
        "cannot convert %r (%s) to an exact rational number; expected int, float, "
        "Fraction, Decimal, a numeric string such as '3/4' or '0.125', a numpy scalar, "
        "or a rational SqrtExtension/Algebraic"
        % (x, type(x).__name__)
    )


cdef Rational _rational_from_text(object x) except *:
    cdef object s = x.decode("utf-8") if isinstance(x, bytes) else x
    s = s.strip()
    if not s:
        raise ValueError("empty string is not a number")
    try:
        if "/" in s:
            num, _, den = s.partition("/")
            return _rational_from_ints(int(num.strip()), int(den.strip()))
        return _rational_from_pyint(int(s))
    except ValueError:
        pass
    try:
        f = _Fraction(s)
    except (ValueError, ArithmeticError) as exc:
        raise ValueError("cannot parse %r as an exact number: %s" % (x, exc)) from None
    return _rational_from_ints(f.numerator, f.denominator)


# ---------------------------------------------------------------------------
# Rational -> Python
# ---------------------------------------------------------------------------

cdef object _fraction(const Rational& r):
    """Exact ``arr2d::Rational`` -> :class:`fractions.Fraction`."""
    cdef string num
    cdef string den
    rational_to_strings(r, num, den)
    return _Fraction(int(_from_string(num)), int(_from_string(den)))


cdef object _wrap_number(const Geom& n):
    """Boxed exact number -> :class:`fractions.Fraction`, :class:`SqrtExtension` or :class:`Algebraic`.

    A sqrt-extension whose value happens to be rational (``b == 0`` or ``c == 0``) is
    returned as a :class:`fractions.Fraction`, so callers get the simplest exact
    representation available.
    """
    cdef int nk = <int>number_kind(n)
    if nk == _NK_RATIONAL:
        return _fraction(number_to_rational(n))
    if nk == _NK_SQRT_EXT:
        if number_is_rational(n):
            return _fraction(number_to_rational(n))
        return _wrap_sqrt_ext(n)
    return _wrap_algebraic(n)


cdef SqrtExtension _wrap_sqrt_ext(const Geom& n):
    cdef SqrtExt s = number_to_sqrt_ext(n)
    cdef SqrtExtension out = SqrtExtension.__new__(SqrtExtension)
    out.g = n
    out._a = _fraction(s.a)
    out._b = _fraction(s.b)
    out._c = _fraction(s.c)
    return out


cdef Algebraic _wrap_algebraic(const Geom& n):
    cdef Algebraic out = Algebraic.__new__(Algebraic)
    out.g = n
    return out


cdef Geom _number_box(object x) except *:
    """Any number-like object -> a boxed exact number (``Geom`` of type ``Number``)."""
    if isinstance(x, SqrtExtension):
        return (<SqrtExtension>x).g
    if isinstance(x, Algebraic):
        return (<Algebraic>x).g
    return box_rational(_to_rational(x))


cdef int _number_cmp(object a, object b) except -2:
    """-1/0/+1 comparison of two number-like objects (exact)."""
    cdef Geom ga = _number_box(a)
    cdef Geom gb = _number_box(b)
    return number_compare(ga, gb)


cdef object _richcmp_number(object self, object other, int op):
    """Shared rich comparison for :class:`SqrtExtension` and :class:`Algebraic`.

    A value that is not number-like at all yields ``NotImplemented`` so that ``==`` / ``!=``
    fall back to identity and ordering raises the standard
    ``TypeError: '<' not supported between instances of ...``.  A value that *looks* like a
    number but cannot be parsed (``"abc"``) only degrades that way for ``==`` / ``!=``;
    ordering re-raises the informative :class:`ValueError`.
    """
    cdef int c
    try:
        c = _number_cmp(self, other)
    except TypeError:
        return NotImplemented
    except ValueError:
        if op == 2 or op == 3:
            return NotImplemented
        raise
    if op == 0:
        return c < 0
    if op == 1:
        return c <= 0
    if op == 2:
        return c == 0
    if op == 3:
        return c != 0
    if op == 4:
        return c > 0
    return c >= 0


# ---------------------------------------------------------------------------
# SqrtExtension
# ---------------------------------------------------------------------------

cdef class SqrtExtension:
    """An exact number of the form ``a + b * sqrt(c)`` with rational ``a``, ``b``, ``c >= 0``.

    These are the coordinates produced by the ``circle_segment`` kind (CGAL's
    ``CGAL::_One_root_number``): intersections of circles and lines have square roots but
    no worse.

    :param a: rational part (any number-like; default ``0``).
    :param b: coefficient of the square root (default ``0``).
    :param c: the (non-negative) radicand (default ``0``).

    >>> SqrtExtension(1, 1, 2)                  # doctest: +SKIP
    SqrtExtension(1, 1, 2)                      # == 1 + sqrt(2)
    """

    cdef Geom g
    # the exact coefficients as fractions.Fraction (value = a + b*sqrt(c));
    # exposed read-only through the a / b / c properties below
    cdef object _a
    cdef object _b
    cdef object _c

    @property
    def a(self):
        """The rational part, a :class:`fractions.Fraction`."""
        return self._a

    @property
    def b(self):
        """The coefficient of the square root, a :class:`fractions.Fraction`."""
        return self._b

    @property
    def c(self):
        """The radicand ``c >= 0``, a :class:`fractions.Fraction`."""
        return self._c

    def __cinit__(self, a=0, b=0, c=0):
        cdef SqrtExt s
        s.a = _to_rational(a)
        s.b = _to_rational(b)
        s.c = _to_rational(c)
        if rational_sign(s.c) < 0:
            raise ValueError("SqrtExtension: the radicand c must be >= 0, got %r" % (c,))
        self.g = box_sqrt_ext(s)
        self._a = _fraction(s.a)
        self._b = _fraction(s.b)
        self._c = _fraction(s.c)

    @property
    def approx(self):
        """The value as a ``float`` (correctly rounded from the exact value).

        :rtype: float
        """
        return number_to_double(self.g)

    @property
    def is_rational(self):
        """``True`` when the value is rational (``b == 0`` or ``c == 0``).

        :rtype: bool
        """
        return bool(number_is_rational(self.g))

    def exact(self):
        """The exact value as a :class:`fractions.Fraction`, or ``None`` if irrational.

        :rtype: fractions.Fraction | None
        """
        if not number_is_rational(self.g):
            return None
        return _fraction(number_to_rational(self.g))

    def sign(self):
        """Return ``-1``, ``0`` or ``+1``: the exact sign of the value.

        :rtype: int
        """
        return number_sign(self.g)

    def interval(self, int bits=53):
        """Return a certified interval ``(lo, hi)`` of floats containing the exact value.

        :param bits: requested relative precision (ignored for sqrt extensions, which are
            always evaluated to full double precision).
        :rtype: tuple[float, float]
        """
        cdef pair[double, double] iv = number_interval(self.g, bits)
        return (iv.first, iv.second)

    def refine(self, int bits=53):
        """Refine the approximation to *bits* and return the resulting interval.

        Equivalent to :meth:`interval` for sqrt extensions (they are exact by
        construction); the method exists so that :class:`SqrtExtension` and
        :class:`Algebraic` are interchangeable.

        :rtype: tuple[float, float]
        """
        return self.interval(bits)

    def __float__(self):
        return number_to_double(self.g)

    def __repr__(self):
        return "SqrtExtension(%s, %s, %s)" % (self._a, self._b, self._c)

    def __str__(self):
        cdef string s = number_repr(self.g)
        return _from_string(s)

    def __richcmp__(self, other, int op):
        return _richcmp_number(self, other, op)

    def __hash__(self):
        """Hash equal to ``hash(Fraction)`` when rational; unhashable otherwise.

        Irrational values are unhashable on purpose: they compare equal to
        :class:`Algebraic` values of the same magnitude, and there is no cheap hash
        that would stay consistent with that.
        """
        if not number_is_rational(self.g):
            raise TypeError(
                "unhashable: SqrtExtension(%s, %s, %s) is irrational" % (self._a, self._b, self._c)
            )
        return hash(_fraction(number_to_rational(self.g)))

    def __bool__(self):
        return number_sign(self.g) != 0

    def __neg__(self):
        return SqrtExtension(-self._a, -self._b, self._c)


# ---------------------------------------------------------------------------
# Algebraic
# ---------------------------------------------------------------------------

cdef class Algebraic:
    """An exact real algebraic number (CGAL's ``CORE::Expr``).

    These are the coordinates of Bezier and conic intersection points.  They cannot be
    written down as a fraction in general; you get a correctly rounded ``float``
    (:attr:`approx`), a certified :meth:`interval`, an exact :meth:`sign` and exact
    comparisons.

    Instances come out of ``point.exact()`` / ``curve.exact()``; the only way to build one
    directly is :meth:`from_rational`.
    """

    cdef Geom g

    def __init__(self, *args, **kwargs):
        raise TypeError(
            "Algebraic cannot be constructed directly; it is produced by exact() "
            "accessors. Use Algebraic.from_rational(x) to box a rational value."
        )

    @staticmethod
    def from_rational(x):
        """Box a rational value as an :class:`Algebraic`.

        The result reports :attr:`is_rational` ``True`` and :meth:`exact` returns the
        :class:`fractions.Fraction` back.

        :param x: any number-like object accepted by the exact conversion.
        :rtype: Algebraic
        """
        cdef Algebraic out = Algebraic.__new__(Algebraic)
        out.g = box_rational(_to_rational(x))
        return out

    @property
    def approx(self):
        """The value as a ``float``.

        :rtype: float
        """
        return number_to_double(self.g)

    @property
    def is_rational(self):
        """``True`` when the value is *known* to be rational.

        Always ``False`` for a genuine ``CORE::Expr``: CORE offers no safe rationality
        test (``ExprRep::ratFlag()`` dereferences a lazily allocated node and can crash --
        number_types_and_errors.md gotcha 4), so the core never claims rationality for
        algebraic numbers.  Values produced by :meth:`from_rational` report ``True``.

        :rtype: bool
        """
        return bool(number_is_rational(self.g))

    def exact(self):
        """The exact value as a :class:`fractions.Fraction`, or ``None`` when not known rational.

        :rtype: fractions.Fraction | None
        """
        if not number_is_rational(self.g):
            return None
        return _fraction(number_to_rational(self.g))

    def sign(self):
        """Return ``-1``, ``0`` or ``+1``: the exact sign of the value.

        :rtype: int
        """
        return number_sign(self.g)

    def interval(self, int bits=53):
        """Return a certified interval ``(lo, hi)`` of floats containing the exact value.

        :param bits: requested relative precision in bits; CORE refines the
            representation on demand until the interval is that tight.
        :rtype: tuple[float, float]
        """
        cdef pair[double, double] iv = number_interval(self.g, bits)
        return (iv.first, iv.second)

    def refine(self, int bits=53):
        """Refine the internal approximation to *bits* and return the resulting interval.

        :rtype: tuple[float, float]
        """
        cdef pair[double, double] iv = number_interval(self.g, bits)
        return (iv.first, iv.second)

    def __float__(self):
        return number_to_double(self.g)

    def __repr__(self):
        cdef string s = number_repr(self.g)
        return "Algebraic(%s)" % (_from_string(s),)

    def __str__(self):
        cdef string s = number_repr(self.g)
        return _from_string(s)

    def __richcmp__(self, other, int op):
        return _richcmp_number(self, other, op)

    def __hash__(self):
        """Hash equal to ``hash(Fraction)`` when rational; unhashable otherwise."""
        if not number_is_rational(self.g):
            raise TypeError("unhashable: Algebraic value is not known to be rational")
        return hash(_fraction(number_to_rational(self.g)))

    def __bool__(self):
        return number_sign(self.g) != 0


# ---------------------------------------------------------------------------
# Orientation arguments
# ---------------------------------------------------------------------------

cdef frozenset _CCW_NAMES = frozenset(
    ("ccw", "counterclockwise", "counter_clockwise", "counter-clockwise",
     "positive", "left", "+1")
)
cdef frozenset _CW_NAMES = frozenset(
    ("cw", "clockwise", "negative", "right", "-1")
)


cdef object _orientation_arg(object o):
    """Normalise an orientation argument to ``+1`` (counterclockwise) or ``-1`` (clockwise).

    Accepts ``"ccw"`` / ``"counterclockwise"`` / ``"cw"`` / ``"clockwise"`` (any case),
    ``+1`` / ``-1``, and ``True`` / ``False`` (``True`` = counterclockwise).

    :rtype: int
    """
    cdef long v
    if o is None:
        return 1
    if isinstance(o, bool):
        return 1 if o else -1
    if isinstance(o, str):
        s = (<str>o).strip().lower()
        if s in _CCW_NAMES:
            return 1
        if s in _CW_NAMES:
            return -1
        raise ValueError(
            "unknown orientation %r; expected 'ccw'/'counterclockwise', "
            "'cw'/'clockwise', +1 or -1" % (o,)
        )
    if isinstance(o, (int, numbers.Integral)):
        v = <long>int(o)
        if v > 0:
            return 1
        if v < 0:
            return -1
        raise ValueError("orientation 0 (collinear) is not a valid orientation; use +1 or -1")
    raise TypeError(
        "expected an orientation ('ccw'/'cw', +1/-1, True/False), got %r"
        % (type(o).__name__,)
    )
