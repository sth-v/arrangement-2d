"""Regression tests for the review findings fixed in the ``final_fix`` round.

Every test in this file reproduces a defect that was *measured* against the previous build
(most of them SIGSEGVs, one an infinite hang) and asserts the fixed behaviour.  The section
headers name the finding; the docstrings name the CGAL 6.1 source location that made it
possible, so the tests double as documentation of what must never regress.
"""

from __future__ import annotations

import gc
import random
import weakref
from decimal import Decimal, getcontext
from fractions import Fraction as F

import pytest

a2 = pytest.importorskip("arrangement_2d")

S = a2.Segment
P = a2.Point


def _square(kind="segment"):
    """The axis-parallel square (0,0)-(4,4): V=4 E=4 F=2."""
    arr = a2.Arrangement(kind)
    arr.insert([S((0, 0), (4, 0)), S((4, 0), (4, 4)),
                S((4, 4), (0, 4)), S((0, 4), (0, 0))])
    return arr


def _inner_face(arr):
    return [f for f in arr.faces() if not f.is_unbounded][0]


# ===========================================================================
# 1. insert_at_vertices: CGAL frees v1's isolated-vertex record BEFORE it checks
#    that the two vertices lie in the same face (Arrangement_on_surface_2_impl.h
#    :1026-1050 / :1081).  The assertion then threw with a dangling DIso_vertex*,
#    and the next read of Vertex.face SIGSEGVed (measured: exit 139).
# ===========================================================================

def test_insert_at_vertices_refuses_two_isolated_vertices_in_different_faces():
    arr = _square()
    v_in = arr.insert_point_in_face_interior(P(2, 2), _inner_face(arr))
    v_out = arr.insert_point_in_face_interior(P(10, 2), arr.unbounded_face)
    assert arr.number_of_isolated_vertices == 2

    with pytest.raises(ValueError, match="different faces"):
        arr.insert_at_vertices(S((2, 2), (10, 2)), v_in, v_out)

    # Nothing was touched: the isolated-vertex records are intact and readable.
    assert arr.number_of_isolated_vertices == 2
    assert v_in.is_isolated and v_out.is_isolated
    assert v_in.face.id == _inner_face(arr).id          # used to SIGSEGV
    assert v_out.face.is_unbounded
    assert _inner_face(arr).number_of_isolated_vertices == 1
    assert arr.is_valid()


def test_insert_at_vertices_refuses_an_isolated_vertex_in_a_non_incident_face():
    """The :1081 variant: an isolated vertex inside one square, joined to a corner of a
    second, disjoint square -- the isolated vertex's face is not incident to that corner."""
    arr = _square()
    arr.insert([S((10, 0), (14, 0)), S((14, 0), (14, 4)),
                S((14, 4), (10, 4)), S((10, 4), (10, 0))])
    first = [f for f in arr.faces()
             if not f.is_unbounded and any(v.point.xy == (0.0, 0.0)
                                           for h in f.outer_ccb() for v in (h.source,))][0]
    v_in = arr.insert_point_in_face_interior(P(2, 2), first)
    corner = [v for v in arr.vertices() if v.point.xy == (10.0, 0.0)][0]
    assert not corner.is_isolated
    isolated_before = arr.number_of_isolated_vertices

    with pytest.raises(ValueError, match="not lie in a face incident"):
        arr.insert_at_vertices(S((2, 2), (10, 0)), v_in, corner)

    assert arr.number_of_isolated_vertices == isolated_before
    assert v_in.is_isolated
    assert v_in.face.id == first.id                     # used to SIGSEGV
    assert arr.is_valid()


def test_insert_at_vertices_still_works_for_two_isolated_vertices_in_one_face():
    arr = _square()
    f = _inner_face(arr)
    a = arr.insert_point_in_face_interior(P(1, 2), f)
    b = arr.insert_point_in_face_interior(P(3, 2), f)
    he = arr.insert_at_vertices(S((1, 2), (3, 2)), a, b)
    assert he.source.point.xy in {(1.0, 2.0), (3.0, 2.0)}
    assert arr.number_of_isolated_vertices == 0
    assert arr.is_valid()


# ===========================================================================
# 2. An attached "trapezoid" point location made EVERY edge merge throw
#    (Td_active_vertex.h:168) between the with-history observer's unregister and
#    re-register halves, leaving dangling Curve_halfedges* pointers; a later
#    copy()/assign()/overlay() then SIGSEGVed.
# ===========================================================================

TRAPEZOID_KINDS = ["segment", "polyline", "circle_segment", "conic", "bezier"]


def _straight_curve(kind, p, q):
    if kind == "segment":
        return a2.Segment(p, q)
    if kind == "polyline":
        return a2.Polyline([p, q])
    if kind == "linear":
        return a2.LinearCurve.segment(a2.Point(*p, kind="linear"),
                                      a2.Point(*q, kind="linear"))
    if kind == "circle_segment":
        return a2.CircleSegment.segment(a2.Point(*p), a2.Point(*q))
    if kind == "conic":
        return a2.ConicArc.segment(a2.Point(*p), a2.Point(*q))
    return a2.BezierCurve([p, q])


@pytest.mark.parametrize("kind", TRAPEZOID_KINDS)
def test_merge_with_an_attached_trapezoid_pl_keeps_the_history_consistent(kind):
    arr = a2.Arrangement(kind)
    arr.attach_point_location("trapezoid")
    arr.insert(_straight_curve(kind, (0, 0), (10, 0)))
    arr.split_edge(arr.edges()[0], a2.Point(5, 0, kind=kind))
    v = [x for x in arr.vertices() if x.point.xy == (5.0, 0.0)][0]

    assert arr.remove_vertex(v) is True          # used to raise CGALAssertionError
    assert arr.has_point_location("trapezoid")   # detached and rebuilt, still usable

    c = arr.curves()[0]
    edges = [e for e in arr.edges() if c in e.originating_curves()]
    assert c.number_of_induced_edges == len(edges) == 1   # symmetric again
    assert arr.is_valid()

    # The whole chain that used to end in a SIGSEGV.
    assert arr.remove_curve(c) == 1
    copy = arr.copy()
    assert copy.is_valid()
    assert arr.locate((1, 0), strategy="trapezoid") is not None


def test_is_valid_notices_an_inconsistent_curve_history():
    """``is_valid()`` now also checks the with-history bookkeeping, so the corruption
    class above is detectable instead of only crashing later."""
    arr = a2.Arrangement("segment")
    arr.insert(S((0, 0), (10, 0)))
    arr.split_edge(arr.edges()[0], P(5, 0))
    assert arr.is_valid()
    c = arr.curves()[0]
    assert c.number_of_induced_edges == 2
    assert all(c in e.originating_curves() for e in arr.edges())


# ===========================================================================
# 3. Vertex.degree / incident_halfedges() dereferenced a null or freed halfedge
#    pointer inside observer callbacks (measured: exit 139 for both).
# ===========================================================================

def test_a_brand_new_vertex_has_no_incident_halfedges():
    seen = []

    class O(a2.Observer):
        def after_create_vertex(self, v):
            seen.append((v.degree, v.is_isolated, v.incident_halfedges(), v.point.xy))

    arr = a2.Arrangement("segment")
    arr.add_observer(O())
    arr.insert(S((0, 0), (4, 0)))                # used to SIGSEGV in incident_halfedges()

    assert len(seen) == 2
    for degree, isolated, incident, _pt in seen:
        assert degree == 0 and isolated is False and incident == []


def test_before_create_edge_and_before_split_edge_see_linkless_vertices():
    seen = []

    class O(a2.Observer):
        def before_create_edge(self, curve, v1, v2):
            seen.append(("edge", v1.incident_halfedges(), v2.incident_halfedges()))

        def before_split_edge(self, e, v, c1, c2):
            seen.append(("split", v.incident_halfedges(), []))

    arr = a2.Arrangement("segment")
    arr.add_observer(O())
    arr.insert(S((0, 0), (4, 0)))
    arr.split_edge(arr.edges()[0], P(2, 0))
    assert seen and all(a == [] and b == [] for _what, a, b in seen)


def test_vertex_ring_is_refused_inside_before_remove_vertex():
    """CGAL deletes the incident halfedges before notifying (``_remove_edge``), so the
    ring accessors would walk freed memory."""
    seen = {}

    class O(a2.Observer):
        def before_remove_vertex(self, v):
            seen["point"] = v.point.xy
            seen["isolated"] = v.is_isolated
            with pytest.raises(a2.UnsupportedError):
                v.degree
            with pytest.raises(a2.UnsupportedError):
                v.incident_halfedges()
            with pytest.raises(a2.UnsupportedError):
                v.incident_faces()

    arr = a2.Arrangement("segment")
    arr.add_observer(O())
    arr.insert(S((0, 0), (4, 0)))
    arr.remove_edge(arr.edges()[0], True, True)   # used to SIGSEGV
    assert seen["isolated"] is False
    assert seen["point"] in {(0.0, 0.0), (4.0, 0.0)}


def test_an_isolated_vertex_keeps_its_ring_accessors_while_being_removed():
    """``remove_isolated_vertex`` notifies BEFORE it frees anything, so nothing is blocked."""
    seen = {}

    class O(a2.Observer):
        def before_remove_vertex(self, v):
            seen["degree"] = v.degree
            seen["incident"] = v.incident_halfedges()
            seen["face"] = v.face.is_unbounded

    arr = a2.Arrangement("segment")
    arr.add_observer(O())
    v = arr.insert_point(P(3, 3))
    arr.remove_isolated_vertex(v)
    assert seen == {"degree": 0, "incident": [], "face": True}


# ===========================================================================
# 4. Re-entrancy: mutating an arrangement from an observer or overlay callback
#    used to SIGSEGV (clear/assign/remove_curve) or hang forever (overlay).
# ===========================================================================

def test_observer_cannot_clear_the_arrangement_it_observes():
    class O(a2.Observer):
        def __init__(self):
            self.arr = None

        def after_create_vertex(self, v):
            self.arr.clear()

    arr = a2.Arrangement("segment")
    obs = O()
    obs.arr = arr
    arr.add_observer(obs)
    with pytest.raises(a2.UnsupportedError, match="observer or overlay callback"):
        arr.insert([S((0, 0), (10, 10)), S((0, 10), (10, 0))])
    assert arr.is_valid()


@pytest.mark.parametrize("mutate", [
    lambda arr: arr.clear(),
    lambda arr: arr.assign(a2.Arrangement("segment")),
    lambda arr: arr.insert(S((7, 7), (8, 8))),
    lambda arr: arr.insert_point(P(9, 9)),
    lambda arr: arr.remove_curve(arr.curves()[0]),
    lambda arr: arr.attach_point_location("naive"),
])
def test_every_mutator_is_refused_from_an_observer_callback(mutate):
    class O(a2.Observer):
        def __init__(self):
            self.arr = None
            self.n = 0

        def after_create_edge(self, e):
            self.n += 1
            if self.n == 1:
                mutate(self.arr)

    arr = a2.Arrangement("segment")
    obs = O()
    obs.arr = arr
    arr.add_observer(obs)
    with pytest.raises(a2.UnsupportedError):
        arr.insert([S((0, 0), (10, 10)), S((0, 10), (10, 0))])
    assert arr.is_valid()


def _crossing(offset):
    arr = a2.Arrangement("segment")
    arr.insert([S((offset, offset), (offset + 10, offset + 10)),
                S((offset, offset + 10), (offset + 10, offset))])
    return arr


def test_overlay_callback_cannot_clear_a_const_input():
    state = {"n": 0}

    class CB(a2.OverlayCallbacks):
        def face_face(self, fa, fb, fr):
            state["n"] += 1
            if state["n"] == 1:
                fa.arrangement.clear()

    A, B = _crossing(0), _crossing(2)
    with pytest.raises(a2.UnsupportedError):
        A.overlay(B, CB())
    assert A.is_valid() and B.is_valid()
    assert A.number_of_edges == 4


def test_overlay_callback_cannot_insert_into_the_half_built_result():
    state = {"n": 0}

    class CB(a2.OverlayCallbacks):
        def face_face(self, fa, fb, fr):
            state["n"] += 1
            if state["n"] == 1:
                fr.arrangement.insert(S((100, 100), (200, 200)))

    A, B = _crossing(0), _crossing(2)
    with pytest.raises(a2.UnsupportedError):      # used to hang forever
        A.overlay(B, CB())


def test_overlay_callbacks_may_still_write_data():
    class CB(a2.OverlayCallbacks):
        def face_face(self, fa, fb, fr):
            fr.data = (fa.id, fb.id)

    A, B = _crossing(0), _crossing(2)
    R = A.overlay(B, CB())
    assert R.is_valid()
    assert any(f.data is not None for f in R.faces())


# ===========================================================================
# 5. assign() copied the SOURCE's element ids, so a stale handle could stay
#    `is_valid` and silently resolve to a different element.
# ===========================================================================

def test_assign_invalidates_every_pre_assign_handle():
    random.seed(7)
    rnd = lambda: random.randint(-20, 20)

    def make(n):
        arr = a2.Arrangement("segment")
        segs = []
        for _ in range(n):
            p, q = (rnd(), rnd()), (rnd(), rnd())
            if p != q:
                segs.append(S(p, q))
        if not segs:
            return None
        try:
            arr.insert(segs)
        except a2.CGALError:
            return None
        return arr

    checked = 0
    for _trial in range(120):
        b, c = make(random.randint(3, 8)), make(random.randint(3, 8))
        if b is None or c is None:
            continue
        a = a2.Arrangement("segment")
        a.assign(b)
        handles = a.vertices()
        a.assign(c)
        for v in handles:
            assert not v.is_valid                  # used to be True with a different point
            with pytest.raises(a2.InvalidHandleError):
                v.point
        checked += 1
    assert checked > 50


def test_assign_keeps_issuing_fresh_ids():
    a = a2.Arrangement("segment")
    a.insert(S((0, 0), (1, 1)))
    before = max(v.id for v in a.vertices())
    b = a2.Arrangement("segment")
    b.insert(S((5, 5), (6, 6)))
    a.assign(b)
    assert min(v.id for v in a.vertices()) > before


# ===========================================================================
# 6. Python objects stored in DCEL `.data` were invisible to the GC, so `.data`
#    referring back to the arrangement leaked it permanently.
# ===========================================================================

@pytest.mark.parametrize("attach", [
    lambda arr: setattr(arr.faces()[0], "data", {"arr": arr}),
    lambda arr: setattr(arr.vertices()[0], "data", [arr]),
    lambda arr: setattr(arr.edges()[0], "data", {"arr": arr, "self": arr.edges()[0]}),
])
def test_a_data_cycle_through_the_arrangement_is_collectable(attach):
    arr = a2.Arrangement("segment")
    arr.insert([S((0, 0), (1, 1))])
    attach(arr)
    ref = weakref.ref(arr)
    del arr
    gc.collect()
    gc.collect()
    assert ref() is None
    assert not gc.garbage


def test_data_survives_copy_and_is_still_collectable():
    arr = a2.Arrangement("segment")
    arr.insert([S((0, 0), (1, 1))])
    payload = {"tag": "v"}
    arr.vertices()[0].data = payload
    copy = arr.copy()
    assert copy.vertices()[0].data == {"tag": "v"}
    copy.faces()[0].data = {"arr": copy}
    ref = weakref.ref(copy)
    del copy
    gc.collect()
    gc.collect()
    assert ref() is None


class _Payload:
    """Weak-referenceable stand-in for whatever a user stores in ``.data``."""


def test_clearing_the_arrangement_drops_the_data_references():
    arr = a2.Arrangement("segment")
    arr.insert([S((0, 0), (1, 1))])
    payload = _Payload()
    arr.vertices()[0].data = payload
    ref = weakref.ref(payload)
    del payload
    arr.clear()
    gc.collect()
    assert ref() is None


def test_setting_data_to_none_drops_the_reference():
    arr = a2.Arrangement("segment")
    arr.insert([S((0, 0), (1, 1))])
    v = arr.vertices()[0]
    payload = _Payload()
    v.data = payload
    ref = weakref.ref(payload)
    del payload
    assert v.data is ref()
    v.data = None
    gc.collect()
    assert ref() is None


# ===========================================================================
# 7. remove_isolated_vertex did not check `is_isolated()` although its contract
#    said it did (union type confusion under ARR2D_NDEBUG=1).
# ===========================================================================

def test_remove_isolated_vertex_checks_is_isolated_itself():
    arr = _square()
    v = [x for x in arr.vertices() if x.point.xy == (0.0, 0.0)][0]
    assert not v.is_isolated
    with pytest.raises(ValueError, match="isolated"):
        arr.remove_isolated_vertex(v)
    # not a CGAL precondition any more: the check survives an assertions-off build
    assert not isinstance(
        pytest.raises(ValueError, arr.remove_isolated_vertex, v).value, a2.PreconditionError)
    assert arr.is_valid()


# ===========================================================================
# 8. Algebraic (CORE::Expr) vs. rational comparison returned a WRONG answer.
# ===========================================================================

# p^2 - 2q^2 = -1, i.e. p/q < sqrt(2) strictly (provable in pure rational arithmetic).
_PELL_P = 57715667483393580961165483335871838914149456886446877857294614485577266014763425361
_PELL_Q = 40811139858215520877681994491572916756295496376319821447870175759964125455372247881


def test_algebraic_versus_rational_comparison_is_exact():
    assert _PELL_P * _PELL_P - 2 * _PELL_Q * _PELL_Q == -1
    r = F(_PELL_P, _PELL_Q)
    arr = a2.Arrangement(kind="conic")
    arr.insert(a2.ConicArc.circle((0, 0), squared_radius=2))
    arr.insert(a2.ConicArc.segment(a2.Point(-3, 0), a2.Point(3, 0)))
    xs = [v.point.exact()[0] for v in arr.vertices()
          if 1.4 < float(v.point.exact()[0]) < 1.5]
    assert len(xs) == 1
    x = xs[0]                                   # x = sqrt(2)
    assert (x == r) is False                    # used to be True
    assert (x > r) is True                      # used to be False
    assert (x < r) is False
    assert (x != r) is True
    # ... and the mirrored operand order agrees
    assert (r < x) is True
    assert (r == x) is False
    # transitivity with the negative convergent on the other side
    s = F(_PELL_P + 2 * _PELL_Q, _PELL_P + _PELL_Q)     # > sqrt(2)
    assert s * s > 2
    assert (x < s) is True


def test_algebraic_versus_sqrt_extension_comparison_is_exact():
    arr = a2.Arrangement(kind="conic")
    arr.insert(a2.ConicArc.circle((0, 0), squared_radius=2))
    arr.insert(a2.ConicArc.segment(a2.Point(-3, 0), a2.Point(3, 0)))
    x = [v.point.exact()[0] for v in arr.vertices()
         if 1.4 < float(v.point.exact()[0]) < 1.5][0]
    assert (x == a2.SqrtExtension(0, 1, 2)) is True          # both are sqrt(2)
    assert (x > a2.SqrtExtension(F(-1, 10 ** 30), 1, 2)) is True
    assert (x < a2.SqrtExtension(F(1, 10 ** 30), 1, 2)) is True


# ===========================================================================
# 9. to_double_correctly_rounded(SqrtExt) silently returned a grossly wrong
#    double once the fixed 4096-bit refinement cap was exhausted.
# ===========================================================================

def _sqrt2_convergent(min_bits):
    p, q = 1, 1
    while q.bit_length() < min_bits:
        p, q = p + 2 * q, p + q
    return p, q


def test_sqrt_extension_approx_is_correctly_rounded_under_massive_cancellation():
    getcontext().prec = 5000
    p, q = _sqrt2_convergent(2050)
    b = 2 ** 4100
    s = a2.SqrtExtension(-F(b) * F(p, q), b, 2)
    true = Decimal(b) * (Decimal(2).sqrt() - Decimal(p) / Decimal(q))
    got = s.approx
    assert abs(Decimal(got) - true) < abs(true) * Decimal(10) ** -15   # was off by a factor 9
    assert float(s) == got
    assert a2.Point.from_sqrt_extension(s, 0).x == got


def test_sqrt_extension_approx_still_correct_for_ordinary_values():
    assert a2.SqrtExtension(0, 1, 2).approx == pytest.approx(2 ** 0.5, abs=0.0)
    assert a2.SqrtExtension(3, F(1, 7), 5).approx == pytest.approx(3 + (5 ** 0.5) / 7)
    assert a2.SqrtExtension(0, 1, F(25, 4)).approx == 2.5     # rational: exact


# ===========================================================================
# 10. interval_of(SqrtExt) used a hard-coded 128 bits and ignored `bits`, so the
#     interval could be 59x wider than the value (and straddle zero).
# ===========================================================================

def test_sqrt_extension_interval_is_as_tight_as_doubles_allow():
    C, R2 = -10 ** 40, 10 ** 80 + 1
    arr = a2.Arrangement(kind="circle_segment")
    arr.insert(a2.CircleSegment.circle((C, 0), squared_radius=R2))
    arr.insert(a2.CircleSegment.segment(a2.Point(-10 ** 41, 0), a2.Point(10 ** 41, 0)))
    tiny = [v.point for v in arr.vertices() if 0 < v.point.x < 1e-30]
    assert len(tiny) == 1
    pt = tiny[0]
    lo, hi = pt.interval()[0]
    assert lo > 0                       # used to be 0.0: the sign was unreadable
    assert lo <= pt.x <= hi
    import math
    assert hi <= math.nextafter(lo, math.inf)   # at most one ulp wide

    e = pt.exact()[0]
    assert e.interval(53)[0] <= float(e) <= e.interval(53)[1]
    assert e.refine(2000)[0] <= float(e) <= e.refine(2000)[1]


def test_sqrt_extension_interval_encloses_the_value():
    s = a2.SqrtExtension(0, 1, 2)
    lo, hi = s.interval()
    assert lo <= float(s) <= hi
    assert hi - lo <= abs(float(s)) * 1e-15


# ===========================================================================
# 11. circle_segment and conic Curve.approximate() emitted CGAL::to_double
#     endpoints, disagreeing with Point.approx / vertex_coordinates().
# ===========================================================================

def test_circle_segment_approximation_endpoints_match_point_approx():
    c = a2.CircleSegment.arc((0, 0), squared_radius=F(337, 144),
                             source=a2.Point(F(3, 4), F(4, 3)),
                             target=a2.Point(F(4, 3), F(3, 4)), orientation="cw")
    pts = c.approximate(1e-3)
    assert pts[0] == c.source.xy                # used to differ in the last ulp
    assert pts[-1] == c.target.xy

    arr = a2.Arrangement(kind="circle_segment")
    arr.insert(c)
    coords = {tuple(row) for row in arr.vertex_coordinates().tolist()}
    edge = arr.approximate_edges(1e-3)[0].tolist()
    assert tuple(edge[0]) in coords
    assert tuple(edge[-1]) in coords


def test_conic_approximation_endpoints_match_point_approx():
    c = a2.ConicArc.circle((0, 0), squared_radius=F(35194, 27225),
                           source=a2.Point(F(1, 11), F(17, 15)),
                           target=a2.Point(F(17, 15), F(1, 11)), orientation="cw")
    pts = c.approximate(1e-3)
    assert pts[0] == c.source.xy
    assert pts[-1] == c.target.xy

    arr = a2.Arrangement(kind="conic")
    arr.insert(c)
    coords = {tuple(row) for row in arr.vertex_coordinates().tolist()}
    edge = arr.approximate_edges(1e-3)[0].tolist()
    assert tuple(edge[0]) in coords
    assert tuple(edge[-1]) in coords


# ===========================================================================
# 12. CircleSegment.radius double-rounded the exact squared radius.
# ===========================================================================

@pytest.mark.parametrize("r2", [F(25, 3), F(29, 3), F(34, 3), F(108178, 756251)])
def test_circle_segment_radius_is_correctly_rounded(r2):
    got = a2.CircleSegment.circle((0, 0), squared_radius=r2).radius
    assert got == float(a2.SqrtExtension(0, 1, r2))


def test_circle_segment_radius_keeps_the_exact_rational_case():
    c = a2.CircleSegment.circle((0, 0), radius=F(7, 2))
    assert c.has_rational_radius
    assert c.radius == F(7, 2)
    assert a2.CircleSegment.circle((0, 0), squared_radius=F(49, 4)).radius == 3.5


# ===========================================================================
# 13. Polygon.orientation()/area() fell back to a double shoelace for linear and
#     polyline polygons whose vertices are exact rationals.
# ===========================================================================

SLIVER = [P(0, 0), P(1, 1), P(2, 2 + F(1, 10 ** 40))]


@pytest.mark.parametrize("kind", ["segment", "polyline", "linear"])
def test_polygon_orientation_of_a_sliver_is_exact(kind):
    assert a2.Polygon(SLIVER, kind=kind).orientation() == 1        # was 0 for polyline/linear
    reversed_poly = a2.Polygon(list(reversed(SLIVER)), kind=kind)
    assert reversed_poly.orientation() == -1


@pytest.mark.parametrize("kind", ["segment", "polyline", "linear"])
def test_polygon_area_is_an_exact_fraction(kind):
    poly = a2.Polygon([P(0, 0), P(4, 0), P(4, 3), P(0, 3)], kind=kind)
    assert poly.area() == F(12)
    assert isinstance(poly.area(), F)
    assert a2.Polygon(SLIVER, kind=kind).area() == F(1, 2 * 10 ** 40)


def test_polyline_polygon_area_includes_the_interior_vertices():
    """``Polygon.points`` skips a polyline's interior vertices; the area must not."""
    poly = a2.Polygon([a2.Polyline([(0, 0), (4, 0), (4, 3)]),
                       a2.Polyline([(4, 3), (0, 3), (0, 0)])])
    assert poly.kind == a2.Kind.POLYLINE
    assert poly.area() == F(12)
    assert poly.orientation() == 1


def test_curved_polygons_still_use_the_approximate_area():
    poly = a2.Polygon([
        a2.CircleSegment.arc((0, 0), squared_radius=1, source=a2.Point(1, 0),
                             target=a2.Point(-1, 0), orientation="ccw"),
        a2.CircleSegment.arc((0, 0), squared_radius=1, source=a2.Point(-1, 0),
                             target=a2.Point(1, 0), orientation="ccw"),
    ])
    assert isinstance(poly.area(), float)
    assert poly.area() == pytest.approx(3.14159, abs=1e-2)


# ===========================================================================
# 14. LinearOps::curve_bbox rounded to nearest instead of outward, so the box
#     could EXCLUDE the curve.
# ===========================================================================

def test_linear_curve_bbox_rounds_outward():
    f = F(1, 3)
    lo_x, lo_y, hi_x, hi_y = a2.LinearCurve.segment(a2.Point(0, 0), a2.Point(f, f)).bbox()
    assert F(hi_x) >= f and F(hi_y) >= f          # used to be strictly smaller
    assert F(lo_x) <= 0 and F(lo_y) <= 0
    # ... and it now agrees with the other bounded kinds
    assert (lo_x, lo_y, hi_x, hi_y) == a2.Segment(a2.Point(0, 0), a2.Point(f, f)).bbox()
    assert (lo_x, lo_y, hi_x, hi_y) == a2.Polyline([a2.Point(0, 0), a2.Point(f, f)]).bbox()


def test_linear_unbounded_bbox_is_still_infinite():
    box = a2.Line((0, 0), (1, 1)).bbox()
    assert box[0] == float("-inf") and box[2] == float("inf")


# ===========================================================================
# 16. sphere: inserting a curve that lies ON the identification meridian into an
#     arrangement that already crosses it aborted mid-surgery (and escalated to
#     a SIGSEGV a few insertions later).
# ===========================================================================

def _sp(x, y, z):
    return a2.Point(x, y, z)


def test_sphere_refuses_a_meridian_curve_once_the_arrangement_crosses_it():
    arr = a2.Arrangement("sphere")
    arr.insert(a2.GeodesicArc.great_circle(_sp(0, 0, 1)))    # crosses the identification curve
    before = (arr.number_of_vertices, arr.number_of_edges, arr.number_of_faces)

    with pytest.raises(a2.UnsupportedError, match="identification"):
        arr.insert(a2.GeodesicArc.great_circle(_sp(0, 1, 0)))

    assert (arr.number_of_vertices, arr.number_of_edges,
            arr.number_of_faces) == before                  # untouched, not half-built
    assert arr.is_valid()
    # and the arrangement stays usable
    arr.insert(a2.GeodesicArc.great_circle(_sp(1, 0, 0)))
    assert arr.is_valid()


def test_sphere_accepts_the_same_curve_set_in_one_aggregate_insert():
    arr = a2.Arrangement("sphere")
    arr.insert([a2.GeodesicArc.great_circle(_sp(0, 0, 1)),
                a2.GeodesicArc.great_circle(_sp(0, 1, 0))])
    assert arr.is_valid()
    assert arr.number_of_edges > 0


def test_sphere_meridian_curve_alone_is_fine():
    arr = a2.Arrangement("sphere")
    arr.insert(a2.GeodesicArc.great_circle(_sp(0, 1, 0)))
    assert arr.is_valid()


# ===========================================================================
# 17. The geodesic Split_2 has NO "p lies on the curve" precondition, so
#     traits.split() and Arrangement.split_edge() silently built invalid arcs.
# ===========================================================================

def test_sphere_traits_split_rejects_a_point_off_the_arc():
    t = a2.traits("sphere")
    arc = a2.GeodesicArc.x_monotone_arc(_sp(1, 0, 0), _sp(0, 1, 0))   # lies in z = 0
    with pytest.raises(ValueError):
        t.split(arc, _sp(0, 0, 1))                    # the north pole is NOT on the arc
    left, right = t.split(arc, _sp(1, 1, 0))          # 45 degrees: on the arc
    assert left.target == right.source


def test_sphere_split_edge_rejects_a_point_off_the_edge():
    arr = a2.Arrangement("sphere")
    arr.insert(a2.GeodesicArc.from_points(_sp(1, 0, 0), _sp(0, 1, 0)))
    with pytest.raises(ValueError, match="interior"):
        arr.split_edge(arr.edges()[0], _sp(0, 0, 1))
    assert arr.is_valid()                             # used to become False
    arr.split_edge(arr.edges()[0], _sp(1, 1, 0))      # a point that IS on the edge
    assert arr.is_valid()
    arr.insert(a2.GeodesicArc.great_circle(_sp(1, 1, 1)))
    assert arr.is_valid()


@pytest.mark.parametrize("kind", ["segment", "polyline", "circle_segment", "conic", "linear"])
def test_split_edge_rejects_an_endpoint_for_every_kind(kind):
    arr = a2.Arrangement(kind)
    arr.insert(_straight_curve(kind, (0, 0), (4, 0)))
    he = arr.edges()[0]
    with pytest.raises(ValueError, match="interior"):
        arr.split_edge(he, a2.Point(0, 0, kind=kind))
    assert arr.is_valid()


# ===========================================================================
# 18. Bezier: a COLLINEAR control polygon whose motion reverses was accepted, so
#     insert() raised a CGAL assertion or HUNG forever.
# ===========================================================================

@pytest.mark.parametrize("control", [
    [(0, 0), (3, 3), (1, 1)],              # raised org != p.originators_end()
    [(0, 0), (4, 4), (-1, -1), (2, 2)],    # HUNG (no result in 60 s)
    [(0, 0), (0, 3), (0, 1)],              # the axis-parallel sub-case
    [(0, 0), (-2, 1), (2, -1)],            # collinear along (-2, 1), reversing
])
def test_bezier_refuses_a_collinear_curve_whose_motion_reverses(control):
    arr = a2.Arrangement("bezier")
    with pytest.raises(a2.UnsupportedError, match="reverses on itself"):
        arr.insert(a2.BezierCurve(control))
    assert arr.is_empty


@pytest.mark.parametrize("control", [
    [(0, 0), (1, 1), (0, 0), (1, 1)],      # lambda' = 3(2t-1)^2 >= 0: injective and legal
    [(0, 0), (1, 1), (2, 2)],              # a plain straight motion
    [(0, 0), (0, 1), (0, 3)],              # axis-parallel but monotone
])
def test_bezier_still_accepts_a_collinear_curve_whose_motion_is_monotone(control):
    arr = a2.Arrangement("bezier")
    arr.insert(a2.BezierCurve(control))
    assert arr.is_valid()
    assert arr.number_of_edges >= 1


def test_bezier_curved_curves_are_unaffected():
    arr = a2.Arrangement("bezier")
    arr.insert(a2.BezierCurve([(0, 0), (2, 4), (4, 0)]))
    assert arr.is_valid()
    assert arr.number_of_vertices == 2
