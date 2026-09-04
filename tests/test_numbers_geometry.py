"""Number and geometry layer of :mod:`arrangement_2d` (public API only).

Scope (DESIGN.md section 3 "numbers" and "geometry"):

* exact conversion of every accepted Python coordinate spelling into the kernel's
  rational type, and back out through ``exact()`` / ``exact_rational()``;
* :class:`arrangement_2d.SqrtExtension` and :class:`arrangement_2d.Algebraic`
  (value, comparison, hashing, repr, certified intervals);
* the :class:`arrangement_2d.Curve` base API exercised on ``Segment``;
* exact conversions between geometry kinds (``Point.to_kind`` / ``Curve.to_kind`` /
  ``Polygon.to_kind``) and the errors raised when a conversion is not exact;
* ``repr`` of every public geometry class.

Every expected number below is derived by hand in a comment next to the assertion;
nothing is copied from a previous run of the library.
"""

from __future__ import annotations

import math
from decimal import Decimal
from fractions import Fraction

import pytest

import arrangement_2d as a2

# Kinds whose points live in the plane.  ``sphere`` is excluded: its points are 3-D
# directions, which is what makes the planar <-> sphere conversions a KindMismatch.
PLANAR_KINDS = ("segment", "linear", "circle_segment", "polyline", "bezier", "conic")

# Planar kinds whose points are *exact rationals* (Epeck::Point_2 or a normalised
# Sqrt_extension).  ``conic`` is missing on purpose: a Conic_point_2 stores CORE::Expr
# coordinates and CORE has no sound rationality test, so the core never claims a conic
# point is rational (CGAL_TRAPS_CHECKLIST "CORE has no safe rationality test for Expr";
# kind_conic.cpp point_is_rational() is documented as ALWAYS false).
RATIONAL_POINT_KINDS = ("segment", "linear", "circle_segment", "polyline", "bezier")


# ===========================================================================
# 1. Python -> exact rational: every accepted coordinate spelling
# ===========================================================================

def test_point_from_python_ints_is_exact():
    # Plain ints go through the int64 fast path; the exact value is n/1.
    p = a2.Point(3, -7)
    assert p.exact() == (Fraction(3), Fraction(-7))
    assert p.approx == (3.0, -7.0)


def test_point_from_big_int_is_exact():
    # 2**200 does not fit in int64, so the decimal-string path is used.  The exact
    # value must still be the integer itself, denominator 1.
    big = 2 ** 200
    p = a2.Point(big, -(2 ** 100))
    assert p.exact() == (Fraction(big), Fraction(-(2 ** 100)))


def test_point_from_float_is_the_exact_binary_value():
    # 0.1 is not one tenth: the nearest double to 1/10 is 3602879701896397 / 2**55,
    # and 2**55 == 36028797018963968.  DESIGN.md section "Numbers" promises exactly this.
    p = a2.Point(0.1, 0.0)
    assert p.exact()[0] == Fraction(3602879701896397, 36028797018963968)
    # ... which is what Python's own exact float -> Fraction conversion gives.
    assert p.exact()[0] == Fraction(0.1)
    assert p.exact()[0] != Fraction(1, 10)


def test_point_from_float_keeps_negative_and_subnormal_values():
    # -0.5 and 2**-1074 (the smallest positive subnormal) are exact binary values.
    tiny = 5e-324  # == 2**-1074
    p = a2.Point(-0.5, tiny)
    assert p.exact() == (Fraction(-1, 2), Fraction(1, 2 ** 1074))


def test_point_from_fraction_is_exact():
    p = a2.Point(Fraction(3, 4), Fraction(-1, 3))
    assert p.exact() == (Fraction(3, 4), Fraction(-1, 3))


def test_point_from_decimal_is_exact():
    # Decimal("0.125") == 125/1000 == 1/8 exactly; Decimal("-2.5") == -5/2.
    p = a2.Point(Decimal("0.125"), Decimal("-2.5"))
    assert p.exact() == (Fraction(1, 8), Fraction(-5, 2))


def test_point_from_strings_is_exact():
    # "3/4" is parsed as a ratio, "1e-3" as a decimal literal (1/1000), and surrounding
    # whitespace is stripped.
    assert a2.Point("3/4", "1e-3").exact() == (Fraction(3, 4), Fraction(1, 1000))
    assert a2.Point(" 3 / 4 ", "-1.25e3").exact() == (Fraction(3, 4), Fraction(-1250))


def test_point_from_numpy_scalars_is_exact():
    np = pytest.importorskip("numpy")
    # np.float64(0.5) and np.float32(0.5) are both exactly 1/2; integer scalars are exact.
    p = a2.Point(np.int64(5), np.float64(0.5))
    assert p.exact() == (Fraction(5), Fraction(1, 2))
    q = a2.Point(np.float32(0.25), np.int8(-3))
    assert q.exact() == (Fraction(1, 4), Fraction(-3))
    # A 0-d array exposes .item() and must convert through it.
    r = a2.Point(np.array(0.25), 1)
    assert r.exact() == (Fraction(1, 4), Fraction(1))


def test_point_from_bool_is_one_or_zero():
    # bool is a subclass of int; True == 1, False == 0.
    assert a2.Point(True, False).exact() == (Fraction(1), Fraction(0))


def test_point_rejects_non_finite_numbers():
    with pytest.raises(ValueError, match="NaN"):
        a2.Point(float("nan"), 0)
    with pytest.raises(ValueError, match="infinite"):
        a2.Point(float("inf"), 0)
    with pytest.raises(ValueError, match="non-finite Decimal"):
        a2.Point(Decimal("Infinity"), 0)


def test_point_rejects_non_numbers():
    with pytest.raises(TypeError):
        a2.Point(1j, 0)
    with pytest.raises(TypeError):
        a2.Point(None, 0)
    with pytest.raises(ValueError, match="empty string"):
        a2.Point("", 0)
    with pytest.raises(ValueError, match="cannot parse"):
        a2.Point("abc", 0)
    with pytest.raises(ZeroDivisionError):
        a2.Point("1/0", 0)


# ===========================================================================
# 2. Point: exact / approximate / interval / comparison / protocol
# ===========================================================================

def test_point_approx_is_the_correctly_rounded_double():
    # CGAL's to_double(Epeck::FT) is NOT correctly rounded (CGAL_TRAPS_CHECKLIST,
    # "Numbers / coordinates"): for 1/3 it yields 0.33333333333333337, one ulp above the
    # true nearest double.  The correctly rounded value is float(Fraction(1, 3)) ==
    # 0.3333333333333333 (0x3FD5555555555555).
    p = a2.Point(Fraction(1, 3), Fraction(2, 3))
    assert p.x == float(Fraction(1, 3)) == 0.3333333333333333
    assert p.y == float(Fraction(2, 3)) == 0.6666666666666666


def test_point_interval_encloses_the_exact_value_and_is_one_ulp_wide():
    p = a2.Point(Fraction(1, 3), 0)
    (lo, hi), (zlo, zhi) = p.interval()
    # The interval must be certified: lo <= 1/3 <= hi, compared exactly.
    assert Fraction(lo) <= Fraction(1, 3) <= Fraction(hi)
    # 1/3 is not a double, so the tightest certified interval is one ulp wide.
    assert hi == math.nextafter(lo, math.inf)
    # An exactly representable coordinate gives a degenerate interval.
    assert (zlo, zhi) == (0.0, 0.0)


def test_point_exact_rational_matches_exact_for_rational_points():
    p = a2.Point(Fraction(7, 2), -1)
    assert p.exact() == p.exact_rational() == (Fraction(7, 2), Fraction(-1))
    assert p.is_rational is True


def test_point_equality_and_hash_use_the_exact_coordinates():
    a = a2.Point(Fraction(1, 2), 3)
    b = a2.Point(0.5, 3)  # 0.5 is exactly 1/2, so these are the same point
    assert a == b
    assert hash(a) == hash(b) == hash((Fraction(1, 2), Fraction(3)))
    assert a != a2.Point(Fraction(1, 2), 4)
    # A non-Point compares unequal rather than raising.
    assert (a == (0.5, 3.0)) is False


def test_point_sequence_protocol_exposes_the_approximations():
    p = a2.Point(1, Fraction(3, 2))
    assert len(p) == 2
    assert list(p) == [1.0, 1.5]
    assert (p[0], p[1]) == (1.0, 1.5)
    assert p.xy == (1.0, 1.5)
    assert p.dimension == 2


def test_point_compare_x_and_compare_xy_are_exact():
    p = a2.Point(1, 2)
    # Lexicographic: equal x, smaller y  ->  -1.
    assert p.compare_xy((1, 3)) == -1
    assert p.compare_xy((1, 2)) == 0
    assert p.compare_xy((0, 99)) == 1
    # compare_x looks at x only.
    assert p.compare_x((0, 0)) == 1
    assert p.compare_x((1, -100)) == 0


def test_planar_point_has_no_z_coordinate():
    p = a2.Point(1, 2)
    with pytest.raises(AttributeError, match="no z coordinate"):
        p.z
    with pytest.raises(AttributeError, match="use .xy"):
        p.xyz
    with pytest.raises(AttributeError, match="'sphere'"):
        p.location


def test_sphere_point_is_an_unnormalised_direction():
    # Point(x, y, z) is a *direction*: exact() gives the direction as it was written,
    # while approx normalises it.  (1, 2, 3) has length sqrt(14).
    p = a2.Point(1, 2, 3)
    assert p.kind is a2.Kind.SPHERE
    assert p.dimension == 3
    assert p.exact() == (Fraction(1), Fraction(2), Fraction(3))
    n = math.sqrt(14.0)
    assert p.xyz == pytest.approx((1 / n, 2 / n, 3 / n), abs=1e-15)
    assert p.location == "interior"


def test_point_dimension_mismatches_are_refused():
    # A sphere point needs three coordinates ...
    with pytest.raises(a2.UnsupportedError, match="3-D directions"):
        a2.Point(1, 2, kind="sphere")
    # ... and a planar point exactly two.
    with pytest.raises(a2.UnsupportedError, match="planar"):
        a2.Point(1, 2, 3, kind="segment")


# ===========================================================================
# 3. Point.to_kind
# ===========================================================================

@pytest.mark.parametrize("target", PLANAR_KINDS)
def test_rational_point_converts_to_every_planar_kind(target):
    p = a2.Point(Fraction(3, 4), -2)
    q = p.to_kind(target)
    assert q.kind == a2.Kind[target.upper()]
    # The approximation must survive every conversion exactly (3/4 and -2 are doubles).
    assert q.approx == (0.75, -2.0)


@pytest.mark.parametrize("source", RATIONAL_POINT_KINDS)
def test_rational_point_round_trips_back_to_the_segment_kind(source):
    p = a2.Point(Fraction(3, 4), -2)
    back = p.to_kind(source).to_kind("segment")
    assert back == p
    assert back.exact() == (Fraction(3, 4), Fraction(-2))


def test_conic_point_never_claims_to_be_rational():
    # KNOWN, DOCUMENTED CGAL/CORE limitation (CGAL_TRAPS_CHECKLIST "Numbers /
    # coordinates": CORE has no safe rationality test for Expr).  Even a conic point
    # built from two rationals reports is_rational == False, exact() gives Algebraic
    # values, and exact_rational() / to_kind("segment") raise NotRepresentableError.
    q = a2.Point(Fraction(3, 4), -2).to_kind("conic")
    assert q.is_rational is False
    assert all(isinstance(v, a2.Algebraic) for v in q.exact())
    with pytest.raises(a2.NotRepresentableError):
        q.exact_rational()
    with pytest.raises(a2.NotRepresentableError):
        q.to_kind("segment")
    # The certified interval is still exact for a value that *is* a double.
    assert q.interval() == ((0.75, 0.75), (-2.0, -2.0))


def test_planar_point_to_sphere_is_a_kind_mismatch():
    with pytest.raises(a2.KindMismatchError):
        a2.Point(1, 2).to_kind("sphere")
    assert issubclass(a2.KindMismatchError, TypeError)


@pytest.mark.parametrize("target", PLANAR_KINDS)
def test_sphere_point_to_any_planar_kind_is_a_kind_mismatch(target):
    with pytest.raises(a2.KindMismatchError):
        a2.Point(1, 2, 3).to_kind(target)


def test_point_to_kind_identity_returns_the_same_object():
    p = a2.Point(1, 2)
    assert p.to_kind("segment") is p
    assert p.to_kind(a2.Kind.SEGMENT) is p
    assert p.to_kind(0) is p


def test_sqrt_extension_point_only_converts_to_kinds_that_can_hold_a_square_root():
    # x = 1 + sqrt(2) is irrational, so only circle_segment (sqrt extensions) and conic
    # (algebraic) can hold it exactly.
    p = a2.Point.from_sqrt_extension(a2.SqrtExtension(1, 1, 2), 0)
    assert p.kind is a2.Kind.CIRCLE_SEGMENT
    assert p.is_rational is False
    assert p.to_kind("circle_segment") is p
    assert p.to_kind("conic").approx == pytest.approx((1 + math.sqrt(2), 0.0), abs=1e-15)
    for target in ("segment", "linear", "polyline", "bezier"):
        with pytest.raises(a2.NotRepresentableError):
            p.to_kind(target)


def test_irrational_point_is_not_hashable():
    p = a2.Point.from_sqrt_extension(a2.SqrtExtension(0, 1, 2), 0)
    with pytest.raises(TypeError, match="hashable"):
        hash(p)


# ===========================================================================
# 4. SqrtExtension
# ===========================================================================

def test_sqrt_extension_value_and_accessors():
    s = a2.SqrtExtension(1, 1, 2)  # 1 + sqrt(2)
    assert (s.a, s.b, s.c) == (Fraction(1), Fraction(1), Fraction(2))
    assert all(isinstance(v, Fraction) for v in (s.a, s.b, s.c))
    assert float(s) == s.approx == pytest.approx(1 + math.sqrt(2), abs=1e-15)
    assert s.is_rational is False
    assert s.exact() is None
    assert s.sign() == 1


def test_sqrt_extension_sign_and_bool():
    assert a2.SqrtExtension(0, -1, 3).sign() == -1  # -sqrt(3) < 0
    assert a2.SqrtExtension(0, 0, 0).sign() == 0
    assert bool(a2.SqrtExtension(1, 1, 2)) is True
    assert bool(a2.SqrtExtension(0, 0, 5)) is False
    # 2 - sqrt(3): 3 < 4, so sqrt(3) < 2 and the value is positive.
    assert a2.SqrtExtension(2, -1, 3).sign() == 1
    # 1 - sqrt(3) is negative for the same reason (3 > 1).
    assert a2.SqrtExtension(1, -1, 3).sign() == -1


def test_sqrt_extension_repr_and_str():
    # repr always shows the three coefficients; str is the normalised algebraic form
    # (unit and zero terms elided -- see the number_repr contract in STAGE1_NOTES).
    assert repr(a2.SqrtExtension(1, 2, 3)) == "SqrtExtension(1, 2, 3)"
    assert str(a2.SqrtExtension(1, 1, 2)) == "1 + sqrt(2)"
    assert str(a2.SqrtExtension(0, 1, 3)) == "sqrt(3)"
    assert str(a2.SqrtExtension(0, -1, 3)) == "-sqrt(3)"
    assert str(a2.SqrtExtension(2, -3, 5)) == "2 - 3*sqrt(5)"
    assert str(a2.SqrtExtension(Fraction(1, 2), 3, 2)) == "1/2 + 3*sqrt(2)"


def test_sqrt_extension_recognises_rational_values():
    # b == 0  ->  rational (the radicand is irrelevant).
    r = a2.SqrtExtension(Fraction(3, 4), 0, 5)
    assert r.is_rational is True
    assert r.exact() == Fraction(3, 4)
    assert str(r) == "3/4"  # normalised to a plain rational
    # A perfect-square radicand is rational too: 1 + 2*sqrt(4) == 1 + 2*2 == 5.
    # (CGAL_TRAPS_CHECKLIST: "is_extended()==true can still be rational".)
    q = a2.SqrtExtension(1, 2, 4)
    assert q.is_rational is True
    assert q.exact() == Fraction(5)
    assert float(q) == 5.0


def test_sqrt_extension_hash_matches_the_fraction_when_rational():
    r = a2.SqrtExtension(Fraction(3, 4), 0, 5)
    assert hash(r) == hash(Fraction(3, 4))
    # An irrational value is deliberately unhashable (it compares equal to Algebraic
    # values of the same magnitude, and no cheap hash stays consistent with that).
    with pytest.raises(TypeError, match="unhashable"):
        hash(a2.SqrtExtension(1, 1, 2))


def test_sqrt_extension_interval_brackets_the_exact_value():
    s = a2.SqrtExtension(1, 1, 2)  # 1 + sqrt(2)
    lo, hi = s.interval()
    # Certified: lo <= 1 + sqrt(2) <= hi.  Checked exactly by squaring (both sides of
    # lo - 1 <= sqrt(2) <= hi - 1 are positive), so no floating point is trusted.
    assert (Fraction(lo) - 1) ** 2 <= 2
    assert (Fraction(hi) - 1) ** 2 >= 2
    assert lo <= hi
    # refine() is the same thing for a sqrt extension (it is exact by construction).
    assert s.refine(200) == s.interval(200) == (lo, hi)


def test_sqrt_extension_comparisons_are_exact():
    s = a2.SqrtExtension(1, 1, 2)  # 1 + sqrt(2) = 2.41421356...
    assert s == a2.SqrtExtension(1, 1, 2)
    assert s > 2.41 and s < 2.42
    assert s >= s and s <= s
    # 1 + sqrt(2) vs 1 + sqrt(3): sqrt(2) < sqrt(3), so the first is smaller.  Exact even
    # though the two radicands differ.
    assert s < a2.SqrtExtension(1, 1, 3)
    # An integer-valued extension compares equal to the plain int and Fraction.
    assert a2.SqrtExtension(3, 0, 0) == 3
    assert a2.SqrtExtension(3, 0, 0) == Fraction(3)


def test_sqrt_extension_comparison_with_non_numbers():
    s = a2.SqrtExtension(1, 1, 2)
    # Equality with something that is not number-like degrades to False, never raises.
    assert (s == "abc") is False
    assert (s == object()) is False
    assert (s != object()) is True
    # Ordering against a non-number raises the standard TypeError.
    with pytest.raises(TypeError):
        s < object()


def test_sqrt_extension_negation():
    s = -a2.SqrtExtension(1, 1, 2)  # -(1 + sqrt(2)) = -1 - sqrt(2)
    assert (s.a, s.b, s.c) == (Fraction(-1), Fraction(-1), Fraction(2))
    assert s.sign() == -1
    assert float(s) == pytest.approx(-(1 + math.sqrt(2)), abs=1e-15)


def test_sqrt_extension_rejects_a_negative_radicand():
    with pytest.raises(ValueError, match="radicand"):
        a2.SqrtExtension(0, 1, -1)


def test_sqrt_extension_has_no_arithmetic_operators():
    # DESIGN.md section 3 exposes only .a/.b/.c, float(), .is_rational, .exact() (plus
    # comparisons and unary minus): the exact numbers are values to read, not to compute
    # with.  Pin that so an accidental half-implemented __add__ shows up.
    s = a2.SqrtExtension(1, 1, 2)
    for op in (lambda: s + 1, lambda: s - 1, lambda: s * 2, lambda: abs(s)):
        with pytest.raises(TypeError):
            op()


# ===========================================================================
# 5. Algebraic
# ===========================================================================

def test_algebraic_from_rational_round_trips():
    v = a2.Algebraic.from_rational(Fraction(3, 4))
    assert v.is_rational is True
    assert v.exact() == Fraction(3, 4)
    assert float(v) == 0.75
    assert v.approx == 0.75
    assert v.sign() == 1
    assert v.interval() == (0.75, 0.75)  # 3/4 is a double: degenerate interval
    assert hash(v) == hash(Fraction(3, 4))
    assert repr(v) == "Algebraic(3/4)"
    assert str(v) == "3/4"


def test_algebraic_cannot_be_constructed_directly():
    with pytest.raises(TypeError, match="from_rational"):
        a2.Algebraic()


def _sqrt2_algebraic():
    """The algebraic number sqrt(2), as the x of a conic intersection point.

    The circle x^2 + y^2 = 2 meets the x axis at x = +-sqrt(2); the positive one is
    returned.  This is the only way to obtain a genuinely irrational Algebraic through
    the public API (Algebraic.from_rational only boxes rationals).
    """
    circle = a2.ConicArc.circle((0, 0), squared_radius=2)
    x_axis = a2.ConicArc.segment((-3, 0), (3, 0))
    for arc in circle.make_x_monotone():
        for item in arc.intersect(x_axis):
            if isinstance(item, tuple):
                x = item[0].exact()[0]
                if x.sign() > 0:
                    return x
    raise AssertionError("the circle x^2+y^2=2 must meet the x axis at +sqrt(2)")


def test_algebraic_irrational_value_from_a_conic_intersection():
    v = _sqrt2_algebraic()
    assert isinstance(v, a2.Algebraic)
    # CORE has no sound rationality test, so an Expr is never reported rational.
    assert v.is_rational is False
    assert v.exact() is None
    assert v.sign() == 1
    assert float(v) == pytest.approx(math.sqrt(2.0), abs=1e-15)
    # repr / str mark the value as approximate with a trailing '~'.
    assert repr(v).startswith("Algebraic(") and repr(v).endswith("~)")
    assert str(v).endswith("~")
    with pytest.raises(TypeError, match="unhashable"):
        hash(v)


def test_algebraic_interval_is_certified():
    v = _sqrt2_algebraic()
    lo, hi = v.interval()
    # Certified: lo <= sqrt(2) <= hi, checked exactly by squaring (lo > 0).
    assert Fraction(lo) ** 2 <= 2
    assert Fraction(hi) ** 2 >= 2
    # Refining never widens the interval (CORE only ever refines).
    lo2, hi2 = v.refine(200)
    assert lo2 >= lo and hi2 <= hi


def test_algebraic_compares_exactly_against_rationals_and_sqrt_extensions():
    v = _sqrt2_algebraic()
    # sqrt(2) = 1.414213562373095048801688...; the double 1.4142135623730951 is
    # 1.41421356237309514547... which is strictly LARGER, and 1.414213562373095 is
    # 1.41421356237309492343... which is strictly SMALLER.
    assert v < 1.4142135623730951
    assert v > 1.414213562373095
    # Continued-fraction convergents of sqrt(2) bracket it from both sides:
    # (140/99)^2 = 19600/9801 < 2 (since 2*9801 = 19602 > 19600), so 140/99 < sqrt(2);
    # (99/70)^2  = 9801/4900  > 2 (since 2*4900 = 9800  < 9801),  so 99/70  > sqrt(2).
    assert v > Fraction(140, 99)
    assert v < Fraction(99, 70)
    # A SqrtExtension and an Expr holding the same value compare EQUAL.
    assert v == a2.SqrtExtension(0, 1, 2)
    assert a2.SqrtExtension(0, 1, 2) == v
    assert v != a2.SqrtExtension(0, 1, 3)


def test_algebraic_has_no_arithmetic_operators():
    v = a2.Algebraic.from_rational(1)
    for op in (lambda: v + 1, lambda: v * 2, lambda: -v):
        with pytest.raises(TypeError):
            op()


def test_circle_line_intersection_yields_sqrt_extension_coordinates():
    # The circle x^2 + y^2 = 4 meets the vertical line x = 1 where y^2 = 3,
    # i.e. at y = +-sqrt(3) exactly.
    circle = a2.Circle((0, 0), 2)
    vertical = a2.CircleSegment.segment((1, -3), (1, 3))
    ys = []
    for arc in circle.make_x_monotone():
        for item in arc.intersect(vertical):
            assert isinstance(item, tuple)  # transversal points, not overlaps
            point, multiplicity = item
            assert multiplicity == 1
            x, y = point.exact()
            assert x == Fraction(1)
            assert isinstance(y, a2.SqrtExtension)
            ys.append(y)
    assert len(ys) == 2
    assert {(y.a, y.b, y.c) for y in ys} == {
        (Fraction(0), Fraction(1), Fraction(3)),
        (Fraction(0), Fraction(-1), Fraction(3)),
    }
    assert sorted(float(y) for y in ys) == pytest.approx(
        [-math.sqrt(3.0), math.sqrt(3.0)], abs=1e-15)


def test_tangential_intersection_reports_multiplicity_two():
    # The circle of radius 1 centred at (0, 1) touches the x axis at the origin:
    # a tangency, so CGAL reports multiplicity 2.
    circle = a2.Circle((0, 1), 1)
    x_axis = a2.CircleSegment.segment((-2, 0), (2, 0))
    hits = [item for arc in circle.make_x_monotone() for item in arc.intersect(x_axis)]
    assert len(hits) == 1
    point, multiplicity = hits[0]
    assert multiplicity == 2
    assert point.exact() == (Fraction(0), Fraction(0))


# ===========================================================================
# 6. Curve base API, exercised on Segment
# ===========================================================================

@pytest.fixture
def seg():
    """The segment (0,0) -> (4,2): slope 1/2, length sqrt(20)."""
    return a2.Segment((0, 0), (4, 2))


def test_curve_is_abstract():
    with pytest.raises(TypeError, match="abstract base class"):
        a2.Curve()


def test_segment_basic_identity(seg):
    assert seg.kind is a2.Kind.SEGMENT
    assert seg.dimension == 2
    # Arr_segment_traits_2 stores an X_monotone_curve_2, so a Segment is x-monotone.
    assert seg.is_x_monotone is True
    assert seg.type_name == "x_monotone_curve"
    assert seg.is_bounded is True


def test_segment_endpoints_and_direction(seg):
    assert seg.source == a2.Point(0, 0)
    assert seg.target == a2.Point(4, 2)
    # (0,0) is lexicographically smaller than (4,2), so min == source.
    assert seg.min_vertex == seg.left == a2.Point(0, 0)
    assert seg.max_vertex == seg.right == a2.Point(4, 2)
    assert seg.has_source is True and seg.has_target is True
    assert seg.is_vertical is False
    assert seg.is_directed_right is True
    # Compare_endpoints_xy_2 returns SMALLER (-1) when the curve is directed right.
    assert seg.compare_endpoints_xy() == -1


def test_vertical_segment_is_reported_vertical():
    v = a2.Segment((1, 5), (1, -5))
    assert v.is_vertical is True
    # Directed downwards: source (1,5) > target (1,-5) lexicographically.
    assert v.is_directed_right is False
    assert v.compare_endpoints_xy() == 1
    assert v.min_vertex == a2.Point(1, -5)


def test_segment_parameter_space_is_interior(seg):
    # A bounded planar curve has both ends in the interior of the parameter space;
    # CGAL's ARR_INTERIOR is 4.
    assert seg.parameter_space_in_x(0) == 4
    assert seg.parameter_space_in_x(1) == 4
    assert seg.parameter_space_in_y(0) == 4
    assert seg.parameter_space_in_y(1) == 4


def test_segment_bbox(seg):
    assert seg.bbox() == (0.0, 0.0, 4.0, 2.0)


def test_segment_approximate_returns_the_two_endpoints(seg):
    # The segment traits ignores the tolerance and always emits both endpoints
    # (CGAL_TRAPS_CHECKLIST, "Rendering / approximation").
    assert seg.approximate() == [(0.0, 0.0), (4.0, 2.0)]
    assert seg.approximate(1e-12) == [(0.0, 0.0), (4.0, 2.0)]


def test_segment_approximate_length(seg):
    # |(4,2)| = sqrt(16 + 4) = sqrt(20) = 2*sqrt(5).
    assert seg.approximate_length() == pytest.approx(math.sqrt(20.0), abs=1e-15)


def test_approximate_rejects_a_non_positive_tolerance(seg):
    # A non-positive tolerance makes CGAL's recursive subdividers loop forever
    # (CGAL_TRAPS_CHECKLIST), so it is refused before reaching CGAL.
    for bad in (0.0, -1.0):
        with pytest.raises(ValueError, match="tolerance"):
            seg.approximate(bad)


def test_unbounded_curve_needs_a_clipping_box():
    line = a2.LinearCurve.line((0, 0), (1, 1))
    assert line.is_bounded is False
    with pytest.raises(ValueError, match="clipping box"):
        line.approximate()
    # y = x clipped to [-1,1]^2 runs from (-1,-1) to (1,1).
    assert line.approximate(1e-3, (-1, -1, 1, 1)) == [(-1.0, -1.0), (1.0, 1.0)]


def test_segment_opposite(seg):
    o = seg.opposite()
    assert o.source == a2.Point(4, 2)
    assert o.target == a2.Point(0, 0)
    assert o.is_directed_right is False
    # Equality is geometric, so the reversed segment is still "the same curve".
    assert o == seg


def test_segment_split(seg):
    # (2,1) is the midpoint of (0,0)-(4,2) and lies on the segment.
    left, right = seg.split((2, 1))
    assert (left.source, left.target) == (a2.Point(0, 0), a2.Point(2, 1))
    assert (right.source, right.target) == (a2.Point(2, 1), a2.Point(4, 2))


def test_segment_split_needs_an_interior_point_on_the_curve(seg):
    # arr2d validates the split point itself for every kind (CGAL's geodesic Split_2 has no
    # such precondition), so these are plain ValueErrors, not CGAL PreconditionErrors.
    # (1,1) is above the segment (the segment is at y = 0.5 for x = 1) ...
    with pytest.raises(ValueError, match="does not lie on the curve"):
        seg.split((1, 1))
    # ... and an endpoint is not an interior point.
    with pytest.raises(ValueError, match="endpoint"):
        seg.split((0, 0))


def test_segment_trim(seg):
    # The sub-segment between (1, 1/2) and (3, 3/2) -- both on y = x/2.
    t = seg.trim((1, Fraction(1, 2)), (3, Fraction(3, 2)))
    assert t.source == a2.Point(1, Fraction(1, 2))
    assert t.target == a2.Point(3, Fraction(3, 2))


def test_segment_trim_outside_the_x_range_is_refused(seg):
    with pytest.raises(a2.PreconditionError):
        seg.trim((0, 0), (9, 9))


def test_segment_merge_and_can_merge(seg):
    left, right = seg.split((2, 1))
    assert left.can_merge(right) is True
    merged = left.merge(right)
    assert merged.source == a2.Point(0, 0)
    assert merged.target == a2.Point(4, 2)
    assert merged == seg


def test_segment_merge_refuses_unmergeable_curves(seg):
    # Disjoint, and collinear-but-not-touching, are both unmergeable ...
    other = a2.Segment((9, 9), (10, 10))
    assert seg.can_merge(other) is False
    with pytest.raises(ValueError, match="mergeable"):
        seg.merge(other)
    # ... and so is a touching but non-collinear continuation.
    bend = a2.Segment((4, 2), (5, 9))
    assert seg.can_merge(bend) is False


def test_segment_intersect_transversal(seg):
    # (0,0)-(4,2) and (0,2)-(4,0) cross where x/2 = 2 - x/2, i.e. x = 2, y = 1.
    other = a2.Segment((0, 2), (4, 0))
    result = seg.intersect(other)
    assert len(result) == 1
    point, multiplicity = result[0]
    assert point == a2.Point(2, 1)
    assert multiplicity == 1  # transversal crossing


def test_segment_intersect_overlap_returns_a_curve(seg):
    # (2,1)-(6,3) lies on the same line y = x/2; the common part is (2,1)-(4,2).
    result = seg.intersect(a2.Segment((2, 1), (6, 3)))
    assert len(result) == 1
    overlap = result[0]
    assert isinstance(overlap, a2.Curve)
    assert overlap.min_vertex == a2.Point(2, 1)
    assert overlap.max_vertex == a2.Point(4, 2)


def test_segment_intersect_disjoint_is_empty(seg):
    assert seg.intersect(a2.Segment((0, 5), (4, 6))) == []


def test_segment_intersect_needs_a_curve(seg):
    with pytest.raises(TypeError, match="expected a Curve"):
        seg.intersect(a2.Point(0, 0))


def test_segment_is_in_x_range(seg):
    # The x range is [0, 4]; the y coordinate is irrelevant.
    assert seg.is_in_x_range((2, 100)) is True
    assert seg.is_in_x_range((0, -100)) is True
    assert seg.is_in_x_range((4, 0)) is True
    assert seg.is_in_x_range((5, 0)) is False
    assert seg.is_in_x_range((-1, 0)) is False


def test_segment_compare_y_at_x(seg):
    # The segment is y = x/2, so at x = 2 it passes through (2,1).
    assert seg.compare_y_at_x((2, 1)) == 0
    assert seg.compare_y_at_x((2, 0)) == -1   # the point is below the curve
    assert seg.compare_y_at_x((2, 5)) == 1    # the point is above the curve
    # Outside the x range the predicate has no meaning and is refused.
    with pytest.raises(ValueError, match="x-range"):
        seg.compare_y_at_x((9, 9))


def test_segment_compare_y_at_x_left_and_right():
    # Two segments crossing at (2,1): "up" has slope +1/2, "down" slope -1/2.
    up = a2.Segment((0, 0), (4, 2))
    down = a2.Segment((0, 2), (4, 0))
    p = a2.Point(2, 1)
    # Immediately right of x = 2, "up" is above "down".
    assert up.compare_y_at_x_right(down, p) == 1
    assert down.compare_y_at_x_right(up, p) == -1
    # Immediately left of x = 2 the order is reversed.
    assert up.compare_y_at_x_left(down, p) == -1
    assert down.compare_y_at_x_left(up, p) == 1


def test_segment_equality_is_geometric_and_ignores_direction(seg):
    assert seg == a2.Segment((0, 0), (4, 2))
    assert seg == a2.Segment((4, 2), (0, 0))  # same point set
    assert seg != a2.Segment((0, 0), (4, 3))
    # A curve of another kind is not comparable -> Python falls back to identity.
    assert (seg == seg.to_kind("polyline")) is False


def test_curves_are_not_hashable(seg):
    with pytest.raises(TypeError, match="not hashable"):
        hash(seg)


def test_segment_make_x_monotone_and_to_curve(seg):
    pieces = seg.make_x_monotone()
    assert len(pieces) == 1
    assert pieces[0] == seg
    assert seg.x_monotone() == seg
    assert seg.to_curve() == seg


def test_segment_supporting_line(seg):
    # CGAL's Line_2(p, q) has (a, b, c) = (p.y - q.y, q.x - p.x, p.x*q.y - p.y*q.x).
    # For (0,0) -> (4,2): a = -2, b = 4, c = 0, i.e. -2x + 4y = 0  <=>  y = x/2.
    a, b, c = seg.supporting_line
    assert (a, b, c) == (Fraction(-2), Fraction(4), Fraction(0))
    # Sanity: both endpoints satisfy the equation exactly.
    for x, y in ((Fraction(0), Fraction(0)), (Fraction(4), Fraction(2))):
        assert a * x + b * y + c == 0


def test_segment_from_coordinates_matches_the_point_constructor():
    assert a2.Segment.from_coordinates(0, 0, 1, 1) == a2.Segment((0, 0), (1, 1))


def test_degenerate_segment_is_refused():
    with pytest.raises(ValueError, match="distinct"):
        a2.Segment((1, 1), (1, 1))


def test_non_x_monotone_curve_refuses_the_x_monotone_accessors():
    # A full circle splits into two x-monotone arcs, so .source has no meaning.
    circle = a2.Circle((0, 0), 1)
    assert circle.is_x_monotone is False
    with pytest.raises(a2.NotXMonotoneError):
        circle.source
    assert len(circle.make_x_monotone()) == 2


# ===========================================================================
# 7. Curve.to_kind
# ===========================================================================

@pytest.mark.parametrize("target", PLANAR_KINDS)
def test_segment_converts_to_every_planar_kind(target):
    seg = a2.Segment((0, 0), (4, 2))
    c = seg.to_kind(target)
    assert isinstance(c, a2.Curve)
    assert c.kind == a2.Kind[target.upper()]
    # Every planar kind keeps the same two endpoints.
    assert c.bbox() == pytest.approx((0.0, 0.0, 4.0, 2.0), abs=1e-12)


@pytest.mark.parametrize("source", ("linear", "circle_segment", "polyline", "bezier"))
def test_segment_conversion_round_trips(source):
    seg = a2.Segment((0, 0), (4, 2))
    back = seg.to_kind(source).to_kind("segment")
    assert back == seg


def test_conic_curve_cannot_be_converted_back_to_a_segment():
    # The round trip fails only because a Conic_point_2 is CORE::Expr and is never
    # reported rational (see test_conic_point_never_claims_to_be_rational).
    conic = a2.Segment((0, 0), (4, 2)).to_kind("conic")
    with pytest.raises(a2.NotRepresentableError, match="algebraic"):
        conic.to_kind("segment")


def test_segment_to_kind_identity_returns_the_same_object():
    seg = a2.Segment((0, 0), (1, 1))
    assert seg.to_kind("segment") is seg
    assert seg.to_kind(a2.Kind.SEGMENT) is seg


def test_to_kind_rejects_an_unknown_kind_name():
    with pytest.raises(ValueError, match="unknown geometry kind"):
        a2.Segment((0, 0), (1, 1)).to_kind("nope")


def test_polyline_converts_into_one_curve_per_sub_segment():
    poly = a2.Polyline([(0, 0), (2, 2), (4, 0)])
    segments = poly.to_kind("segment")
    assert isinstance(segments, list) and len(segments) == 2
    assert segments[0] == a2.Segment((0, 0), (2, 2))
    assert segments[1] == a2.Segment((2, 2), (4, 0))
    # ... and the same for every other straight-curve kind.
    assert len(poly.to_kind("bezier")) == 2
    assert len(poly.to_kind("conic")) == 2


def test_unbounded_linear_curves_have_no_bounded_image():
    ray = a2.LinearCurve.ray((0, 0), (1, 1))
    line = a2.LinearCurve.line((0, 0), (1, 1))
    for curve in (ray, line):
        for target in ("segment", "circle_segment", "polyline"):
            with pytest.raises(a2.NotRepresentableError, match="unbounded"):
                curve.to_kind(target)
        for target in ("bezier", "conic"):
            with pytest.raises(a2.UnsupportedError):
                curve.to_kind(target)
    # The identity conversion still works.
    assert ray.to_kind("linear") is ray


def test_planar_curves_do_not_lift_onto_the_sphere():
    for curve in (a2.Segment((0, 0), (1, 1)),
                  a2.Polyline([(0, 0), (1, 1), (2, 0)]),
                  a2.Circle((0, 0), 1)):
        with pytest.raises(a2.UnsupportedError, match="sphere"):
            curve.to_kind("sphere")


def test_geodesic_arc_does_not_project_into_the_plane():
    arc = a2.GeodesicArc.from_points((1, 0, 0), (0, 1, 0))
    with pytest.raises(a2.NotRepresentableError):
        arc.to_kind("segment")
    with pytest.raises(a2.KindMismatchError):
        arc.to_kind("polyline")
    with pytest.raises(a2.KindMismatchError):
        arc.to_kind("conic")
    for target in ("linear", "bezier"):
        with pytest.raises(a2.UnsupportedError):
            arc.to_kind(target)


def test_linear_bezier_is_a_segment_but_a_quadratic_one_is_not():
    line_bezier = a2.BezierCurve([(0, 0), (4, 2)])
    assert line_bezier.to_kind("segment") == a2.Segment((0, 0), (4, 2))
    # B(t) = (1-t)^2 P0 + 2t(1-t) P1 + t^2 P2 with P1 = (2,4) bulges away from the chord,
    # so it is provably not the straight chord (0,0)-(4,0).
    curved = a2.BezierCurve([(0, 0), (2, 4), (4, 0)])
    with pytest.raises(a2.NotRepresentableError, match="chord"):
        curved.to_kind("segment")


def test_quadratic_bezier_is_exactly_a_parabolic_conic_arc():
    # B(t) = 2t(1-t)(1,2) + t^2 (2,0) = (2t, 4t - 4t^2), so x = 2t and
    # y = 2x - x^2, i.e. x^2 - 2x + y = 0  ->  (r,s,t,u,v,w) = (1,0,0,-2,1,0).
    conic = a2.BezierCurve([(0, 0), (1, 2), (2, 0)]).to_kind("conic")
    assert conic.conic_type == "parabola"
    r, s, t, u, v, w = conic.coefficients
    # CGAL integerises and may negate the whole tuple; compare up to a non-zero scalar.
    scale = r  # r != 0 for this parabola
    assert scale != 0
    assert (r / scale, s / scale, t / scale, u / scale, v / scale, w / scale) == (
        Fraction(1), Fraction(0), Fraction(0), Fraction(-2), Fraction(1), Fraction(0))


def test_circle_converts_to_a_conic_but_not_to_a_straight_kind():
    circle = a2.Circle((0, 0), 2)  # x^2 + y^2 = 4
    conic = circle.to_kind("conic")
    assert conic.is_full is True
    r, s, t, u, v, w = conic.coefficients
    # Up to a non-zero scalar this must be x^2 + y^2 - 4 = 0.
    assert (s / r, t / r, u / r, v / r, w / r) == (
        Fraction(1), Fraction(0), Fraction(0), Fraction(0), Fraction(-4))
    with pytest.raises(a2.NotRepresentableError):
        circle.to_kind("segment")
    with pytest.raises(a2.UnsupportedError):
        circle.to_kind("linear")


def test_circle_segment_curves_have_no_polyline_image():
    # DOCUMENTED CGAL limitation (CGAL_TRAPS_CHECKLIST, "Circle-segment kind"):
    # Arr_circle_segment_traits_2 has no Construct_x_monotone_curve_2, so the polyline
    # kind cannot *prove* that a circle-segment curve is straight and refuses it -- even
    # for a curve that is in fact a straight segment.
    straight = a2.CircleSegment.segment((0, 0), (4, 2))
    assert straight.is_linear is True
    with pytest.raises(a2.NotRepresentableError, match="straight curve"):
        straight.to_kind("polyline")
    # The kinds that can prove it accept the very same curve.
    assert straight.to_kind("segment") == a2.Segment((0, 0), (4, 2))
    assert straight.to_kind("linear").is_segment is True


def test_curve_conversions_that_need_algebraic_endpoints_are_refused():
    conic_arc = a2.ConicArc.segment((0, 0), (4, 2))
    for target in ("circle_segment", "polyline"):
        with pytest.raises(a2.NotRepresentableError):
            conic_arc.to_kind(target)
    for target in ("linear", "bezier"):
        with pytest.raises(a2.UnsupportedError):
            conic_arc.to_kind(target)


# ===========================================================================
# 8. Polygon / PolygonWithHoles
# ===========================================================================

@pytest.fixture
def triangle():
    """The counterclockwise triangle (0,0), (4,0), (0,3): area 6."""
    return a2.Polygon([(0, 0), (4, 0), (0, 3)])


def test_polygon_from_points_builds_a_closed_chain(triangle):
    assert len(triangle) == 3
    assert triangle.is_closed is True
    assert triangle.kind is a2.Kind.SEGMENT
    assert triangle.points == [a2.Point(0, 0), a2.Point(4, 0), a2.Point(0, 3)]
    assert triangle.curves[0] == a2.Segment((0, 0), (4, 0))


def test_polygon_area_and_orientation_are_exact_for_segments(triangle):
    # Shoelace: 1/2 * |x1(y2-y3) + x2(y3-y1) + x3(y1-y2)| = 1/2 * (4*3) = 6, positive
    # because (0,0) -> (4,0) -> (0,3) turns counterclockwise.
    assert triangle.area() == Fraction(6)
    assert isinstance(triangle.area(), Fraction)
    assert triangle.orientation() == 1
    reversed_ = triangle.reverse()
    assert reversed_.orientation() == -1
    assert reversed_.area() == Fraction(-6)


def test_polygon_bbox_and_approximate(triangle):
    assert triangle.bbox() == (0.0, 0.0, 4.0, 3.0)
    # The closing point is not repeated.
    assert triangle.approximate() == [(0.0, 0.0), (4.0, 0.0), (0.0, 3.0)]


def test_polygon_closing_point_is_dropped_and_short_input_refused():
    closed = a2.Polygon([(0, 0), (4, 0), (0, 3), (0, 0)])
    assert len(closed) == 3
    with pytest.raises(ValueError, match="at least 3 distinct points"):
        a2.Polygon([(0, 0), (4, 0)])


def test_polygon_from_curves_must_chain():
    with pytest.raises(ValueError, match="broken"):
        a2.Polygon([a2.Segment((0, 0), (1, 0)), a2.Segment((2, 0), (3, 0))])


@pytest.mark.parametrize("target", ("segment", "linear", "circle_segment", "polyline",
                                    "bezier", "conic"))
def test_polygon_to_kind_converts_every_boundary_curve(triangle, target):
    p = triangle.to_kind(target)
    assert p.kind == a2.Kind[target.upper()]
    assert len(p) == 3
    assert p.is_closed is True
    # The shape is unchanged: same orientation and same approximated outline.
    assert p.orientation() == 1
    assert p.approximate() == [(0.0, 0.0), (4.0, 0.0), (0.0, 3.0)]


def test_polygon_to_kind_identity_returns_the_same_object(triangle):
    assert triangle.to_kind("segment") is triangle
    assert triangle.to_kind(a2.Kind.SEGMENT) is triangle


def test_polygon_to_kind_sphere_is_refused(triangle):
    with pytest.raises(a2.UnsupportedError, match="sphere"):
        triangle.to_kind("sphere")


def test_polygon_area_is_approximate_for_non_segment_kinds(triangle):
    # Only the SEGMENT kind has an exact rational area; the others fall back to the
    # signed area of approximate(1e-3), a float.
    area = triangle.to_kind("conic").area()
    assert isinstance(area, float)
    assert area == pytest.approx(6.0, abs=1e-6)


def test_polygon_equality_compares_curves_and_kind(triangle):
    assert triangle == a2.Polygon([(0, 0), (4, 0), (0, 3)])
    # Same shape, different kind -> not equal.
    assert (triangle == triangle.to_kind("polyline")) is False
    with pytest.raises(TypeError, match="not hashable"):
        hash(triangle)


@pytest.mark.parametrize("kind_name", ("segment", "linear", "polyline", "conic"))
def test_polygon_from_points_accepts_a_target_kind(kind_name):
    p = a2.Polygon([(0, 0), (4, 0), (0, 3)], kind=kind_name)
    assert p.kind == a2.Kind[kind_name.upper()]
    assert len(p) == 3
    assert p.is_closed is True
    assert p.orientation() == 1


def test_polygon_from_points_accepts_the_circle_segment_kind():
    # DESIGN.md section 3: ``Polygon(points_or_curves, kind=None)`` turns points into
    # straight boundary curves of that kind, and _geometry.pxi::_make_straight has a
    # dedicated circle_segment branch (only 'bezier' is documented as unsupported).
    p = a2.Polygon([(0, 0), (4, 0), (0, 3)], kind="circle_segment")
    assert p.kind is a2.Kind.CIRCLE_SEGMENT
    assert len(p) == 3
    assert p.is_closed is True
    assert p.orientation() == 1


def test_polygon_from_points_is_unsupported_for_the_bezier_kind():
    # Documented: the Bezier traits has no straight x-monotone curve constructor.
    with pytest.raises(a2.UnsupportedError, match="bezier"):
        a2.Polygon([(0, 0), (4, 0), (0, 3)], kind="bezier")


def test_polygon_with_holes_accessors():
    outer = a2.Polygon([(0, 0), (4, 0), (4, 4), (0, 4)])
    hole = a2.Polygon([(1, 1), (2, 1), (2, 2), (1, 2)])
    pwh = a2.PolygonWithHoles(outer, [hole])
    assert pwh.outer is outer
    assert pwh.holes == (hole,)
    assert pwh.is_unbounded is False
    assert pwh.kind is a2.Kind.SEGMENT
    assert pwh.bbox() == (0.0, 0.0, 4.0, 4.0)
    outline, holes = pwh.approximate()
    assert outline == [(0.0, 0.0), (4.0, 0.0), (4.0, 4.0), (0.0, 4.0)]
    assert len(holes) == 1 and len(holes[0]) == 4
    # The unbounded polygon has no outer boundary; its bbox comes from the holes.
    unbounded = a2.PolygonWithHoles(None, [hole])
    assert unbounded.is_unbounded is True
    assert unbounded.outer is None
    assert unbounded.bbox() == (1.0, 1.0, 2.0, 2.0)
    assert unbounded.approximate()[0] is None


# ===========================================================================
# 9. repr of every geometry class
# ===========================================================================

def test_repr_of_points():
    assert repr(a2.Point(1, Fraction(3, 2))) == "Point(1, 3/2)"
    assert repr(a2.Point(1, 2, kind="linear")) == "Point(1, 2)"
    assert repr(a2.Point(1, 2, kind="polyline")) == "Point(1, 2)"
    assert repr(a2.Point(1, 2, kind="bezier")) == "BezierPoint(1, 2)"
    # A conic point is algebraic: the coordinates are marked approximate with '~'.
    assert repr(a2.Point(1, 2, kind="conic")) == "ConicPoint(~1, ~2)"
    assert repr(a2.Point(0, 0, 1)) == "SpherePoint(0, 0, 1)"
    # NOTE: the circle-segment point repr is the bare coordinate tuple -- it does not
    # name a class the way every other kind does (reported as an inconsistency).
    assert repr(a2.Point(1, 2, kind="circle_segment")) == "(1, 2)"


def test_repr_of_sqrt_extension_point():
    p = a2.Point.from_sqrt_extension(a2.SqrtExtension(1, 1, 2), 0)
    assert repr(p) == "(1 + 1*sqrt(2), 0)"


def test_repr_of_segment_and_linear_curves():
    assert repr(a2.Segment((0, 0), (2, 0))) == "Segment((0, 0), (2, 0))"
    assert repr(a2.LinearCurve.segment((0, 0), (2, 0))) == "Segment((0, 0), (2, 0))"
    assert repr(a2.Ray((0, 0), (1, 2))) == "Ray((0, 0), direction=(1, 2))"
    # Line((0,0),(1,2)) is -2x + y = 0 (CGAL's Line_2(p, q) coefficients).
    assert repr(a2.Line((0, 0), (1, 2))) == "Line(a=-2, b=1, c=0)"
    assert repr(a2.line_from_coefficients(1, -1, 0)) == "Line(a=1, b=-1, c=0)"


def test_repr_of_circle_segment_curves():
    assert repr(a2.Circle((0, 0), 2)) == \
        "Circle(center=(0, 0), squared_radius=4, orientation=ccw)"
    arc = a2.CircleSegment.arc((0, 0), 1, source=(1, 0), target=(0, 1))
    assert repr(arc) == ("CircularArc(center=(0, 0), squared_radius=1, "
                         "orientation=ccw, source=(1, 0), target=(0, 1))")
    assert repr(a2.CircleSegment.segment((0, 0), (2, 1))) == "Segment((0, 0), (2, 1))"


def test_repr_of_polyline_and_bezier():
    assert repr(a2.Polyline([(0, 0), (1, 1), (2, 0)])) == \
        "Polyline([(0, 0), (1, 1), (2, 0)])"
    assert repr(a2.BezierCurve([(0, 0), (1, 3), (2, 0)])) == \
        "BezierCurve([(0, 0), (1, 3), (2, 0)])"


def test_repr_of_conic_curves():
    # A full conic prints "Conic(full ...)", an arc prints "ConicArc(...)".
    assert repr(a2.ConicArc.circle((0, 0), 2)).startswith("Conic(full ")
    assert repr(a2.ConicArc.segment((0, 0), (2, 1))).startswith("ConicArc(")
    assert "orientation=" in repr(a2.ConicArc.circle((0, 0), 2))


def test_repr_of_geodesic_arcs():
    assert repr(a2.GeodesicArc.from_points((1, 0, 0), (0, 1, 0))) == \
        "GeodesicArc(source=(1, 0, 0), target=(0, 1, 0), normal=(0, 0, 1))"
    assert repr(a2.GeodesicArc.great_circle((0, 0, 1))) == "GreatCircle(normal=(0, 0, 1))"


def test_repr_of_polygons():
    tri = a2.Polygon([(0, 0), (4, 0), (0, 3)])
    assert repr(tri) == "<Polygon kind='segment' curves=3 closed=True>"
    square = a2.Polygon([(0, 0), (4, 0), (4, 4), (0, 4)])
    hole = a2.Polygon([(1, 1), (2, 1), (2, 2), (1, 2)])
    assert repr(a2.PolygonWithHoles(square, [hole])) == \
        "<PolygonWithHoles kind='segment' outer=(4 curves) holes=1>"
    assert repr(a2.PolygonWithHoles(None, [hole])) == \
        "<PolygonWithHoles kind='segment' outer=unbounded holes=1>"


def test_repr_of_numbers():
    assert repr(a2.SqrtExtension(1, 2, 3)) == "SqrtExtension(1, 2, 3)"
    assert repr(a2.Algebraic.from_rational(Fraction(1, 3))) == "Algebraic(1/3)"


# ===========================================================================
# 10. kind-specific geometry accessors that carry exact numbers
# ===========================================================================

def test_circle_keeps_a_rational_radius_when_it_was_given_one():
    # The radius constructor is the preferred one: CGAL then keeps the vertical tangency
    # points rational (CGAL_TRAPS_CHECKLIST, "Circle-segment kind").
    c = a2.Circle((0, 0), 2)
    assert c.has_rational_radius is True
    assert c.squared_radius == Fraction(4)
    assert c.radius == Fraction(2)
    assert c.is_full is True and c.is_circular is True and c.is_linear is False
    assert c.orientation == 1
    assert c.center == a2.Point(0, 0, kind="circle_segment")
    # With squared_radius=2 the radius is irrational and comes back as a float.
    d = a2.CircleSegment.circle((0, 0), squared_radius=2)
    assert d.has_rational_radius is False
    assert d.squared_radius == Fraction(2)
    assert isinstance(d.radius, float)
    assert d.radius == pytest.approx(math.sqrt(2.0), abs=1e-15)


def test_circle_arc_approximation_stays_on_the_circle():
    # The quarter arc of the unit circle from (1,0) to (0,1): every emitted point must
    # lie on the circle, and the endpoints are always emitted exactly.
    arc = a2.CircleSegment.arc((0, 0), 1, source=(1, 0), target=(0, 1))
    pts = arc.approximate(1e-3)
    assert pts[0] == (1.0, 0.0) and pts[-1] == (0.0, 1.0)
    assert max(abs(math.hypot(x, y) - 1.0) for x, y in pts) < 1e-12
    # Arc length pi/2, approximated from the chords.
    assert arc.approximate_length(1e-6) == pytest.approx(math.pi / 2, abs=1e-5)
    assert arc.bbox() == (0.0, 0.0, 1.0, 1.0)


def test_linear_curve_flavours_and_exact_coefficients():
    seg = a2.LinearCurve.segment((0, 0), (4, 2))
    ray = a2.LinearCurve.ray((0, 0), (1, 1))
    line = a2.LinearCurve.line((0, 0), (1, 2))
    assert (seg.which, ray.which, line.which) == ("segment", "ray", "line")
    assert (seg.is_segment, ray.is_ray, line.is_line) == (True, True, True)
    # target - source for a segment; the given direction for a ray.
    assert seg.direction == (Fraction(4), Fraction(2))
    assert ray.direction == (Fraction(1), Fraction(1))
    # Line_2((0,0), (1,2)): a = 0-2 = -2, b = 1-0 = 1, c = 0*2 - 0*1 = 0.
    assert line.supporting_line == (Fraction(-2), Fraction(1), Fraction(0))
    assert line.has_source is False and line.has_target is False
    assert ray.has_source is True and ray.has_target is False


def test_polyline_vertex_and_segment_accessors():
    pl = a2.Polyline([(0, 0), (1, 1), (2, 0), (3, 3)])
    assert pl.number_of_points == len(pl) == 4
    assert pl.number_of_subcurves == 3
    assert pl.points == [a2.Point(0, 0), a2.Point(1, 1), a2.Point(2, 0), a2.Point(3, 3)]
    assert pl[0] == a2.Point(0, 0)
    assert pl[-1] == a2.Point(3, 3)
    assert pl[0:2] == [a2.Point(0, 0), a2.Point(1, 1)]
    assert pl.segments[1] == a2.Segment((1, 1), (2, 0))
    assert list(pl) == pl.points
    with pytest.raises(IndexError):
        pl[4]
    # The chain goes up, down and up again, so it is not x-monotone.
    assert pl.is_x_monotone is False
    assert pl.bbox() == (0.0, 0.0, 3.0, 3.0)


def test_bezier_evaluation_is_exact_for_a_rational_parameter():
    # B(t) = (1-t)^2 (0,0) + 2t(1-t)(2,4) + t^2 (4,0).
    # B(1/2) = 1/4*(0,0) + 1/2*(2,4) + 1/4*(4,0) = (1 + 1, 2) = (2, 2).
    bz = a2.BezierCurve([(0, 0), (2, 4), (4, 0)])
    assert bz.degree == 2
    assert bz.control_points == [a2.Point(0, 0), a2.Point(2, 4), a2.Point(4, 0)]
    exact_mid = bz.evaluate(Fraction(1, 2))
    assert exact_mid.kind is a2.Kind.BEZIER
    assert exact_mid.exact_rational() == (Fraction(2), Fraction(2))
    # A float parameter gives an approximate (x, y) tuple instead.
    assert bz.evaluate(0.5) == pytest.approx((2.0, 2.0), abs=1e-12)
    # B(1/4) = 9/16*(0,0) + 3/8*(2,4) + 1/16*(4,0) = (3/4 + 1/4, 3/2) = (1, 3/2).
    assert bz.evaluate(Fraction(1, 4)).exact_rational() == (Fraction(1), Fraction(3, 2))
    assert bz.has_self_intersections is False


def test_bezier_sample_hits_the_endpoints():
    bz = a2.BezierCurve([(0, 0), (2, 4), (4, 0)])
    pts = bz.sample(0.0, 1.0, 3)
    assert len(pts) == 3
    assert pts[0] == pytest.approx((0.0, 0.0), abs=1e-12)
    assert pts[1] == pytest.approx((2.0, 2.0), abs=1e-12)
    assert pts[2] == pytest.approx((4.0, 0.0), abs=1e-12)
    with pytest.raises(ValueError, match="n >= 2"):
        bz.sample(0.0, 1.0, 1)


def test_rational_quadratic_bezier_is_the_exact_circular_arc():
    # The classic rational quadratic Bezier for a quarter of the unit circle:
    # P0 = (1,0), P1 = (1,1), P2 = (0,1) with weights (1, 1, 2) gives
    # x = (1 - t^2)/(1 + t^2), y = 2t/(1 + t^2)  ->  x^2 + y^2 = 1.
    arc = a2.ConicArc.from_rational_bezier((1, 0), (1, 1), (0, 1), 1, 1, 2)
    assert arc.conic_type == "ellipse"
    r, s, t, u, v, w = arc.coefficients
    # Up to a non-zero scalar: x^2 + y^2 - 1 = 0.
    assert (s / r, t / r, u / r, v / r, w / r) == (
        Fraction(1), Fraction(0), Fraction(0), Fraction(0), Fraction(-1))


def test_bezier_from_rational_is_the_same_construction_from_the_bezier_side():
    # DESIGN.md section 3: ``BezierCurve.from_rational(points, weights) -> ConicArc``
    # for degree 2 (3 control points).  It must build exactly the same conic as
    # ConicArc.from_rational_bezier -- the quarter of the unit circle here.
    same = a2.BezierCurve.from_rational([(1, 0), (1, 1), (0, 1)], [1, 1, 2])
    assert same.kind is a2.Kind.CONIC
    expected = a2.ConicArc.from_rational_bezier((1, 0), (1, 1), (0, 1), 1, 1, 2)
    assert same.coefficients == expected.coefficients
    # The degree-1 case must likewise agree with ConicArc.segment: the supporting line of
    # (0,0)-(4,2) is -2x + 4y = 0, and CGAL stores it as (0, 0, 0, 1, -2, 0).
    linear = a2.BezierCurve.from_rational([(0, 0), (4, 2)])
    assert linear.coefficients == a2.ConicArc.segment((0, 0), (4, 2)).coefficients


def test_rational_bezier_of_degree_three_is_not_representable():
    with pytest.raises(a2.NotRepresentableError, match="degree 3"):
        a2.BezierCurve.from_rational([(0, 0), (1, 1), (2, 1), (3, 0)])


def test_conic_from_five_points_recovers_the_circle():
    # Five points of x^2 + y^2 = 25, in arc order from (-5,0) to (5,0).
    arc = a2.ConicArc.from_points((-5, 0), (-4, 3), (0, 5), (4, 3), (5, 0))
    assert arc.conic_type == "ellipse"
    assert arc.is_full is False
    r, s, t, u, v, w = arc.coefficients
    assert (s / r, t / r, u / r, v / r, w / r) == (
        Fraction(1), Fraction(0), Fraction(0), Fraction(0), Fraction(-25))


def test_conic_ellipse_coefficients():
    # Semi-axes a = 3 along x and b = 2 along y: x^2/9 + y^2/4 = 1, i.e.
    # 4x^2 + 9y^2 - 36 = 0.
    ell = a2.ConicArc.ellipse((0, 0), 3, 2)
    assert ell.is_full is True
    assert ell.conic_type == "ellipse"
    r, s, t, u, v, w = ell.coefficients
    assert (s / r, t / r, u / r, v / r, w / r) == (
        Fraction(9, 4), Fraction(0), Fraction(0), Fraction(0), Fraction(-9))


def test_geodesic_arc_accessors():
    # The quarter of the equator from (1,0,0) to (0,1,0); its great circle is the
    # equator, whose normal is the north pole (0,0,1) = (1,0,0) x (0,1,0).
    arc = a2.GeodesicArc.from_points((1, 0, 0), (0, 1, 0))
    assert arc.source == a2.Point(1, 0, 0)
    assert arc.target == a2.Point(0, 1, 0)
    assert arc.normal == a2.Point(0, 0, 1)
    assert (arc.is_full, arc.is_vertical, arc.is_meridian, arc.is_degenerate) == \
        (False, False, False, False)
    assert arc.bbox() == (0.0, 0.0, 0.0, 1.0, 1.0, 0.0)
    # Every sampled point is a unit vector on the z = 0 plane.
    pts = arc.approximate(1e-2)
    assert pts[0] == pytest.approx((1.0, 0.0, 0.0), abs=1e-15)
    assert pts[-1] == pytest.approx((0.0, 1.0, 0.0), abs=1e-15)
    for x, y, z in pts:
        assert z == pytest.approx(0.0, abs=1e-15)
        assert math.sqrt(x * x + y * y + z * z) == pytest.approx(1.0, abs=1e-12)
