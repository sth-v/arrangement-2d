"""Tests for the ``conic`` geometry kind (``Arr_conic_traits_2``).

Every expected number below is hand-derived; the derivation is written in the
comment next to the assertion.  Conventions used throughout:

* a conic is ``r x^2 + s y^2 + t x y + u x + v y + w = 0``;
* :attr:`ConicArc.coefficients` reports what **CGAL stored**, i.e. the input
  integerised and globally negated whenever the requested traversal orientation
  differs from the one implied by the coefficient signs (docs/dev/STAGE2_NOTES.md,
  "conic::coefficients() returns what CGAL STORED"), so the tests compare the
  stored 6-tuple against a hand-derived one, never against the input;
* conic point coordinates are ``CORE::Expr`` (:class:`arrangement_2d.Algebraic`);
  CORE has no sound rationality test, so ``point.is_rational`` is always ``False``
  and ``point.exact_rational()`` always raises (CGAL_TRAPS_CHECKLIST.md, "CORE has
  no safe rationality test for Expr").
"""

from __future__ import annotations

import math
import os
import subprocess
import sys
from fractions import Fraction as F

import pytest

a2 = pytest.importorskip("arrangement_2d")

pytestmark = pytest.mark.skipif(
    not a2.kind_available("conic"), reason="the conic kind is not linked into this build"
)


# ---------------------------------------------------------------------------
# helpers and fixtures
# ---------------------------------------------------------------------------

def conic_value(coeffs, x, y):
    """``r x^2 + s y^2 + t x y + u x + v y + w`` evaluated exactly (Fractions in, Fraction out)."""
    r, s, t, u, v, w = coeffs
    x = F(x)
    y = F(y)
    return r * x * x + s * y * y + t * x * y + u * x + v * y + w


def rational_bezier_point(p0, p1, p2, w0, w1, w2, t):
    """The exact point of the rational quadratic Bezier at parameter `t` (all rational).

    ``B(t) = sum(b_i(t) w_i P_i) / sum(b_i(t) w_i)`` with the quadratic Bernstein
    polynomials ``b_0 = (1-t)^2``, ``b_1 = 2t(1-t)``, ``b_2 = t^2``.
    """
    t = F(t)
    b = ((1 - t) ** 2, 2 * t * (1 - t), t * t)
    w = (F(w0), F(w1), F(w2))
    den = sum(bi * wi for bi, wi in zip(b, w))
    num_x = sum(bi * wi * F(p[0]) for bi, wi, p in zip(b, w, (p0, p1, p2)))
    num_y = sum(bi * wi * F(p[1]) for bi, wi, p in zip(b, w, (p0, p1, p2)))
    return (num_x / den, num_y / den)


def simpson(f, a, b, n=2001):
    """Composite Simpson quadrature of `f` on ``[a, b]`` (n odd)."""
    h = (b - a) / (n - 1)
    total = 0.0
    for i in range(n):
        weight = 1 if i in (0, n - 1) else (4 if i % 2 else 2)
        total += weight * f(a + i * h)
    return total * h / 3


#: Complete elliptic integral of the second kind, E(m) = int_0^{pi/2} sqrt(1 - m sin^2 t) dt.
#: E(3/4) = 1.2110560275684... — used for every ellipse arc length below.
E_THREE_QUARTERS = simpson(lambda t: math.sqrt(1.0 - 0.75 * math.sin(t) ** 2), 0.0, math.pi / 2)


@pytest.fixture(autouse=True)
def restore_hyperbolic_flag():
    """``conic_allow_hyperbolic`` is a process-wide switch: keep every test independent."""
    previous = a2.conic_allow_hyperbolic()
    yield
    a2.conic_allow_hyperbolic(previous)


@pytest.fixture
def allow_hyperbolic():
    a2.conic_allow_hyperbolic(True)
    yield
    a2.conic_allow_hyperbolic(False)


@pytest.fixture
def ellipse():
    """x^2/4 + y^2 = 1, i.e. x^2 + 4y^2 - 4 = 0 (semi-axes 2 and 1), counterclockwise."""
    return a2.ConicArc.ellipse((0, 0), 2, 1)


@pytest.fixture
def circle_3_2():
    """x^2 + y^2 = 9/4 (radius 3/2), counterclockwise."""
    return a2.ConicArc.circle((0, 0), squared_radius=F(9, 4))


@pytest.fixture
def quarter_ellipse():
    """The counterclockwise quarter of x^2 + 4y^2 = 4 from (2, 0) to (0, 1)."""
    return a2.ConicArc.from_coefficients(1, 4, 0, 0, 0, -4, orientation="ccw",
                                         source=(2, 0), target=(0, 1))


# ---------------------------------------------------------------------------
# circle constructor
# ---------------------------------------------------------------------------

def test_circle_full_is_ccw_and_negated():
    # x^2 + y^2 - 4 = 0 has 4rs - t^2 = 4 > 0 and the coefficient signs imply the
    # clockwise traversal, so asking for counterclockwise negates all six.
    c = a2.ConicArc.circle((0, 0), 2)
    assert c.coefficients == (F(-1), F(-1), F(0), F(0), F(0), F(4))
    assert c.orientation == 1 and c.is_full and c.conic_type == "ellipse"


def test_circle_full_cw_keeps_natural_coefficients():
    c = a2.ConicArc.circle((0, 0), 2, orientation="cw")
    assert c.coefficients == (F(1), F(1), F(0), F(0), F(0), F(-4))
    assert c.orientation == -1


def test_circle_squared_radius_matches_radius():
    # radius 2 and squared_radius 4 describe the same circle.
    assert (a2.ConicArc.circle((0, 0), 2).coefficients
            == a2.ConicArc.circle((0, 0), squared_radius=4).coefficients)


def test_circle_off_centre_coefficients():
    # (x-3)^2 + y^2 = 4  ->  x^2 + y^2 - 6x + 5 = 0, negated for the ccw traversal.
    c = a2.ConicArc.circle((3, 0), 2)
    assert c.coefficients == (F(-1), F(-1), F(0), F(6), F(0), F(-5))


def test_circle_is_not_x_monotone_and_splits_in_two():
    # A full circle has exactly two vertical tangency points, (-2,0) and (2,0).
    c = a2.ConicArc.circle((0, 0), 2)
    assert not c.is_x_monotone
    pieces = c.make_x_monotone()
    assert len(pieces) == 2
    with pytest.raises(a2.NotXMonotoneError):
        c.x_monotone()


def test_circle_x_monotone_pieces_chain_head_to_tail():
    a, b = a2.ConicArc.circle((0, 0), 2).make_x_monotone()
    assert a.target == b.source and b.target == a.source


def test_circle_arc_quarter_bbox():
    # ccw from (2,0) to (0,2) is the first quadrant quarter: bbox [0,2] x [0,2].
    arc = a2.ConicArc.circle((0, 0), 2, source=(2, 0), target=(0, 2))
    assert not arc.is_full
    xmin, ymin, xmax, ymax = arc.bbox()
    assert (xmin, ymin) == pytest.approx((0.0, 0.0), abs=1e-9)
    assert (xmax, ymax) == pytest.approx((2.0, 2.0), abs=1e-9)


def test_circle_arc_upper_half_is_one_x_monotone_piece():
    # ccw from (2,0) to (-2,0) is the upper half; the only tangency points are its
    # own endpoints, so it is already x-monotone.
    arc = a2.ConicArc.circle((0, 0), 2, source=(2, 0), target=(-2, 0))
    assert len(arc.make_x_monotone()) == 1


def test_circle_arc_three_quarters_splits_at_one_tangency():
    # ccw from (2,0) to (0,-2) sweeps 270 degrees and passes the tangency at (-2,0).
    arc = a2.ConicArc.circle((0, 0), 2, source=(2, 0), target=(0, -2))
    assert len(arc.make_x_monotone()) == 2


def test_circle_rejects_zero_radius():
    with pytest.raises(ValueError):
        a2.ConicArc.circle((0, 0), 0)


def test_circle_requires_exactly_one_radius():
    with pytest.raises(TypeError):
        a2.ConicArc.circle((0, 0))
    with pytest.raises(TypeError):
        a2.ConicArc.circle((0, 0), 2, squared_radius=4)


def test_circle_arc_requires_both_endpoints():
    with pytest.raises(TypeError):
        a2.ConicArc.circle((0, 0), 2, source=(2, 0))


# ---------------------------------------------------------------------------
# ellipse constructor
# ---------------------------------------------------------------------------

def test_ellipse_axis_aligned_coefficients(ellipse):
    # x^2/4 + y^2 = 1 -> x^2 + 4y^2 - 4 = 0, negated for the ccw traversal.
    assert ellipse.coefficients == (F(-1), F(-4), F(0), F(0), F(0), F(4))
    assert ellipse.is_full and ellipse.orientation == 1 and ellipse.conic_type == "ellipse"


def test_ellipse_with_equal_semi_axes_is_a_circle():
    assert (a2.ConicArc.ellipse((0, 0), 2, 2).coefficients
            == a2.ConicArc.circle((0, 0), 2).coefficients)


def test_ellipse_axis_aligned_bbox(ellipse):
    xmin, ymin, xmax, ymax = ellipse.bbox()
    assert (xmin, ymin, xmax, ymax) == pytest.approx((-2.0, -1.0, 2.0, 1.0), abs=1e-9)


def test_ellipse_rotated_coefficients():
    # centre (1,2), semi-axes a=2 along d=(3,4) and b=1 across it.  With
    # A = b^2 dx^2 + a^2 dy^2 = 9 + 64 = 73, B = b^2 dy^2 + a^2 dx^2 = 16 + 36 = 52,
    # C = 2 dx dy (b^2 - a^2) = 24 * (-3) = -72, u = -2A cx - C cy = -146 + 144 = -2,
    # v = -2B cy - C cx = -208 + 72 = -136,
    # w = A cx^2 + B cy^2 + C cx cy - a^2 b^2 (dx^2+dy^2) = 73 + 208 - 144 - 100 = 37.
    # The natural orientation of (73, 52, -72, -2, -136, 37) is clockwise.
    e = a2.ConicArc.ellipse((1, 2), 2, 1, direction=(3, 4), orientation="cw")
    assert e.coefficients == (F(73), F(52), F(-72), F(-2), F(-136), F(37))
    assert a2.ConicArc.ellipse((1, 2), 2, 1, direction=(3, 4)).coefficients == \
        (F(-73), F(-52), F(72), F(2), F(136), F(-37))


def test_ellipse_rotated_axis_endpoints_are_exactly_on_the_conic():
    # |d| = 5, so the four axis endpoints of the ellipse are
    # centre +- a*d/|d| = (1,2) +- (6/5, 8/5) and centre +- b*d^perp/|d| = (1,2) +- (-4/5, 3/5).
    e = a2.ConicArc.ellipse((1, 2), 2, 1, direction=(3, 4), orientation="cw")
    for x, y in ((F(11, 5), F(18, 5)), (F(-1, 5), F(2, 5)),
                 (F(1, 5), F(13, 5)), (F(9, 5), F(7, 5))):
        assert conic_value(e.coefficients, x, y) == 0


def test_ellipse_rotated_bbox():
    # For semi-axes a, b along the unit direction (c, s) the bbox half-extents are
    # sqrt(a^2 c^2 + b^2 s^2) = sqrt(4*9/25 + 16/25) = sqrt(52/25) and
    # sqrt(a^2 s^2 + b^2 c^2) = sqrt(4*16/25 + 9/25) = sqrt(73/25).
    e = a2.ConicArc.ellipse((1, 2), 2, 1, direction=(3, 4))
    hx, hy = math.sqrt(52) / 5, math.sqrt(73) / 5
    xmin, ymin, xmax, ymax = e.bbox()
    assert (xmin, xmax) == pytest.approx((1 - hx, 1 + hx), abs=1e-9)
    assert (ymin, ymax) == pytest.approx((2 - hy, 2 + hy), abs=1e-9)


def test_ellipse_rejects_bad_centre():
    with pytest.raises(ValueError):
        a2.ConicArc.ellipse((0, 0, 0), 2, 1)


# ---------------------------------------------------------------------------
# segment constructor
# ---------------------------------------------------------------------------

def test_segment_carries_its_supporting_line():
    # The line through (0,0) and (3,4) is 4x - 3y = 0; r = s = t = 0.
    s = a2.ConicArc.segment((0, 0), (3, 4))
    assert s.coefficients == (F(0), F(0), F(0), F(4), F(-3), F(0))
    assert s.orientation == 0 and not s.is_full and s.conic_type == "segment"


def test_segment_endpoints_and_length():
    s = a2.ConicArc.segment((0, 0), (3, 4))
    assert s.source.approx == pytest.approx((0.0, 0.0))
    assert s.target.approx == pytest.approx((3.0, 4.0))
    assert s.approximate_length() == pytest.approx(5.0)  # 3-4-5 triangle


def test_segment_approximation_is_just_the_two_endpoints():
    pts = a2.ConicArc.segment((0, 0), (3, 4)).approximate(1e-6)
    assert len(pts) == 2
    assert pts[0] == pytest.approx((0.0, 0.0), abs=1e-12)
    assert pts[1] == pytest.approx((3.0, 4.0), abs=1e-12)


def test_special_segment_from_algebraic_endpoints_has_zero_coefficients():
    # Two algebraic (CORE::Expr) endpoints select CGAL's "special segment" branch,
    # which stores no supporting line at all (STAGE2_NOTES: all six coefficients zero).
    s = a2.ConicArc.segment(a2.Point.from_algebraic(0, 0), a2.Point.from_algebraic(3, 4))
    assert s.coefficients == (F(0),) * 6
    assert s.orientation == 0 and s.conic_type == "segment"
    assert s.approximate_length() == pytest.approx(5.0)
    # geometrically identical to the rational-endpoint segment
    assert s == a2.ConicArc.segment((0, 0), (3, 4))


# ---------------------------------------------------------------------------
# from_coefficients
# ---------------------------------------------------------------------------

def test_from_coefficients_full_ellipse():
    f = a2.ConicArc.from_coefficients(1, 4, 0, 0, 0, -4)
    # 4rs - t^2 = 16 > 0, and the given signs already describe the clockwise traversal.
    assert f.is_full and f.orientation == -1 and f.conic_type == "ellipse"
    assert f.coefficients == (F(1), F(4), F(0), F(0), F(0), F(-4))


def test_from_coefficients_quarter_arc(quarter_ellipse):
    assert quarter_ellipse.coefficients == (F(-1), F(-4), F(0), F(0), F(0), F(4))
    assert quarter_ellipse.orientation == 1 and not quarter_ellipse.is_full
    assert quarter_ellipse.source.approx == pytest.approx((2.0, 0.0))
    assert quarter_ellipse.target.approx == pytest.approx((0.0, 1.0))
    assert len(quarter_ellipse.make_x_monotone()) == 1


def test_from_coefficients_full_parabola_is_rejected():
    # A full conic must be bounded: 4rs - t^2 = 0 is a parabola.
    with pytest.raises(ValueError, match="4rs"):
        a2.ConicArc.from_coefficients(1, 0, 0, -2, 1, 0)


def test_from_coefficients_imaginary_ellipse_is_rejected():
    # x^2 + y^2 + 4 = 0 has no real point.
    with pytest.raises(ValueError, match="imaginary"):
        a2.ConicArc.from_coefficients(1, 1, 0, 0, 0, 4)


def test_from_coefficients_point_conic_is_rejected():
    # x^2 + 4y^2 = 0 is the single point (0,0).
    with pytest.raises(ValueError, match="single point"):
        a2.ConicArc.from_coefficients(1, 4, 0, 0, 0, 0)


def test_from_coefficients_endpoint_not_on_the_conic_is_rejected():
    # (1,1) is not on x^2 + 4y^2 = 4 (1 + 4 = 5 != 4).
    with pytest.raises(ValueError):
        a2.ConicArc.from_coefficients(1, 4, 0, 0, 0, -4, orientation="ccw",
                                      source=(1, 1), target=(0, 1))


def test_from_coefficients_argument_combinations():
    with pytest.raises(TypeError):  # orientation without endpoints
        a2.ConicArc.from_coefficients(1, 4, 0, 0, 0, -4, orientation="ccw")
    with pytest.raises(TypeError):  # endpoints without orientation
        a2.ConicArc.from_coefficients(1, 4, 0, 0, 0, -4, source=(2, 0), target=(0, 1))
    with pytest.raises(TypeError):  # only one endpoint
        a2.ConicArc.from_coefficients(1, 4, 0, 0, 0, -4, orientation="ccw", source=(2, 0))


def test_parabola_arc_builds():
    # y = 2x - x^2, i.e. x^2 - 2x + y = 0; 4rs - t^2 = 0 -> parabola.
    par = a2.ConicArc.from_coefficients(1, 0, 0, -2, 1, 0, orientation="cw",
                                        source=(0, 0), target=(2, 0))
    assert par.conic_type == "parabola" and not par.is_full
    assert par.coefficients == (F(1), F(0), F(0), F(-2), F(1), F(0))
    assert len(par.make_x_monotone()) == 1
    xmin, ymin, xmax, ymax = par.bbox()  # apex at (1,1)
    assert (xmin, ymin, xmax, ymax) == pytest.approx((0.0, 0.0, 2.0, 1.0), abs=1e-9)


def test_arc_with_the_wrong_orientation_is_not_rejected_at_construction():
    # DOCUMENTED CGAL 6.1 TRAP (CGAL_TRAPS_CHECKLIST "Conic kind": invalid arcs are
    # returned silently, and Conic_arc_2::is_valid() does not check that the requested
    # traversal orientation agrees with the endpoints).  The counterclockwise arc from
    # (0,0) to (2,0) on y = 2x - x^2 does not exist (that traversal is clockwise), yet
    # the constructor accepts it and CGAL only asserts later.
    with pytest.raises((a2.CGALError, ValueError)):
        bad = a2.ConicArc.from_coefficients(1, 0, 0, -2, 1, 0, orientation="ccw",
                                            source=(0, 0), target=(2, 0))
        bad.make_x_monotone()


# ---------------------------------------------------------------------------
# from_points
# ---------------------------------------------------------------------------

def test_from_points_recovers_the_ellipse():
    # Five points of x^2 + 4y^2 - 4 = 0; p1 is the source and p5 the target.
    fp = a2.ConicArc.from_points((2, 0), (0, 1), (-2, 0), (0, -1), (F(6, 5), F(4, 5)))
    assert fp.coefficients == (F(1), F(4), F(0), F(0), F(0), F(-4))
    assert fp.conic_type == "ellipse" and not fp.is_full


def test_from_points_all_five_lie_exactly_on_the_stored_conic():
    pts = [(2, 0), (0, 1), (-2, 0), (0, -1), (F(6, 5), F(4, 5))]
    coeffs = a2.ConicArc.from_points(*pts).coefficients
    assert all(conic_value(coeffs, x, y) == 0 for x, y in pts)


def test_from_points_clockwise_arc_spans_three_quadrants():
    # (2,0) -> (1.2,0.8) the long way round (through (0,-1), (-2,0), (0,1)) passes the
    # tangency at (-2,0), so the arc is NOT x-monotone: it splits into two pieces that
    # join at (-2,0), leaving the arc endpoints (2,0) and (6/5,4/5) used once each.
    fp = a2.ConicArc.from_points((2, 0), (0, 1), (-2, 0), (0, -1), (F(6, 5), F(4, 5)))
    assert fp.orientation == -1
    assert not fp.is_x_monotone
    pieces = fp.make_x_monotone()
    assert len(pieces) == 2
    ends = sorted(tuple(round(c, 9) for c in p.approx)
                  for piece in pieces for p in (piece.source, piece.target))
    assert ends == [(-2.0, 0.0), (-2.0, 0.0), (1.2, 0.8), (2.0, 0.0)]


def test_from_points_rejects_collinear_points():
    with pytest.raises(ValueError, match="collinear"):
        a2.ConicArc.from_points((0, 0), (1, 0), (2, 0), (3, 0), (4, 0))


def test_from_points_rejects_algebraic_points():
    # make_from_five_points needs rational input, and a conic point never proves rational.
    with pytest.raises(a2.NotRepresentableError):
        a2.ConicArc.from_points(a2.Point.from_algebraic(2, 0), (0, 1), (-2, 0),
                                (0, -1), (F(6, 5), F(4, 5)))


# ---------------------------------------------------------------------------
# from_rational_bezier
# ---------------------------------------------------------------------------

def test_rational_bezier_unit_weights_is_the_polynomial_parabola():
    # B(t) = (2t, 4t(1-t)) with weights 1, so x = 2t and y = 4t - 4t^2 = 2x - x^2,
    # i.e. x^2 - 2x + y = 0.
    rb = a2.ConicArc.from_rational_bezier((0, 0), (1, 2), (2, 0))
    assert rb.coefficients == (F(1), F(0), F(0), F(-2), F(1), F(0))
    assert rb.conic_type == "parabola"


def test_rational_bezier_weight_one_half_gives_an_ellipse():
    # w = (1, 1/2, 1): D(t) = 1 - t + t^2 (positive definite), so the curve is an
    # ellipse arc; CGAL stores 4x^2 + 3y^2 - 8x + 4y = 0 (4rs - t^2 = 48 > 0).
    rb = a2.ConicArc.from_rational_bezier((0, 0), (1, 2), (2, 0), 1, F(1, 2), 1)
    assert rb.coefficients == (F(4), F(3), F(0), F(-8), F(4), F(0))
    assert rb.conic_type == "ellipse"


def test_rational_bezier_weight_two_gives_a_safe_hyperbola():
    # w = (1, 2, 1) gives 16x^2 - 3y^2 - 32x + 16y = 0 (4rs - t^2 = -192 < 0).
    # t = 0 makes the branch axis exact, so the hyperbolic gate lets it through.
    rb = a2.ConicArc.from_rational_bezier((0, 0), (1, 2), (2, 0), 1, 2, 1)
    assert rb.coefficients == (F(16), F(-3), F(0), F(-32), F(16), F(0))
    assert rb.conic_type == "hyperbola"


def test_rational_bezier_collinear_control_points_give_a_segment():
    rb = a2.ConicArc.from_rational_bezier((0, 0), (1, 1), (2, 2))
    assert rb.conic_type == "segment" and rb.orientation == 0
    assert rb.coefficients == (F(0), F(0), F(0), F(1), F(-1), F(0))  # line x - y = 0


def test_rational_bezier_rejects_non_positive_weights():
    with pytest.raises(ValueError, match="positive"):
        a2.ConicArc.from_rational_bezier((0, 0), (1, 2), (2, 0), 1, 0, 1)
    with pytest.raises(ValueError, match="positive"):
        a2.ConicArc.from_rational_bezier((0, 0), (1, 2), (2, 0), 1, -1, 1)


@pytest.mark.parametrize("weights", [(1, 1, 1), (1, F(1, 2), 1), (1, 2, 1), (2, 3, 5)])
def test_rational_bezier_is_exact(weights):
    """Points evaluated from the Bezier definition satisfy the stored conic exactly."""
    p0, p1, p2 = (0, 0), (1, 2), (2, 0)
    coeffs = a2.ConicArc.from_rational_bezier(p0, p1, p2, *weights).coefficients
    for k in range(0, 11):
        x, y = rational_bezier_point(p0, p1, p2, *weights, t=F(k, 10))
        assert conic_value(coeffs, x, y) == 0, f"t = {k}/10"


def test_rational_bezier_endpoints_are_the_outer_control_points():
    rb = a2.ConicArc.from_rational_bezier((0, 0), (1, 2), (2, 0), 1, F(1, 2), 1)
    ends = {rb.source.approx, rb.target.approx}
    assert ends == {(0.0, 0.0), (2.0, 0.0)}


# ---------------------------------------------------------------------------
# BezierCurve.from_rational  (DESIGN.md 3: ".from_rational(points, weights) -> ConicArc")
# ---------------------------------------------------------------------------

def test_bezier_from_rational_quadratic_gives_the_conic_arc():
    # DESIGN.md 3 promises ``BezierCurve.from_rational(points, weights) -> ConicArc``
    # for degree 2, and it must agree with ConicArc.from_rational_bezier.
    # BUG: _geometry.pxi:1298-1301 boxes the control points with
    # ``_as_point(pts[i], _K_CONIC)`` instead of ``_as_conic_rational_point(pts[i])``,
    # so the core always sees algebraic points and refuses every call.
    rb = a2.BezierCurve.from_rational([(0, 0), (1, 2), (2, 0)], [1, F(1, 2), 1])
    assert rb.kind == a2.Kind.CONIC
    assert rb.coefficients == (F(4), F(3), F(0), F(-8), F(4), F(0))
    assert rb == a2.ConicArc.from_rational_bezier((0, 0), (1, 2), (2, 0), 1, F(1, 2), 1)


def test_bezier_from_rational_linear_is_the_segment():
    # A rational linear Bezier is just the segment p0..p1.  Its control points are boxed
    # with _as_conic_rational_point (exact rationals), so the exact Rat_segment_2 overload
    # of conic::make_segment applies and the arc carries its supporting line 4x - 3y = 0 --
    # exactly what ConicArc.segment((0,0),(3,4)) produces.  (Before the fix the conic-kind
    # boxing forced CGAL's "special segment" fallback, whose six coefficients are all 0.)
    rb = a2.BezierCurve.from_rational([(0, 0), (3, 4)])
    assert rb.kind == a2.Kind.CONIC and rb.conic_type == "segment"
    assert rb == a2.ConicArc.segment((0, 0), (3, 4))
    assert rb.coefficients == (F(0), F(0), F(0), F(4), F(-3), F(0))


def test_bezier_from_rational_rejects_higher_degrees():
    with pytest.raises(a2.NotRepresentableError, match="degree 3"):
        a2.BezierCurve.from_rational([(0, 0), (1, 2), (2, 2), (3, 0)])


def test_bezier_from_rational_checks_the_weight_count():
    with pytest.raises(ValueError, match="one weight per control point"):
        a2.BezierCurve.from_rational([(0, 0), (1, 2), (2, 0)], [1, 1])


# ---------------------------------------------------------------------------
# from_circle_segment
# ---------------------------------------------------------------------------

def test_from_circle_segment_full_circle():
    fc = a2.ConicArc.from_circle_segment(a2.Circle((0, 0), 3))
    assert fc.kind == a2.Kind.CONIC and fc.is_full
    assert fc.coefficients == (F(-1), F(-1), F(0), F(0), F(0), F(9))  # x^2+y^2-9=0, ccw


def test_from_circle_segment_arc_matches_the_native_constructor():
    cs = a2.CircleSegment.arc((0, 0), 2, source=(2, 0), target=(0, 2))
    assert (a2.ConicArc.from_circle_segment(cs)
            == a2.ConicArc.circle((0, 0), 2, source=(2, 0), target=(0, 2)))


def test_from_circle_segment_straight_segment():
    cs = a2.CircleSegment.segment((0, 0), (3, 4))
    assert a2.ConicArc.from_circle_segment(cs) == a2.ConicArc.segment((0, 0), (3, 4))


def test_circle_segment_to_kind_conic_is_the_same_conversion():
    cs = a2.CircleSegment.arc((0, 0), 2, source=(2, 0), target=(0, 2))
    assert cs.to_kind("conic") == a2.ConicArc.from_circle_segment(cs)


# ---------------------------------------------------------------------------
# arc_with_defining_conics
# ---------------------------------------------------------------------------

def test_arc_with_defining_conics_reproduces_the_quarter_ellipse(quarter_ellipse):
    # Source = the intersection of x^2+4y^2-4=0 with y^2=0 near (2,0);
    # target = its intersection with x^2=0 near (0,1).
    arc = a2.ConicArc.arc_with_defining_conics(
        (1, 4, 0, 0, 0, -4), "ccw",
        (2.0, 0.0), (0, 1, 0, 0, 0, 0),
        (0.0, 1.0), (1, 0, 0, 0, 0, 0))
    assert arc.coefficients == quarter_ellipse.coefficients
    assert arc == quarter_ellipse


def test_arc_with_defining_conics_validates_tuple_length():
    with pytest.raises(ValueError, match="6 coefficients"):
        a2.ConicArc.arc_with_defining_conics((1, 4, 0, 0, 0), "ccw", (2.0, 0.0),
                                             (0, 1, 0, 0, 0, 0), (0.0, 1.0),
                                             (1, 0, 0, 0, 0, 0))


# ---------------------------------------------------------------------------
# coefficients / orientation / conic_type / is_full
# ---------------------------------------------------------------------------

@pytest.mark.parametrize("build, expected", [
    (lambda: a2.ConicArc.circle((0, 0), 2), "ellipse"),
    (lambda: a2.ConicArc.ellipse((0, 0), 2, 1), "ellipse"),
    (lambda: a2.ConicArc.from_coefficients(1, 0, 0, -2, 1, 0, orientation="cw",
                                           source=(0, 0), target=(2, 0)), "parabola"),
    (lambda: a2.ConicArc.from_coefficients(1, -4, 0, 0, 0, -4, orientation="cw",
                                           source=(F(5, 2), F(-3, 4)),
                                           target=(F(5, 2), F(3, 4))), "hyperbola"),
    (lambda: a2.ConicArc.segment((0, 0), (3, 4)), "segment"),
], ids=["circle", "ellipse", "parabola", "hyperbola", "segment"])
def test_conic_type(build, expected):
    # sign(4rs - t^2) on the STORED coefficients: > 0 ellipse, 0 parabola,
    # < 0 hyperbola; orientation 0 (collinear) short-circuits to "segment".
    assert build().conic_type == expected


def test_is_full_is_false_for_every_x_monotone_piece(ellipse):
    assert ellipse.is_full
    assert all(not piece.is_full for piece in ellipse.make_x_monotone())


def test_x_monotone_piece_keeps_the_supporting_conic(ellipse):
    for piece in ellipse.make_x_monotone():
        assert piece.coefficients == ellipse.coefficients
        assert piece.orientation == ellipse.orientation
        assert piece.conic_type == "ellipse"


def test_orientation_values():
    assert a2.ConicArc.circle((0, 0), 2).orientation == 1
    assert a2.ConicArc.circle((0, 0), 2, orientation="cw").orientation == -1
    assert a2.ConicArc.segment((0, 0), (1, 1)).orientation == 0


def test_to_curve_round_trip(ellipse):
    piece = ellipse.make_x_monotone()[0]
    assert piece.is_x_monotone and piece.type_name == "x_monotone_curve"
    general = piece.to_curve()
    assert not general.is_x_monotone and general.type_name == "curve"
    assert general.coefficients == ellipse.coefficients


# ---------------------------------------------------------------------------
# exact() -> Algebraic
# ---------------------------------------------------------------------------

def intersections_with_horizontal(curve, y):
    """All intersection points of `curve` with the line y = const, as (Point, multiplicity)."""
    line = a2.ConicArc.segment((-10, y), (10, y))
    out = []
    for piece in curve.make_x_monotone():
        out += piece.intersect(line)
    return out


def test_intersection_points_are_algebraic(ellipse):
    # x^2 + 4*(1/4) = 4  ->  x = +-sqrt(3).
    hits = intersections_with_horizontal(ellipse, F(1, 2))
    assert len(hits) == 2
    for point, multiplicity in hits:
        assert multiplicity == 1
        assert point.kind == a2.Kind.CONIC
        xv, yv = point.exact()
        assert isinstance(xv, a2.Algebraic) and isinstance(yv, a2.Algebraic)


def test_intersection_x_coordinates_are_plus_minus_sqrt_three(ellipse):
    hits = intersections_with_horizontal(ellipse, F(1, 2))
    xs = sorted(float(p.exact()[0]) for p, _ in hits)
    assert xs == pytest.approx([-math.sqrt(3), math.sqrt(3)], abs=1e-12)


def test_algebraic_interval_is_certified(ellipse):
    # the two roots are -sqrt(3) and +sqrt(3); each certified interval must contain
    # its own root and be no wider than a couple of ulps.
    for point, _ in intersections_with_horizontal(ellipse, F(1, 2)):
        xv = point.exact()[0]
        lo, hi = xv.interval()
        assert lo <= xv.sign() * math.sqrt(3) <= hi
        assert 0.0 <= hi - lo <= 1e-14


def test_algebraic_sign_and_float(ellipse):
    hits = sorted(intersections_with_horizontal(ellipse, F(1, 2)),
                  key=lambda h: h[0].approx[0])
    left, right = hits[0][0], hits[1][0]
    assert left.exact()[0].sign() == -1
    assert right.exact()[0].sign() == 1
    assert float(right.exact()[0]) == pytest.approx(math.sqrt(3))


def test_algebraic_of_a_genuine_expr_is_not_known_rational(ellipse):
    # CGAL_TRAPS_CHECKLIST: CORE has no safe rationality test for Expr.
    point = intersections_with_horizontal(ellipse, F(1, 2))[0][0]
    xv = point.exact()[0]
    assert xv.is_rational is False and xv.exact() is None
    with pytest.raises(TypeError):
        hash(xv)


def test_algebraic_from_rational_round_trips():
    value = a2.Algebraic.from_rational(F(3, 4))
    assert value.is_rational and value.exact() == F(3, 4)
    assert float(value) == 0.75 and value.sign() == 1 and bool(value)


def test_conic_point_exact_returns_algebraic_even_for_rational_input():
    p = a2.Point(F(1, 2), F(3, 4)).to_kind("conic")
    xv, yv = p.exact()
    assert isinstance(xv, a2.Algebraic) and isinstance(yv, a2.Algebraic)
    assert (float(xv), float(yv)) == (0.5, 0.75)


def test_conic_point_is_never_reported_rational():
    p = a2.Point(F(1, 2), F(3, 4)).to_kind("conic")
    assert p.is_rational is False
    with pytest.raises(a2.NotRepresentableError):
        p.exact_rational()
    with pytest.raises(TypeError):
        hash(p)


def test_conic_point_interval_is_exact_for_rational_coordinates():
    p = a2.Point.from_algebraic(F(1, 2), F(3, 4))
    assert p.interval() == ((0.5, 0.5), (0.75, 0.75))
    assert p.interval(200) == ((0.5, 0.5), (0.75, 0.75))


def test_point_from_algebraic_equals_converted_rational_point():
    assert a2.Point.from_algebraic(F(1, 2), F(3, 4)) == a2.Point(F(1, 2), F(3, 4)).to_kind("conic")


def test_conic_point_compare_xy():
    p = a2.Point.from_algebraic(0, 0)
    q = a2.Point.from_algebraic(0, 1)
    assert p.compare_xy(q) == -1 and q.compare_xy(p) == 1 and p.compare_xy(p) == 0
    assert p.compare_x(q) == 0


# ---------------------------------------------------------------------------
# approximate()
# ---------------------------------------------------------------------------

def test_approximate_starts_at_source_and_ends_at_target(quarter_ellipse):
    pts = quarter_ellipse.approximate(1e-3)
    assert pts[0] == pytest.approx((2.0, 0.0), abs=1e-12)
    assert pts[-1] == pytest.approx((0.0, 1.0), abs=1e-12)


def test_approximate_vertices_satisfy_the_conic_equation(quarter_ellipse):
    # every emitted vertex must lie on x^2 + 4y^2 - 4 = 0
    residual = max(abs(x * x + 4 * y * y - 4) for x, y in quarter_ellipse.approximate(1e-3))
    assert residual < 1e-12


def test_approximate_full_ellipse_is_closed(ellipse):
    pts = ellipse.approximate(1e-3)
    assert pts[0] == pts[-1]
    assert max(abs(x * x + 4 * y * y - 4) for x, y in pts) < 1e-12


def test_approximate_full_ellipse_contains_both_tangency_points(ellipse):
    pts = ellipse.approximate(1e-3)
    assert any(p == pytest.approx((-2.0, 0.0), abs=1e-12) for p in pts)
    assert any(p == pytest.approx((2.0, 0.0), abs=1e-12) for p in pts)


@pytest.mark.parametrize("tolerance", [1e-2, 1e-3, 1e-4])
def test_approximate_chord_sagitta_respects_the_tolerance(tolerance):
    # For a circle of radius 2 centred at the origin the distance from a chord
    # midpoint m to the circle is exactly 2 - |m|.
    pts = a2.ConicArc.circle((0, 0), 2).approximate(tolerance)
    worst = max(2.0 - math.hypot((pts[i][0] + pts[i + 1][0]) / 2,
                                 (pts[i][1] + pts[i + 1][1]) / 2)
                for i in range(len(pts) - 1))
    assert 0.0 <= worst <= tolerance


def test_approximate_coarser_tolerance_gives_fewer_points(ellipse):
    assert len(ellipse.approximate(1e-1)) < len(ellipse.approximate(1e-3))


@pytest.mark.parametrize("tolerance", [0.0, -1.0, float("nan")])
def test_approximate_rejects_non_positive_tolerance(ellipse, tolerance):
    with pytest.raises(ValueError, match="positive"):
        ellipse.approximate(tolerance)


def test_approximate_length_of_the_quarter_ellipse(quarter_ellipse):
    # arc length of x = 2 cos u, y = sin u for u in [0, pi/2] is 2 E(3/4).
    assert quarter_ellipse.approximate_length(1e-6) == pytest.approx(2 * E_THREE_QUARTERS, abs=1e-5)


def test_approximate_length_of_the_full_ellipse(ellipse):
    # perimeter = 4 a E(e^2) = 8 E(3/4)
    assert ellipse.approximate_length(1e-6) == pytest.approx(8 * E_THREE_QUARTERS, abs=1e-4)


def test_approximate_length_of_a_full_circle():
    assert a2.ConicArc.circle((0, 0), 2).approximate_length(1e-6) == pytest.approx(4 * math.pi,
                                                                                   abs=1e-4)


# ---------------------------------------------------------------------------
# hyperbolic safety predicate
# ---------------------------------------------------------------------------

SAFE_HYPERBOLA = (1, -4, 0, 0, 0, -4)          # x^2 - 4y^2 = 4, axis-aligned (t = 0)
SAFE_ENDPOINTS = ((F(5, 2), F(-3, 4)), (F(5, 2), F(3, 4)))
# x*y = 1 rotated by (cos, sin) = (4/5, 3/5):  -12x^2 + 7xy + 12y^2 - 25 = 0.
UNSAFE_HYPERBOLA = (-12, 12, 7, 0, 0, -25)
# (1,1) and (2,1/2) on xy = 1 rotate to (1/5, 7/5) and (13/10, 8/5).
UNSAFE_ENDPOINTS = ((F(1, 5), F(7, 5)), (F(13, 10), F(8, 5)))


def test_hyperbolic_flag_defaults_to_false():
    assert a2.conic_allow_hyperbolic() is False


def test_hyperbolic_flag_setter_returns_the_new_value():
    assert a2.conic_allow_hyperbolic(True) is True
    assert a2.conic_allow_hyperbolic() is True
    assert a2.conic_allow_hyperbolic(False) is False


def test_safe_hyperbolic_arc_builds_by_default():
    # (R,S,T) = sign(N)*(r,s,t) with N = (4rs - t^2) w = (-16)(-4) = 64 > 0, so T = 0
    # and the CGAL branch axis is exact: the arc is SAFE and needs no opt-in.
    src, tgt = SAFE_ENDPOINTS
    arc = a2.ConicArc.from_coefficients(*SAFE_HYPERBOLA, orientation="cw",
                                        source=src, target=tgt)
    assert arc.conic_type == "hyperbola"
    assert arc.coefficients == (F(-1), F(4), F(0), F(0), F(0), F(4))  # negated for cw


def test_safe_hyperbolic_arc_is_usable():
    src, tgt = SAFE_ENDPOINTS
    arc = a2.ConicArc.from_coefficients(*SAFE_HYPERBOLA, orientation="cw",
                                        source=src, target=tgt)
    # the arc runs around the right-branch vertex (2,0), so it splits in two there
    assert len(arc.make_x_monotone()) == 2
    xmin, ymin, xmax, ymax = arc.bbox()
    assert (xmin, ymin, xmax, ymax) == pytest.approx((2.0, -0.75, 2.5, 0.75), abs=1e-9)


def test_safe_hyperbolic_endpoints_are_on_the_conic():
    for x, y in SAFE_ENDPOINTS:
        assert conic_value([F(c) for c in SAFE_HYPERBOLA], x, y) == 0


def test_unsafe_hyperbolic_endpoints_are_on_the_conic():
    for x, y in UNSAFE_ENDPOINTS:
        assert conic_value([F(c) for c in UNSAFE_HYPERBOLA], x, y) == 0


@pytest.mark.parametrize("orientation", ["ccw", "cw"])
def test_unsafe_hyperbolic_arc_is_refused(orientation):
    # N = (4rs - t^2) w = (-576 - 49)(-25) > 0, so (R,S,T) = (-12,12,7): T != 0,
    # P = R + S = 0 and E = T^2 - (R-S)^2 = 49 - 576 = -527, hence
    # sign(P*sqrt((R-S)^2+T^2) - E) = sign(527) > 0  ->  UNSAFE.
    src, tgt = UNSAFE_ENDPOINTS
    with pytest.raises(a2.UnsupportedError, match="build_hyperbolic_arc_data"):
        a2.ConicArc.from_coefficients(*UNSAFE_HYPERBOLA, orientation=orientation,
                                      source=src, target=tgt)


def test_unsafe_hyperbolic_arc_builds_after_opt_in(allow_hyperbolic):
    src, tgt = UNSAFE_ENDPOINTS
    arc = a2.ConicArc.from_coefficients(*UNSAFE_HYPERBOLA, orientation="cw",
                                        source=src, target=tgt)
    assert arc.conic_type == "hyperbola"
    assert arc.coefficients == (F(12), F(-12), F(-7), F(0), F(0), F(25))  # negated for cw


def test_hyperbolic_gate_is_reinstated_when_the_flag_goes_back_to_false():
    src, tgt = UNSAFE_ENDPOINTS
    a2.conic_allow_hyperbolic(True)
    a2.ConicArc.from_coefficients(*UNSAFE_HYPERBOLA, orientation="cw", source=src, target=tgt)
    a2.conic_allow_hyperbolic(False)
    with pytest.raises(a2.UnsupportedError):
        a2.ConicArc.from_coefficients(*UNSAFE_HYPERBOLA, orientation="cw",
                                      source=src, target=tgt)


def test_degenerate_hyperbola_is_rejected_whatever_the_flag(allow_hyperbolic):
    # x^2 - y^2 = 0 is a pair of lines (N = 0), never a usable hyperbolic arc.
    with pytest.raises(ValueError, match="degenerate hyperbola"):
        a2.ConicArc.from_coefficients(1, -1, 0, 0, 0, 0, orientation="cw",
                                      source=(1, 1), target=(2, 2))


def test_hyperbolic_gate_does_not_touch_ellipses_or_parabolas():
    a2.ConicArc.ellipse((0, 0), 2, 1)
    a2.ConicArc.from_coefficients(1, 0, 0, -2, 1, 0, orientation="cw",
                                  source=(0, 0), target=(2, 0))


# ---------------------------------------------------------------------------
# x-monotone traits operations
# ---------------------------------------------------------------------------

def test_min_and_max_vertex(quarter_ellipse):
    assert quarter_ellipse.min_vertex.approx == pytest.approx((0.0, 1.0))
    assert quarter_ellipse.max_vertex.approx == pytest.approx((2.0, 0.0))
    assert quarter_ellipse.left.approx == quarter_ellipse.min_vertex.approx
    assert quarter_ellipse.right.approx == quarter_ellipse.max_vertex.approx


def test_direction_flags(quarter_ellipse):
    # stored source (2,0) is lexicographically larger than the target (0,1)
    assert quarter_ellipse.is_directed_right is False
    assert quarter_ellipse.compare_endpoints_xy() == 1
    assert quarter_ellipse.is_vertical is False


def test_opposite_flips_the_direction(quarter_ellipse):
    opposite = quarter_ellipse.opposite()
    assert opposite.is_directed_right is True
    assert opposite.source.approx == pytest.approx((0.0, 1.0))
    assert opposite.target.approx == pytest.approx((2.0, 0.0))
    assert opposite == quarter_ellipse           # Equal_2 ignores the direction


def test_split_at_an_interior_point(quarter_ellipse):
    # (6/5, 4/5) is on x^2+4y^2=4: 36/25 + 64/25 = 4.
    left, right = quarter_ellipse.split((F(6, 5), F(4, 5)))
    assert left.max_vertex.approx == pytest.approx((1.2, 0.8))
    assert right.min_vertex.approx == pytest.approx((1.2, 0.8))
    assert left.min_vertex.approx == pytest.approx((0.0, 1.0))
    assert right.max_vertex.approx == pytest.approx((2.0, 0.0))


def test_merge_undoes_split(quarter_ellipse):
    left, right = quarter_ellipse.split((F(6, 5), F(4, 5)))
    assert left.can_merge(right)
    assert left.merge(right) == quarter_ellipse


def test_two_disjoint_arcs_are_not_mergeable(quarter_ellipse, ellipse):
    other = ellipse.make_x_monotone()[0]      # the lower half
    assert quarter_ellipse.can_merge(other) is False


def test_trim(quarter_ellipse):
    trimmed = quarter_ellipse.trim((F(6, 5), F(4, 5)), (0, 1))
    assert trimmed.min_vertex.approx == pytest.approx((0.0, 1.0))
    assert trimmed.max_vertex.approx == pytest.approx((1.2, 0.8))


def test_compare_y_at_x(quarter_ellipse):
    # at x = 1 the arc is at y = sqrt(3)/2 = 0.866, so (1,0) is below it.
    assert quarter_ellipse.compare_y_at_x((1, 0)) == -1
    assert quarter_ellipse.compare_y_at_x((1, 1)) == 1
    assert quarter_ellipse.compare_y_at_x((F(6, 5), F(4, 5))) == 0


def test_is_in_x_range(quarter_ellipse):
    assert quarter_ellipse.is_in_x_range((1, 5)) is True
    assert quarter_ellipse.is_in_x_range((3, 0)) is False


def test_intersect_ellipse_with_a_chord(ellipse):
    hits = intersections_with_horizontal(ellipse, F(1, 2))
    xs = sorted(round(p.approx[0], 12) for p, _ in hits)
    assert xs == pytest.approx([-math.sqrt(3), math.sqrt(3)], abs=1e-9)
    assert all(p.approx[1] == pytest.approx(0.5) for p, _ in hits)


def test_intersect_disjoint_curves_is_empty():
    a = a2.ConicArc.circle((0, 0), 1, source=(1, 0), target=(-1, 0))
    b = a2.ConicArc.segment((-5, 5), (5, 5))
    assert a.intersect(b) == []


def test_intersect_identical_arcs_reports_an_overlap():
    arc = a2.ConicArc.circle((0, 0), 2, source=(2, 0), target=(-2, 0))
    result = arc.intersect(arc)
    assert len(result) == 1 and isinstance(result[0], a2.Curve)
    assert result[0] == arc


def test_curve_equality_and_unhashability():
    a = a2.ConicArc.circle((0, 0), 2, source=(2, 0), target=(0, 2))
    b = a2.ConicArc.from_coefficients(1, 1, 0, 0, 0, -4, orientation="ccw",
                                      source=(2, 0), target=(0, 2))
    assert a == b and not (a != b)
    assert a != a2.ConicArc.circle((0, 0), 2, source=(2, 0), target=(0, -2))
    with pytest.raises(TypeError):
        hash(a)


def test_repr_formats(ellipse, quarter_ellipse):
    assert repr(ellipse) == "Conic(full -1, -4, 0, 0, 0, 4, orientation=ccw)"
    assert repr(quarter_ellipse) == (
        "ConicArc(-1, -4, 0, 0, 0, 4, orientation=ccw, source=(~2, ~0), target=(~0, ~1))")
    assert repr(quarter_ellipse.source) == "ConicPoint(~2, ~0)"


def test_no_public_constructor():
    with pytest.raises(TypeError, match="no public constructor"):
        a2.ConicArc()


# ---------------------------------------------------------------------------
# conversions to / from other kinds
# ---------------------------------------------------------------------------

def test_segment_kind_converts_to_a_conic_segment():
    converted = a2.Segment((0, 0), (3, 4)).to_kind("conic")
    assert converted.kind == a2.Kind.CONIC
    assert converted.coefficients == (F(0), F(0), F(0), F(4), F(-3), F(0))


def test_polyline_converts_to_one_conic_segment_per_subcurve():
    pieces = a2.Polyline([(0, 0), (1, 1), (2, 0)]).to_kind("conic")
    assert len(pieces) == 2
    assert all(p.conic_type == "segment" for p in pieces)


def test_quadratic_bezier_converts_exactly():
    # the polynomial Bezier (0,0),(1,2),(2,0) is the parabola x^2 - 2x + y = 0
    converted = a2.BezierCurve([(0, 0), (1, 2), (2, 0)]).to_kind("conic")
    assert converted.coefficients == (F(1), F(0), F(0), F(-2), F(1), F(0))


def test_cubic_bezier_does_not_convert():
    with pytest.raises(a2.NotRepresentableError, match="degree 3"):
        a2.BezierCurve([(0, 0), (1, 2), (2, 2), (3, 0)]).to_kind("conic")


def test_unbounded_linear_curve_does_not_convert():
    with pytest.raises(a2.UnsupportedError, match="unbounded"):
        a2.Ray((0, 0), (1, 1)).to_kind("conic")


def test_bounded_linear_segment_converts():
    converted = a2.LinearCurve.segment((0, 0), (3, 4)).to_kind("conic")
    assert converted == a2.ConicArc.segment((0, 0), (3, 4))


def test_conic_never_converts_back_to_a_rational_kind():
    # DOCUMENTED LIMITATION (CGAL_TRAPS_CHECKLIST: "CORE has no safe rationality test
    # for Expr"): a conic point never proves rational, so even a conic segment with
    # rational endpoints cannot be turned back into a SEGMENT curve.
    with pytest.raises(a2.NotRepresentableError):
        a2.ConicArc.segment((0, 0), (3, 4)).to_kind("segment")


def test_to_kind_conic_on_a_conic_is_the_identity(ellipse):
    assert ellipse.to_kind("conic") is ellipse


# ---------------------------------------------------------------------------
# arrangements
# ---------------------------------------------------------------------------

def test_arrangement_of_one_ellipse(ellipse):
    # 2 vertical tangency vertices (+-2, 0), 2 x-monotone edges, inside + outside.
    arr = a2.Arrangement("conic")
    arr.insert(ellipse)
    assert (arr.number_of_vertices, arr.number_of_edges, arr.number_of_faces) == (2, 2, 2)
    assert arr.is_valid() and arr.number_of_unbounded_faces == 1


def test_arrangement_of_ellipse_and_circle(ellipse, circle_3_2):
    # x^2+4y^2=4 and x^2+y^2=9/4 meet where 3y^2 = 7/4, i.e. y^2 = 7/12 and x^2 = 5/3:
    # 4 transversal intersections.  V = 2 (ellipse tangencies) + 2 (circle tangencies)
    # + 4 = 8; each closed curve carries 6 vertices hence 6 edges, E = 12;
    # Euler F = 2 - V + E = 6 (unbounded + lens + 2 ellipse lobes + 2 circle caps).
    arr = a2.Arrangement("conic")
    arr.insert([ellipse, circle_3_2])
    assert (arr.number_of_vertices, arr.number_of_edges, arr.number_of_faces) == (8, 12, 6)
    assert arr.is_valid()


def test_arrangement_of_ellipse_circle_and_segment(ellipse, circle_3_2):
    # Adding y = 0 from (-3,0) to (3,0) puts 2 new vertices (its endpoints) and passes
    # through the 4 existing tangency vertices (+-2,0) and (+-3/2,0), so it is cut into
    # 5 edges.  V = 10, E = 17, F = 2 - 10 + 17 = 9.
    arr = a2.Arrangement("conic")
    arr.insert([ellipse, circle_3_2, a2.ConicArc.segment((-3, 0), (3, 0))])
    assert arr.number_of_vertices == 10
    assert arr.number_of_edges == 17
    assert arr.number_of_halfedges == 34
    assert arr.number_of_faces == 9
    assert arr.number_of_unbounded_faces == 1
    assert arr.number_of_curves == 3
    assert len(arr) == 17
    assert arr.is_valid()


def test_arrangement_vertex_degrees(ellipse, circle_3_2):
    # the two segment endpoints are degree 1 (dangling antennas), every other vertex
    # has two curves crossing it -> degree 4.
    arr = a2.Arrangement("conic")
    arr.insert([ellipse, circle_3_2, a2.ConicArc.segment((-3, 0), (3, 0))])
    degrees = sorted(v.degree for v in arr.vertices())
    assert degrees == [1, 1] + [4] * 8


def test_arrangement_history_induced_edges(ellipse, circle_3_2):
    arr = a2.Arrangement("conic")
    handles = arr.insert([ellipse, circle_3_2, a2.ConicArc.segment((-3, 0), (3, 0))])
    assert [h.number_of_induced_edges for h in handles] == [6, 6, 5]
    assert sum(h.number_of_induced_edges for h in handles) == arr.number_of_edges


def test_arrangement_remove_curve(ellipse, circle_3_2):
    # Removing the ellipse deletes its 6 edges; the 4 intersection vertices survive
    # (they still carry circle edges) and (+-2,0) survive (they carry segment edges).
    # V = 10, E = 6 (circle) + 5 (segment) = 11, F = 2 - 10 + 11 = 3.
    arr = a2.Arrangement("conic")
    handles = arr.insert([ellipse, circle_3_2, a2.ConicArc.segment((-3, 0), (3, 0))])
    assert arr.remove_curve(handles[0]) == 6
    assert (arr.number_of_vertices, arr.number_of_edges, arr.number_of_faces) == (10, 11, 3)
    assert arr.is_valid()


def test_arrangement_bbox(ellipse, circle_3_2):
    # extreme vertices: x = +-3 (segment endpoints), y = +-sqrt(7/12) (intersections)
    arr = a2.Arrangement("conic")
    arr.insert([ellipse, circle_3_2, a2.ConicArc.segment((-3, 0), (3, 0))])
    xmin, ymin, xmax, ymax = arr.bbox()
    assert (xmin, xmax) == pytest.approx((-3.0, 3.0), abs=1e-12)
    assert (ymin, ymax) == pytest.approx((-math.sqrt(7 / 12), math.sqrt(7 / 12)), abs=1e-12)


def test_arrangement_bounded_faces_and_face_polygons(ellipse, circle_3_2):
    arr = a2.Arrangement("conic")
    arr.insert([ellipse, circle_3_2, a2.ConicArc.segment((-3, 0), (3, 0))])
    bounded = arr.bounded_faces()
    assert len(bounded) == 8                      # 9 faces minus the unbounded one
    for face in bounded:
        polygon = face.polygon()
        assert len(polygon.holes) == 0
        assert len(polygon.outer.curves) == len(face.outer_ccb())


def test_arrangement_locate_inside_the_lens(ellipse, circle_3_2):
    arr = a2.Arrangement("conic")
    arr.insert([ellipse, circle_3_2])
    face = arr.locate((0, 0))                     # the centre is inside both curves
    assert isinstance(face, a2.Face) and not face.is_unbounded
    outside = arr.locate((10, 10))
    assert isinstance(outside, a2.Face) and outside.is_unbounded


def test_arrangement_locate_a_vertex(ellipse):
    arr = a2.Arrangement("conic")
    arr.insert(ellipse)
    assert isinstance(arr.locate((2, 0)), a2.Vertex)


def test_arrangement_insert_point_makes_an_isolated_vertex(ellipse):
    arr = a2.Arrangement("conic")
    arr.insert(ellipse)
    v = arr.insert_point((0, 0))
    assert v.is_isolated and arr.number_of_isolated_vertices == 1
    assert arr.number_of_vertices == 3


def test_arrangement_do_intersect(ellipse):
    arr = a2.Arrangement("conic")
    arr.insert(ellipse)
    assert arr.do_intersect(a2.ConicArc.segment((-5, 0), (5, 0))) is True
    assert arr.do_intersect(a2.ConicArc.segment((-5, 5), (5, 5))) is False
    assert arr.number_of_edges == 2               # do_intersect must not modify


def test_conic_arrangement_of_the_square_fixture(square_arr):
    # the same combinatorics as the SEGMENT arrangement of tests/conftest.py:
    # V = 8, E = 9, F = 3 -- carried over curve by curve into the conic kind.
    arr = a2.Arrangement("conic")
    arr.insert([handle.curve.to_kind("conic") for handle in square_arr.curves()])
    assert (arr.number_of_vertices, arr.number_of_edges, arr.number_of_faces) == \
        (square_arr.number_of_vertices, square_arr.number_of_edges, square_arr.number_of_faces)
    assert (arr.number_of_vertices, arr.number_of_edges, arr.number_of_faces) == (8, 9, 3)
    assert arr.is_valid()


# ---------------------------------------------------------------------------
# Boolean set operations on conic polygons
# ---------------------------------------------------------------------------

@pytest.fixture
def ellipse_polygon(ellipse):
    """The full ellipse as a 2-curve conic polygon (its two x-monotone halves)."""
    return a2.Polygon(ellipse.make_x_monotone())


@pytest.fixture
def circle_polygon():
    """The circle (x-3)^2 + y^2 = 4 as a 2-curve conic polygon."""
    return a2.Polygon(a2.ConicArc.circle((3, 0), 2).make_x_monotone())


def test_conic_polygon_basics(ellipse_polygon):
    assert ellipse_polygon.kind == a2.Kind.CONIC
    assert len(ellipse_polygon.curves) == 2       # one x-monotone half each
    assert ellipse_polygon.orientation() == 1     # the ccw ellipse stays ccw
    assert a2.orientation(ellipse_polygon) == 1
    assert a2.is_valid_polygon(ellipse_polygon)
    assert ellipse_polygon.reverse().orientation() == -1


def test_polygon_set_insert(ellipse_polygon):
    ps = a2.PolygonSet("conic")
    assert ps.is_empty
    ps.insert(ellipse_polygon)
    assert not ps.is_empty and ps.number_of_polygons_with_holes == 1
    assert ps.kind == a2.Kind.CONIC


def test_polygon_set_union_of_ellipse_and_circle(ellipse_polygon, circle_polygon):
    # x^2+4y^2=4 and (x-3)^2+y^2=4 meet where x^2 - 8x + 8 = 0, i.e. x = 4 - 2 sqrt 2
    # (the root 4 + 2 sqrt 2 is outside the ellipse): 2 intersection points.
    # The union boundary is broken at those 2 points and at the 2 remaining vertical
    # tangencies (-2, 0) and (5, 0)  ->  4 x-monotone arcs, no hole.
    A = a2.PolygonSet("conic"); A.insert(ellipse_polygon)
    B = a2.PolygonSet("conic"); B.insert(circle_polygon)
    union = A | B
    assert union.number_of_polygons_with_holes == 1
    pwh = union.polygons_with_holes()[0]
    assert len(pwh.outer.curves) == 4 and len(pwh.holes) == 0


def test_polygon_set_intersection_is_a_lens(ellipse_polygon, circle_polygon):
    # the lens is bounded by the right ellipse arc (split at the tangency (2,0)) and
    # the left circle arc (split at the tangency (1,0))  ->  4 arcs.
    A = a2.PolygonSet("conic"); A.insert(ellipse_polygon)
    B = a2.PolygonSet("conic"); B.insert(circle_polygon)
    lens = A & B
    assert lens.number_of_polygons_with_holes == 1
    assert len(lens.polygons_with_holes()[0].outer.curves) == 4
    assert lens.oriented_side((F(3, 2), 0)) == 1        # inside both
    assert lens.oriented_side((0, 0)) == -1             # inside the ellipse only


def test_polygon_set_difference(ellipse_polygon, circle_polygon):
    # ellipse minus circle: left ellipse arc (split at (-2,0)) plus the left circle
    # arc (split at (1,0))  ->  4 arcs, one component.
    A = a2.PolygonSet("conic"); A.insert(ellipse_polygon)
    B = a2.PolygonSet("conic"); B.insert(circle_polygon)
    diff = A - B
    assert diff.number_of_polygons_with_holes == 1
    assert len(diff.polygons_with_holes()[0].outer.curves) == 4
    assert diff.oriented_side((-1, 0)) == 1
    assert diff.oriented_side((3, 0)) == -1


def test_polygon_set_symmetric_difference_has_a_hole(ellipse_polygon, circle_polygon):
    # union minus intersection: the union boundary (4 arcs) with the lens (4 arcs) cut
    # out of it as a single hole.
    A = a2.PolygonSet("conic"); A.insert(ellipse_polygon)
    B = a2.PolygonSet("conic"); B.insert(circle_polygon)
    sym = A ^ B
    assert sym.number_of_polygons_with_holes == 1
    pwh = sym.polygons_with_holes()[0]
    assert len(pwh.outer.curves) == 4 and len(pwh.holes) == 1
    assert len(pwh.holes[0].curves) == 4
    assert sym.oriented_side((F(3, 2), 0)) == -1        # the lens is the hole


def test_polygon_set_complement_is_unbounded(ellipse_polygon):
    A = a2.PolygonSet("conic"); A.insert(ellipse_polygon)
    comp = ~A
    assert comp.number_of_polygons_with_holes == 1
    pwh = comp.polygons_with_holes()[0]
    assert pwh.is_unbounded and len(pwh.holes) == 1
    assert comp.oriented_side((0, 0)) == -1
    assert comp.oriented_side((10, 10)) == 1


def test_polygon_set_do_intersect(ellipse_polygon, circle_polygon):
    A = a2.PolygonSet("conic"); A.insert(ellipse_polygon)
    B = a2.PolygonSet("conic"); B.insert(circle_polygon)
    far = a2.PolygonSet("conic")
    far.insert(a2.Polygon(a2.ConicArc.circle((100, 0), 1).make_x_monotone()))
    assert A.do_intersect(B) is True
    assert A.do_intersect(far) is False


def test_polygon_set_disjoint_union_has_two_components(ellipse_polygon):
    A = a2.PolygonSet("conic"); A.insert(ellipse_polygon)
    far = a2.PolygonSet("conic")
    far.insert(a2.Polygon(a2.ConicArc.circle((100, 0), 1).make_x_monotone()))
    assert (A | far).number_of_polygons_with_holes == 2


def test_polygon_set_to_arrangement(ellipse_polygon, circle_polygon):
    # the union arrangement has the 2 intersection vertices plus the 2 tangencies:
    # V = 4, E = 4, F = 2 - 4 + 4 = 2 (inside + unbounded), 1 contained face.
    A = a2.PolygonSet("conic"); A.insert(ellipse_polygon)
    B = a2.PolygonSet("conic"); B.insert(circle_polygon)
    arr, contained = (A | B).to_arrangement()
    assert (arr.number_of_vertices, arr.number_of_edges, arr.number_of_faces) == (4, 4, 2)
    assert len(contained) == 1 and not contained[0].is_unbounded
    assert arr.kind == a2.Kind.CONIC


def test_polygon_set_copy_is_independent(ellipse_polygon, circle_polygon):
    A = a2.PolygonSet("conic"); A.insert(ellipse_polygon)
    B = A.copy()
    B.join(circle_polygon)
    assert len(A.polygons_with_holes()[0].outer.curves) == 2
    assert len(B.polygons_with_holes()[0].outer.curves) == 4


def test_free_boolean_functions(ellipse_polygon, circle_polygon):
    assert a2.join(ellipse_polygon, circle_polygon).number_of_polygons_with_holes == 1
    assert a2.intersection(ellipse_polygon, circle_polygon).number_of_polygons_with_holes == 1
    assert a2.do_intersect(ellipse_polygon, circle_polygon) is True
    assert a2.oriented_side(ellipse_polygon, (0, 0)) == 1


# ---------------------------------------------------------------------------
# process hygiene
# ---------------------------------------------------------------------------

_CHILD_SOURCE = """
import arrangement_2d as a2
from fractions import Fraction as F

ellipse = a2.ConicArc.ellipse((0, 0), 2, 1)
circle = a2.ConicArc.circle((0, 0), squared_radius=F(9, 4))
arr = a2.Arrangement('conic')
arr.insert([ellipse, circle, a2.ConicArc.segment((-3, 0), (3, 0))])
exact = [v.point.exact() for v in arr.vertices()]
ps = a2.PolygonSet('conic')
ps.insert(a2.Polygon(ellipse.make_x_monotone()))
ps.join(a2.Polygon(a2.ConicArc.circle((3, 0), 2).make_x_monotone()))
a2.conic_allow_hyperbolic(True)
a2.ConicArc.from_coefficients(-12, 12, 7, 0, 0, -25, orientation='cw',
                              source=(F(1, 5), F(7, 5)), target=(F(13, 10), F(8, 5)))
print(arr.number_of_vertices, ps.number_of_polygons_with_holes, len(exact))
"""


def test_process_exits_cleanly_after_using_conics():
    # CGAL_TRAPS_CHECKLIST: anything holding a CORE::Expr that is destroyed during
    # static teardown aborts with "! blocks.empty()" (CORE MemoryPool).  A conic-heavy
    # child process must therefore still exit with status 0.
    package_root = os.path.dirname(os.path.dirname(os.path.abspath(a2.__file__)))
    env = dict(os.environ)
    env["PYTHONPATH"] = os.pathsep.join(
        [package_root] + ([env["PYTHONPATH"]] if env.get("PYTHONPATH") else []))
    proc = subprocess.run([sys.executable, "-c", _CHILD_SOURCE],
                          capture_output=True, text=True, timeout=600, env=env)
    assert proc.returncode == 0, f"stdout={proc.stdout!r} stderr={proc.stderr!r}"
    assert proc.stdout.split() == ["10", "1", "10"]
    assert "blocks.empty" not in proc.stderr
