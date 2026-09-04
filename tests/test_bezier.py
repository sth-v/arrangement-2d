"""Tests for the ``bezier`` geometry kind (CGAL ``Arr_Bezier_curve_traits_2``).

Every expected number in this file is hand-derived from the closed form of the reference
curves defined below (the derivations are written out next to each constant and repeated
in the individual test doc-strings).  Nothing is copied from a program run.

Reference curves (Bernstein form, ``B(t) = sum_i C(n,i) t^i (1-t)^(n-i) P_i``):

``CUBIC_B``  = (0,0) (4,1) (-2,2) (2,3)   -- the reference cubic of
    ``docs/dev/cgal61_api/traits_bezier.md`` section 11.
    The y control values 0,1,2,3 are equally spaced, so ``y(t) = 3t``; expanding x gives
    ``x(t) = 20t^3 - 30t^2 + 12t``.  ``x'(t) = 12(5t^2 - 5t + 1)`` vanishes at
    ``t = (5 -/+ sqrt5)/10``, hence THREE x-monotone pieces.  On those roots
    ``t^2 = t - 1/5``, so ``x = 2 - 2t`` there: the two vertical tangency points are
    exactly ``(1 + sqrt5/5, (15 - 3 sqrt5)/10)`` and ``(1 - sqrt5/5, (15 + 3 sqrt5)/10)``.

``QUAD_B2``  = (0,3) (2,-1) (3,4)         -- the companion quadratic of the same section.

``PARABOLA`` = (0,0) (1/2,3/2) (1,0)      -- rational control points.
    ``x(t) = t`` and ``y(t) = 3t(1-t)``, i.e. the graph ``y = 3x(1-x)`` over [0,1].

``S_CUBIC``  = (0,0) (1,3) (2,-3) (3,0)   -- x controls equally spaced => ``x(t) = 3t``;
    ``y(t) = 9t(1-t)(1-2t) = 18t^3 - 27t^2 + 9t``.  x is strictly increasing => ONE piece.

``T_CUBIC``  = (0,-2) (1,4) (2,-2) (3,-2) -- ``x(t) = 3t``, ``y(t) = 18t^3 - 36t^2 + 18t - 2``.
    ``y_S(t) - y_T(t) = 9t^2 - 9t + 2 = (3t-1)(3t-2)``, so S_CUBIC and T_CUBIC cross exactly
    at ``t = 1/3`` -> ``(1, 2/3)`` and ``t = 2/3`` -> ``(2, -2/3)``.

``LOOP``     = (-1,0) (3,10) (-3,10) (1,0)  -- self-intersecting cubic.
    ``x(t) = 20t^3 - 30t^2 + 12t - 1 = (2t-1)(10t^2 - 10t + 1)``, ``y(t) = 30t(1-t)``.
    x vanishes at ``t = 1/2`` and at ``t = (5 -/+ sqrt15)/10``; on the latter pair
    ``t - t^2 = 1/10`` so ``y = 3`` for BOTH, i.e. the curve crosses itself exactly at the
    rational point ``(0, 3)``.  ``x'`` vanishes at ``t = (5 -/+ sqrt5)/10`` where
    ``t - t^2 = 1/5`` and ``x = 1 - 2t``: the vertical tangency points are exactly
    ``(sqrt5/5, 6)`` and ``(-sqrt5/5, 6)``.

``DEG5``     = (0,0) (1,4) (2,-4) (3,4) (4,-4) (5,0)  -- degree 5, ``x(t) = 5t``.
    ``y(1/2) = (0 + 5*4 + 10*(-4) + 10*4 + 5*(-4) + 0)/32 = 0`` and
    ``y(1/5) = (1024 - 512 + 128 - 16)/625 = 624/625``.

``LENS_TOP`` = (0,0) (1,3) (3,3) (4,0) and ``LENS_BOT`` = (4,0) (3,-3) (1,-3) (0,0)
    together form a closed "lens" traversed clockwise (right along the top, left along the
    bottom).  ``x_top(t) = 3t + 3t^2 - 2t^3`` has ``x' = 3 + 6t - 6t^2 > 0`` on [0,1] and
    ``x_bot(t) = 4 - 3t - 3t^2 + 2t^3`` has ``x' < 0`` on [0,1], so each arc is a single
    x-monotone piece and the two chain into a closed boundary.
"""

from __future__ import annotations

import math
import os
import subprocess
import sys
from fractions import Fraction as F

import pytest

a2 = pytest.importorskip("arrangement_2d")

if not a2.kind_available("bezier"):  # pragma: no cover - build without the Bezier TU
    pytest.skip("the bezier kind is not available in this build", allow_module_level=True)


# ---------------------------------------------------------------------------
# reference control polygons (see the module doc-string for the derivations)
# ---------------------------------------------------------------------------

CUBIC_B = [(0, 0), (4, 1), (-2, 2), (2, 3)]
QUAD_B2 = [(0, 3), (2, -1), (3, 4)]
PARABOLA = [(0, 0), (F(1, 2), F(3, 2)), (1, 0)]
S_CUBIC = [(0, 0), (1, 3), (2, -3), (3, 0)]
T_CUBIC = [(0, -2), (1, 4), (2, -2), (3, -2)]
LOOP = [(-1, 0), (3, 10), (-3, 10), (1, 0)]
DEG5 = [(0, 0), (1, 4), (2, -4), (3, 4), (4, -4), (5, 0)]
LENS_TOP = [(0, 0), (1, 3), (3, 3), (4, 0)]
LENS_BOT = [(4, 0), (3, -3), (1, -3), (0, 0)]

# t = (5 - sqrt5)/10 and (5 + sqrt5)/10, the vertical-tangency parameters of CUBIC_B/LOOP
T_TAN_LO = (5 - math.sqrt(5)) / 10
T_TAN_HI = (5 + math.sqrt(5)) / 10
SQRT5_OVER_5 = math.sqrt(5) / 5


# ---------------------------------------------------------------------------
# fixtures / small helpers
# ---------------------------------------------------------------------------

@pytest.fixture
def cubic():
    """The reference cubic of traits_bezier.md section 11."""
    return a2.BezierCurve(CUBIC_B)


@pytest.fixture
def parabola():
    """y = 3x(1-x) over [0, 1], as a quadratic Bezier with rational control points."""
    return a2.BezierCurve(PARABOLA)


@pytest.fixture
def loop():
    """The self-intersecting cubic (crosses itself at (0, 3))."""
    return a2.BezierCurve(LOOP)


@pytest.fixture
def crossing_pair():
    """S_CUBIC and T_CUBIC: two cubics crossing exactly at (1, 2/3) and (2, -2/3)."""
    return a2.BezierCurve(S_CUBIC), a2.BezierCurve(T_CUBIC)


@pytest.fixture
def lens_polygon():
    """A closed Bezier "polygon" made of two cubic arcs, traversed clockwise."""
    return a2.Polygon([a2.BezierCurve(LENS_TOP), a2.BezierCurve(LENS_BOT)])


def _x_of_cubic_b(t):
    """x(t) = 20t^3 - 30t^2 + 12t (exact, for a Fraction t)."""
    return 20 * t ** 3 - 30 * t ** 2 + 12 * t


def _y_s_cubic(t):
    """y(t) = 9t(1-t)(1-2t) of S_CUBIC (exact, for a Fraction t)."""
    return 9 * t * (1 - t) * (1 - 2 * t)


def _dist_point_segment(p, a, b):
    px, py = p
    ax, ay = a
    bx, by = b
    dx, dy = bx - ax, by - ay
    denom = dx * dx + dy * dy
    if denom == 0.0:
        return math.hypot(px - ax, py - ay)
    t = max(0.0, min(1.0, ((px - ax) * dx + (py - ay) * dy) / denom))
    return math.hypot(px - ax - t * dx, py - ay - t * dy)


def _dist_to_polyline(p, pts):
    return min(_dist_point_segment(p, pts[i], pts[i + 1]) for i in range(len(pts) - 1))


_PKG_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(a2.__file__)))


def _run_child(code, timeout=300):
    """Run `code` in a fresh interpreter that can import arrangement_2d."""
    env = dict(os.environ)
    env["PYTHONPATH"] = os.pathsep.join(
        [_PKG_ROOT] + ([env["PYTHONPATH"]] if env.get("PYTHONPATH") else [])
    )
    return subprocess.run(
        [sys.executable, "-c", code],
        capture_output=True, text=True, timeout=timeout, env=env,
    )


# ===========================================================================
# construction
# ===========================================================================

def test_quadratic_construction(parabola):
    """3 control points -> degree 2, kind 'bezier'."""
    assert parabola.kind == a2.Kind.BEZIER
    assert parabola.degree == 2
    assert len(parabola.control_points) == 3


def test_cubic_construction(cubic):
    """4 control points -> degree 3, and they come back in order."""
    assert cubic.degree == 3
    assert [tuple(p.exact()) for p in cubic.control_points] == [
        (F(0), F(0)), (F(4), F(1)), (F(-2), F(2)), (F(2), F(3))
    ]


def test_degree5_construction():
    """6 control points -> degree 5."""
    d5 = a2.BezierCurve(DEG5)
    assert d5.degree == 5
    assert len(d5.control_points) == 6
    assert [tuple(p.exact()) for p in d5.control_points] == [
        (F(x), F(y)) for x, y in DEG5
    ]


def test_rational_control_points(parabola):
    """Fraction control points are stored exactly (1/2, 3/2)."""
    cps = [tuple(p.exact()) for p in parabola.control_points]
    assert cps == [(F(0), F(0)), (F(1, 2), F(3, 2)), (F(1), F(0))]


def test_control_points_from_float_are_exact_binary_values():
    """A float coordinate goes in as its exact binary value: 0.5 -> 1/2, 0.25 -> 1/4."""
    c = a2.BezierCurve([(0, 0), (0.5, 0.25), (1, 0)])
    assert tuple(c.control_points[1].exact()) == (F(1, 2), F(1, 4))


def test_control_points_from_string_fractions():
    """A "3/4" string coordinate is parsed exactly."""
    c = a2.BezierCurve([("3/4", "1/2"), (1, 0)])
    assert tuple(c.control_points[0].exact()) == (F(3, 4), F(1, 2))


def test_control_points_are_rational_segment_points(cubic):
    """Control points are exact rational points (reported with the SEGMENT kind)."""
    for p in cubic.control_points:
        assert p.is_rational
        assert p.kind == a2.Kind.SEGMENT


def test_two_control_points_is_a_straight_degree_one_curve():
    """2 control points -> degree 1, exactly convertible to a Segment."""
    line = a2.BezierCurve([(0, 0), (3, 4)])
    assert line.degree == 1
    seg = line.to_kind("segment")
    assert isinstance(seg, a2.Segment)
    assert tuple(seg.source.exact()) == (F(0), F(0))
    assert tuple(seg.target.exact()) == (F(3), F(4))


def test_to_kind_segment_requires_a_straight_curve():
    """A genuinely curved Bezier arc has no exact straight-line image."""
    with pytest.raises(a2.NotRepresentableError):
        a2.BezierCurve([(0, 0), (1, 1), (2, 0)]).to_kind("segment")


def test_to_kind_polyline_of_a_curved_arc_is_not_representable(cubic):
    """Same for the polyline kind (its pieces would need rational endpoints)."""
    with pytest.raises(a2.NotRepresentableError):
        cubic.to_kind("polyline")


def test_to_kind_is_the_identity_for_the_same_kind(cubic):
    assert cubic.to_kind("bezier") is cubic
    assert cubic.to_kind(a2.Kind.BEZIER) is cubic


def test_too_few_control_points_raises():
    """A Bezier curve needs at least 2 control points."""
    with pytest.raises(ValueError):
        a2.BezierCurve([(0, 0)])
    with pytest.raises(ValueError):
        a2.BezierCurve([])


def test_control_point_arity_is_checked():
    """A 3-coordinate control point is rejected (the Bezier kind is planar)."""
    with pytest.raises(ValueError):
        a2.BezierCurve([(0, 0, 0), (1, 1, 1)])


def test_curve_id_is_stable_and_distinct(cubic):
    """curve_id identifies the supporting Curve_2 rep: stable per object, distinct
    between two separately built (even geometrically identical) curves."""
    assert cubic.curve_id == cubic.curve_id
    other = a2.BezierCurve(CUBIC_B)
    assert other.curve_id != cubic.curve_id


def test_x_monotone_curve_equality_is_geometric():
    """Two x-monotone curves with the same control polygon compare equal."""
    s = a2.BezierCurve(S_CUBIC)
    assert s == a2.BezierCurve(S_CUBIC)
    assert s == s.make_x_monotone()[0]
    assert s != a2.BezierCurve(T_CUBIC)


def test_general_curve_equality_is_geometric(cubic):
    """Two general (non-x-monotone) curves with the same control polygon are equal.

    ``Curve.__eq__`` (DESIGN.md section 3, "Common curve API") promotes both operands with
    ``_xg`` and returns ``NotImplemented`` when the promotion fails, so for a Bezier curve
    that splits into several x-monotone pieces ``==`` silently degrades to an identity
    comparison instead of comparing the geometry.
    """
    assert cubic == a2.BezierCurve(CUBIC_B)
    assert cubic != a2.BezierCurve(LOOP)


def test_repr_lists_the_control_points(cubic):
    """repr() is self-describing and names the control polygon."""
    assert repr(cubic) == "BezierCurve([(0, 0), (4, 1), (-2, 2), (2, 3)])"


def test_has_self_intersections(cubic, loop, parabola):
    """LOOP crosses itself at (0,3); CUBIC_B and the parabola do not."""
    assert loop.has_self_intersections is True
    assert cubic.has_self_intersections is False
    assert parabola.has_self_intersections is False


def test_bbox_is_the_control_polygon_box(cubic, parabola):
    """The bbox is the (convex-hull) control-polygon box, not the tight curve box:
    CUBIC_B's control x range is [-2, 4] and y range [0, 3]; the parabola's control
    polygon reaches y = 3/2 although the curve only reaches y = 3/4."""
    assert cubic.bbox() == (-2.0, 0.0, 4.0, 3.0)
    assert parabola.bbox() == (0.0, 0.0, 1.0, 1.5)


# ===========================================================================
# make_x_monotone / parameter_range / supporting_curve
# ===========================================================================

def test_make_x_monotone_cubic_gives_three_pieces(cubic):
    """x'(t) = 12(5t^2-5t+1) has two roots in (0,1) -> 3 x-monotone pieces."""
    pieces = cubic.make_x_monotone()
    assert len(pieces) == 3
    assert all(isinstance(p, a2.BezierCurve) for p in pieces)
    assert [p.xid for p in pieces] == [1, 2, 3]


def test_make_x_monotone_piece_directions(cubic):
    """x increases, decreases, increases across the two vertical tangencies."""
    assert [p.is_directed_right for p in cubic.make_x_monotone()] == [True, False, True]


def test_make_x_monotone_parameter_ranges(cubic):
    """The pieces split the unit interval at t = (5 -/+ sqrt5)/10."""
    ranges = [p.parameter_range for p in cubic.make_x_monotone()]
    assert ranges[0][0] == 0.0
    assert ranges[2][1] == 1.0
    assert ranges[0][1] == pytest.approx(T_TAN_LO, abs=1e-12)
    assert ranges[1] == pytest.approx((T_TAN_LO, T_TAN_HI), abs=1e-12)
    assert ranges[2][0] == pytest.approx(T_TAN_HI, abs=1e-12)


def test_make_x_monotone_ranges_are_ordered_and_contiguous(cubic):
    """parameter_range is the ORDERED interval and the pieces tile [0, 1]."""
    ranges = [p.parameter_range for p in cubic.make_x_monotone()]
    for lo, hi in ranges:
        assert lo < hi
    for i in range(len(ranges) - 1):
        assert ranges[i][1] == ranges[i + 1][0]


def test_make_x_monotone_monotone_cubic_is_one_piece():
    """S_CUBIC has x(t) = 3t, strictly increasing -> a single x-monotone piece."""
    pieces = a2.BezierCurve(S_CUBIC).make_x_monotone()
    assert len(pieces) == 1
    assert pieces[0].parameter_range == (0.0, 1.0)
    assert pieces[0].is_directed_right is True


def test_make_x_monotone_quadratic_is_one_piece(parabola):
    """x(t) = t is strictly increasing -> a single piece."""
    assert len(parabola.make_x_monotone()) == 1


def test_make_x_monotone_degree5_is_one_piece():
    """DEG5 has x(t) = 5t -> a single x-monotone piece despite the wiggly y."""
    assert len(a2.BezierCurve(DEG5).make_x_monotone()) == 1


def test_make_x_monotone_loop_gives_three_pieces(loop):
    """LOOP has the same x'(t) roots as CUBIC_B -> 3 pieces."""
    pieces = loop.make_x_monotone()
    assert len(pieces) == 3
    assert [p.xid for p in pieces] == [1, 2, 3]


def test_pieces_chain_end_to_end(cubic):
    """Consecutive x-monotone pieces share an endpoint (parametric order)."""
    pieces = cubic.make_x_monotone()
    for i in range(len(pieces) - 1):
        assert pieces[i].target == pieces[i + 1].source


def test_piece_endpoints_match_evaluate(cubic):
    """The first piece runs from B(0) = (0,0) to the tangency (1 + sqrt5/5, (15-3sqrt5)/10)."""
    first = cubic.make_x_monotone()[0]
    assert tuple(first.source.exact()) == (F(0), F(0))
    assert first.target.x == pytest.approx(1 + SQRT5_OVER_5, abs=1e-12)
    assert first.target.y == pytest.approx(3 * T_TAN_LO, abs=1e-12)


def test_parameter_range_requires_an_x_monotone_curve(cubic):
    """A curve that splits into 3 pieces has no single parameter range."""
    with pytest.raises(a2.NotXMonotoneError):
        cubic.parameter_range


def test_xid_requires_an_x_monotone_curve(cubic):
    """Likewise for the x-monotone piece index."""
    with pytest.raises(a2.NotXMonotoneError):
        cubic.xid


def test_x_monotone_general_curve_is_promoted():
    """A general Curve_2 that happens to be x-monotone is promoted silently."""
    s = a2.BezierCurve(S_CUBIC)
    assert s.is_x_monotone is False          # stored as a general Curve_2
    assert s.parameter_range == (0.0, 1.0)   # ... but promotable
    assert s.xid == 1
    assert s.make_x_monotone()[0].is_x_monotone is True


def test_piece_reports_the_supporting_control_polygon(cubic):
    """An x-monotone piece keeps the whole supporting curve: same degree, same control
    points; only parameter_range/xid describe the piece itself."""
    piece = cubic.make_x_monotone()[1]
    assert piece.degree == 3
    assert [tuple(p.exact()) for p in piece.control_points] == [
        (F(x), F(y)) for x, y in CUBIC_B
    ]
    assert piece.curve_id == cubic.curve_id


def test_piece_evaluate_uses_the_supporting_curve_parameter(cubic):
    """evaluate() is a supporting-curve query, so a parameter outside the piece's own
    range is still valid: piece 2 covers t in [(5-sqrt5)/10, (5+sqrt5)/10] yet
    piece.evaluate(1/10) returns B(1/10) = (20/1000 - 30/100 + 12/10, 3/10) = (23/25, 3/10).
    """
    piece = cubic.make_x_monotone()[1]
    assert piece.parameter_range[0] > 0.1        # 1/10 is NOT inside this piece
    assert tuple(piece.evaluate(F(1, 10)).exact()) == (F(23, 25), F(3, 10))
    assert _x_of_cubic_b(F(1, 10)) == F(23, 25)


def test_supporting_curve_round_trip(cubic):
    """Every piece reports the original curve (same control points, same id)."""
    for piece in cubic.make_x_monotone():
        sup = piece.supporting_curve
        assert sup.curve_id == cubic.curve_id
        assert sup.degree == 3
        assert [tuple(p.exact()) for p in sup.control_points] == [
            (F(x), F(y)) for x, y in CUBIC_B
        ]


def test_piece_min_max_and_left_right(parabola):
    """For a left-to-right piece min/left = source and max/right = target."""
    piece = parabola.make_x_monotone()[0]
    assert piece.is_directed_right is True
    assert piece.left == piece.source == piece.min_vertex
    assert piece.right == piece.target == piece.max_vertex
    assert piece.is_vertical is False


def test_opposite_flips_the_direction(parabola):
    """opposite() reverses source/target but keeps the geometry."""
    piece = parabola.make_x_monotone()[0]
    opp = piece.opposite()
    assert opp.is_directed_right is False
    assert opp.source == piece.target
    assert opp.target == piece.source
    assert opp.compare_endpoints_xy() == -piece.compare_endpoints_xy()


# ===========================================================================
# evaluate / sample
# ===========================================================================

def test_evaluate_exact_fraction(cubic):
    """B(1/3): x = 20/27 - 30/9 + 4 = 38/27, y = 3/3 = 1 (traits_bezier.md section 11)."""
    p = cubic.evaluate(F(1, 3))
    assert isinstance(p, a2.Point)
    assert p.kind == a2.Kind.BEZIER
    assert p.is_rational
    assert tuple(p.exact()) == (F(38, 27), F(1))


def test_evaluate_exact_endpoints(cubic):
    """B(0) is the first control point and B(1) the last."""
    assert tuple(cubic.evaluate(0).exact()) == (F(0), F(0))
    assert tuple(cubic.evaluate(1).exact()) == (F(2), F(3))


def test_evaluate_integer_parameter_is_exact(cubic):
    """An int t takes the exact path (returns a Point, not a tuple)."""
    assert isinstance(cubic.evaluate(0), a2.Point)
    assert isinstance(cubic.evaluate(1), a2.Point)


def test_evaluate_float_is_approximate(cubic):
    """A float t returns a plain (x, y) tuple of floats close to the exact value."""
    x, y = cubic.evaluate(1.0 / 3.0)
    assert isinstance(x, float) and isinstance(y, float)
    assert x == pytest.approx(38.0 / 27.0, abs=1e-12)
    assert y == pytest.approx(1.0, abs=1e-12)


def test_evaluate_parabola_matches_closed_form(parabola):
    """x(t) = t and y(t) = 3t(1-t): B(1/3) = (1/3, 2/3), B(1/2) = (1/2, 3/4)."""
    assert tuple(parabola.evaluate(F(1, 3)).exact()) == (F(1, 3), F(2, 3))
    assert tuple(parabola.evaluate(F(1, 2)).exact()) == (F(1, 2), F(3, 4))


def test_evaluate_degree5_exact():
    """DEG5(1/2) = (5/2, 0) and DEG5(1/5) = (1, 624/625) (module doc-string)."""
    d5 = a2.BezierCurve(DEG5)
    assert tuple(d5.evaluate(F(1, 2)).exact()) == (F(5, 2), F(0))
    assert tuple(d5.evaluate(F(1, 5)).exact()) == (F(1), F(624, 625))


def test_evaluate_loop_self_intersection_parameters(loop):
    """LOOP(1/2) = (0, 15/2): x(1/2) = 0 and y(1/2) = 30/4."""
    assert tuple(loop.evaluate(F(1, 2)).exact()) == (F(0), F(15, 2))


def test_evaluate_out_of_range_parameter_raises(cubic):
    """The parameter must lie in [0, 1]."""
    with pytest.raises(ValueError):
        cubic.evaluate(2)
    with pytest.raises(ValueError):
        cubic.evaluate(F(-1, 2))


def test_sample_count_and_endpoints(parabola):
    """sample(0, 1, 5) returns 5 points starting at B(0) and ending at B(1)."""
    pts = parabola.sample(0.0, 1.0, 5)
    assert len(pts) == 5
    assert pts[0] == pytest.approx((0.0, 0.0), abs=1e-12)
    assert pts[-1] == pytest.approx((1.0, 0.0), abs=1e-12)


def test_sample_matches_evaluate(parabola):
    """Uniform samples of y = 3x(1-x): sample(0,1,5) hits t = 0, 1/4, 1/2, 3/4, 1."""
    pts = parabola.sample(0.0, 1.0, 5)
    for k, (x, y) in enumerate(pts):
        t = F(k, 4)
        assert x == pytest.approx(float(t), abs=1e-12)
        assert y == pytest.approx(float(3 * t * (1 - t)), abs=1e-12)


def test_sample_subrange(parabola):
    """sample(1/4, 3/4, 3) -> t = 1/4, 1/2, 3/4 -> y = 9/16, 3/4, 9/16."""
    pts = parabola.sample(0.25, 0.75, 3)
    assert pts == pytest.approx([(0.25, 0.5625), (0.5, 0.75), (0.75, 0.5625)], abs=1e-12)


def test_sample_of_cubic_matches_closed_form(cubic):
    """sample() follows x(t) = 20t^3-30t^2+12t, y(t) = 3t."""
    n = 9
    pts = cubic.sample(0.0, 1.0, n)
    for k, (x, y) in enumerate(pts):
        t = F(k, n - 1)
        assert x == pytest.approx(float(_x_of_cubic_b(t)), abs=1e-12)
        assert y == pytest.approx(float(3 * t), abs=1e-12)


def test_sample_needs_at_least_two_points(parabola):
    with pytest.raises(ValueError):
        parabola.sample(0.0, 1.0, 1)


# ===========================================================================
# points: exact / interval / refine / originators / parameter_at
# ===========================================================================

def test_rational_point_exact_is_fractions(cubic):
    """A point built at a rational parameter keeps exact rational coordinates."""
    p = cubic.evaluate(F(1, 3))
    ex = p.exact()
    assert all(isinstance(v, F) for v in ex)
    assert ex == (F(38, 27), F(1))


def test_tangency_point_is_algebraic(cubic):
    """The vertical tangency at t = (5-sqrt5)/10 has irrational x = 1 + sqrt5/5."""
    p = cubic.make_x_monotone()[0].target
    assert p.is_rational is False
    ex = p.exact()
    assert all(isinstance(v, a2.Algebraic) for v in ex)
    assert float(ex[0]) == pytest.approx(1 + SQRT5_OVER_5, abs=1e-12)
    assert float(ex[1]) == pytest.approx(3 * T_TAN_LO, abs=1e-12)


def test_algebraic_interval_contains_the_approximation(cubic):
    """The certified interval brackets .approx and the true value 1 + sqrt5/5."""
    ax = cubic.make_x_monotone()[0].target.exact()[0]
    lo, hi = ax.interval()
    assert lo <= ax.approx <= hi
    assert lo <= 1 + SQRT5_OVER_5 <= hi


def test_algebraic_refine_never_widens_the_interval(cubic):
    """CORE only refines: interval(200) is nested inside interval(53) and still
    brackets the exact value (the double pair bottoms out at 1 ulp)."""
    ax = cubic.make_x_monotone()[0].target.exact()[0]
    lo0, hi0 = ax.interval(53)
    lo1, hi1 = ax.refine(200)
    assert lo1 >= lo0 and hi1 <= hi0
    assert lo1 <= 1 + SQRT5_OVER_5 <= hi1
    lo2, hi2 = ax.interval(53)
    assert lo2 >= lo0 and hi2 <= hi0


def test_algebraic_of_a_tangency_with_rational_value(loop):
    """LOOP's tangency points are (+/- sqrt5/5, 6): y is exactly 6 but CORE cannot
    prove rationality, so it is reported as an Algebraic bracketing 6."""
    p = loop.make_x_monotone()[0].target
    ay = p.exact()[1]
    assert isinstance(ay, a2.Algebraic)
    assert ay.is_rational is False
    lo, hi = ay.interval()
    assert lo <= 6.0 <= hi
    assert float(ay) == pytest.approx(6.0, abs=1e-12)


def test_point_interval_brackets_both_coordinates(loop):
    """Point.interval() gives one certified interval per coordinate."""
    p = loop.make_x_monotone()[0].target
    (xlo, xhi), (ylo, yhi) = p.interval()
    assert xlo <= SQRT5_OVER_5 <= xhi
    assert ylo <= 6.0 <= yhi


def test_algebraic_is_not_hashable_and_has_a_sign(cubic):
    """An Algebraic that is not known rational is unhashable but has an exact sign."""
    ax = cubic.make_x_monotone()[0].target.exact()[0]
    assert ax.sign() == 1
    assert ax.exact() is None
    with pytest.raises(TypeError):
        hash(ax)


def test_point_compare_xy_and_compare_x(cubic):
    """B(1/3) = (38/27, 1) lies to the RIGHT of B(2/3) = (16/27, 2):
    x(2/3) = 160/27 - 120/9 + 8 = 16/27."""
    p13 = cubic.evaluate(F(1, 3))
    p23 = cubic.evaluate(F(2, 3))
    assert tuple(p23.exact()) == (F(16, 27), F(2))
    assert p13.compare_xy(p23) == 1
    assert p23.compare_xy(p13) == -1
    assert p13.compare_xy(p13) == 0
    assert p13.compare_x(p23) == 1


def test_bezier_point_equality_and_repr(cubic):
    """A point built at t = 1/2 equals the rational point with the same coordinates:
    x(1/2) = 20/8 - 30/4 + 6 = 1, y(1/2) = 3/2."""
    p = cubic.evaluate(F(1, 2))
    assert tuple(p.exact()) == (F(1), F(3, 2))
    assert p == a2.Point(1, F(3, 2), kind="bezier")
    assert p != a2.Point(0, 0, kind="bezier")
    assert repr(p).startswith("BezierPoint(")


def test_point_originators_single_curve(cubic):
    """B(1/3) has exactly one originator: (curve_id of B, t = 1/3)."""
    p = cubic.evaluate(F(1, 3))
    origs = cubic.point_originators(p)
    assert len(origs) == 1
    cid, t = origs[0]
    assert cid == cubic.curve_id
    assert t == pytest.approx(1.0 / 3.0, abs=1e-12)


def test_point_originators_of_a_tangency(cubic):
    """A vertical tangency point originates from B at t = (5-sqrt5)/10."""
    p = cubic.make_x_monotone()[0].target
    origs = cubic.point_originators(p)
    assert [cid for cid, _ in origs] == [cubic.curve_id]
    assert origs[0][1] == pytest.approx(T_TAN_LO, abs=1e-12)


def test_point_originators_of_an_intersection(crossing_pair):
    """The crossing (1, 2/3) lies on both cubics: two originators, both at t = 1/3
    (both curves are parametrised by x(t) = 3t)."""
    c1, c2 = crossing_pair
    p = c1.make_x_monotone()[0].intersect(c2.make_x_monotone()[0])[0][0]
    origs = c1.point_originators(p)
    assert len(origs) == 2
    assert {cid for cid, _ in origs} == {c1.curve_id, c2.curve_id}
    for _, t in origs:
        assert t == pytest.approx(1.0 / 3.0, abs=1e-12)


def test_parameter_at_default_curve(cubic):
    """parameter_at() defaults to the curve it is called on."""
    p = cubic.evaluate(F(1, 3))
    t = cubic.parameter_at(p)
    assert isinstance(t, a2.Algebraic)
    assert float(t) == pytest.approx(1.0 / 3.0, abs=1e-12)


def test_parameter_at_explicit_curve_id(crossing_pair):
    """The crossing point's parameter can be asked for on either curve (both 1/3)."""
    c1, c2 = crossing_pair
    p = c1.make_x_monotone()[0].intersect(c2.make_x_monotone()[0])[0][0]
    assert float(c1.parameter_at(p, c1.curve_id)) == pytest.approx(1 / 3, abs=1e-12)
    assert float(c1.parameter_at(p, c2.curve_id)) == pytest.approx(1 / 3, abs=1e-12)


def test_parameter_at_tangency(cubic):
    """The tangency parameter is the algebraic root (5 - sqrt5)/10."""
    p = cubic.make_x_monotone()[0].target
    assert float(cubic.parameter_at(p)) == pytest.approx(T_TAN_LO, abs=1e-12)


def test_parameter_at_unknown_curve_id_raises(cubic):
    """A point has no originator on a curve it does not lie on."""
    p = cubic.evaluate(F(1, 3))
    with pytest.raises(a2.UnsupportedError):
        cubic.parameter_at(p, 12345)


# ===========================================================================
# split / trim / merge
# ===========================================================================

def test_split_of_a_non_x_monotone_curve_raises(cubic):
    """split() needs an x-monotone curve; CUBIC_B has 3 pieces."""
    with pytest.raises(a2.NotXMonotoneError):
        cubic.split(cubic.evaluate(F(1, 10)))


def test_split_at_an_interior_point(cubic):
    """Splitting piece 1 at B(1/10) gives the parameter ranges [0, 1/10] and
    [1/10, (5-sqrt5)/10]."""
    piece = cubic.make_x_monotone()[0]
    c1, c2 = piece.split(cubic.evaluate(F(1, 10)))
    assert c1.parameter_range[0] == 0.0
    assert c1.parameter_range[1] == pytest.approx(0.1, abs=1e-12)
    assert c2.parameter_range[0] == pytest.approx(0.1, abs=1e-12)
    assert c2.parameter_range[1] == pytest.approx(T_TAN_LO, abs=1e-12)


def test_split_point_out_of_x_range_raises(cubic):
    """CGAL's Bezier Split_2 silently accepts a free rational point; the binding
    rejects it first (CGAL_TRAPS_CHECKLIST.md, "Bezier kind")."""
    piece = cubic.make_x_monotone()[0]
    with pytest.raises(ValueError, match="not in the x-range"):
        piece.split(a2.Point(1000, 1000, kind="bezier"))


def test_split_point_in_x_range_but_off_curve_raises():
    """A point with an in-range x but the wrong y is refused as well."""
    s = a2.BezierCurve(S_CUBIC)
    piece = s.make_x_monotone()[0]
    # x = 3/2 is in [0, 3]; the curve has y(1/2) = 0 there, so (3/2, 5) is off it.
    with pytest.raises(ValueError, match="does not lie on the curve"):
        piece.split(a2.Point(F(3, 2), 5, kind="bezier"))


def test_split_at_an_endpoint_raises(cubic):
    """An endpoint is not an interior split point."""
    piece = cubic.make_x_monotone()[0]
    with pytest.raises(ValueError, match="endpoint"):
        piece.split(piece.source)
    with pytest.raises(ValueError, match="endpoint"):
        piece.split(piece.target)


def test_split_then_merge_round_trip(cubic):
    """Merging the two halves restores the original parameter range."""
    piece = cubic.make_x_monotone()[0]
    c1, c2 = piece.split(cubic.evaluate(F(1, 10)))
    assert c1.can_merge(c2)
    merged = c1.merge(c2)
    assert merged.parameter_range == pytest.approx(piece.parameter_range, abs=1e-15)


def test_trim_between_two_parameters(parabola):
    """trim(B(1/4), B(3/4)) keeps t in [1/4, 3/4]; y(1/4) = y(3/4) = 9/16."""
    piece = parabola.make_x_monotone()[0]
    t = piece.trim(parabola.evaluate(F(1, 4)), parabola.evaluate(F(3, 4)))
    assert t.parameter_range == pytest.approx((0.25, 0.75), abs=1e-12)
    assert tuple(t.source.exact()) == (F(1, 4), F(9, 16))
    assert tuple(t.target.exact()) == (F(3, 4), F(9, 16))


def test_non_adjacent_pieces_cannot_merge(cubic):
    """Pieces 1 and 3 do not share an endpoint."""
    p1, _p2, p3 = cubic.make_x_monotone()
    assert p1.can_merge(p3) is False


def test_compare_y_at_x(parabola):
    """y = 3x(1-x): at x = 1/2 the curve is at y = 3/4."""
    piece = parabola.make_x_monotone()[0]
    assert piece.compare_y_at_x((F(1, 2), F(1, 2))) == -1
    assert piece.compare_y_at_x((F(1, 2), F(3, 4))) == 0
    assert piece.compare_y_at_x((F(1, 2), 1)) == 1


def test_is_in_x_range(parabola):
    """The parabola piece spans x in [0, 1]."""
    piece = parabola.make_x_monotone()[0]
    assert piece.is_in_x_range((F(1, 2), 100)) is True
    assert piece.is_in_x_range((5, 0)) is False


# ===========================================================================
# approximate
# ===========================================================================

def test_approximate_endpoints_are_the_exact_endpoints(cubic):
    """The first/last polyline points are the endpoints' own approximations."""
    piece = cubic.make_x_monotone()[0]
    pts = piece.approximate(1e-3)
    assert pts[0] == pytest.approx(piece.source.xy, abs=0.0)
    assert pts[-1] == pytest.approx(piece.target.xy, abs=0.0)


def test_approximate_is_within_tolerance_of_evaluate():
    """Every exact curve point B(k/100) is closer than the tolerance to the polyline.

    S_CUBIC is a single x-monotone piece over t in [0, 1], so the exact points are
    simply evaluate(Fraction(k, 100)).
    """
    s = a2.BezierCurve(S_CUBIC)
    piece = s.make_x_monotone()[0]
    tol = 1e-3
    pts = piece.approximate(tol)
    worst = 0.0
    for k in range(101):
        p = s.evaluate(F(k, 100))
        worst = max(worst, _dist_to_polyline(p.xy, pts))
    assert worst < tol


def test_approximate_of_the_parabola_lies_on_the_curve(parabola):
    """Every returned point satisfies y = 3x(1-x) to double precision."""
    pts = parabola.make_x_monotone()[0].approximate(1e-4)
    assert len(pts) >= 2
    for x, y in pts:
        assert y == pytest.approx(3 * x * (1 - x), abs=1e-12)


def test_approximate_tighter_tolerance_gives_more_points(cubic):
    """A 100x tighter tolerance must not produce fewer points."""
    piece = cubic.make_x_monotone()[0]
    assert len(piece.approximate(1e-5)) > len(piece.approximate(1e-2))


def test_approximate_invalid_tolerance_raises(cubic):
    """tolerance <= 0 segfaults/hangs CGAL's Approximate_2; it is validated first."""
    piece = cubic.make_x_monotone()[0]
    with pytest.raises(ValueError, match="tolerance"):
        piece.approximate(0.0)
    with pytest.raises(ValueError, match="tolerance"):
        piece.approximate(-1.0)


def test_approximate_length_of_the_parabola(parabola):
    """Exact arc length of y = 3x(1-x) over [0,1] is (3*sqrt(10) + asinh(3))/6."""
    exact = (3 * math.sqrt(10) + math.asinh(3)) / 6
    got = parabola.make_x_monotone()[0].approximate_length(1e-6)
    assert got == pytest.approx(exact, abs=1e-5)
    assert got <= exact  # a chord polyline can only under-estimate the arc


# ===========================================================================
# intersect
# ===========================================================================

def test_intersect_two_cubics_gives_two_points(crossing_pair):
    """y_S - y_T = (3t-1)(3t-2) -> crossings at (1, 2/3) and (2, -2/3)."""
    c1, c2 = crossing_pair
    res = c1.make_x_monotone()[0].intersect(c2.make_x_monotone()[0])
    assert len(res) == 2
    pts = [r[0] for r in res]
    assert all(isinstance(p, a2.Point) for p in pts)
    assert pts[0].xy == pytest.approx((1.0, 2.0 / 3.0), abs=1e-12)
    assert pts[1].xy == pytest.approx((2.0, -2.0 / 3.0), abs=1e-12)
    for _p, mult in res:
        assert mult in (0, 1)  # the Bezier traits reports only 0 (unknown) or 1


def test_intersect_reports_no_overlap_for_the_same_xid(cubic):
    """Documented CGAL limitation (STAGE2_NOTES / kind_bezier): Intersect_2 returns
    `false` for two subcurves of the SAME Curve_2 with equal xid, so intersecting a
    piece with a sub-piece of itself yields only the shared endpoint, never an overlap.
    """
    piece = cubic.make_x_monotone()[0]
    left, _right = piece.split(cubic.evaluate(F(1, 10)))
    res = piece.intersect(left)
    assert all(isinstance(r, tuple) for r in res)      # no overlap curve
    assert [r[0] for r in res] == [piece.source]       # only the shared endpoint


def test_intersect_identical_but_distinct_curves_gives_an_overlap(cubic):
    """A geometrically identical, separately built curve has a different id and DOES
    produce an overlap curve."""
    twin = a2.BezierCurve(CUBIC_B)
    res = cubic.make_x_monotone()[0].intersect(twin.make_x_monotone()[0])
    assert len(res) == 1
    overlap = res[0]
    assert isinstance(overlap, a2.BezierCurve)
    assert overlap.parameter_range[0] == 0.0
    assert overlap.parameter_range[1] == pytest.approx(T_TAN_LO, abs=1e-12)


def test_intersect_loop_branches_at_the_self_intersection(loop):
    """Pieces 1 and 3 of LOOP both pass through the self-intersection (0, 3)
    (at t = (5-sqrt15)/10 and t = (5+sqrt15)/10) and meet only there."""
    p1, _p2, p3 = loop.make_x_monotone()
    res = p1.intersect(p3)
    assert len(res) == 1
    pt, _mult = res[0]
    assert pt.xy == pytest.approx((0.0, 3.0), abs=1e-12)


def test_intersect_adjacent_loop_pieces_share_the_tangency(loop):
    """Pieces 1 and 2 share the vertical tangency (sqrt5/5, 6)."""
    p1, p2, _p3 = loop.make_x_monotone()
    res = p1.intersect(p2)
    assert len(res) == 1
    pt, _mult = res[0]
    assert pt.xy == pytest.approx((SQRT5_OVER_5, 6.0), abs=1e-12)


# ===========================================================================
# from_rational  (rational Bezier -> exact conic)
# ===========================================================================

def test_from_rational_degree1_is_a_conic_segment():
    """A rational linear Bezier is the segment p0..p1 (weights only reparametrise)."""
    c = a2.BezierCurve.from_rational([(0, 0), (3, 4)])
    assert isinstance(c, a2.ConicArc)
    assert c.kind == a2.Kind.CONIC


def test_from_rational_degree2_gives_a_conic():
    """A rational quadratic Bezier is exactly a conic arc (DESIGN.md section 3).

    With unit weights, (0,0) (1,2) (2,0) is the polynomial quadratic x(t) = 2t,
    y(t) = 4t(1-t), i.e. y = 2x - x^2 -> the conic 1*x^2 + 0*y^2 + 0*xy - 2x + 1*y + 0 = 0.
    """
    c = a2.BezierCurve.from_rational([(0, 0), (1, 2), (2, 0)])
    assert isinstance(c, a2.ConicArc)
    assert c.conic_type == "parabola"
    assert c.coefficients == (F(1), F(0), F(0), F(-2), F(1), F(0))


def test_conicarc_from_rational_bezier_is_the_same_parabola():
    """The equivalent ConicArc factory (which takes the same rational control points)
    produces exactly that conic -- this is the reference the BezierCurve.from_rational
    test above is compared against."""
    b = a2.ConicArc.from_rational_bezier((0, 0), (1, 2), (2, 0))
    assert b.conic_type == "parabola"
    assert b.coefficients == (F(1), F(0), F(0), F(-2), F(1), F(0))


def test_from_rational_degree2_matches_conicarc_from_rational_bezier():
    """BezierCurve.from_rational(3 points) must agree with ConicArc.from_rational_bezier."""
    a = a2.BezierCurve.from_rational([(0, 0), (1, 2), (2, 0)])
    b = a2.ConicArc.from_rational_bezier((0, 0), (1, 2), (2, 0))
    assert a.coefficients == b.coefficients


def test_from_rational_with_weights_is_a_circular_arc():
    """The standard rational quadratic quarter circle: P0=(1,0), P1=(1,1), P2=(0,1)
    with weights 1, 1, 2 traces x^2 + y^2 = 1."""
    c = a2.BezierCurve.from_rational([(1, 0), (1, 1), (0, 1)], [1, 1, 2])
    assert isinstance(c, a2.ConicArc)
    r, s, t, u, v, w = c.coefficients
    assert r == s and t == 0 and u == 0 and v == 0
    assert w == -r  # r(x^2 + y^2) - r = 0


def test_from_rational_degree3_is_not_representable():
    """Only degrees 1 and 2 are exactly representable as conics."""
    with pytest.raises(a2.NotRepresentableError):
        a2.BezierCurve.from_rational([(0, 0), (1, 1), (2, 0), (3, 1)])


def test_from_rational_weight_count_must_match():
    with pytest.raises(ValueError):
        a2.BezierCurve.from_rational([(0, 0), (1, 1), (2, 0)], [1, 1])


# ===========================================================================
# arrangements
# ===========================================================================

def test_reference_pair_arrangement_counts(cubic):
    """traits_bezier.md section 11: {CUBIC_B, QUAD_B2} give V=9, E=10, F=3."""
    arr = a2.Arrangement("bezier")
    arr.insert([cubic, a2.BezierCurve(QUAD_B2)])
    assert (arr.number_of_vertices, arr.number_of_edges, arr.number_of_faces) == (9, 10, 3)
    assert arr.number_of_curves == 2
    assert arr.is_valid()


def test_reference_pair_history(cubic):
    """CUBIC_B splits into 6 edges and QUAD_B2 into 4 (10 edges in total)."""
    arr = a2.Arrangement("bezier")
    h1, h2 = arr.insert([cubic, a2.BezierCurve(QUAD_B2)])
    assert h1.number_of_induced_edges == 6
    assert h2.number_of_induced_edges == 4
    assert [tuple(p.exact()) for p in h1.curve.control_points] == [
        (F(x), F(y)) for x, y in CUBIC_B
    ]
    assert sum(h.number_of_induced_edges for h in arr.curves()) == arr.number_of_edges


def test_self_intersecting_curve_arrangement_counts(loop):
    """LOOP alone: 5 vertices, 5 edges, 2 faces.

    Split parameters, in order: 0 (endpoint), (5-sqrt15)/10 (self-intersection),
    (5-sqrt5)/10 (tangency), (5+sqrt5)/10 (tangency), (5+sqrt15)/10 (self-intersection
    again -> the SAME point (0,3)), 1 (endpoint).  That is 4 interior split parameters,
    hence 5 edges, and 5 distinct vertices: (-1,0), (0,3), (+/- sqrt5/5, 6), (1,0).
    Euler: V - E + F = 2 -> F = 2 (the loop's interior plus the unbounded face).
    """
    arr = a2.Arrangement("bezier")
    arr.insert(loop)
    assert (arr.number_of_vertices, arr.number_of_edges, arr.number_of_faces) == (5, 5, 2)
    assert arr.is_valid()
    assert len(arr.bounded_faces()) == 1


def test_self_intersection_vertex_has_degree_four(loop):
    """At (0, 3) four edge-ends meet: the two tails and the two loop ends."""
    arr = a2.Arrangement("bezier")
    arr.insert(loop)
    degrees = sorted(v.degree for v in arr.vertices())
    assert degrees == [1, 1, 2, 2, 4]
    quad = [v for v in arr.vertices() if v.degree == 4]
    assert len(quad) == 1
    assert quad[0].point.xy == pytest.approx((0.0, 3.0), abs=1e-12)


def test_self_intersecting_arrangement_vertex_positions(loop):
    """The 5 vertices are (-1,0), (1,0), (0,3) and (+/- sqrt5/5, 6)."""
    arr = a2.Arrangement("bezier")
    arr.insert(loop)
    got = sorted(tuple(v.point.xy) for v in arr.vertices())
    want = sorted([(-1.0, 0.0), (1.0, 0.0), (0.0, 3.0),
                   (-SQRT5_OVER_5, 6.0), (SQRT5_OVER_5, 6.0)])
    assert len(got) == len(want)
    for g, w in zip(got, want):
        assert g == pytest.approx(w, abs=1e-12)


def test_self_intersecting_arrangement_bounded_face_has_three_edges(loop):
    """The loop's interior is bounded by the three arcs between the two tangencies."""
    arr = a2.Arrangement("bezier")
    arr.insert(loop)
    face = arr.bounded_faces()[0]
    assert len(face.outer_ccb()) == 3
    assert face.number_of_inner_ccbs == 0


def test_self_intersecting_arrangement_bbox(loop):
    """Vertex approximations span x in [-1, 1] and y in [0, 6]."""
    arr = a2.Arrangement("bezier")
    arr.insert(loop)
    assert arr.bbox() == pytest.approx((-1.0, 0.0, 1.0, 6.0), abs=1e-12)


def test_two_crossing_cubics_counts(crossing_pair):
    """S_CUBIC and T_CUBIC cross at (1, 2/3) and (2, -2/3).

    Vertices: the 4 distinct endpoints (0,0), (3,0), (0,-2), (3,-2) plus the 2 crossings
    = 6.  Each curve is x-monotone and is cut twice -> 3 edges each = 6 edges.  The two
    curves cross, so the arrangement is connected: V - E + F = 2 -> F = 2, i.e. one
    bounded face (between the curves for x in [1, 2]) plus the unbounded face.
    """
    c1, c2 = crossing_pair
    arr = a2.Arrangement("bezier")
    arr.insert([c1, c2])
    assert (arr.number_of_vertices, arr.number_of_edges, arr.number_of_faces) == (6, 6, 2)
    assert arr.is_valid()


def test_two_crossing_cubics_history(crossing_pair):
    """Each curve induces exactly 3 edges."""
    c1, c2 = crossing_pair
    arr = a2.Arrangement("bezier")
    h1, h2 = arr.insert([c1, c2])
    assert h1.number_of_induced_edges == 3
    assert h2.number_of_induced_edges == 3


def test_two_crossing_cubics_vertices(crossing_pair):
    """Four rational endpoint vertices, two algebraic-looking crossing vertices."""
    c1, c2 = crossing_pair
    arr = a2.Arrangement("bezier")
    arr.insert([c1, c2])
    got = sorted(tuple(v.point.xy) for v in arr.vertices())
    want = sorted([(0.0, 0.0), (3.0, 0.0), (0.0, -2.0), (3.0, -2.0),
                   (1.0, 2.0 / 3.0), (2.0, -2.0 / 3.0)])
    assert len(got) == len(want)
    for g, w in zip(got, want):
        assert g == pytest.approx(w, abs=1e-12)
    # Only the four curve endpoints are *provably* rational: an intersection point is a
    # CORE::Expr and CORE has no sound rationality test (STAGE2_NOTES, kind_bezier).
    assert sum(1 for v in arr.vertices() if v.point.is_rational) == 4


def test_two_crossing_cubics_bounded_face(crossing_pair):
    """The bounded face is bounded by one arc of each curve."""
    c1, c2 = crossing_pair
    arr = a2.Arrangement("bezier")
    h1, h2 = arr.insert([c1, c2])
    face = arr.bounded_faces()[0]
    ccb = face.outer_ccb()
    assert len(ccb) == 2
    assert {h.originating_curves()[0].id for h in ccb} == {h1.id, h2.id}


def test_degree5_arrangement_is_a_single_edge():
    """DEG5 is x-monotone and has no self-intersections: V=2, E=1, F=1."""
    arr = a2.Arrangement("bezier")
    arr.insert(a2.BezierCurve(DEG5))
    assert (arr.number_of_vertices, arr.number_of_edges, arr.number_of_faces) == (2, 1, 1)
    assert arr.is_valid()


def test_locate_on_a_bezier_arrangement(loop):
    """(0, 5) is inside the loop, (100, 100) outside, (0, 3) is the crossing vertex."""
    arr = a2.Arrangement("bezier")
    arr.insert(loop)
    inside = arr.locate((0, 5))
    assert isinstance(inside, a2.Face) and not inside.is_unbounded
    assert arr.locate((100, 100)).is_unbounded
    assert isinstance(arr.locate((0, 3)), a2.Vertex)


def test_locate_agrees_across_supported_strategies(loop):
    """naive/simple/walk/trapezoid all find the same bounded face."""
    arr = a2.Arrangement("bezier")
    arr.insert(loop)
    faces = [arr.locate((0, 5), strategy=s) for s in ("naive", "simple", "walk", "trapezoid")]
    assert all(isinstance(f, a2.Face) and not f.is_unbounded for f in faces)
    assert len({f.id for f in faces}) == 1


@pytest.mark.parametrize("strategy", ["landmarks", "triangulation"])
def test_unsupported_point_location_strategies(loop, strategy):
    """Landmarks needs Construct_x_monotone_curve_2(p,q) and triangulation a Kernel;
    the Bezier traits has neither (CGAL_TRAPS_CHECKLIST.md, point location)."""
    arr = a2.Arrangement("bezier")
    arr.insert(loop)
    with pytest.raises(a2.UnsupportedError):
        arr.locate((0, 5), strategy=strategy)


def test_locate_a_point_on_an_edge():
    """S_CUBIC passes through (3/2, 0) (t = 1/2, y = 9*(1/2)*(1/2)*0 = 0)."""
    s = a2.BezierCurve(S_CUBIC)
    arr = a2.Arrangement("bezier")
    arr.insert(s)
    assert _y_s_cubic(F(1, 2)) == 0
    assert isinstance(arr.locate((F(3, 2), 0)), a2.Halfedge)


def test_zone_and_do_intersect(loop):
    """The horizontal Bezier segment y = 5/2 crosses the loop; y = -3 misses it."""
    arr = a2.Arrangement("bezier")
    arr.insert(loop)
    hit = a2.BezierCurve([(-2, F(5, 2)), (2, F(5, 2))])
    miss = a2.BezierCurve([(-2, -3), (2, -3)])
    assert arr.do_intersect(hit) is True
    assert arr.do_intersect(miss) is False
    assert len(arr.zone(hit)) > 1
    assert len(arr.zone(miss)) == 1          # entirely inside the unbounded face
    assert arr.number_of_edges == 5          # zone() does not modify the arrangement


def test_remove_curve(crossing_pair):
    """Removing T_CUBIC drops its 3 edges; CGAL's _remove_curve leaves the crossing
    vertices behind (they stay at degree 2), so V goes 6 -> 4 and E 6 -> 3."""
    c1, c2 = crossing_pair
    arr = a2.Arrangement("bezier")
    _h1, h2 = arr.insert([c1, c2])
    assert arr.remove_curve(h2) == 3
    assert arr.number_of_edges == 3
    assert arr.number_of_curves == 1
    assert arr.is_valid()


def test_insert_point_creates_an_isolated_vertex(loop):
    arr = a2.Arrangement("bezier")
    arr.insert(loop)
    v = arr.insert_point(a2.Point(10, 10, kind="bezier"))
    assert v.is_isolated
    assert arr.number_of_vertices == 6
    assert arr.is_valid()


def test_decompose_returns_one_entry_per_vertex(loop):
    arr = a2.Arrangement("bezier")
    arr.insert(loop)
    dec = arr.decompose()
    assert len(dec) == arr.number_of_vertices == 5
    assert all(v.is_valid for v, _below, _above in dec)


def test_arrangement_copy_is_independent(crossing_pair):
    c1, c2 = crossing_pair
    arr = a2.Arrangement("bezier")
    arr.insert([c1, c2])
    clone = arr.copy()
    assert (clone.number_of_vertices, clone.number_of_edges, clone.number_of_faces) == (6, 6, 2)
    clone.clear()
    assert clone.is_empty
    assert arr.number_of_edges == 6


def test_bezier_arrangement_has_no_fictitious_face(loop):
    """The Bezier kind uses the bounded planar topology."""
    arr = a2.Arrangement("bezier")
    arr.insert(loop)
    assert arr.is_unbounded_kind is False
    assert arr.number_of_unbounded_faces == 1
    with pytest.raises(a2.UnsupportedError):
        arr.fictitious_face


def test_curve_through_a_vertical_tangency_vertex_raises_instead_of_crashing(loop):
    """DEGENERATE CASE, undocumented in CGAL_TRAPS_CHECKLIST.md.

    LOOP's two vertical tangency vertices are (+/- sqrt5/5, 6), so the straight Bezier
    curve y = 6 passes exactly through both of them.  CGAL 6.1 cannot handle that:
    `_Bezier_x_monotone_2::_compare_slopes` hits `CGAL_assertion(in_range1)`
    (Bezier_x_monotone_2.h:1679).  What this test pins down is the *contract*: the
    assertion is translated into a Python `CGALAssertionError` and the process survives.
    """
    arr = a2.Arrangement("bezier")
    arr.insert(loop)
    with pytest.raises(a2.CGALError):
        arr.insert(a2.BezierCurve([(-2, 6), (2, 6)]))


def test_intersect_through_a_self_intersection_point_does_not_crash():
    """A curve through LOOP's self-intersection point (0, 3) must not kill the process.

    y = 3 meets LOOP at t = (5 -/+ sqrt15)/10, i.e. at TWO parameters that map to the
    SAME point (0, 3), so CGAL's resultant produces 2 candidate points for the Bezier
    and 1 for the line.  `_Bezier_cache::get_intersections` then runs its pairing loop a
    second time with an empty `pts2`: `for (k = n_pts2 - 1; !found && k > 0; k--)` with
    `unsigned int k` and `n_pts2 == 0` wraps around to 4294967295 and indexes
    `dist_vec` far out of bounds (Bezier_cache.h:458 declares `k`, the loop is at line 488).

    Run in a child process because the failure mode is SIGSEGV, and run it several times
    because reading past the end of `dist_vec` is undefined behaviour: measured, roughly
    5 runs in 6 die with SIGSEGV and the rest happen to survive and return the correct
    single intersection point (0, 3).

    The configuration cannot be computed safely with CGAL 6.1, so `BezierOps::intersect`
    detects it exactly (the self-intersection points of a supporting curve come from the
    SOUND `id1 == id2` branch of the same cache) and refuses with `UnsupportedError`.
    The child therefore has to survive every run and report the same outcome each time.
    """
    code = (
        "import arrangement_2d as a2\n"
        "loop = a2.BezierCurve([(-1,0),(3,10),(-3,10),(1,0)])\n"
        "line = a2.BezierCurve([(-2,3),(2,3)])\n"
        "piece = loop.make_x_monotone()[0]\n"
        "try:\n"
        "    res = piece.intersect(line.make_x_monotone()[0])\n"
        "except a2.UnsupportedError as exc:\n"
        "    print('REFUSED', 'ITSELF' in str(exc))\n"
        "else:\n"
        "    print('n=%d' % len(res))\n"
    )
    runs = [_run_child(code) for _ in range(8)]
    codes = [r.returncode for r in runs]
    assert codes == [0] * 8, (
        "child interpreters died with return codes %r (SIGSEGV is -11)" % (codes,)
    )
    outs = {r.stdout.strip() for r in runs}
    assert outs == {"REFUSED True"}, outs


@pytest.mark.parametrize("op", ["insert", "insert_reversed", "insert_curves",
                                "zone", "do_intersect", "overlay"])
def test_sweep_through_a_self_intersection_point_does_not_crash(op):
    """The same CGAL 6.1 memory corruption, reached through the SWEEP rather than through
    ``Curve.intersect``.

    ``Arrangement.insert`` / ``insert_curves`` / ``zone`` / ``do_intersect`` / ``overlay``
    call CGAL's ``Intersect_2`` from inside the sweep-line and zone algorithms, which never
    go through ``KindOps::intersect``, so the guard on that method did not protect them:
    measured before the fix, all of them SIGSEGV'd in roughly 5 runs out of 6, in either
    insertion order.  ``KindOps::check_sweepable`` is the pre-flight version of the same
    exact test -- it is handed the whole set of curves CGAL is about to sweep (the
    arrangement's edges plus the new curves) and refuses the pair -- so every one of these
    entry points must now raise ``UnsupportedError`` in EVERY run and leave the process
    alive.
    """
    setup = (
        "import arrangement_2d as a2\n"
        "loop = a2.BezierCurve([(-1,0),(3,10),(-3,10),(1,0)])\n"
        "line = a2.BezierCurve([(-2,3),(2,3)])\n"
        "arr = a2.Arrangement('bezier')\n"
    )
    body = {
        "insert": "arr.insert(loop)\narr.insert(line)\n",
        "insert_reversed": "arr.insert(line)\narr.insert(loop)\n",
        "insert_curves": "arr.insert_curves([loop, line])\n",
        "zone": "arr.insert(loop)\narr.zone(line)\n",
        "do_intersect": "arr.insert(loop)\narr.do_intersect(line)\n",
        "overlay": ("arr.insert(loop)\n"
                    "other = a2.Arrangement('bezier')\n"
                    "other.insert(line)\n"
                    "arr.overlay(other)\n"),
    }[op]
    code = (
        setup
        + "try:\n"
        + "".join("    %s\n" % line for line in body.rstrip("\n").split("\n"))
        + "except a2.UnsupportedError as exc:\n"
        "    print('REFUSED', 'ITSELF' in str(exc))\n"
        "else:\n"
        "    print('COMPLETED')\n"
    )
    runs = [_run_child(code) for _ in range(6)]
    codes = [r.returncode for r in runs]
    assert codes == [0] * 6, (
        "child interpreters died with return codes %r (SIGSEGV is -11); stderr=%r"
        % (codes, [r.stderr[-400:] for r in runs])
    )
    outs = {r.stdout.strip() for r in runs}
    assert outs == {"REFUSED True"}, outs


def test_the_sweep_pre_check_leaves_ordinary_bezier_arrangements_alone(cubic, parabola):
    """The pre-flight guard must only fire on the corrupting configuration.

    CUBIC_B and PARABOLA do not cross themselves (``has_no_self_intersections``), so the
    check costs one memoised control-polygon test per supporting curve and inserts as
    before; and LOOP, which does cross itself, is still perfectly insertable on its own and
    together with a curve that misses (0, 3).
    """
    arr = a2.Arrangement("bezier")
    arr.insert([cubic, parabola])
    assert arr.number_of_curves == 2 and arr.is_valid()

    loop = a2.BezierCurve(LOOP)
    solo = a2.Arrangement("bezier")
    solo.insert(loop)                                   # a curve against ITSELF is fine
    assert solo.number_of_curves == 1 and solo.is_valid()
    # y = 5 misses the self-intersection point (0, 3) -> ordinary insertion
    solo.insert(a2.BezierCurve([(-2, 5), (2, 5)]))
    assert solo.number_of_curves == 2 and solo.is_valid()


def test_boolean_ops_through_a_self_intersection_point_do_not_crash():
    """The third door to the same CGAL 6.1 corruption: the Boolean-set sweep.

    Every ``PolygonSet`` operation runs CGAL's surface sweep over the boundary curves of both
    operands, so it calls ``Intersect_2`` on pairs of SUPPORTING curves whose x-monotone
    pieces need not touch at all.  Reproduction: P1 is bounded by the sub-arc of LOOP between
    t = 3/10 and t = 7/10 -- rational endpoints (+/-11/25, 63/10), simple, and nowhere near
    LOOP's crossing at (0, 3) -- closed by the straight curve between them; P2 is a lens whose
    top edge is the straight Bezier from (-2, 3) to (2, 3), i.e. through (0, 3).  The two
    polygons are disjoint, yet before the fix ``join`` and ``intersection`` SIGSEGV'd in every
    run (``insert`` does not: it uses non-intersecting insertion).  ``PolygonSetImpl`` now runs
    the same ``KindOps::check_sweepable`` pre-flight as ``ArrImpl``.
    """
    code = (
        "from fractions import Fraction as F\n"
        "import arrangement_2d as a2\n"
        "loop = a2.BezierCurve([(-1,0),(3,10),(-3,10),(1,0)])\n"
        "arc = loop.make_x_monotone()[1].trim(loop.evaluate(F(3,10)), loop.evaluate(F(7,10)))\n"
        "P1 = a2.Polygon([arc, a2.BezierCurve([(F(-11,25),F(63,10)),(F(11,25),F(63,10))])])\n"
        "P2 = a2.Polygon([a2.BezierCurve([(-2,3),(2,3)]),\n"
        "                 a2.BezierCurve([(2,3),(0,1),(-2,3)])])\n"
        "ps = a2.PolygonSet('bezier'); ps.insert(P1)\n"
        "qs = a2.PolygonSet('bezier'); qs.insert(P2)\n"
        "try:\n"
        "    ps.join(qs)\n"
        "except a2.UnsupportedError as exc:\n"
        "    print('REFUSED', 'ITSELF' in str(exc))\n"
        "else:\n"
        "    print('COMPLETED')\n"
    )
    runs = [_run_child(code) for _ in range(4)]
    assert [r.returncode for r in runs] == [0] * 4, (
        "child interpreters died with return codes %r (SIGSEGV is -11); stderr=%r"
        % ([r.returncode for r in runs], [r.stderr[-400:] for r in runs])
    )
    assert {r.stdout.strip() for r in runs} == {"REFUSED True"}


# ===========================================================================
# Boolean set operations on Bezier polygons
# ===========================================================================

def test_bezier_polygon_from_two_arcs(lens_polygon):
    """Two cubic arcs chain into a closed 2-curve boundary of kind 'bezier'."""
    assert lens_polygon.kind == a2.Kind.BEZIER
    assert len(lens_polygon) == 2
    assert lens_polygon.is_closed


def test_bezier_polygon_orientation_is_clockwise(lens_polygon):
    """Right along the top, left along the bottom = clockwise."""
    assert lens_polygon.orientation() == -1
    assert lens_polygon.reverse().orientation() == 1


def test_bezier_polygon_from_points_is_unsupported():
    """The Bezier traits has no straight x-monotone curve constructor, so a polygon
    cannot be built from bare points (CGAL_TRAPS_CHECKLIST.md, Bezier kind)."""
    with pytest.raises(a2.UnsupportedError):
        a2.Polygon([(0, 0), (1, 0), (0, 1)], kind="bezier")


def test_polygon_set_rejects_a_clockwise_polygon(lens_polygon):
    """CGAL requires CCW outer boundaries and never fixes them itself."""
    ps = a2.PolygonSet("bezier")
    assert a2.is_valid_polygon(lens_polygon) is False
    with pytest.raises(ValueError, match="clockwise"):
        ps.insert(lens_polygon, fix_orientation=False)


def test_polygon_set_insert_fixes_the_orientation(lens_polygon):
    """insert() reverses a clockwise outer boundary by default."""
    ps = a2.PolygonSet("bezier")
    ps.insert(lens_polygon)
    assert ps.number_of_polygons_with_holes == 1
    assert ps.is_valid()
    pwh = ps.polygons_with_holes()[0]
    assert len(pwh.outer) == 2
    assert pwh.outer.orientation() == 1
    assert pwh.holes == ()


def test_polygon_set_oriented_side(lens_polygon):
    """(2, 0) is inside the lens (it spans x in [0,4] around y = 0); (10, 10) is outside;
    (0, 0) is the lens's left tip, i.e. on the boundary."""
    ps = a2.PolygonSet("bezier")
    ps.insert(lens_polygon)
    assert ps.oriented_side((2, 0)) == 1
    assert ps.oriented_side((10, 10)) == -1
    assert ps.oriented_side((0, 0)) == 0


def test_polygon_set_union_of_two_overlapping_lenses(lens_polygon):
    """A second lens shifted by (+3, 0) overlaps the first for x in [3, 4].

    The union is one simply connected region whose boundary is four arcs: the top of
    lens 1 up to the upper crossing, the top of lens 2, the bottom of lens 2 down to the
    lower crossing and the bottom of lens 1.  Both lens tips (4,0) and (3,0) end up
    strictly inside the other lens, so they are no longer boundary vertices.
    """
    shifted = a2.Polygon([
        a2.BezierCurve([(x + 3, y) for x, y in LENS_TOP]),
        a2.BezierCurve([(x + 3, y) for x, y in LENS_BOT]),
    ])
    ps = a2.PolygonSet("bezier")
    ps.insert(lens_polygon)
    other = a2.PolygonSet("bezier")
    other.insert(shifted)
    assert ps.do_intersect(other)
    ps.join(other)
    assert ps.number_of_polygons_with_holes == 1
    pwh = ps.polygons_with_holes()[0]
    assert len(pwh.outer) == 4
    assert pwh.holes == ()
    assert ps.oriented_side((0, 0)) == 0     # still the left tip of the union
    assert ps.oriented_side((7, 0)) == 0     # the right tip of the shifted lens
    assert ps.oriented_side((4, 0)) == 1     # lens 1's tip is now interior


def test_polygon_set_intersection_of_two_lenses(lens_polygon):
    """The intersection of the two overlapping lenses is a single region."""
    shifted = a2.Polygon([
        a2.BezierCurve([(x + 3, y) for x, y in LENS_TOP]),
        a2.BezierCurve([(x + 3, y) for x, y in LENS_BOT]),
    ])
    ps = a2.PolygonSet("bezier")
    ps.insert(lens_polygon)
    other = a2.PolygonSet("bezier")
    other.insert(shifted)
    ps.intersection(other)
    assert ps.number_of_polygons_with_holes == 1
    assert ps.oriented_side((F(7, 2), 0)) == 1   # x = 3.5 is in both lenses
    assert ps.oriented_side((1, 0)) == -1        # only in lens 1
    assert ps.oriented_side((6, 0)) == -1        # only in lens 2


def test_polygon_set_difference_of_two_lenses(lens_polygon):
    """lens1 - lens2 keeps the part of lens 1 left of the overlap."""
    shifted = a2.Polygon([
        a2.BezierCurve([(x + 3, y) for x, y in LENS_TOP]),
        a2.BezierCurve([(x + 3, y) for x, y in LENS_BOT]),
    ])
    ps = a2.PolygonSet("bezier")
    ps.insert(lens_polygon)
    other = a2.PolygonSet("bezier")
    other.insert(shifted)
    ps.difference(other)
    assert not ps.is_empty
    assert ps.oriented_side((1, 0)) == 1
    assert ps.oriented_side((F(7, 2), 0)) == -1


def test_polygon_set_to_arrangement(lens_polygon):
    """The lens as an arrangement: 2 vertices (the tips), 2 edges, 2 faces."""
    ps = a2.PolygonSet("bezier")
    ps.insert(lens_polygon)
    arr, contained = ps.to_arrangement()
    assert arr.kind == a2.Kind.BEZIER
    assert (arr.number_of_vertices, arr.number_of_edges, arr.number_of_faces) == (2, 2, 2)
    assert len(contained) == 1
    assert not contained[0].is_unbounded


# ===========================================================================
# traits functors and bulk export
# ===========================================================================

def test_traits_object_for_the_bezier_kind(cubic):
    """The kind's traits object exposes the same functors as the curves do."""
    tr = a2.traits("bezier")
    assert tr.kind == a2.Kind.BEZIER
    assert tr.dimension == 2
    assert len(tr.make_x_monotone(cubic)) == 3
    assert tr.compare_xy(cubic.evaluate(F(1, 3)), cubic.evaluate(F(2, 3))) == 1


def test_traits_construct_x_monotone_curve_is_unsupported():
    """The Bezier traits has no Construct_x_monotone_curve_2(p, q) -- this is also why
    the landmarks point-location strategy and point-based polygons are unavailable."""
    tr = a2.traits("bezier")
    with pytest.raises(a2.UnsupportedError):
        tr.construct_x_monotone_curve(a2.Point(0, 0, kind="bezier"),
                                      a2.Point(1, 1, kind="bezier"))


def test_vertex_coordinates_and_edge_vertex_indices(loop):
    """Bulk export of the 5-vertex / 5-edge loop arrangement."""
    arr = a2.Arrangement("bezier")
    arr.insert(loop)
    coords = [tuple(row) for row in arr.vertex_coordinates()]
    idx = [tuple(row) for row in arr.edge_vertex_indices()]
    assert len(coords) == 5
    assert len(idx) == 5
    for i, j in idx:
        assert 0 <= i < 5 and 0 <= j < 5 and i != j
    # every vertex is used by at least one edge
    assert {i for pair in idx for i in pair} == set(range(5))


def test_approximate_edges(loop):
    """One polyline per edge, each with at least its two endpoints."""
    arr = a2.Arrangement("bezier")
    arr.insert(loop)
    polys = arr.approximate_edges(1e-2)
    assert len(polys) == 5
    assert all(len(p) >= 2 for p in polys)


def test_batched_locate(loop):
    """Batched point location agrees with the single-point results."""
    arr = a2.Arrangement("bezier")
    arr.insert(loop)
    res = arr.batched_locate([(0, 5), (100, 100), (0, 3)])
    assert isinstance(res[0], a2.Face) and not res[0].is_unbounded
    assert isinstance(res[1], a2.Face) and res[1].is_unbounded
    assert isinstance(res[2], a2.Vertex)
    assert res[2].point.xy == pytest.approx((0.0, 3.0), abs=1e-12)


def test_curve_handle_induced_edges(loop):
    """The single curve induces all 5 edges, each a valid halfedge with a distinct id."""
    arr = a2.Arrangement("bezier")
    handle = arr.insert(loop)
    edges = handle.induced_edges()
    assert len(edges) == 5 == handle.number_of_induced_edges
    assert all(isinstance(h, a2.Halfedge) and h.is_valid for h in edges)
    assert len({h.edge_id for h in edges}) == 5


def test_face_polygon_of_the_loop_interior(loop):
    """The bounded face's exact boundary is the 3 Bezier arcs between the tangencies."""
    arr = a2.Arrangement("bezier")
    arr.insert(loop)
    pwh = arr.bounded_faces()[0].polygon()
    assert isinstance(pwh, a2.PolygonWithHoles)
    assert pwh.kind == a2.Kind.BEZIER
    assert len(pwh.outer) == 3
    assert pwh.holes == ()
    assert pwh.outer.is_closed


# ===========================================================================
# process teardown
# ===========================================================================

def test_process_exits_cleanly_after_using_bezier_objects():
    """CORE::Expr values in static storage abort at exit (`! blocks.empty()` in CORE's
    MemoryPool).  A session that builds Bezier curves, an arrangement with algebraic
    vertices and a Bezier polygon set must still exit with status 0.
    """
    code = (
        "from fractions import Fraction as F\n"
        "import arrangement_2d as a2\n"
        "b = a2.BezierCurve([(0,0),(4,1),(-2,2),(2,3)])\n"
        "b2 = a2.BezierCurve([(0,3),(2,-1),(3,4)])\n"
        "arr = a2.Arrangement('bezier')\n"
        "arr.insert([b, b2])\n"
        "vals = [v.point.exact() for v in arr.vertices()]\n"
        "pieces = b.make_x_monotone()\n"
        "_ = [p.approximate(1e-3) for p in pieces]\n"
        "_ = b.parameter_at(pieces[0].target)\n"
        "ps = a2.PolygonSet('bezier')\n"
        "ps.insert(a2.Polygon([a2.BezierCurve([(0,0),(1,3),(3,3),(4,0)]),\n"
        "                      a2.BezierCurve([(4,0),(3,-3),(1,-3),(0,0)])]))\n"
        "print('OK', arr.number_of_vertices, ps.number_of_polygons_with_holes)\n"
    )
    proc = _run_child(code)
    assert proc.returncode == 0, "stderr=%r" % (proc.stderr[-2000:],)
    assert proc.stdout.strip().endswith("OK 9 1")


def test_process_exits_cleanly_after_only_importing_and_building_a_curve():
    """The minimal Bezier session must not abort in CORE's MemoryPool either."""
    code = (
        "import arrangement_2d as a2\n"
        "c = a2.BezierCurve([(0,0),(1,1)])\n"
        "print('deg', c.degree)\n"
    )
    proc = _run_child(code)
    assert proc.returncode == 0, "stderr=%r" % (proc.stderr[-2000:],)
    assert "deg 1" in proc.stdout
