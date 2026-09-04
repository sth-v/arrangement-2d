"""Tests for the ``sphere`` kind: geodesic arcs on the unit sphere.

Traits: ``CGAL::Arr_geodesic_arc_on_sphere_traits_2<Epeck>`` with
``Arr_spherical_topology_traits_2``.  Two facts drive almost every expectation here:

* a **point** is an *unnormalised direction* ``(x, y, z)``; two directions are equal iff
  they are **positive** multiples of each other, so ``(1,0,0) == (2,0,0)`` but
  ``(1,0,0) != (-1,0,0)``.  ``exact()`` returns the stored (unnormalised) rationals,
  ``approx`` returns the *normalised* unit vector;
* the parameter space is ``(longitude, latitude)`` with the **identification meridian**
  ``y == 0 && x < 0`` glued together, and the two poles ``(0,0,±1)`` contracted to a
  point.  Nothing is unbounded: ``number_of_unbounded_faces() == 0`` and the
  distinguished face is the **spherical face**, the unique face with *zero* outer CCBs,
  which always contains the north pole.

Euler on the sphere is ``V - E + F = 2`` for every connected arrangement (genus 0, and
the spherical face is a real face); every V/E/F triple below is derived by hand and then
cross-checked against that identity.

Two tests here keep their ``test_bug_*`` names because they pin down genuine CGAL 6.1 bugs
(not documented limitations) that arr2d works around -- they FAILED when they were written
and pass now that ``SphereOps::construct_opposite`` rebuilds the arc with a negated normal
and the point hash normalises by the largest |component|:

* ``test_bug_opposite_arc_approximates_to_the_same_arc``
* ``test_bug_equal_directions_hash_equal``
"""

from __future__ import annotations

import math
from fractions import Fraction

import pytest

a2 = pytest.importorskip("arrangement_2d")


# ---------------------------------------------------------------------------
# helpers / fixtures
#
# tests/conftest.py only provides planar fixtures (``square_arr`` is a segment
# arrangement, ``kind`` parametrises all seven kinds), so the sphere gets its own.
# ---------------------------------------------------------------------------

#: A geodesic triangle whose three corners are strictly inside the parameter space:
#: none of them is a pole and none of them has ``y == 0 && x < 0``.  Each side is a
#: minor arc that stays in the octant x>0, y>0, z>0, so no side crosses the
#: identification meridian either.
TRIANGLE_CORNERS = ((3, 1, 1), (1, 3, 1), (1, 1, 3))


def triangle_arcs():
    """The three sides of :data:`TRIANGLE_CORNERS`, as a closed chain."""
    p, q, r = TRIANGLE_CORNERS
    return [
        a2.GeodesicArc.from_points(p, q),
        a2.GeodesicArc.from_points(q, r),
        a2.GeodesicArc.from_points(r, p),
    ]


@pytest.fixture
def triangle_arr():
    """Spherical triangle, all corners in the interior of the parameter space."""
    arr = a2.Arrangement("sphere")
    arr.insert(triangle_arcs())
    return arr


@pytest.fixture
def octahedron_arr():
    """The three coordinate great circles (x=0, y=0, z=0 planes)."""
    arr = a2.Arrangement("sphere")
    arr.insert([
        a2.GeodesicArc.great_circle((1, 0, 0)),
        a2.GeodesicArc.great_circle((0, 1, 0)),
        a2.GeodesicArc.great_circle((0, 0, 1)),
    ])
    return arr


def unit(v):
    """`v` scaled to unit length (plain Python floats)."""
    n = math.sqrt(sum(c * c for c in v))
    return tuple(c / n for c in v)


def euler(arr):
    """V - E + F; must be 2 for a connected arrangement on a sphere."""
    return arr.number_of_vertices - arr.number_of_edges + arr.number_of_faces


def spherical_faces(arr):
    """Every face with zero outer CCBs (there is exactly one on a sphere)."""
    return [f for f in arr.faces() if f.number_of_outer_ccbs == 0]


# ===========================================================================
# 1. Point(x, y, z): location, exact unnormalised rationals, approx normalised
# ===========================================================================

def test_point_with_three_coordinates_is_sphere_kind():
    # Point(x, y) defaults to SEGMENT, Point(x, y, z) to SPHERE (DESIGN.md section 3).
    assert a2.Point(1, 2, 3).kind == a2.Kind.SPHERE
    assert a2.Point(1, 2).kind == a2.Kind.SEGMENT


def test_point_dimension_is_three():
    p = a2.Point(1, 2, 3)
    assert p.dimension == 3
    assert len(p) == 3
    assert len(list(p)) == 3


def test_point_exact_returns_the_stored_unnormalised_rationals():
    # The direction is stored as given; CGAL never normalises a Direction_3.
    p = a2.Point(3, 4, 12)
    assert p.exact() == (Fraction(3), Fraction(4), Fraction(12))
    assert p.exact_rational() == (Fraction(3), Fraction(4), Fraction(12))
    assert p.is_rational is True


def test_point_exact_keeps_non_integral_rationals():
    p = a2.Point(Fraction(1, 3), "2/7", 0.5)
    assert p.exact() == (Fraction(1, 3), Fraction(2, 7), Fraction(1, 2))


def test_point_approx_is_the_normalised_direction():
    # |(3,4,12)| = sqrt(9+16+144) = sqrt(169) = 13, so the unit vector is
    # (3/13, 4/13, 12/13) exactly -- a rare case where the normalisation is rational.
    p = a2.Point(3, 4, 12)
    assert p.approx == pytest.approx((3 / 13, 4 / 13, 12 / 13), abs=1e-15)
    assert p.xyz == p.approx
    assert (p.x, p.y, p.z) == p.approx


def test_point_approx_is_unit_length():
    p = a2.Point(7, -2, 5)
    assert math.sqrt(sum(c * c for c in p.approx)) == pytest.approx(1.0, abs=1e-15)


def test_point_approx_is_scale_invariant():
    # Positive scaling of a direction is the identity on the sphere.
    assert a2.Point(3, 4, 12).approx == a2.Point(30, 40, 120).approx


def test_point_xy_gives_the_first_two_unit_coordinates():
    p = a2.Point(3, 4, 12)
    assert p.xy == (3 / 13, 4 / 13)


def test_point_interval_encloses_the_approximation():
    # interval() is certified: every coordinate of approx must lie inside its interval.
    p = a2.Point(1, 2, 3)
    iv = p.interval()
    assert len(iv) == 3
    for (lo, hi), v in zip(iv, p.approx):
        assert lo <= v <= hi
        assert -1.0 <= lo <= hi <= 1.0


def test_point_interval_of_an_exactly_representable_direction_is_tight():
    # (3,4,12)/13 is exactly representable in binary? no -- but the enclosure is narrow.
    p = a2.Point(3, 4, 12)
    for (lo, hi), v in zip(p.interval(), p.approx):
        assert hi - lo < 1e-12
        assert lo <= v <= hi


@pytest.mark.parametrize("xyz, expected", [
    ((1, 1, 1), "interior"),
    ((1, 0, 0), "interior"),      # y == 0 but x > 0: not the identification meridian
    ((0, 0, 1), "max_boundary"),  # north pole
    ((0, 0, -1), "min_boundary"), # south pole
    ((-1, 0, 0), "mid_boundary"), # on the identification meridian y == 0, x < 0
    ((-3, 0, 5), "mid_boundary"),
])
def test_point_location(xyz, expected):
    assert a2.Point(*xyz).location == expected


def test_point_location_is_sphere_only():
    with pytest.raises(AttributeError):
        _ = a2.Point(1, 2).location


def test_point_equality_is_projective_with_positive_factors_only():
    assert a2.Point(1, 0, 0) == a2.Point(2, 0, 0)
    assert a2.Point(1, 2, 3) == a2.Point(Fraction(1, 2), 1, Fraction(3, 2))
    assert a2.Point(1, 0, 0) != a2.Point(-1, 0, 0)      # antipodal, not equal
    assert a2.Point(-1, 0, 0) == a2.Point(-2, 0, 0)


def test_bug_equal_directions_hash_equal():
    """``__hash__`` must agree with ``__eq__`` (Python data model).

    ``Point.__hash__`` hashes the *unnormalised* rational triple, so two directions
    that compare equal can hash differently.  Not a documented CGAL limitation --
    a bug in ``arrangement_2d/_geometry.pxi``.
    """
    p, q = a2.Point(1, 0, 0), a2.Point(2, 0, 0)
    assert p == q
    assert hash(p) == hash(q)


def test_point_compare_xy_and_compare_x_use_the_parameter_space():
    # Longitudes: (1,0,0) -> 0, (0,1,0) -> +90 degrees, so (1,0,0) < (0,1,0).
    p, q = a2.Point(1, 0, 0), a2.Point(0, 1, 0)
    assert p.compare_xy(q) == -1
    assert q.compare_xy(p) == 1
    assert p.compare_x(q) == -1
    assert p.compare_xy(a2.Point(5, 0, 0)) == 0


def test_point_compare_x_of_two_directions_with_the_same_longitude():
    # (1,1,0) and (1,1,5) share the longitude 45 degrees but differ in latitude.
    p, q = a2.Point(1, 1, 0), a2.Point(1, 1, 5)
    assert p.compare_x(q) == 0
    assert p.compare_xy(q) == -1


def test_point_zero_direction_is_rejected():
    with pytest.raises(ValueError, match="not a direction"):
        a2.Point(0, 0, 0)


def test_point_two_coordinate_constructor_is_unsupported_for_the_sphere():
    with pytest.raises(a2.UnsupportedError, match="3-D directions"):
        a2.Point(1, 2, kind="sphere")


def test_point_cannot_be_converted_to_a_planar_kind():
    with pytest.raises(a2.KindMismatchError):
        a2.Point(1, 2, 3).to_kind("segment")


def test_point_to_kind_sphere_is_the_identity():
    p = a2.Point(1, 2, 3)
    assert p.to_kind("sphere") is p


def test_point_repr_shows_the_exact_direction():
    assert repr(a2.Point(1, -2, Fraction(3, 4))) == "SpherePoint(1, -2, 3/4)"


# ===========================================================================
# 2. GeodesicArc constructors
# ===========================================================================

def test_from_points_builds_the_minor_arc_with_normal_p_cross_q():
    # (1,0,0) x (0,1,0) = (0,0,1): the arc is the CCW quarter of the equator.
    arc = a2.GeodesicArc.from_points((1, 0, 0), (0, 1, 0))
    assert arc.kind == a2.Kind.SPHERE
    assert arc.normal == a2.Point(0, 0, 1)
    assert arc.is_full is False
    assert arc.is_degenerate is False


def test_from_points_is_a_general_curve_not_an_x_monotone_one():
    # Construct_curve_2 yields a Curve_2, which the binding boxes as a general curve.
    arc = a2.GeodesicArc.from_points((1, 0, 0), (0, 1, 0))
    assert arc.is_x_monotone is False
    assert arc.type_name == "curve"


def test_from_points_rejects_identical_directions():
    with pytest.raises(ValueError, match="distinct directions"):
        a2.GeodesicArc.from_points((1, 0, 0), (2, 0, 0))


def test_from_points_rejects_antipodal_directions():
    # Infinitely many great circles pass through an antipodal pair.
    with pytest.raises(ValueError, match="antipodal"):
        a2.GeodesicArc.from_points((1, 0, 0), (-1, 0, 0))


def test_from_points_and_normal_accepts_an_antipodal_pair():
    arc = a2.GeodesicArc.from_points_and_normal((1, 0, 0), (-1, 0, 0), (0, 0, 1))
    assert arc.normal == a2.Point(0, 0, 1)
    assert arc.is_full is False
    assert arc.source == a2.Point(1, 0, 0)
    assert arc.target == a2.Point(-1, 0, 0)


def test_from_points_and_normal_rejects_a_point_off_the_great_circle():
    with pytest.raises(ValueError, match="source direction does not lie"):
        a2.GeodesicArc.from_points_and_normal((1, 1, 1), (0, 1, 0), (0, 0, 1))


def test_from_points_and_normal_rejects_a_zero_normal():
    with pytest.raises(ValueError, match="not a direction"):
        a2.GeodesicArc.from_points_and_normal((1, 0, 0), (0, 1, 0), (0, 0, 0))


def test_from_points_and_normal_rejects_equal_endpoints():
    with pytest.raises(ValueError, match="distinct directions"):
        a2.GeodesicArc.from_points_and_normal((1, 0, 0), (2, 0, 0), (0, 0, 1))


def test_great_circle_is_full():
    gc = a2.GeodesicArc.great_circle((0, 0, 1))
    assert gc.is_full is True
    assert gc.normal == a2.Point(0, 0, 1)
    assert gc.is_x_monotone is False


def test_great_circle_rejects_a_zero_normal():
    with pytest.raises(ValueError, match="not a direction"):
        a2.GeodesicArc.great_circle((0, 0, 0))


def test_x_monotone_constructor_returns_an_x_monotone_curve():
    x = a2.GeodesicArc.x_monotone_arc((1, 0, 0), (0, 1, 0))
    assert x.is_x_monotone is True
    assert x.type_name == "x_monotone_curve"
    assert x.source == a2.Point(1, 0, 0)
    assert x.target == a2.Point(0, 1, 0)


def test_x_monotone_constructor_refuses_an_arc_crossing_the_identification():
    # The minor arc from (-1,1,1) to (-1,-1,1) passes through y == 0, x < 0.
    with pytest.raises(ValueError, match="not x-monotone"):
        a2.GeodesicArc.x_monotone_arc((-1, 1, 1), (-1, -1, 1))


def test_geodesic_arc_has_no_public_constructor():
    with pytest.raises(TypeError, match="no public constructor"):
        a2.GeodesicArc()


def test_geodesic_arc_alias_spherical_arc():
    assert a2.SphericalArc is a2.GeodesicArc


# ===========================================================================
# 3. is_full / is_vertical / is_meridian
# ===========================================================================

def test_is_vertical_is_false_for_an_equatorial_arc():
    arc = a2.GeodesicArc.from_points((1, 0, 0), (0, 1, 0))
    assert arc.is_vertical is False
    assert arc.is_meridian is False


def test_is_vertical_is_true_for_an_arc_on_a_meridian():
    # (0,0,1) -> (1,0,0) lies on the great circle x-z, which passes through both poles.
    arc = a2.GeodesicArc.from_points((0, 0, 1), (1, 0, 0))
    assert arc.is_vertical is True
    # ... but it is only half a meridian, so is_meridian() (== pole to pole) is False.
    assert arc.is_meridian is False


def test_is_meridian_is_true_only_for_a_full_pole_to_pole_arc():
    # is_meridian() == left().is_min_boundary() && right().is_max_boundary()
    # (traits_geodesic_sphere.md section 2), i.e. south pole to north pole.
    arc = a2.GeodesicArc.from_points_and_normal((0, 0, -1), (0, 0, 1), (0, 1, 0))
    assert arc.is_vertical is True
    assert arc.is_meridian is True
    assert arc.is_full is False


def test_a_vertical_great_circle_is_vertical_but_not_a_meridian():
    gc = a2.GeodesicArc.great_circle((0, 1, 0))
    assert gc.is_full is True
    assert gc.is_vertical is True


def test_normal_is_a_sphere_point():
    n = a2.GeodesicArc.from_points((1, 0, 0), (0, 1, 0)).normal
    assert isinstance(n, a2.Point)
    assert n.kind == a2.Kind.SPHERE
    assert n.location == "max_boundary"       # (0,0,1) is the north pole


# ===========================================================================
# 4. make_x_monotone piece counts
# ===========================================================================

def test_make_x_monotone_of_a_minor_arc_is_one_piece():
    arc = a2.GeodesicArc.from_points((1, 0, 0), (0, 1, 0))
    pieces = arc.make_x_monotone()
    assert len(pieces) == 1
    assert pieces[0].is_x_monotone is True
    assert pieces[0].source == a2.Point(1, 0, 0)
    assert pieces[0].target == a2.Point(0, 1, 0)


def test_make_x_monotone_of_a_great_circle_is_two_pieces():
    # A full circle wraps around the whole longitude range, and CGAL 6.1 disables full
    # x-monotone arcs, so the equator is cut at the identification meridian (-1,0,0)
    # AND at longitude 0 (1,0,0): exactly 2 pieces.
    gc = a2.GeodesicArc.great_circle((0, 0, 1))
    pieces = gc.make_x_monotone()
    assert len(pieces) == 2
    assert all(p.is_x_monotone for p in pieces)
    ends = {(repr(p.source), repr(p.target)) for p in pieces}
    assert ends == {
        (repr(a2.Point(-1, 0, 0)), repr(a2.Point(1, 0, 0))),
        (repr(a2.Point(1, 0, 0)), repr(a2.Point(-1, 0, 0))),
    }


def test_make_x_monotone_of_a_vertical_great_circle_is_two_meridians():
    # A great circle through both poles splits at the two poles into 2 full meridians.
    gc = a2.GeodesicArc.great_circle((0, 1, 0))
    pieces = gc.make_x_monotone()
    assert len(pieces) == 2
    assert all(p.is_meridian for p in pieces)


def test_make_x_monotone_splits_an_arc_crossing_the_identification_meridian():
    # (-1,1,1) -> (-1,-1,1) crosses y == 0 at x < 0, i.e. the identification curve;
    # the crossing point is (-2,0,2) == (-1,0,1).
    arc = a2.GeodesicArc.from_points((-1, 1, 1), (-1, -1, 1))
    pieces = arc.make_x_monotone()
    assert len(pieces) == 2
    assert pieces[0].target == a2.Point(-1, 0, 1)
    assert pieces[1].source == a2.Point(-1, 0, 1)


def test_make_x_monotone_of_an_already_x_monotone_curve_is_itself():
    x = a2.GeodesicArc.x_monotone_arc((1, 0, 0), (0, 1, 0))
    pieces = x.make_x_monotone()
    assert len(pieces) == 1
    assert pieces[0] == x


def test_to_curve_of_an_x_monotone_arc_round_trips():
    x = a2.GeodesicArc.x_monotone_arc((1, 0, 0), (0, 1, 0))
    c = x.to_curve()
    assert c.is_x_monotone is False
    assert c.make_x_monotone()[0] == x


# ===========================================================================
# 5. x-monotone curve API
# ===========================================================================

def test_x_monotone_endpoints_and_extremes():
    x = a2.GeodesicArc.x_monotone_arc((1, 0, 0), (0, 1, 0))
    assert x.is_directed_right is True
    assert x.source == x.left == x.min_vertex == a2.Point(1, 0, 0)
    assert x.target == x.right == x.max_vertex == a2.Point(0, 1, 0)
    assert x.has_source and x.has_target
    assert x.is_bounded is True
    assert x.compare_endpoints_xy() == -1


def test_general_curve_accessors_raise_not_x_monotone():
    gc = a2.GeodesicArc.great_circle((0, 0, 1))
    for attr in ("source", "target", "left", "right", "min_vertex", "max_vertex",
                 "is_directed_right"):
        with pytest.raises(a2.NotXMonotoneError):
            getattr(gc, attr)


def test_split_at_an_interior_point():
    x = a2.GeodesicArc.x_monotone_arc((1, 0, 0), (0, 1, 0))
    left, right = x.split((1, 1, 0))                # 45 degrees, on the arc
    assert left.source == a2.Point(1, 0, 0)
    assert left.target == a2.Point(1, 1, 0)
    assert right.source == a2.Point(1, 1, 0)
    assert right.target == a2.Point(0, 1, 0)


def test_can_merge_and_merge_two_adjacent_arcs():
    a = a2.GeodesicArc.x_monotone_arc((1, 0, 0), (0, 1, 0))
    b = a2.GeodesicArc.x_monotone_arc((0, 1, 0), (-1, 1, 0))
    assert a.can_merge(b) is True
    merged = a.merge(b)
    assert merged.source == a2.Point(1, 0, 0)
    assert merged.target == a2.Point(-1, 1, 0)


def test_arcs_on_different_great_circles_are_not_mergeable():
    a = a2.GeodesicArc.x_monotone_arc((1, 0, 0), (0, 1, 0))
    b = a2.GeodesicArc.x_monotone_arc((0, 1, 0), (0, 1, 1))
    assert a.can_merge(b) is False


def test_intersect_two_crossing_arcs():
    # The equatorial quarter (1,0,0)->(0,1,0) meets the vertical arc through (1,1,0)
    # in exactly that one point; a transversal crossing has multiplicity 1.
    a = a2.GeodesicArc.x_monotone_arc((1, 0, 0), (0, 1, 0))
    b = a2.GeodesicArc.x_monotone_arc((1, 1, -1), (1, 1, 1))
    res = a.intersect(b)
    assert len(res) == 1
    pt, mult = res[0]
    assert pt == a2.Point(1, 1, 0)
    assert mult == 1


def test_intersect_disjoint_arcs_is_empty():
    a = a2.GeodesicArc.x_monotone_arc((1, 0, 0), (0, 1, 0))
    b = a2.GeodesicArc.x_monotone_arc((1, 0, 1), (0, 1, 1))   # a parallel small-ish arc
    assert a.intersect(b) == []


def test_compare_y_at_x_and_is_in_x_range():
    x = a2.GeodesicArc.x_monotone_arc((1, 0, 0), (0, 1, 0))   # the equator, latitude 0
    assert x.is_in_x_range((1, 1, 1)) is True
    assert x.compare_y_at_x((1, 1, 1)) == 1               # northern point: above
    assert x.compare_y_at_x((1, 1, -1)) == -1            # southern point: below
    assert x.compare_y_at_x((1, 1, 0)) == 0              # on the arc


def test_trim_is_not_available_for_the_sphere_traits():
    # Arr_geodesic_arc_on_sphere_traits_2 defines no Trim_2 functor.
    x = a2.GeodesicArc.x_monotone_arc((1, 0, 0), (0, 1, 0))
    with pytest.raises(a2.UnsupportedError, match="Trim_2"):
        x.trim((1, 0, 0), (1, 1, 0))


def test_curve_equality_ignores_the_traversal_direction():
    # Equal_2 compares point sets: an arc and its opposite are the same curve.
    x = a2.GeodesicArc.x_monotone_arc((1, 0, 0), (0, 1, 0))
    assert x == a2.GeodesicArc.x_monotone_arc((1, 0, 0), (0, 1, 0))
    assert x == x.opposite()
    assert x != a2.GeodesicArc.x_monotone_arc((0, 1, 0), (-1, 1, 0))


def test_opposite_swaps_source_and_target():
    x = a2.GeodesicArc.x_monotone_arc((1, 0, 0), (0, 1, 0))
    o = x.opposite()
    assert o.source == x.target
    assert o.target == x.source
    assert o.is_directed_right is not x.is_directed_right


def test_curve_cannot_be_converted_to_a_planar_kind():
    x = a2.GeodesicArc.x_monotone_arc((1, 0, 0), (0, 1, 0))
    with pytest.raises(a2.NotRepresentableError):
        x.to_kind("segment")


def test_planar_curve_cannot_be_converted_to_the_sphere():
    arr = a2.Arrangement("sphere")
    with pytest.raises(a2.UnsupportedError):
        arr.insert(a2.Segment((0, 0), (1, 1)))


def test_curve_parameter_space_of_an_interior_arc_is_interior():
    # ARR_INTERIOR == 4 in the raw code that Curve.parameter_space_in_* returns.
    x = a2.GeodesicArc.x_monotone_arc((1, 0, 0), (0, 1, 0))
    assert x.parameter_space_in_x(0) == 4
    assert x.parameter_space_in_y(1) == 4


def test_curve_parameter_space_of_a_meridian_touches_both_poles():
    # ARR_BOTTOM_BOUNDARY == 2, ARR_TOP_BOUNDARY == 3, ARR_LEFT_BOUNDARY == 0.
    m = a2.GeodesicArc.from_points_and_normal((0, 0, -1), (0, 0, 1), (0, 1, 0))
    assert m.parameter_space_in_y(0) == 2
    assert m.parameter_space_in_y(1) == 3
    assert m.parameter_space_in_x(0) == 0


# ===========================================================================
# 6. approximate() -- points on the unit sphere within tolerance
# ===========================================================================

def test_approximate_returns_three_dimensional_points_on_the_unit_sphere():
    arc = a2.GeodesicArc.from_points((1, 0, 0), (0, 1, 0))
    pts = arc.approximate(1e-3)
    assert len(pts) >= 2
    for p in pts:
        assert len(p) == 3
        assert math.sqrt(sum(c * c for c in p)) == pytest.approx(1.0, abs=1e-12)


def test_approximate_starts_at_the_source_and_ends_at_the_target():
    arc = a2.GeodesicArc.from_points((3, 1, 1), (1, 3, 1))
    pts = arc.approximate(1e-3)
    assert pts[0] == pytest.approx(unit((3, 1, 1)), abs=1e-12)
    assert pts[-1] == pytest.approx(unit((1, 3, 1)), abs=1e-12)


def test_approximate_stays_within_the_requested_tolerance_of_the_great_circle():
    # Every sample must lie on the supporting plane (normal . p == 0) up to rounding,
    # and consecutive samples must be closer than the chordal tolerance allows.
    arc = a2.GeodesicArc.from_points((1, 0, 0), (0, 1, 0))
    tol = 1e-2
    pts = arc.approximate(tol)
    n = unit((0, 0, 1))
    for p in pts:
        assert abs(sum(a * b for a, b in zip(n, p))) < 1e-12
    for p, q in zip(pts, pts[1:]):
        chord = math.dist(p, q)
        # sagitta = 1 - sqrt(1 - (chord/2)^2) <= tol
        assert 1.0 - math.sqrt(max(0.0, 1.0 - (chord / 2.0) ** 2)) <= tol + 1e-12


def test_finer_tolerance_gives_more_samples():
    arc = a2.GeodesicArc.from_points((1, 0, 0), (0, 1, 0))
    assert len(arc.approximate(1e-4)) > len(arc.approximate(1e-2))


def test_approximate_of_a_full_great_circle_closes_up():
    gc = a2.GeodesicArc.great_circle((0, 0, 1))
    pts = gc.approximate(1e-2)
    assert pts[0] == pytest.approx(pts[-1], abs=1e-12)
    for p in pts:
        assert p[2] == pytest.approx(0.0, abs=1e-12)


@pytest.mark.parametrize("bad", [0.0, -1.0, float("nan")])
def test_approximate_rejects_a_non_positive_tolerance(bad):
    # CGAL's Approximate_2 loops forever for error <= 0 (checklist: rendering gotcha 7).
    arc = a2.GeodesicArc.from_points((1, 0, 0), (0, 1, 0))
    with pytest.raises(ValueError, match="must be > 0"):
        arc.approximate(bad)


def test_approximate_rejects_a_tolerance_above_two():
    # acos(1 - error) is a domain error above 2 and yields NaN.
    arc = a2.GeodesicArc.from_points((1, 0, 0), (0, 1, 0))
    with pytest.raises(ValueError, match="must be <= 2"):
        arc.approximate(2.5)


def test_approximate_length_of_a_quarter_great_circle():
    arc = a2.GeodesicArc.from_points((1, 0, 0), (0, 1, 0))
    assert arc.approximate_length(1e-5) == pytest.approx(math.pi / 2, rel=1e-4)


def test_approximate_length_of_a_triangle_side():
    # cos(theta) = (3*1 + 1*3 + 1*1) / (sqrt(11) * sqrt(11)) = 7/11.
    arc = a2.GeodesicArc.from_points((3, 1, 1), (1, 3, 1))
    assert arc.approximate_length(1e-6) == pytest.approx(math.acos(7 / 11), rel=1e-5)


def test_bbox_of_an_arc_is_three_dimensional():
    arc = a2.GeodesicArc.from_points((1, 0, 0), (0, 1, 0))
    box = arc.bbox()
    assert len(box) == 6
    xmin, ymin, zmin, xmax, ymax, zmax = box
    # The quarter arc lives in x>=0, y>=0, z==0.
    assert xmin == pytest.approx(0.0, abs=1e-6)
    assert ymin == pytest.approx(0.0, abs=1e-6)
    assert (zmin, zmax) == pytest.approx((0.0, 0.0), abs=1e-12)
    assert (xmax, ymax) == pytest.approx((1.0, 1.0), abs=1e-6)


def test_bug_opposite_arc_approximates_to_the_same_arc():
    """``opposite()`` must not change the *geometry* of an arc.

    CGAL's ``X_monotone_curve_2::opposite()`` swaps source/target but keeps the
    normal, so the invariant ``normal == source x target`` that
    ``Approximate_2`` (and ``Compare_y_at_x_2``) rely on is broken and the
    approximation walks the complementary (major) arc.  Reachable from Python
    through ``Curve.opposite()``, ``Halfedge.directed_curve``,
    ``Arrangement.approximate_edges()`` and ``Face.boundary_points()``.
    ``SphereOps::construct_opposite`` rebuilds the arc with a negated normal.

    (``pytest.approx`` cannot compare a list of tuples -- it does not support
    nested data structures -- so the samples are compared one by one.)
    """
    x = a2.GeodesicArc.x_monotone_arc((1, 0, 0), (0, 1, 0))
    o = x.opposite()
    assert x == o                                   # same point set per Equal_2
    forward = x.approximate(1e-3)
    backward = o.approximate(1e-3)
    assert len(backward) == len(forward)
    for got, want in zip(backward, forward[::-1]):
        assert got == pytest.approx(want, abs=1e-9)
    # the negated normal is what makes compare_y_at_x agree on both directions: (1,1,1) is
    # above the plane z = 0 for the arc AND for its reverse.
    assert x.compare_y_at_x((1, 1, 1)) == 1
    assert o.compare_y_at_x((1, 1, 1)) == 1


def test_equality_of_meridians_uses_the_midpoint_not_the_raw_normal():
    """A MERIDIAN is the one arc whose endpoints do not identify it.

    Both poles are the endpoints of *every* meridian, so ``Equal_2`` falls back to comparing
    the normals (Arr_geodesic_arc_on_sphere_traits_2.h:1478-1481).  That is wrong in both
    directions once ``opposite()`` is involved: under the "counterclockwise around ``normal``
    from ``source`` to ``target``" invariant a meridian and its reverse carry OPPOSITE
    normals (so CGAL calls the same half-circle two different curves), while CGAL's own
    ``opposite()``, which keeps the normal, produces the OTHER half of the great circle (so
    CGAL calls two disjoint half-circles equal).  ``SphereOps::curve_equal`` compares
    ``normal x source`` -- the arc's own midpoint direction -- instead.
    """
    S, N = (0, 0, -1), (0, 0, 1)
    m0 = a2.GeodesicArc.from_points_and_normal(S, N, (0, -1, 0))    # longitude 0
    mpi = a2.GeodesicArc.from_points_and_normal(S, N, (0, 1, 0))    # longitude 180 degrees
    assert m0.is_meridian and mpi.is_meridian
    # the two halves of the vertical great circle x-z are disjoint apart from the poles
    assert m0 != mpi
    # ... while a meridian and its reverse are the same point set
    assert m0 == m0.opposite()
    assert m0.opposite() != mpi
    # our opposite() negates the normal (CGAL's keeps it, which is what breaks Equal_2)
    assert m0.normal == a2.Point(0, -1, 0, kind="sphere")
    assert m0.opposite().normal == a2.Point(0, 1, 0, kind="sphere")
    # and it really is the same half circle: both approximations pass through (1, 0, 0)
    for pts in (m0.approximate(0.5), m0.opposite().approximate(0.5)):
        assert any(p == pytest.approx((1.0, 0.0, 0.0), abs=1e-9) for p in pts)


# ===========================================================================
# 7. Arrangement of a spherical triangle
# ===========================================================================

def test_triangle_counts(triangle_arr):
    # 3 corners -> 3 vertices; the 3 sides are minor arcs that meet only at the
    # corners and cross neither a pole nor the identification meridian, so each is a
    # single edge; the sphere is cut into the triangle and its complement -> 2 faces.
    assert triangle_arr.number_of_vertices == 3
    assert triangle_arr.number_of_edges == 3
    assert triangle_arr.number_of_faces == 2
    assert triangle_arr.number_of_halfedges == 6
    assert len(triangle_arr) == 3


def test_triangle_satisfies_euler(triangle_arr):
    assert euler(triangle_arr) == 2


def test_triangle_is_valid(triangle_arr):
    assert triangle_arr.is_valid() is True
    assert triangle_arr.is_empty is False


def test_sphere_has_no_unbounded_faces(triangle_arr):
    # Arr_spherical_topology_traits_2::is_unbounded() is hard-coded false.
    assert triangle_arr.number_of_unbounded_faces == 0
    assert triangle_arr.unbounded_faces() == []
    assert all(f.is_unbounded is False for f in triangle_arr.faces())
    assert triangle_arr.is_unbounded_kind is False


def test_spherical_face_is_the_unique_face_with_zero_outer_ccbs(triangle_arr):
    faces = spherical_faces(triangle_arr)
    assert len(faces) == 1
    assert faces[0] == triangle_arr.spherical_face
    assert triangle_arr.spherical_face.has_outer_ccb is False
    assert triangle_arr.spherical_face.number_of_inner_ccbs == 1


def test_spherical_face_contains_the_north_pole(triangle_arr):
    # The triangle sits in the octant x,y,z > 0, so the north pole is outside it.
    assert triangle_arr.locate((0, 0, 1)) == triangle_arr.spherical_face


def test_unbounded_face_is_the_spherical_face(triangle_arr):
    assert triangle_arr.unbounded_face == triangle_arr.spherical_face


def test_spherical_face_is_sphere_only():
    arr = a2.Arrangement("segment")
    with pytest.raises(a2.UnsupportedError, match="only defined for the 'sphere' kind"):
        _ = arr.spherical_face


def test_sphere_has_no_fictitious_face(triangle_arr):
    with pytest.raises(a2.UnsupportedError, match="no fictitious face"):
        _ = triangle_arr.fictitious_face


def test_triangle_inner_face_has_one_outer_ccb_of_three_halfedges(triangle_arr):
    inner = [f for f in triangle_arr.faces() if f.number_of_outer_ccbs == 1]
    assert len(inner) == 1
    ccb = inner[0].outer_ccb()
    assert len(ccb) == 3
    assert {v.id for h in ccb for v in (h.source, h.target)} == \
           {v.id for v in triangle_arr.vertices()}


def test_spherical_face_boundary_is_reported_as_an_inner_ccb(triangle_arr):
    sf = triangle_arr.spherical_face
    assert sf.outer_ccbs() == []
    assert [len(c) for c in sf.inner_ccbs()] == [3]
    # Face::outer_ccb() has CGAL_precondition(number_of_outer_ccbs() == 1).
    with pytest.raises(ValueError, match="no outer CCB"):
        sf.outer_ccb()


def test_triangle_vertex_points_are_the_input_corners(triangle_arr):
    got = {repr(v.point) for v in triangle_arr.vertices()}
    assert got == {repr(a2.Point(*c)) for c in TRIANGLE_CORNERS}
    assert all(v.degree == 2 for v in triangle_arr.vertices())
    assert all(v.point.location == "interior" for v in triangle_arr.vertices())


def test_triangle_face_polygon_has_three_curves(triangle_arr):
    inner = [f for f in triangle_arr.faces() if f.number_of_outer_ccbs == 1][0]
    pwh = inner.polygon()
    assert len(pwh.outer.curves) == 3
    assert tuple(pwh.holes) == ()


def test_spherical_face_has_no_polygon(triangle_arr):
    with pytest.raises(a2.UnsupportedError, match="no outer CCB"):
        triangle_arr.spherical_face.polygon()


# ===========================================================================
# 8. Octahedron: three great circles
# ===========================================================================

def test_octahedron_counts(octahedron_arr):
    # The three coordinate planes meet the sphere in three great circles; any two of
    # them cross at the two poles of the third axis, giving the 6 axis directions
    # (+-x, +-y, +-z) as vertices.  Each great circle carries 4 of those 6 vertices
    # (all but its own two poles), so it is cut into 4 arcs: 3 * 4 = 12 edges.
    # Euler: F = 2 - V + E = 2 - 6 + 12 = 8, the eight octants.
    assert octahedron_arr.number_of_vertices == 6
    assert octahedron_arr.number_of_edges == 12
    assert octahedron_arr.number_of_faces == 8
    assert euler(octahedron_arr) == 2


def test_octahedron_is_valid(octahedron_arr):
    assert octahedron_arr.is_valid() is True


def test_octahedron_vertices_are_the_six_axis_directions(octahedron_arr):
    want = {(1, 0, 0), (-1, 0, 0), (0, 1, 0), (0, -1, 0), (0, 0, 1), (0, 0, -1)}
    got = {tuple(int(c) for c in v.point.exact()) for v in octahedron_arr.vertices()}
    assert got == want
    assert all(v.degree == 4 for v in octahedron_arr.vertices())


def test_octahedron_vertex_locations(octahedron_arr):
    loc = {repr(v.point): v.point.location for v in octahedron_arr.vertices()}
    assert loc["SpherePoint(0, 0, 1)"] == "max_boundary"
    assert loc["SpherePoint(0, 0, -1)"] == "min_boundary"
    assert loc["SpherePoint(-1, 0, 0)"] == "mid_boundary"
    assert loc["SpherePoint(1, 0, 0)"] == "interior"


def test_octahedron_every_face_is_a_spherical_triangle(octahedron_arr):
    for f in octahedron_arr.faces():
        cycles = f.outer_ccbs() + f.inner_ccbs()
        assert [len(c) for c in cycles] == [3]


def test_octahedron_spherical_face_still_has_zero_outer_ccbs(octahedron_arr):
    assert len(spherical_faces(octahedron_arr)) == 1
    assert octahedron_arr.spherical_face.number_of_outer_ccbs == 0
    assert octahedron_arr.spherical_face.number_of_inner_ccbs == 1


def test_octahedron_pole_vertices_report_boundary_parameter_space(octahedron_arr):
    north = [v for v in octahedron_arr.vertices()
             if v.point.location == "max_boundary"][0]
    assert north.parameter_space_in_y == "top"
    ident = [v for v in octahedron_arr.vertices()
             if v.point.location == "mid_boundary"][0]
    assert ident.parameter_space_in_x == "left"
    assert ident.parameter_space_in_y == "interior"


# ===========================================================================
# 9. History
# ===========================================================================

def test_insert_returns_one_curve_handle_per_input_curve():
    arr = a2.Arrangement("sphere")
    arcs = triangle_arcs()
    handles = arr.insert(arcs)
    assert len(handles) == 3
    assert arr.number_of_curves == 3
    for h, arc in zip(handles, arcs):
        assert h.curve == arc


def test_insert_of_a_single_curve_returns_a_curve_handle():
    arr = a2.Arrangement("sphere")
    h = arr.insert(a2.GeodesicArc.great_circle((0, 0, 1)))
    assert isinstance(h, a2.CurveHandle)
    assert arr.number_of_curves == 1


def test_each_triangle_side_induces_exactly_one_edge(triangle_arr):
    for h in triangle_arr.curves():
        assert h.number_of_induced_edges == 1
        assert len(h.induced_edges()) == 1


def test_each_great_circle_of_the_octahedron_induces_four_edges(octahedron_arr):
    for h in octahedron_arr.curves():
        assert h.number_of_induced_edges == 4


def test_originating_curves_of_every_edge(triangle_arr):
    for e in triangle_arr.edges():
        origins = e.originating_curves()
        assert len(origins) == 1
        assert e.number_of_originating_curves == 1
        assert origins[0] in triangle_arr.curves()


def test_remove_curve_deletes_its_edges(triangle_arr):
    h = triangle_arr.curves()[0]
    removed = triangle_arr.remove_curve(h)
    assert removed == 1
    # One side of the triangle is gone: V stays 3, E drops to 2 and the triangle
    # merges with its complement -> F = 1.  Euler: 3 - 2 + 1 = 2.
    assert triangle_arr.number_of_vertices == 3
    assert triangle_arr.number_of_edges == 2
    assert triangle_arr.number_of_faces == 1
    assert triangle_arr.number_of_curves == 2
    assert triangle_arr.is_valid() is True


def test_an_arc_crossing_the_identification_meridian_induces_two_edges():
    arr = a2.Arrangement("sphere")
    h = arr.insert(a2.GeodesicArc.from_points((-1, 1, 1), (-1, -1, 1)))
    assert h.number_of_induced_edges == 2


def test_history_survives_copy(triangle_arr):
    clone = triangle_arr.copy()
    assert clone.number_of_curves == 3
    assert all(h.number_of_induced_edges == 1 for h in clone.curves())
    assert clone.number_of_vertices == 3
    assert clone.number_of_edges == 3
    assert clone.number_of_faces == 2


# ===========================================================================
# 10. Point location (naive only; other strategies raise UnsupportedError)
# ===========================================================================

def test_locate_naive_finds_the_triangle_interior(triangle_arr):
    inside = triangle_arr.locate((1, 1, 1), "naive")
    assert isinstance(inside, a2.Face)
    assert inside.number_of_outer_ccbs == 1


def test_locate_default_matches_naive(triangle_arr):
    assert triangle_arr.locate((1, 1, 1)) == triangle_arr.locate((1, 1, 1), "naive")
    assert triangle_arr.locate((1, 1, 1)) == triangle_arr.locate((1, 1, 1), "default")


def test_locate_outside_the_triangle_gives_the_spherical_face(triangle_arr):
    assert triangle_arr.locate((-1, -1, -1)) == triangle_arr.spherical_face


def test_locate_on_a_vertex_returns_that_vertex(triangle_arr):
    v = triangle_arr.locate(TRIANGLE_CORNERS[0])
    assert isinstance(v, a2.Vertex)
    assert v.point == a2.Point(*TRIANGLE_CORNERS[0])


def test_locate_on_an_edge_returns_a_halfedge():
    arr = a2.Arrangement("sphere")
    arr.insert([a2.GeodesicArc.great_circle((0, 0, 1))])
    r = arr.locate((1, 1, 0))            # on the equator, longitude 45 degrees
    assert isinstance(r, a2.Halfedge)


@pytest.mark.parametrize("strategy",
                         ["simple", "walk", "trapezoid", "triangulation", "landmarks"])
def test_unsupported_point_location_strategies(triangle_arr, strategy):
    # Arr_spherical_topology_traits_2 has no initial_face(), so simple/walk/trapezoid
    # do not compile; the triangulation strategy is planar-only.  landmarks DOES compile
    # here but cannot be used: the walk joins the nearest landmark to the query point with
    # Construct_x_monotone_curve_2, whose CGAL 6.1 precondition forbids an antipodal pair
    # (Arr_geodesic_arc_on_sphere_traits_2.h:611) -- and CGAL, not the caller, picks the
    # landmark, so nothing can steer around it (measured: locating the north pole of a
    # two-octant arrangement raises it while every other strategy answers correctly).
    assert triangle_arr.supports_point_location(strategy) is False
    with pytest.raises(a2.UnsupportedError, match="not available for kind 'sphere'"):
        triangle_arr.locate((1, 1, 1), strategy)


def test_naive_point_location_is_supported(triangle_arr):
    assert triangle_arr.supports_point_location("naive") is True


def test_locate_rejects_an_unknown_strategy_name(triangle_arr):
    with pytest.raises(ValueError, match="unknown point-location strategy"):
        triangle_arr.locate((1, 1, 1), "nonesuch")


def test_vertical_ray_shooting_is_unsupported(triangle_arr):
    for shoot in (triangle_arr.ray_shoot_up, triangle_arr.ray_shoot_down):
        with pytest.raises(a2.UnsupportedError, match="vertical ray shooting"):
            shoot((1, 1, 1))


def test_attach_and_detach_a_naive_strategy(triangle_arr):
    triangle_arr.attach_point_location("naive")
    assert triangle_arr.has_point_location("naive") is True
    assert triangle_arr.locate((1, 1, 1), "naive").number_of_outer_ccbs == 1
    triangle_arr.detach_point_location("naive")
    assert triangle_arr.has_point_location("naive") is False


def test_batched_locate_agrees_with_naive(triangle_arr):
    pts = [(1, 1, 1), (-1, -1, -1), TRIANGLE_CORNERS[0]]
    batched = triangle_arr.batched_locate(pts)
    assert len(batched) == len(pts)
    for p, got in zip(pts, batched):
        assert got == triangle_arr.locate(p, "naive")


def test_batched_locate_at_the_poles_is_answered_naively(triangle_arr):
    # A pole query breaks the batched sweep (null halfedge dereference), so the
    # binding answers boundary points one by one with the naive strategy.
    res = triangle_arr.batched_locate([(0, 0, 1), (0, 0, -1), (-1, 0, 0)])
    assert res[0] == triangle_arr.spherical_face
    assert res[1] == triangle_arr.locate((0, 0, -1), "naive")
    assert res[2] == triangle_arr.locate((-1, 0, 0), "naive")


# ===========================================================================
# 11. Aggregate insertion of arcs crossing / lying on the identification curve
# ===========================================================================

def test_aggregate_insert_of_a_triangle_crossing_the_identification_meridian():
    # A triangle around the -x axis.  Corners (-3,1,1) and (-3,-1,1) are interior;
    # (-3,0,-1) has y == 0, x < 0 and therefore lies ON the identification meridian.
    # Only the side (-3,1,1)--(-3,-1,1) crosses the meridian, at (-3,0,1); it is
    # therefore split into 2 edges while the other two sides stay whole.
    # V = 3 corners + 1 crossing = 4, E = 2 + 1 + 1 = 4, F = 2 - 4 + 4 = 2.
    arr = a2.Arrangement("sphere")
    handles = arr.insert([
        a2.GeodesicArc.from_points((-3, 1, 1), (-3, -1, 1)),
        a2.GeodesicArc.from_points((-3, -1, 1), (-3, 0, -1)),
        a2.GeodesicArc.from_points((-3, 0, -1), (-3, 1, 1)),
    ])
    assert arr.number_of_vertices == 4
    assert arr.number_of_edges == 4
    assert arr.number_of_faces == 2
    assert euler(arr) == 2
    assert arr.is_valid() is True
    assert [h.number_of_induced_edges for h in handles] == [2, 1, 1]


def test_identification_crossing_creates_a_single_mid_boundary_vertex():
    arr = a2.Arrangement("sphere")
    arr.insert([
        a2.GeodesicArc.from_points((-3, 1, 1), (-3, -1, 1)),
        a2.GeodesicArc.from_points((-3, -1, 1), (-3, 0, -1)),
        a2.GeodesicArc.from_points((-3, 0, -1), (-3, 1, 1)),
    ])
    # A point on the identification curve is ONE DCEL vertex, not two.
    mid = [v for v in arr.vertices() if v.point.location == "mid_boundary"]
    assert len(mid) == 2                     # the corner and the crossing point
    # The crossing direction is stored unreduced (CGAL never normalises a direction),
    # so compare with the projective Point equality, not with repr().
    assert sorted(repr(v.point) for v in mid) == [
        "SpherePoint(-3, 0, -1)", "SpherePoint(-6, 0, 2)"]
    assert {v.point == a2.Point(-3, 0, 1) for v in mid} == {True, False}
    crossing = [v for v in mid if v.point == a2.Point(-3, 0, 1)][0]
    assert crossing.degree == 2              # a single vertex, both sides attached


def test_aggregate_insert_of_an_arc_lying_on_the_identification_meridian():
    # The equator plus the half meridian from the south to the north pole through
    # (-1,0,0): that half meridian lies exactly ON the identification curve.
    # V = {(1,0,0), (-1,0,0)} from the equator + the two poles = 4.
    # E: the equator's 2 x-monotone arcs (they already end at (-1,0,0), so the
    #    crossing adds no split) + the meridian split at (-1,0,0) into 2 = 4.
    # F = 2 - 4 + 4 = 2 (the slit from a pole to the equator separates nothing).
    arr = a2.Arrangement("sphere")
    arr.insert([
        a2.GeodesicArc.great_circle((0, 0, 1)),
        a2.GeodesicArc.from_points_and_normal((0, 0, -1), (0, 0, 1), (0, 1, 0)),
    ])
    assert arr.number_of_vertices == 4
    assert arr.number_of_edges == 4
    assert arr.number_of_faces == 2
    assert euler(arr) == 2
    assert arr.is_valid() is True
    assert {v.point.location for v in arr.vertices()} == {
        "interior", "mid_boundary", "min_boundary", "max_boundary"}


def test_single_great_circle_counts():
    # A full circle is not x-monotone, so CGAL cuts it into 2 pieces at longitude
    # +-180 (the identification meridian) and at longitude 0: V = 2, E = 2 and the
    # equator separates the two hemispheres: F = 2.  Euler: 2 - 2 + 2 = 2.
    arr = a2.Arrangement("sphere")
    arr.insert([a2.GeodesicArc.great_circle((0, 0, 1))])
    assert arr.number_of_vertices == 2
    assert arr.number_of_edges == 2
    assert arr.number_of_faces == 2
    assert euler(arr) == 2
    assert {repr(v.point) for v in arr.vertices()} == {
        repr(a2.Point(1, 0, 0)), repr(a2.Point(-1, 0, 0))}


def test_vertical_great_circle_splits_at_the_poles():
    # A great circle through both poles is cut into 2 meridians: V = 2 (the poles),
    # E = 2, F = 2 - 2 + 2 = 2.
    arr = a2.Arrangement("sphere")
    arr.insert([a2.GeodesicArc.great_circle((0, 1, 0))])
    assert arr.number_of_vertices == 2
    assert arr.number_of_edges == 2
    assert arr.number_of_faces == 2
    assert {v.point.location for v in arr.vertices()} == {"min_boundary", "max_boundary"}


# ===========================================================================
# 12. Overlay
# ===========================================================================

def _equator_arr():
    arr = a2.Arrangement("sphere")
    arr.insert([a2.GeodesicArc.great_circle((0, 0, 1))])
    return arr


def _yz_circle_arr():
    arr = a2.Arrangement("sphere")
    arr.insert([a2.GeodesicArc.great_circle((1, 0, 0))])
    return arr


def test_overlay_of_two_great_circles_counts():
    # A: the equator, V = {(1,0,0), (-1,0,0)}, E = 2.
    # B: the y-z great circle (through both poles), V = {(0,0,1), (0,0,-1)}, E = 2.
    # They cross at (0,1,0) and (0,-1,0): 2 more vertices -> V = 6.
    # Each circle now carries 4 vertices -> 4 arcs each -> E = 8.
    # F = 2 - 6 + 8 = 4 (two great circles cut the sphere into 4 lunes).
    A, B = _equator_arr(), _yz_circle_arr()
    R = A.overlay(B)
    assert R.number_of_vertices == 6
    assert R.number_of_edges == 8
    assert R.number_of_faces == 4
    assert euler(R) == 2
    assert R.is_valid() is True
    assert R.kind == a2.Kind.SPHERE


def test_overlay_keeps_the_inputs_untouched():
    A, B = _equator_arr(), _yz_circle_arr()
    A.overlay(B)
    assert (A.number_of_vertices, A.number_of_edges, A.number_of_faces) == (2, 2, 2)
    assert (B.number_of_vertices, B.number_of_edges, B.number_of_faces) == (2, 2, 2)


def test_overlay_face_face_callback_sees_every_combination():
    A, B = _equator_arr(), _yz_circle_arr()
    for i, f in enumerate(A.faces()):
        f.data = ("A", i)
    for i, f in enumerate(B.faces()):
        f.data = ("B", i)

    class CB(a2.OverlayCallbacks):
        def face_face(self, fa, fb, fr):
            fr.data = (fa.data, fb.data)

    R = A.overlay(B, CB())
    got = {f.data for f in R.faces()}
    # 2 faces in A times 2 faces in B = 4 distinct pairs, one per resulting face.
    assert len(got) == 4
    assert got == {(("A", i), ("B", j)) for i in (0, 1) for j in (0, 1)}


def test_overlay_on_face_convenience_hook():
    A, B = _equator_arr(), _yz_circle_arr()
    for f in A.faces():
        f.data = "a"
    for f in B.faces():
        f.data = "b"
    R = A.overlay(B, on_face=lambda fa, fb: (fa.data, fb.data))
    assert {f.data for f in R.faces()} == {("a", "b")}


def test_overlay_fires_the_edge_edge_vertex_callback():
    seen = []

    class CB(a2.OverlayCallbacks):
        def edge_edge_vertex(self, ea, eb, vr):
            seen.append(vr.id)

    A, B = _equator_arr(), _yz_circle_arr()
    A.overlay(B, CB())
    # exactly the two crossings (0,1,0) and (0,-1,0)
    assert len(seen) == 2


def test_overlay_result_carries_the_history_of_both_inputs():
    A, B = _equator_arr(), _yz_circle_arr()
    R = A.overlay(B)
    assert R.number_of_curves == 2


def test_overlay_of_different_kinds_is_a_kind_mismatch():
    A = _equator_arr()
    planar = a2.Arrangement("segment")
    with pytest.raises(a2.KindMismatchError):
        A.overlay(planar)


# ===========================================================================
# 13. Vertical decomposition
# ===========================================================================

def test_decompose_of_an_interior_only_triangle(triangle_arr):
    # One entry per vertex, sorted; below/above are Vertex|Halfedge|Face|None.
    entries = triangle_arr.decompose()
    assert len(entries) == 3
    for v, below, above in entries:
        assert isinstance(v, a2.Vertex)
        assert below is None or isinstance(below, (a2.Vertex, a2.Halfedge, a2.Face))
        assert above is None or isinstance(above, (a2.Vertex, a2.Halfedge, a2.Face))


def test_decompose_raises_when_a_vertex_is_on_a_pole(octahedron_arr):
    # CGAL's vertical decomposition aborts on pole / identification vertices; the
    # binding scans the vertices first and reports a clean error instead.
    with pytest.raises(a2.UnsupportedError, match="interior of the parameter space"):
        octahedron_arr.decompose()


def test_decompose_raises_when_a_vertex_is_on_the_identification_curve():
    arr = a2.Arrangement("sphere")
    arr.insert([a2.GeodesicArc.great_circle((0, 0, 1))])   # (-1,0,0) is mid_boundary
    with pytest.raises(a2.UnsupportedError, match="interior of the parameter space"):
        arr.decompose()


def test_decompose_of_an_empty_arrangement_is_empty():
    assert a2.Arrangement("sphere").decompose() == []


# ===========================================================================
# 14. remove_vertex / remove_isolated_vertex on boundary vertices
# ===========================================================================

def test_remove_vertex_on_a_pole_is_unsupported(octahedron_arr):
    north = [v for v in octahedron_arr.vertices()
             if v.point.location == "max_boundary"][0]
    with pytest.raises(a2.UnsupportedError, match="pole or on the identification"):
        octahedron_arr.remove_vertex(north)
    assert octahedron_arr.is_valid() is True


def test_remove_isolated_vertex_on_a_pole_is_unsupported(octahedron_arr):
    south = [v for v in octahedron_arr.vertices()
             if v.point.location == "min_boundary"][0]
    with pytest.raises(a2.UnsupportedError, match="pole or on the identification"):
        octahedron_arr.remove_isolated_vertex(south)


def test_remove_vertex_on_the_identification_curve_is_unsupported(octahedron_arr):
    ident = [v for v in octahedron_arr.vertices()
             if v.point.location == "mid_boundary"][0]
    with pytest.raises(a2.UnsupportedError, match="pole or on the identification"):
        octahedron_arr.remove_vertex(ident)


def test_remove_vertex_of_an_isolated_pole_vertex_is_unsupported():
    arr = a2.Arrangement("sphere")
    v = arr.insert_point((0, 0, 1))
    assert v.is_isolated is True
    with pytest.raises(a2.UnsupportedError, match="pole or on the identification"):
        arr.remove_vertex(v)


def test_remove_vertex_of_an_interior_vertex_merges_the_two_edges():
    # One input arc split by an inserted point: the two halves come from the same
    # history curve, so CGAL may merge them back and the vertex disappears.
    arr = a2.Arrangement("sphere")
    h = arr.insert(a2.GeodesicArc.from_points((1, 0, 0), (0, 1, 0)))
    v = arr.insert_point((1, 1, 0))
    assert (arr.number_of_vertices, arr.number_of_edges) == (3, 2)
    assert arr.remove_vertex(v) is True
    assert (arr.number_of_vertices, arr.number_of_edges) == (2, 1)
    assert h.number_of_induced_edges == 1
    assert arr.is_valid() is True


def test_remove_isolated_interior_vertex():
    arr = a2.Arrangement("sphere")
    v = arr.insert_point((1, 1, 1))
    assert v.is_isolated is True
    assert v.face == arr.spherical_face
    f = arr.remove_isolated_vertex(v)
    assert f == arr.spherical_face
    assert arr.number_of_vertices == 0


# ===========================================================================
# 15. Miscellaneous arrangement surface
# ===========================================================================

def test_empty_sphere_arrangement():
    arr = a2.Arrangement("sphere")
    assert arr.kind == a2.Kind.SPHERE
    assert arr.dimension == 3
    assert arr.is_empty is True
    assert arr.number_of_vertices == 0
    assert arr.number_of_edges == 0
    # The bare sphere is one face, the spherical face itself.
    assert arr.number_of_faces == 1
    assert arr.spherical_face.number_of_outer_ccbs == 0


def test_insert_a_three_tuple_inserts_a_point():
    arr = a2.Arrangement("sphere")
    v = arr.insert((1, 2, 3))
    assert isinstance(v, a2.Vertex)
    assert v.point == a2.Point(1, 2, 3)


def test_clear_resets_the_arrangement(triangle_arr):
    triangle_arr.clear()
    assert triangle_arr.is_empty is True
    assert triangle_arr.number_of_faces == 1


def test_vertex_coordinates_are_three_dimensional_and_unit_length(triangle_arr):
    coords = triangle_arr.vertex_coordinates()
    assert len(coords) == 3
    for row in coords:
        assert len(row) == 3
        assert math.sqrt(sum(float(c) ** 2 for c in row)) == pytest.approx(1.0, abs=1e-12)


def test_edge_vertex_indices(triangle_arr):
    idx = triangle_arr.edge_vertex_indices()
    assert len(idx) == 3
    pairs = {tuple(sorted(int(c) for c in row)) for row in idx}
    assert pairs == {(0, 1), (0, 2), (1, 2)}


def test_face_boundaries(triangle_arr):
    fb = triangle_arr.face_boundaries()
    assert len(fb) == 2
    assert sorted(len(cyc) for face in fb for cyc in face) == [3, 3]


def test_arrangement_bbox_is_three_dimensional(triangle_arr):
    box = triangle_arr.bbox()
    assert len(box) == 6
    xmin, ymin, zmin, xmax, ymax, zmax = box
    assert xmin <= xmax and ymin <= ymax and zmin <= zmax
    # every corner is a permutation of (3,1,1)/sqrt(11)
    assert xmin == pytest.approx(1 / math.sqrt(11), abs=1e-12)
    assert xmax == pytest.approx(3 / math.sqrt(11), abs=1e-12)


def test_approximate_edges_returns_three_dimensional_polylines(triangle_arr):
    chains = triangle_arr.approximate_edges(1e-2)
    assert len(chains) == 3
    for chain in chains:
        for p in chain:
            assert len(p) == 3
            assert math.sqrt(sum(float(c) ** 2 for c in p)) == pytest.approx(1.0, abs=1e-9)


def test_observer_sees_the_aggregate_insertion():
    events = []

    class Obs(a2.Observer):
        def before_global_change(self):
            events.append("bgc")

        def after_global_change(self):
            events.append("agc")

        def after_create_vertex(self, v):
            events.append("v")

        def after_create_edge(self, e):
            events.append("e")

        def after_split_face(self, f, new_f, is_hole):
            events.append("sf")

    arr = a2.Arrangement("sphere")
    obs = Obs()
    arr.add_observer(obs)
    arr.insert(triangle_arcs())
    # One global-change bracket for the whole aggregate insertion, 3 vertices,
    # 3 edges and exactly one face split (the third edge closes the triangle).
    assert events[0] == "bgc"
    assert events[-1] == "agc"
    assert events.count("v") == 3
    assert events.count("e") == 3
    assert events.count("sf") == 1
    arr.remove_observer(obs)
    assert arr.observers() == []


def test_element_data_round_trip_and_copy(triangle_arr):
    for i, v in enumerate(triangle_arr.vertices()):
        v.data = {"i": i}
    triangle_arr.edges()[0].data = "edge"
    triangle_arr.spherical_face.data = 42
    clone = triangle_arr.copy()
    assert sorted(v.data["i"] for v in clone.vertices()) == [0, 1, 2]
    assert clone.spherical_face.data == 42


def test_handles_are_invalidated_by_clear(triangle_arr):
    v = triangle_arr.vertices()[0]
    triangle_arr.clear()
    assert v.is_valid is False
    with pytest.raises(a2.InvalidHandleError):
        _ = v.point


def test_zone_of_an_arc_crossing_the_triangle(triangle_arr):
    # From the barycentre-ish direction (1,1,1) to the north pole: the supporting
    # plane is x == y, which passes through the corner (1,1,3), so the walk leaves
    # the triangle exactly at that vertex.
    arc = a2.GeodesicArc.from_points((1, 1, 1), (0, 0, 1))
    zone = triangle_arr.zone(arc)
    assert [type(z).__name__ for z in zone] == ["Face", "Vertex", "Face"]
    assert zone[1].point == a2.Point(1, 1, 3)
    assert zone[-1] == triangle_arr.spherical_face


def test_do_intersect(triangle_arr):
    assert triangle_arr.do_intersect(
        a2.GeodesicArc.from_points((1, 1, 1), (0, 0, 1))) is True
    # An arc entirely inside the triangle touches nothing.
    assert triangle_arr.do_intersect(
        a2.GeodesicArc.from_points((1, 1, 1), (2, 1, 1))) is False


def test_boolean_set_operations_are_not_available_for_the_sphere():
    with pytest.raises(a2.UnsupportedError, match="Boolean set operations"):
        a2.PolygonSet(kind="sphere")


def test_traits_object_of_the_sphere_kind():
    t = a2.traits("sphere")
    assert t.kind == a2.Kind.SPHERE
    assert t.dimension == 3
    assert t.compare_xy((1, 0, 0), (0, 1, 0)) == -1
    assert t.equal((1, 0, 0), (2, 0, 0)) is True
    assert len(t.make_x_monotone(a2.GeodesicArc.great_circle((0, 0, 1)))) == 2
    assert t.is_vertical(
        a2.GeodesicArc.from_points_and_normal((0, 0, -1), (0, 0, 1), (0, 1, 0))) is True


def test_traits_construct_x_monotone_curve_refuses_a_non_monotone_pair():
    t = a2.traits("sphere")
    with pytest.raises(ValueError, match="not x-monotone"):
        t.construct_x_monotone_curve((-1, 1, 1), (-1, -1, 1))


def test_traits_parameter_space_names_for_a_meridian():
    t = a2.traits("sphere")
    m = a2.GeodesicArc.from_points_and_normal((0, 0, -1), (0, 0, 1), (0, 1, 0))
    assert t.parameter_space_in_y(m, "min") == "bottom"
    assert t.parameter_space_in_y(m, "max") == "top"
    assert t.parameter_space_in_x(m, "min") == "left"


# ===========================================================================
# 16. Incremental (non-sweep) construction on the sphere
# ===========================================================================

def test_insert_in_face_interior_and_from_left_vertex():
    # Two chained quarter arcs of the equator built without the sweep.
    arr = a2.Arrangement("sphere")
    h1 = arr.insert_in_face_interior(
        a2.GeodesicArc.x_monotone_arc((1, 0, 0), (0, 1, 0)), arr.spherical_face)
    assert (arr.number_of_vertices, arr.number_of_edges) == (2, 1)
    h2 = arr.insert_from_left_vertex(
        a2.GeodesicArc.x_monotone_arc((0, 1, 0), (-1, 1, 0)), h1.target)
    assert (arr.number_of_vertices, arr.number_of_edges) == (3, 2)
    assert arr.number_of_faces == 1          # an open chain separates nothing
    assert h2.source == h1.target
    assert arr.is_valid() is True
    # insert_non_intersecting / insert_in_face_interior record no history.
    assert arr.number_of_curves == 0


def test_insert_from_right_vertex_checks_the_curve_end():
    # (0,1,0) is the LEFT (min) end of the arc (0,1,0)->(-1,1,0) (longitude 90 < 135),
    # so insert_from_right_vertex must refuse it.
    arr = a2.Arrangement("sphere")
    h = arr.insert_in_face_interior(
        a2.GeodesicArc.x_monotone_arc((1, 0, 0), (0, 1, 0)), arr.spherical_face)
    with pytest.raises(a2.PreconditionError, match="right curve end"):
        arr.insert_from_right_vertex(
            a2.GeodesicArc.x_monotone_arc((0, 1, 0), (-1, 1, 0)), h.target)


def test_insert_at_vertices_closes_a_spherical_triangle():
    # Three arcs joined at their endpoints: V = 3, E = 3, F = 2 (Euler 3 - 3 + 2 = 2).
    arr = a2.Arrangement("sphere")
    a = arr.insert_in_face_interior(
        a2.GeodesicArc.x_monotone_arc((1, 0, 0), (0, 1, 0)), arr.spherical_face)
    b = arr.insert_from_left_vertex(
        a2.GeodesicArc.x_monotone_arc((0, 1, 0), (0, 1, 1)), a.target)
    assert arr.number_of_faces == 1
    arr.insert_at_vertices(
        a2.GeodesicArc.x_monotone_arc((1, 0, 0), (0, 1, 1)), a.source, b.target)
    assert (arr.number_of_vertices, arr.number_of_edges, arr.number_of_faces) == (3, 3, 2)
    assert euler(arr) == 2
    assert arr.is_valid() is True


def test_insert_non_intersecting_records_no_history():
    arr = a2.Arrangement("sphere")
    h = arr.insert_non_intersecting(a2.GeodesicArc.x_monotone_arc((1, 0, 0), (0, 1, 0)))
    assert isinstance(h, a2.Halfedge)
    assert arr.number_of_edges == 1
    assert arr.number_of_curves == 0


def test_insert_point_on_an_existing_edge_splits_it():
    arr = a2.Arrangement("sphere")
    h = arr.insert(a2.GeodesicArc.from_points((1, 0, 0), (0, 1, 0)))
    v = arr.insert_point((1, 1, 0))          # halfway along the arc
    assert v.is_isolated is False
    assert arr.number_of_edges == 2
    assert h.number_of_induced_edges == 2


def test_insert_point_at_an_existing_vertex_returns_it(triangle_arr):
    before = triangle_arr.number_of_vertices
    v = triangle_arr.insert_point(TRIANGLE_CORNERS[0])
    assert triangle_arr.number_of_vertices == before
    assert v.point == a2.Point(*TRIANGLE_CORNERS[0])


def test_split_edge_at_an_interior_point():
    arr = a2.Arrangement("sphere")
    arr.insert(a2.GeodesicArc.from_points((1, 0, 0), (0, 1, 0)))
    e = arr.edges()[0]
    arr.split_edge(e, (1, 1, 0))
    assert (arr.number_of_vertices, arr.number_of_edges) == (3, 2)
    assert arr.is_valid() is True


def test_modify_vertex_only_accepts_an_equal_direction():
    arr = a2.Arrangement("sphere")
    v = arr.insert_point((1, 1, 1))
    # (2,2,2) is the same direction, just a different representation.
    w = arr.modify_vertex(v, (2, 2, 2))
    assert repr(w.point) == "SpherePoint(2, 2, 2)"
    with pytest.raises(a2.PreconditionError, match="different from the current one"):
        arr.modify_vertex(w, (1, 1, 2))


def test_modify_edge_replaces_the_stored_curve():
    arr = a2.Arrangement("sphere")
    h = arr.insert_in_face_interior(
        a2.GeodesicArc.x_monotone_arc((1, 0, 0), (0, 1, 0)), arr.spherical_face)
    out = arr.modify_edge(h, a2.GeodesicArc.x_monotone_arc((2, 0, 0), (0, 2, 0)))
    assert repr(out.curve.source) == "SpherePoint(2, 0, 0)"


def test_remove_edge_merges_the_two_incident_faces(triangle_arr):
    e = triangle_arr.edges()[0]
    f = triangle_arr.remove_edge(e)
    # The triangle and the spherical face merge; the two endpoints stay because they
    # are still incident to the other two sides.  V = 3, E = 2, F = 1 (3 - 2 + 1 = 2).
    assert f == triangle_arr.spherical_face
    assert (triangle_arr.number_of_vertices, triangle_arr.number_of_edges,
            triangle_arr.number_of_faces) == (3, 2, 1)
    assert triangle_arr.is_valid() is True


def test_assign_copies_another_arrangement(triangle_arr):
    other = a2.Arrangement("sphere")
    other.assign(triangle_arr)
    assert (other.number_of_vertices, other.number_of_edges,
            other.number_of_faces) == (3, 3, 2)
    assert other.number_of_curves == 3


# ===========================================================================
# 17. DCEL traversal
# ===========================================================================

def test_incident_halfedges_point_into_the_vertex(triangle_arr):
    for v in triangle_arr.vertices():
        inc = v.incident_halfedges()
        assert len(inc) == v.degree == 2
        assert all(h.target.id == v.id for h in inc)


def test_incident_faces_of_a_triangle_corner(triangle_arr):
    v = triangle_arr.vertices()[0]
    faces = v.incident_faces()
    assert len(faces) == 2                      # the triangle and its complement
    assert triangle_arr.spherical_face in faces


def test_twin_next_prev_and_ccb(triangle_arr):
    e = triangle_arr.edges()[0]
    assert e.twin.twin == e
    assert e.edge_id == e.twin.edge_id
    assert e.next.prev == e
    assert len(e.ccb()) == 3
    assert len(e.twin.ccb()) == 3
    assert e.is_fictitious is False


def test_one_side_of_every_edge_is_on_the_spherical_faces_inner_ccb(triangle_arr):
    for e in triangle_arr.edges():
        sides = {e.is_on_outer_ccb, e.twin.is_on_outer_ccb}
        assert sides == {True, False}
        assert e.is_on_inner_ccb is not e.is_on_outer_ccb


def test_face_edges_and_adjacent_faces(triangle_arr):
    inner = [f for f in triangle_arr.faces() if f.number_of_outer_ccbs == 1][0]
    assert len(inner.edges()) == 3
    assert inner.adjacent_faces() == [triangle_arr.spherical_face]
    assert inner.isolated_vertices() == []
    assert inner.number_of_isolated_vertices == 0


def test_halfedge_direction_and_curve(triangle_arr):
    for e in triangle_arr.halfedges():
        assert e.direction in ("left_to_right", "right_to_left")
        assert e.curve.is_x_monotone is True
        assert e.curve == e.twin.curve         # same geometry, both directions


def test_isolated_vertex_belongs_to_the_spherical_face():
    arr = a2.Arrangement("sphere")
    v = arr.insert_point((1, 1, 1))
    assert arr.number_of_isolated_vertices == 1
    assert v.face == arr.spherical_face
    assert arr.spherical_face.number_of_isolated_vertices == 1
    assert arr.spherical_face.isolated_vertices() == [v]


def test_vertices_at_infinity_is_zero_on_the_sphere(triangle_arr):
    # There are no fictitious cells under Arr_spherical_topology_traits_2.
    assert triangle_arr.number_of_vertices_at_infinity == 0
    assert all(v.is_at_open_boundary is False for v in triangle_arr.vertices())


def test_bug_approximate_edges_follow_their_own_edges(triangle_arr):
    """``approximate_edges`` must sample the edge, not its complement.

    Same root cause as ``test_bug_opposite_arc_approximates_to_the_same_arc``:
    ``he_directed_curve`` calls ``construct_opposite`` for a halfedge whose stored
    curve runs the other way, and the resulting arc's normal no longer matches
    ``source x target``, so ``Approximate_2`` walks the major arc.  Every side of
    this triangle has the same geodesic length ``acos(7/11)``, so the polyline
    lengths must all agree with it.
    """
    want = math.acos(7 / 11)
    for chain in triangle_arr.approximate_edges(1e-4):
        pts = [tuple(float(c) for c in p) for p in chain]
        length = sum(math.dist(p, q) for p, q in zip(pts, pts[1:]))
        assert length == pytest.approx(want, rel=1e-3)
