"""Kind-independent :class:`~arrangement_2d.Arrangement` API, exercised on the ``segment`` kind.

The ``segment`` kind is used throughout because it is the only kind whose geometry is
trivially hand-checkable, which lets every expected number below be *derived* (each one
carries the derivation in a comment) instead of being recorded from a run.  Everything
tested here is kind-independent machinery: handle identity and invalidation, ``.data``,
observers, overlay, point location, zone, vertical decomposition, the traits functor
facade, error translation and the ``repr`` formats.

Conventions used by this file
-----------------------------
* Only the public API is used (``import arrangement_2d as a2``); nothing reaches into
  ``arrangement_2d._core`` directly.
* Faces/halfedges are never compared across a mutation by identity unless the test is
  *about* that; ``Halfedge`` comparisons that only care about the edge use ``edge_id``
  (``locate`` may hand back either twin -- CGAL_TRAPS_CHECKLIST.md, "Point location").
* Every test builds its own arrangement (the ``square_arr`` fixture of ``conftest.py`` is
  function scoped and may be mutated freely).
"""

from __future__ import annotations

import copy as _copy
import gc
import os
import subprocess
import sys
import textwrap
import weakref

import pytest

a2 = pytest.importorskip("arrangement_2d")

S = a2.Segment
P = a2.Point


# ---------------------------------------------------------------------------
# local helpers / fixtures
# ---------------------------------------------------------------------------

#: The four edges of the axis-parallel square with corners (0,0) and (4,4).
SQUARE = (S((0, 0), (4, 0)), S((4, 0), (4, 4)), S((4, 4), (0, 4)), S((0, 4), (0, 0)))


@pytest.fixture
def square():
    """The bare square [0,4]^2: V=4 (the corners), E=4, F=2 (inside + unbounded)."""
    arr = a2.Arrangement("segment")
    arr.insert(list(SQUARE))
    return arr


def vertex_at(arr, x, y):
    """The (unique) vertex of *arr* at (x, y)."""
    return next(v for v in arr.vertices() if v.point == P(x, y))


def edge_with(arr, curve):
    """The (unique) edge of *arr* whose stored curve is *curve*."""
    return next(h for h in arr.edges() if h.curve == curve)


def record_all(arr):
    """Attach an observer recording ``(event_name, args)`` for *every* notification.

    Returns the ``log`` list.  The observer is built dynamically from
    :class:`a2.Observer`'s own method list, so it also records events this file does not
    name explicitly.
    """
    log = []
    names = [n for n in dir(a2.Observer) if n.startswith(("before_", "after_"))]

    def make(name):
        def method(self, *args):
            log.append((name, args))

        return method

    recorder = type("_Recorder", (a2.Observer,), {n: make(n) for n in names})
    arr.add_observer(recorder())
    return log


def names_of(log):
    """The event names of a log of ``(name, ...)`` tuples, in arrival order."""
    return [entry[0] for entry in log]


# ===========================================================================
# 1. handle identity, equality, hashing
# ===========================================================================

def test_handles_compare_and_hash_by_element_identity(square):
    """Two handle objects for the same DCEL element are equal and hash equally."""
    for first, second in ((square.vertices(), square.vertices()),
                          (square.edges(), square.edges()),
                          (square.faces(), square.faces()),
                          (square.curves(), square.curves())):
        for a, b in zip(first, second):
            assert a is not b                      # fresh Python wrappers every call
            assert a == b and not (a != b)
            assert hash(a) == hash(b)
            assert len({a, b}) == 1                # usable as dict/set keys


def test_distinct_elements_are_not_equal(square):
    """Different DCEL elements of the same arrangement never compare equal."""
    vs = square.vertices()
    assert len(set(vs)) == 4                       # the square has 4 distinct corners
    es = square.edges()
    assert len(set(es)) == 4
    assert len(set(square.halfedges())) == 8       # 2 halfedges per edge
    assert vs[0] != vs[1]
    assert es[0] != es[0].twin                     # a halfedge is not its twin


def test_handles_of_different_arrangements_are_never_equal(square):
    """Identity is per-arrangement: a copy's handles are distinct even at the same id."""
    other = square.copy()
    v, w = square.vertices()[0], other.vertices()[0]
    assert v.id == w.id                            # clone keeps the element ids
    assert v != w
    assert hash(v) != hash(w)
    assert v.arrangement is square and w.arrangement is other


def test_handle_equality_against_foreign_types_is_false(square):
    v, h, f, c = square.vertices()[0], square.edges()[0], square.faces()[0], square.curves()[0]
    for handle in (v, h, f, c):
        assert (handle == 42) is False
        assert (handle != 42) is True
        assert (handle == None) is False           # noqa: E711 - exercising __eq__
    # ... and different handle classes never compare equal to each other
    assert v != h and h != f and f != c


def test_halfedge_edge_id_is_shared_by_the_twin_pair(square):
    """``edge_id`` = min(id, twin.id); the 4 edges therefore give 4 distinct edge ids."""
    for h in square.halfedges():
        assert h.edge_id == min(h.id, h.twin.id)
        assert h.edge_id == h.twin.edge_id
    assert len({h.edge_id for h in square.halfedges()}) == 4


def test_handles_cannot_be_constructed_directly():
    for cls in (a2.Vertex, a2.Halfedge, a2.Face, a2.CurveHandle):
        with pytest.raises(TypeError):
            cls()


def test_arrangement_identity_equality_and_hash(square):
    other = square.copy()
    assert square == square and not (square != square)
    assert square != other                         # identity, not structural equality
    assert (square == 5) is False
    assert len({square, other}) == 2


# ===========================================================================
# 2. handle invalidation after every removal / merge
# ===========================================================================

def test_remove_edge_invalidates_the_halfedge_pair(square):
    """Removing one of the 4 square edges destroys both its halfedges."""
    e = edge_with(square, S((0, 0), (4, 0)))
    twin, src, tgt = e.twin, e.source, e.target
    square.remove_edge(e)
    assert e.is_valid is False and twin.is_valid is False
    # the two corners keep degree 1, so they are neither isolated nor redundant: they stay
    assert src.is_valid is True and tgt.is_valid is True
    assert src.degree == 1 and tgt.degree == 1
    for prop in ("curve", "source", "face", "direction"):
        with pytest.raises(a2.InvalidHandleError):
            getattr(e, prop)


def test_remove_edge_invalidates_the_face_that_is_merged_away(square):
    """Removing an edge of the square merges the inner face into the unbounded one."""
    inner = square.bounded_faces()[0]
    outer = square.unbounded_face
    merged = square.remove_edge(edge_with(square, S((0, 0), (4, 0))))
    assert merged == outer                         # the unbounded face is the survivor
    assert outer.is_valid is True
    assert inner.is_valid is False
    with pytest.raises(a2.InvalidHandleError):
        inner.number_of_inner_ccbs
    # V=4 E=3 F=1: one edge gone, the inner face gone
    assert (square.number_of_vertices, square.number_of_edges, square.number_of_faces) == (4, 3, 1)


def test_merge_edge_invalidates_the_second_edge_and_the_shared_vertex(square):
    """``merge_edge(e1, e2)`` keeps e1's halfedge pair and drops e2's."""
    bottom = edge_with(square, S((0, 0), (4, 0)))
    square.split_edge(bottom, P(2, 0))             # V=5 E=5
    mid = vertex_at(square, 2, 0)
    e1, e2 = (h.twin for h in mid.incident_halfedges())
    e1_twin, e2_twin = e1.twin, e2.twin            # grabbed while both edges still exist
    merged = square.merge_edge(e1, e2)

    assert mid.is_valid is False
    with pytest.raises(a2.InvalidHandleError):
        mid.point
    assert e2.is_valid is False and e2_twin.is_valid is False
    with pytest.raises(a2.InvalidHandleError):     # a stale handle refuses every accessor
        e2.twin
    # e1's record survives and now carries the merged curve; the returned halfedge is its twin
    assert e1.is_valid is True and e1_twin.is_valid is True
    assert e1.curve == S((0, 0), (4, 0))
    assert merged == e1_twin
    assert (square.number_of_vertices, square.number_of_edges) == (4, 4)


def test_split_edge_keeps_the_original_halfedge_valid(square):
    """A split re-uses the original halfedge record for the piece next to its source."""
    bottom = edge_with(square, S((0, 0), (4, 0)))
    original_id = bottom.id
    returned = square.split_edge(bottom, P(1, 0))
    assert bottom.is_valid is True
    assert returned == bottom and returned.id == original_id
    # `bottom` is stored right-to-left, so its source is (4,0): it keeps the (1,0)-(4,0) half
    assert bottom.curve == S((1, 0), (4, 0))
    assert bottom.target.point == P(1, 0)
    assert (square.number_of_vertices, square.number_of_edges) == (5, 5)


def test_remove_vertex_invalidates_it_and_the_absorbed_edge():
    """Two collinear edges meeting at a degree-2 vertex merge when the vertex is removed."""
    arr = a2.Arrangement("segment")
    arr.insert_non_intersecting_curves([S((0, 0), (2, 0)), S((2, 0), (4, 0))])
    mid = vertex_at(arr, 2, 0)
    incident = mid.incident_halfedges()             # the 2 halfedges pointing at (2,0)
    assert arr.remove_vertex(mid) is True
    assert mid.is_valid is False
    # exactly one of the two incident halfedge records survives (it becomes the merged edge)
    assert sorted(h.is_valid for h in incident) == [False, True]
    assert (arr.number_of_vertices, arr.number_of_edges) == (2, 1)
    assert arr.edges()[0].curve == S((0, 0), (4, 0))


def test_remove_vertex_refuses_a_corner_and_keeps_the_handle_valid(square):
    """A degree-2 corner whose two edges are not mergeable is kept: remove_vertex -> False."""
    corner = vertex_at(square, 0, 0)
    assert square.remove_vertex(corner) is False
    assert corner.is_valid is True
    assert square.number_of_vertices == 4


def test_remove_isolated_vertex_invalidates_it(square):
    face = square.bounded_faces()[0]
    v = square.insert_point_in_face_interior((2, 2), face)
    assert v.is_isolated and face.number_of_isolated_vertices == 1
    assert square.remove_isolated_vertex(v) == face
    assert v.is_valid is False
    assert face.number_of_isolated_vertices == 0
    with pytest.raises(a2.InvalidHandleError):
        v.point


def test_remove_curve_invalidates_the_curve_handle():
    """Two overlapping collinear inputs induce 3 edges; removing one drops only its own."""
    arr = a2.Arrangement("segment")
    c1 = arr.insert(S((0, 0), (4, 4)))
    c2 = arr.insert(S((1, 1), (6, 6)))
    # vertices (0,0) (1,1) (4,4) (6,6) -> 4; edges 0-1, 1-4 (shared), 4-6 -> 3
    assert (arr.number_of_vertices, arr.number_of_edges, arr.number_of_curves) == (4, 3, 2)
    assert edge_with(arr, S((1, 1), (4, 4))).number_of_originating_curves == 2

    removed = arr.remove_curve(c1)
    assert removed == 1                             # only (0,0)-(1,1) was induced by c1 alone
    assert c1.is_valid is False
    assert c2.is_valid is True
    with pytest.raises(a2.InvalidHandleError):
        c1.number_of_induced_edges
    assert (arr.number_of_vertices, arr.number_of_edges, arr.number_of_curves) == (3, 2, 1)


def test_clear_invalidates_every_handle(square):
    vs, es, fs, cs = square.vertices(), square.edges(), square.faces(), square.curves()
    unbounded = square.unbounded_face
    square.clear()
    assert square.is_empty and square.number_of_faces == 1
    for handle in list(vs) + list(es) + list(fs) + list(cs) + [unbounded]:
        assert handle.is_valid is False
    # a fresh unbounded face is created with a new id
    assert square.unbounded_face.id != unbounded.id
    assert square.unbounded_face.is_valid is True


def test_assign_invalidates_every_handle(square):
    vs = square.vertices()
    replacement = a2.Arrangement("segment")
    replacement.insert(list(SQUARE))
    square.assign(replacement)
    assert all(v.is_valid is False for v in vs)
    assert (square.number_of_vertices, square.number_of_edges) == (4, 4)
    assert square.is_valid()


def test_invalid_handles_repr_without_raising(square):
    v, h, f, c = square.vertices()[0], square.edges()[0], square.faces()[0], square.curves()[0]
    ids = (v.id, h.id, f.id, c.id)
    square.clear()
    assert repr(v) == "Vertex(<invalid>, id=%d)" % ids[0]
    assert repr(h) == "Halfedge(<invalid>, id=%d)" % ids[1]
    assert repr(f) == "Face(<invalid>, id=%d)" % ids[2]
    assert repr(c) == "CurveHandle(<invalid>, id=%d)" % ids[3]


# ===========================================================================
# 3. element .data
# ===========================================================================

def test_data_roundtrip_on_vertices_halfedges_and_faces(square):
    payloads = {}
    for i, v in enumerate(square.vertices()):
        v.data = payloads.setdefault(("v", i), ["vertex", i])
        assert v.data is payloads[("v", i)]
    for i, h in enumerate(square.halfedges()):
        h.data = payloads.setdefault(("h", i), ["halfedge", i])
    for i, f in enumerate(square.faces()):
        f.data = payloads.setdefault(("f", i), ["face", i])
    # re-fetching the elements gives the very same objects back
    assert [v.data for v in square.vertices()] == [payloads[("v", i)] for i in range(4)]
    assert [h.data for h in square.halfedges()] == [payloads[("h", i)] for i in range(8)]
    assert [f.data for f in square.faces()] == [payloads[("f", i)] for i in range(2)]


def test_data_defaults_to_none_and_can_be_cleared(square):
    v = square.vertices()[0]
    assert v.data is None
    v.data = "x"
    assert v.data == "x"
    v.data = None
    assert v.data is None


def test_halfedge_data_is_independent_of_the_twin(square):
    h = square.edges()[0]
    h.data = "left"
    h.twin.data = "right"
    assert h.data == "left" and h.twin.data == "right"
    assert square.edges()[0].data == "left"


def test_copy_shares_the_data_objects(square):
    marker = {"shared": True}
    for f in square.faces():
        f.data = marker
    for v in square.vertices():
        v.data = marker
    clone = square.copy()
    # copy() duplicates the DCEL but only increfs the payloads (documented in copy())
    assert all(f.data is marker for f in clone.faces())
    assert all(v.data is marker for v in clone.vertices())


def test_copy_data_slots_are_independent(square):
    square.vertices()[0].data = "original"
    clone = square.copy()
    clone.vertices()[0].data = "clone"
    assert square.vertices()[0].data == "original"
    assert clone.vertices()[0].data == "clone"


def test_data_survives_dropping_the_python_handle(square):
    payload = [1, 2, 3]
    square.faces()[0].data = payload             # the handle is dropped immediately
    del payload
    assert square.faces()[0].data == [1, 2, 3]   # the arrangement owns a reference


def test_data_on_a_stale_handle_raises(square):
    v = square.vertices()[0]
    square.clear()
    with pytest.raises(a2.InvalidHandleError):
        v.data
    with pytest.raises(a2.InvalidHandleError):
        v.data = "x"


def test_data_survives_a_split_on_the_original_halfedge(square):
    """``after_split_edge(e1, e2)``: e1 is the original record and keeps its data."""
    bottom = edge_with(square, S((0, 0), (4, 0)))
    bottom.data = "bottom"
    bottom.twin.data = "bottom-twin"
    square.split_edge(bottom, P(1, 0))
    assert bottom.data == "bottom" and bottom.twin.data == "bottom-twin"
    new_edge = edge_with(square, S((0, 0), (1, 0)))
    assert new_edge.data is None and new_edge.twin.data is None


# ===========================================================================
# 4. observers
# ===========================================================================

def test_add_remove_and_list_observers(square):
    obs = a2.Observer()
    assert square.add_observer(obs) is obs
    assert square.observers() == [obs]
    with pytest.raises(ValueError):
        square.add_observer(obs)                   # already attached
    square.remove_observer(obs)
    assert square.observers() == []
    with pytest.raises(ValueError):
        square.remove_observer(obs)                # not attached any more
    with pytest.raises(TypeError):
        square.add_observer(None)


def test_only_overridden_observer_methods_are_dispatched():
    """A plain :class:`a2.Observer` overrides nothing, so nothing is ever called."""
    calls = []

    class Silent(a2.Observer):
        pass

    class Loud(a2.Observer):
        def after_create_vertex(self, v):
            calls.append(v.id)

    arr = a2.Arrangement("segment")
    arr.add_observer(Silent())
    arr.add_observer(Loud())
    arr.insert(S((0, 0), (1, 1)))
    assert len(calls) == 2                         # exactly the 2 endpoints


def test_duck_typed_observer_is_accepted():
    """``add_observer`` takes "any object with matching methods", not only subclasses."""
    seen = []

    class Duck:
        def after_create_vertex(self, v):
            seen.append(v.point)

    arr = a2.Arrangement("segment")
    duck = Duck()
    arr.add_observer(duck)
    arr.insert(S((0, 0), (1, 1)))
    assert seen == [P(0, 0), P(1, 1)]
    assert arr.observers() == [duck]


def test_observer_sequence_for_a_single_curve_insertion():
    """Inserting one segment into an empty arrangement: 2 vertices, 1 edge, 1 new hole."""
    arr = a2.Arrangement("segment")
    log = record_all(arr)
    arr.insert(S((0, 0), (4, 0)))

    assert names_of(log) == [
        "before_global_change",
        "before_create_vertex", "after_create_vertex",     # (0,0)
        "before_create_vertex", "after_create_vertex",     # (4,0)
        "before_create_edge", "after_create_edge",
        # a dangling edge inside the unbounded face becomes a new inner CCB of that face
        "before_add_inner_ccb", "after_add_inner_ccb",
        "after_global_change",
    ]
    events = dict(log)
    assert events["before_create_vertex"] == (P(4, 0),)    # last call wins in the dict
    curve, v1, v2 = events["before_create_edge"]
    assert curve == S((0, 0), (4, 0))
    assert {v1.point, v2.point} == {P(0, 0), P(4, 0)}
    (he,) = events["after_create_edge"]
    assert he.curve == S((0, 0), (4, 0)) and he.is_valid
    assert events["after_add_inner_ccb"][0].edge_id == he.edge_id


def test_observer_aggregate_insertion_is_bracketed_by_one_global_change_pair():
    """The whole sweep of an aggregate insert is wrapped in exactly one global-change pair."""
    arr = a2.Arrangement("segment")
    log = record_all(arr)
    arr.insert(list(SQUARE))
    names = names_of(log)
    assert names[0] == "before_global_change"
    assert names[-1] == "after_global_change"
    assert names.count("before_global_change") == 1
    assert names.count("after_global_change") == 1
    # 4 corners created, 4 edges created (V=4 E=4 for the square)
    assert names.count("after_create_vertex") == 4
    assert names.count("after_create_edge") == 4


def test_observer_sequence_for_split_edge(square):
    log = record_all(square)
    bottom = edge_with(square, S((0, 0), (4, 0)))   # stored right-to-left: source (4,0)
    square.split_edge(bottom, P(1, 0))

    assert names_of(log) == [
        "before_create_vertex", "after_create_vertex",
        "before_split_edge", "after_split_edge",
    ]
    events = dict(log)
    assert events["before_create_vertex"] == (P(1, 0),)
    e, v, c1, c2 = events["before_split_edge"]
    assert e == bottom and v.point == P(1, 0)
    # c1 is the piece on the source side of `e` (source is (4,0)), c2 the target side
    assert c1 == S((1, 0), (4, 0)) and c2 == S((0, 0), (1, 0))
    e1, e2 = events["after_split_edge"]
    assert e1 == bottom and e1.curve == c1          # the original record keeps c1
    assert e2 != bottom and e2.curve == c2
    assert e1.is_valid and e2.is_valid


def test_observer_sequence_for_merge_edge(square):
    """The handles of a ``before_*`` notification are inspected *inside* the callback.

    Both arguments of ``before_merge_edge`` are live while the notification runs, but one
    of the two edges is gone by the time ``merge_edge`` returns, so anything derived from
    it (``edge_id`` walks to the twin!) has to be read in the callback.
    """
    bottom = edge_with(square, S((0, 0), (4, 0)))
    square.split_edge(bottom, P(2, 0))
    mid = vertex_at(square, 2, 0)
    e1, e2 = (h.twin for h in mid.incident_halfedges())
    edge_ids = {e1.edge_id, e2.edge_id}            # read while both edges still exist
    log = []

    class Obs(a2.Observer):
        def before_merge_edge(self, a, b, curve):
            log.append(("before_merge_edge", {a.edge_id, b.edge_id},
                        a.is_valid and b.is_valid, curve))

        def before_remove_vertex(self, v):
            # `v.degree` / `v.incident_halfedges()` are NOT readable here: CGAL has already
            # re-linked he1 onto he3's target (Arrangement_on_surface_2_impl.h:1721) before
            # it notifies, so the halfedge ring around v is half-dismantled.
            with pytest.raises(a2.UnsupportedError):
                v.degree
            log.append(("before_remove_vertex", v.point, v.is_valid, v.is_isolated))

        def after_remove_vertex(self):
            log.append(("after_remove_vertex",))

        def after_merge_edge(self, e):
            log.append(("after_merge_edge", e.id, e.is_valid, e.curve))

    square.add_observer(Obs())
    merged = square.merge_edge(e1, e2)

    # merging removes the shared degree-2 vertex in the middle of the notification pair
    assert names_of(log) == ["before_merge_edge", "before_remove_vertex",
                             "after_remove_vertex", "after_merge_edge"]
    assert log[0][1] == edge_ids                   # the two edges named by the notification
    assert log[0][2] is True                       # both edges are live inside the callback
    assert log[0][3] == S((0, 0), (4, 0))          # the merged curve is announced up front
    assert log[1][1:] == (P(2, 0), True, False)    # the doomed vertex is still identifiable
    assert log[3][1:] == (merged.id, True, S((0, 0), (4, 0)))


def test_observer_sequence_for_remove_edge(square):
    inner = square.bounded_faces()[0]
    outer = square.unbounded_face
    e = edge_with(square, S((0, 0), (4, 0)))
    edge_id = e.edge_id
    log = []

    class Obs(a2.Observer):
        def before_remove_edge(self, he):
            # the halfedge is still live here, so its curve/edge_id can be read
            log.append(("before_remove_edge", he.edge_id, he.is_valid, he.curve))

        def before_merge_face(self, f1, f2, he):
            log.append(("before_merge_face", {f1, f2}, f1.is_valid and f2.is_valid,
                        he.edge_id))

        def after_merge_face(self, f):
            log.append(("after_merge_face", f, f.is_valid))

        def after_remove_edge(self):
            log.append(("after_remove_edge",))

    square.add_observer(Obs())
    square.remove_edge(e)

    assert names_of(log) == ["before_remove_edge", "before_merge_face",
                             "after_merge_face", "after_remove_edge"]
    assert log[0][1:] == (edge_id, True, S((0, 0), (4, 0)))
    # CGAL does not say which of f1/f2 survives (CGAL_TRAPS_CHECKLIST.md, "Observer traces")
    assert log[1][1] == {inner, outer}
    assert log[1][2] is True and log[1][3] == edge_id
    assert log[2][1] == outer and log[2][2] is True    # after_merge_face names the survivor
    assert inner.is_valid is False


def test_observer_sequence_for_face_split(square):
    """Adding the diagonal between two existing corners splits the inner face in two."""
    log = record_all(square)
    inner = square.bounded_faces()[0]
    square.insert_at_vertices(S((0, 0), (4, 4)), vertex_at(square, 0, 0), vertex_at(square, 4, 4))

    assert names_of(log) == [
        "before_create_edge",
        "before_split_face", "after_split_face",
        "after_create_edge",
    ]
    events = dict(log)
    curve, v1, v2 = events["before_create_edge"]
    assert curve == S((0, 0), (4, 4))
    assert {v1.point, v2.point} == {P(0, 0), P(4, 4)}
    f, he = events["before_split_face"]
    assert f == inner and f.is_valid
    assert he.curve == S((0, 0), (4, 4)) and he.is_valid
    old_f, new_f, is_hole = events["after_split_face"]
    assert old_f == inner and new_f != inner
    assert is_hole is False                       # both halves are ordinary bounded faces
    assert old_f.is_valid and new_f.is_valid
    assert old_f.number_of_outer_ccbs == 1 and new_f.number_of_outer_ccbs == 1
    assert square.number_of_faces == 3            # unbounded + 2 triangles


def test_observer_sees_clear_and_assign(square):
    log = record_all(square)
    square.clear()
    assert names_of(log) == ["before_clear", "after_clear"]

    log.clear()
    other = a2.Arrangement("segment")
    other.insert(list(SQUARE))
    square.assign(other)
    names = names_of(log)
    assert "before_assign" in names and "after_assign" in names
    assert names.index("before_assign") < names.index("after_assign")
    assert names.count("before_clear") == names.count("after_clear") >= 1


def test_observer_is_not_notified_after_removal(square):
    log = record_all(square)
    observer = square.observers()[0]
    square.insert(S((-1, 2), (5, 2)))
    assert log                                       # events did arrive
    square.remove_observer(observer)
    before = len(log)
    square.insert(S((-1, 3), (5, 3)))
    assert len(log) == before                        # detached: silence


def test_observer_exception_propagates_after_the_operation(square):
    """A raising notification does not abort the operation; the error surfaces afterwards."""
    class Boom(a2.Observer):
        def after_split_edge(self, e1, e2):
            raise RuntimeError("boom")

    square.add_observer(Boom())
    bottom = edge_with(square, S((0, 0), (4, 0)))
    with pytest.raises(RuntimeError, match="boom"):
        square.split_edge(bottom, P(2, 0))
    # the split itself completed (CGAL cannot unwind through the notification)
    assert (square.number_of_vertices, square.number_of_edges) == (5, 5)
    assert square.is_valid()


def test_observer_exception_from_a_removal_propagates(square):
    class Boom(a2.Observer):
        def before_remove_edge(self, e):
            raise KeyError("nope")

    square.add_observer(Boom())
    with pytest.raises(KeyError):
        square.remove_edge(square.edges()[0])
    assert square.number_of_edges == 3               # the removal still happened


def test_observer_exception_is_reported_only_once(square):
    """The recorded exception is consumed by the operation that re-raises it."""
    class BoomOnce(a2.Observer):
        def __init__(self):
            self.armed = True

        def after_create_vertex(self, v):
            if self.armed:
                self.armed = False
                raise ValueError("once")

    square.add_observer(BoomOnce())
    with pytest.raises(ValueError, match="once"):
        square.insert(S((10, 10), (12, 12)))
    # the pending slot was cleared, so the next operation is not poisoned by the old error
    square.insert(S((20, 20), (22, 22)))
    assert square.is_valid()


def test_before_split_face_halfedge_face_must_not_crash(tmp_path):
    """``Halfedge.face`` inside ``before_split_face`` currently SEGFAULTS.

    CGAL_TRAPS_CHECKLIST.md ("Observer traces") records the CGAL 6.1 trap *and* the code
    response it requires: "inside ``before_split_face(f,e)`` ``e->face()``/``e->twin()->face()``
    SEGFAULT -- block ``he_face`` for that halfedge (and twin) while dispatching that event
    to Python".  That block is not implemented (``ArrImpl::he_face`` dereferences
    unconditionally), so a Python observer that merely *looks* at ``e.face`` kills the
    interpreter.  The call is made in a subprocess so that this file can report the crash
    instead of taking pytest down with it.
    """
    script = tmp_path / "split_face_probe.py"
    script.write_text(textwrap.dedent(
        """
        import arrangement_2d as a2
        S = a2.Segment

        class Obs(a2.Observer):
            def before_split_face(self, f, e):
                try:
                    e.face
                except Exception as exc:
                    print("RAISED", type(exc).__name__)
                else:
                    print("RETURNED")

        arr = a2.Arrangement("segment")
        arr.insert([S((0, 0), (4, 0)), S((4, 0), (4, 4)),
                    S((4, 4), (0, 4)), S((0, 4), (0, 0))])
        arr.add_observer(Obs())
        vs = {v.point.xy: v for v in arr.vertices()}
        arr.insert_at_vertices(S((0, 0), (4, 4)), vs[(0.0, 0.0)], vs[(4.0, 4.0)])
        print("SURVIVED")
        """
    ))
    env = dict(os.environ)
    env["PYTHONPATH"] = os.path.dirname(os.path.dirname(os.path.abspath(a2.__file__)))
    proc = subprocess.run([sys.executable, str(script)], capture_output=True, text=True,
                          env=env, timeout=300)
    assert proc.returncode == 0, (
        "accessing Halfedge.face inside before_split_face crashed the interpreter "
        "(returncode %r); stdout=%r stderr=%r" % (proc.returncode, proc.stdout, proc.stderr[-400:])
    )
    assert "SURVIVED" in proc.stdout


# ===========================================================================
# 5. overlay
# ===========================================================================

def overlay_inputs():
    """Two arrangements engineered so that all ten OverlayTraits events fire.

    ``A`` is the square [0,4]^2.  ``B`` is built so that, with respect to A,

    * ``(0,0)``            is a vertex of both                     -> vertex_vertex   (1)
    * ``(3,-1)-(5,1)``     passes through A's corner ``(4,0)``     -> vertex_edge     (1)
    * ``(0,4)``, ``(4,4)`` sit in B's unbounded face               -> vertex_face     (2)
    * ``(1,0)``, ``(3,0)`` sit on A's bottom edge                  -> edge_vertex     (2)
    * B's 5 remaining vertices sit inside faces of A               -> face_vertex     (5)
    * ``(2,2)-(6,2)`` crosses A's right edge at ``(4,2)``          -> edge_edge_vertex(1)
    * ``(1,0)-(3,0)`` overlaps A's bottom edge                     -> edge_edge       (1)
    * A's 6 edge pieces run through faces of B                     -> edge_face       (6)
    * B's 5 edge pieces run through faces of A                     -> face_edge       (5)
    * the 2 result faces                                           -> face_face       (2)
    """
    A = a2.Arrangement("segment")
    A.insert(list(SQUARE))
    B = a2.Arrangement("segment")
    B.insert([S((1, 0), (3, 0)), S((0, 0), (0, -2)), S((2, 2), (6, 2)), S((3, -1), (5, 1))])
    return A, B


OVERLAY_EVENTS = ("vertex_vertex", "vertex_edge", "vertex_face", "edge_vertex", "face_vertex",
                  "edge_edge_vertex", "edge_edge", "edge_face", "face_edge", "face_face")


def collecting_callbacks(bucket):
    def make(name):
        def method(self, x, y, z):
            bucket.setdefault(name, []).append((x, y, z))

        return method

    return type("_Collect", (a2.OverlayCallbacks,), {n: make(n) for n in OVERLAY_EVENTS})()


def test_overlay_invokes_all_ten_callbacks():
    A, B = overlay_inputs()
    bucket = {}
    R = A.overlay(B, collecting_callbacks(bucket))
    assert R.is_valid() and R.kind == a2.Kind.SEGMENT
    counts = {name: len(bucket.get(name, ())) for name in OVERLAY_EVENTS}
    assert counts == {
        "vertex_vertex": 1, "vertex_edge": 1, "vertex_face": 2, "edge_vertex": 2,
        "face_vertex": 5, "edge_edge_vertex": 1, "edge_edge": 1, "edge_face": 6,
        "face_edge": 5, "face_face": 2,
    }


def test_overlay_callback_counts_match_the_result_size():
    """Every result feature is announced exactly once, by exactly one callback."""
    A, B = overlay_inputs()
    bucket = {}
    R = A.overlay(B, collecting_callbacks(bucket))
    n = {name: len(bucket.get(name, ())) for name in OVERLAY_EVENTS}
    vertex_events = ("vertex_vertex", "vertex_edge", "vertex_face", "edge_vertex",
                     "face_vertex", "edge_edge_vertex")
    assert sum(n[e] for e in vertex_events) == R.number_of_vertices == 12
    assert n["edge_edge"] + n["edge_face"] + n["face_edge"] == R.number_of_edges == 12
    assert n["face_face"] == R.number_of_faces == 2


def test_overlay_callback_handle_types_and_owners():
    A, B = overlay_inputs()
    bucket = {}
    R = A.overlay(B, collecting_callbacks(bucket))
    expected = {
        "vertex_vertex": (a2.Vertex, a2.Vertex, a2.Vertex),
        "vertex_edge": (a2.Vertex, a2.Halfedge, a2.Vertex),
        "vertex_face": (a2.Vertex, a2.Face, a2.Vertex),
        "edge_vertex": (a2.Halfedge, a2.Vertex, a2.Vertex),
        "face_vertex": (a2.Face, a2.Vertex, a2.Vertex),
        "edge_edge_vertex": (a2.Halfedge, a2.Halfedge, a2.Vertex),
        "edge_edge": (a2.Halfedge, a2.Halfedge, a2.Halfedge),
        "edge_face": (a2.Halfedge, a2.Face, a2.Halfedge),
        "face_edge": (a2.Face, a2.Halfedge, a2.Halfedge),
        "face_face": (a2.Face, a2.Face, a2.Face),
    }
    for name, (ta, tb, tr) in expected.items():
        for x, y, z in bucket[name]:
            assert isinstance(x, ta) and isinstance(y, tb) and isinstance(z, tr)
            # first handle into A, second into B, third into the freshly built result
            assert x.arrangement is A and y.arrangement is B and z.arrangement is R
            assert x.is_valid and y.is_valid and z.is_valid


def test_overlay_vertex_vertex_and_edge_edge_geometry():
    A, B = overlay_inputs()
    bucket = {}
    A.overlay(B, collecting_callbacks(bucket))
    (va, vb, vr), = bucket["vertex_vertex"]
    assert va.point == vb.point == vr.point == P(0, 0)
    (ea, eb, er), = bucket["edge_edge"]
    assert ea.curve == S((0, 0), (4, 0))            # A's whole bottom edge
    assert eb.curve == S((1, 0), (3, 0))            # B's overlapping piece
    assert er.curve == S((1, 0), (3, 0))            # the overlap itself
    (xa, xb, xr), = bucket["edge_edge_vertex"]
    assert xr.point == P(4, 2)                      # A's right edge x=4 meets B's y=2


def test_overlay_shorthand_hooks_set_data():
    A, B = overlay_inputs()
    R = A.overlay(B, on_vertex=lambda a, b: "V", on_edge=lambda a, b: "E",
                  on_face=lambda a, b: "F")
    assert {v.data for v in R.vertices()} == {"V"}
    assert {f.data for f in R.faces()} == {"F"}
    # only the halfedge reported by the overlay event is decorated, not its twin
    tagged = [h for h in R.halfedges() if h.data == "E"]
    assert len(tagged) == R.number_of_edges == 12


def test_overlay_hook_runs_after_the_explicit_callback():
    A, B = overlay_inputs()
    order = []

    class CB(a2.OverlayCallbacks):
        def face_face(self, fa, fb, fr):
            order.append("callback")
            fr.data = "from-callback"

    R = A.overlay(B, CB(), on_face=lambda fa, fb: order.append("hook") or "from-hook")
    assert order == ["callback", "hook", "callback", "hook"]    # 2 result faces
    assert {f.data for f in R.faces()} == {"from-hook"}          # the hook wins


def test_overlay_callbacks_can_be_a_dict():
    A, B = overlay_inputs()
    seen = []
    R = A.overlay(B, {"face_face": lambda fa, fb, fr: seen.append((fa, fb, fr))})
    assert len(seen) == 2 == R.number_of_faces


def test_overlay_callbacks_propagate_data_from_both_inputs():
    """The canonical use of the callbacks: label the result from the two inputs' ``.data``."""
    A, B = overlay_inputs()
    for f in A.faces():
        f.data = "A-inside" if not f.is_unbounded else "A-outside"
    for f in B.faces():
        f.data = "B-outside"                          # B has only its unbounded face
    for v in A.vertices():
        v.data = ("A", v.point.xy)

    class Labeller(a2.OverlayCallbacks):
        def face_face(self, fa, fb, fr):
            fr.data = (fa.data, fb.data)

        def vertex_vertex(self, va, vb, vr):
            fr_data = ("both", va.data)
            vr.data = fr_data

        def vertex_face(self, va, fb, vr):
            vr.data = ("from-A", va.data)

    R = A.overlay(B, Labeller())
    assert sorted(str(f.data) for f in R.faces()) == [
        "('A-inside', 'B-outside')", "('A-outside', 'B-outside')"]
    # the (0,0) corner exists in both inputs and keeps A's payload
    coincident = next(v for v in R.vertices() if v.point == P(0, 0))
    assert coincident.data == ("both", ("A", (0.0, 0.0)))
    # A's corners (0,4) and (4,4) are interior to B's unbounded face
    from_a = [v for v in R.vertices() if isinstance(v.data, tuple) and v.data[0] == "from-A"]
    assert sorted(v.point.xy for v in from_a) == [(0.0, 4.0), (4.0, 4.0)]


def test_overlay_default_copies_no_data():
    A, B = overlay_inputs()
    for f in A.faces():
        f.data = "A"
    R = A.overlay(B)
    assert all(f.data is None for f in R.faces())
    assert all(v.data is None for v in R.vertices())


def test_overlay_leaves_the_inputs_untouched():
    A, B = overlay_inputs()
    before = (A.number_of_vertices, A.number_of_edges, A.number_of_faces,
              B.number_of_vertices, B.number_of_edges, B.number_of_faces)
    A.overlay(B)
    after = (A.number_of_vertices, A.number_of_edges, A.number_of_faces,
             B.number_of_vertices, B.number_of_edges, B.number_of_faces)
    assert before == after
    assert A.is_valid() and B.is_valid()


def test_overlay_history_holds_both_input_curve_sets():
    A, B = overlay_inputs()
    R = A.overlay(B)
    assert R.number_of_curves == A.number_of_curves + B.number_of_curves == 8


def test_overlay_callback_exception_propagates():
    A, B = overlay_inputs()

    class Boom(a2.OverlayCallbacks):
        def face_face(self, fa, fb, fr):
            raise KeyError("overlay-boom")

    with pytest.raises(KeyError):
        A.overlay(B, Boom())
    # the inputs survive and a second overlay works
    assert A.is_valid() and B.is_valid()
    assert A.overlay(B).is_valid()


def test_overlay_argument_errors():
    A, B = overlay_inputs()
    with pytest.raises(TypeError):
        A.overlay(None)
    with pytest.raises(ValueError):
        A.overlay(A)                                 # an arrangement with itself
    with pytest.raises(a2.KindMismatchError):
        A.overlay(a2.Arrangement("polyline"))
    with pytest.raises(ValueError, match="unknown overlay callback"):
        A.overlay(B, {"no_such_event": lambda *args: None})
    with pytest.raises(TypeError, match="not callable"):
        A.overlay(B, {"face_face": 5})
    with pytest.raises(TypeError, match="on_face must be callable"):
        A.overlay(B, on_face=5)


# ===========================================================================
# 6. point location
# ===========================================================================

ALL_STRATEGIES = tuple(a2.point_location_strategies())
# `triangulation` is deliberately not offered: CGAL's Arr_triangulation_point_location
# silently returns the wrong face for faces with holes (CGAL_TRAPS_CHECKLIST.md).
USABLE_STRATEGIES = ("naive", "simple", "walk", "landmarks", "trapezoid")
# only these three support vertical ray shooting (point_location_and_decomposition.md)
RAY_STRATEGIES = ("simple", "walk", "trapezoid")


def test_point_location_strategy_names():
    assert ALL_STRATEGIES == ("naive", "simple", "walk", "landmarks", "trapezoid",
                              "triangulation")


def test_supports_point_location(square_arr):
    for name in USABLE_STRATEGIES:
        assert square_arr.supports_point_location(name) is True
    assert square_arr.supports_point_location("triangulation") is False
    assert square_arr.supports_point_location(None) is True      # the default always works


def test_attach_has_detach_point_location(square_arr):
    assert all(square_arr.has_point_location(n) is False for n in ALL_STRATEGIES)
    for name in USABLE_STRATEGIES:
        square_arr.attach_point_location(name)
        assert square_arr.has_point_location(name) is True
    # attaching several at once is fine (multi-attach is safe, per the traps checklist)
    assert all(square_arr.has_point_location(n) for n in USABLE_STRATEGIES)
    for name in USABLE_STRATEGIES:
        square_arr.detach_point_location(name)
        assert square_arr.has_point_location(name) is False
    square_arr.detach_point_location("walk")         # detaching twice is a no-op


def test_locate_agrees_across_every_usable_strategy(square_arr):
    """The fixture is the square [0,4]^2 cut by y=2, so (2,1) is in the lower half."""
    lower = square_arr.locate((2, 1))
    assert isinstance(lower, a2.Face) and not lower.is_unbounded
    for name in USABLE_STRATEGIES:
        assert square_arr.locate((2, 1), name) == lower
        assert square_arr.locate((2, 3), name) != lower
        assert square_arr.locate((10, 10), name).is_unbounded is True
        assert square_arr.locate((0, 0), name) == vertex_at(square_arr, 0, 0)
        on_edge = square_arr.locate((2, 2), name)
        assert isinstance(on_edge, a2.Halfedge)
        assert on_edge.curve == S((0, 2), (4, 2))


def test_attached_strategies_stay_correct_after_a_mutation(square_arr):
    for name in USABLE_STRATEGIES:
        square_arr.attach_point_location(name)
    # add a floating segment inside the lower face: V 8->10, E 9->10, F unchanged (3)
    square_arr.insert(S((1, 1), (3, 1)))
    assert (square_arr.number_of_vertices, square_arr.number_of_edges,
            square_arr.number_of_faces) == (10, 10, 3)
    assert square_arr.is_valid()
    upper = square_arr.locate((2, 3))
    for name in USABLE_STRATEGIES:
        assert square_arr.locate((2, 3), name) == upper
        assert square_arr.locate((2, 1), name) != upper


def test_ray_shooting_support_per_strategy(square_arr):
    for name in RAY_STRATEGIES + (None,):
        up = square_arr.ray_shoot_up((2, 1), name)
        down = square_arr.ray_shoot_down((2, 1), name)
        assert isinstance(up, a2.Halfedge) and up.curve == S((0, 2), (4, 2))
        assert isinstance(down, a2.Halfedge) and down.curve == S((0, 0), (4, 0))
    for name in ("naive", "landmarks", "triangulation"):
        with pytest.raises(a2.UnsupportedError):
            square_arr.ray_shoot_up((2, 1), name)
        with pytest.raises(a2.UnsupportedError):
            square_arr.ray_shoot_down((2, 1), name)


def test_ray_shooting_escapes_to_the_unbounded_face(square_arr):
    assert square_arr.ray_shoot_up((2, 10)).is_unbounded is True
    assert square_arr.ray_shoot_down((2, -10)).is_unbounded is True
    # straight below the corner (0,0): the ray hits that vertex first
    assert square_arr.ray_shoot_up((0, -1)) == vertex_at(square_arr, 0, 0)


def test_point_location_strategy_errors(square_arr):
    with pytest.raises(ValueError, match="unknown point-location strategy"):
        square_arr.locate((1, 1), "bogus")
    with pytest.raises(TypeError):
        square_arr.locate((1, 1), 3.5)
    with pytest.raises(a2.UnsupportedError):
        square_arr.locate((1, 1), "triangulation")
    with pytest.raises(a2.UnsupportedError):
        square_arr.attach_point_location("triangulation")
    with pytest.raises(ValueError):
        square_arr.attach_point_location(None)       # "default" is not attachable


# ===========================================================================
# 7. batched_locate
# ===========================================================================

def test_batched_locate_matches_locate_and_keeps_duplicates(square_arr):
    """CGAL sorts the queries internally; the binding restores the input order."""
    points = [(2, 1), (2, 3), (10, 10), (2, 1), (0, 0), (2, 2), (2, 1)]
    got = square_arr.batched_locate(points)
    assert len(got) == len(points)
    for point, result in zip(points, got):
        one = square_arr.locate(point)
        # a point-location query may hand back either twin, so edges compare by edge_id
        # (CGAL_TRAPS_CHECKLIST.md, "Point location")
        if isinstance(result, a2.Halfedge):
            assert isinstance(one, a2.Halfedge) and result.edge_id == one.edge_id
        else:
            assert result == one
    # the three duplicates of (2,1) all give the same lower face
    assert got[0] == got[3] == got[6]
    assert isinstance(got[0], a2.Face) and not got[0].is_unbounded
    assert isinstance(got[4], a2.Vertex) and got[4].point == P(0, 0)
    assert isinstance(got[5], a2.Halfedge)
    assert got[2].is_unbounded is True


def test_batched_locate_is_order_independent(square_arr):
    """Shuffling the queries permutes the answers the same way."""
    points = [(2, 1), (2, 3), (10, 10), (0, 0), (2, 2), (-3, 2), (4, 4)]
    straight = square_arr.batched_locate(points)
    order = [4, 0, 6, 2, 5, 1, 3]                    # a fixed, non-trivial permutation
    shuffled = square_arr.batched_locate([points[i] for i in order])
    assert shuffled == [straight[i] for i in order]


def test_batched_locate_accepts_points_and_tuples_and_empty(square_arr):
    assert square_arr.batched_locate([]) == []
    mixed = square_arr.batched_locate([P(2, 1), (2, 1)])
    assert mixed[0] == mixed[1]


# ===========================================================================
# 8. zone
# ===========================================================================

def test_zone_of_a_vertical_cut_is_ordered_along_the_curve(square_arr):
    """The fixture is [0,4]^2 with the chord y=2; x=2 from y=-1 to y=5 crosses it all."""
    z = square_arr.zone(S((2, -1), (2, 5)))
    # unbounded face -> bottom edge -> lower face -> chord -> upper face -> top edge -> unbounded
    assert len(z) == 7
    assert isinstance(z[0], a2.Face) and z[0].is_unbounded
    assert isinstance(z[-1], a2.Face) and z[-1].is_unbounded
    assert [type(x).__name__ for x in z] == ["Face", "Halfedge", "Face", "Halfedge",
                                             "Face", "Halfedge", "Face"]
    assert z[1].curve == S((0, 0), (4, 0))
    assert z[3].curve == S((0, 2), (4, 2))
    assert z[5].curve == S((4, 4), (0, 4))
    assert z[2] != z[4]                              # the lower and upper halves differ


def test_zone_reports_vertices_when_the_curve_runs_through_them(square):
    z = square.zone(S((0, 0), (2, 2)))
    # the curve starts at the corner (0,0) and ends inside the square
    assert [type(x).__name__ for x in z] == ["Vertex", "Face"]
    assert z[0].point == P(0, 0)
    assert z[1].is_unbounded is False


def test_zone_along_an_existing_edge(square):
    """A query overlapping an edge reports the edge, then the vertex, then the exterior."""
    z = square.zone(S((1, 0), (6, 0)))
    assert [type(x).__name__ for x in z] == ["Halfedge", "Vertex", "Face"]
    assert z[0].curve == S((0, 0), (4, 0))
    assert z[1].point == P(4, 0)
    assert z[2].is_unbounded is True


def test_zone_of_an_empty_arrangement_is_the_single_face():
    arr = a2.Arrangement("segment")
    z = arr.zone(S((0, 0), (1, 1)))
    assert len(z) == 1 and z[0] == arr.unbounded_face


def test_zone_does_not_modify_the_arrangement_and_matches_do_intersect(square_arr):
    before = (square_arr.number_of_vertices, square_arr.number_of_edges,
              square_arr.number_of_faces, square_arr.number_of_curves)
    for curve, expected in ((S((2, -1), (2, 5)), True),      # crosses everything
                            (S((1, 1), (3, 1)), False),      # strictly inside a face
                            (S((10, 10), (11, 11)), False)):  # far outside
        z = square_arr.zone(curve)
        assert square_arr.do_intersect(curve) is expected
        # do_intersect is true exactly when the zone contains a vertex or a halfedge
        assert any(isinstance(x, (a2.Vertex, a2.Halfedge)) for x in z) is expected
    after = (square_arr.number_of_vertices, square_arr.number_of_edges,
             square_arr.number_of_faces, square_arr.number_of_curves)
    assert before == after


# ===========================================================================
# 9. vertical decomposition
# ===========================================================================

def decomposition_example():
    """Two horizontal segments plus one isolated vertex between them.

    ``s1 = (0,0)-(4,0)``, ``s2 = (0,2)-(4,2)``, isolated vertex at ``(2,1)``.
    V = 5 (4 endpoints + the isolated one), E = 2, F = 1 (nothing is enclosed).
    """
    arr = a2.Arrangement("segment")
    arr.insert([S((0, 0), (4, 0)), S((0, 2), (4, 2))])
    arr.insert_point((2, 1))
    return arr


def test_decompose_returns_one_triple_per_vertex_in_lexicographic_order():
    arr = decomposition_example()
    assert (arr.number_of_vertices, arr.number_of_edges, arr.number_of_faces) == (5, 2, 1)
    entries = arr.decompose()
    assert len(entries) == 5
    keys = [(v.point.x, v.point.y) for v, _, _ in entries]
    assert keys == sorted(keys)
    assert keys == [(0.0, 0.0), (0.0, 2.0), (2.0, 1.0), (4.0, 0.0), (4.0, 2.0)]
    assert all(isinstance(v, a2.Vertex) for v, _, _ in entries)


def test_decompose_below_and_above_features():
    arr = decomposition_example()
    by_point = {(v.point.x, v.point.y): (below, above) for v, below, above in arr.decompose()}

    # (2,1) sits between the two segments: a vertical ray hits an edge in both directions
    below, above = by_point[(2.0, 1.0)]
    assert isinstance(below, a2.Halfedge) and below.curve == S((0, 0), (4, 0))
    assert isinstance(above, a2.Halfedge) and above.curve == S((0, 2), (4, 2))

    # (0,0) is straight below (0,2): the ray up hits that *vertex*, the ray down escapes
    below, above = by_point[(0.0, 0.0)]
    assert isinstance(below, a2.Face) and below.is_unbounded
    assert isinstance(above, a2.Vertex) and above.point == P(0, 2)

    below, above = by_point[(0.0, 2.0)]
    assert isinstance(below, a2.Vertex) and below.point == P(0, 0)
    assert isinstance(above, a2.Face) and above.is_unbounded

    # the same on the right-hand side
    assert by_point[(4.0, 0.0)][1].point == P(4, 2)
    assert by_point[(4.0, 2.0)][0].point == P(4, 0)


def test_decompose_reports_none_when_an_incident_vertical_edge_blocks_the_ray(square_arr):
    """CGAL reports nothing above/below a vertex incident to a vertical edge that way."""
    by_point = {(v.point.x, v.point.y): (b, a) for v, b, a in square_arr.decompose()}
    # (0,0) has the vertical edge (0,0)-(0,2) going up and open space below
    below, above = by_point[(0.0, 0.0)]
    assert isinstance(below, a2.Face) and below.is_unbounded
    assert above is None
    # (0,2) has vertical edges both up and down
    assert by_point[(0.0, 2.0)] == (None, None)
    # the chord end (-1,2) is outside the square: the unbounded face both ways
    below, above = by_point[(-1.0, 2.0)]
    assert below.is_unbounded and above.is_unbounded


# ===========================================================================
# 10. traits functor facade
# ===========================================================================

def test_traits_identity_kind_and_repr(square):
    t = square.traits
    assert isinstance(t, a2.Traits)
    assert t is a2.traits("segment")                 # the facade is cached per kind
    assert t is a2.traits(a2.Kind.SEGMENT)
    assert t.kind == a2.Kind.SEGMENT
    assert t.dimension == 2
    assert repr(t) == "Traits(kind='segment')"


def test_traits_point_predicates():
    t = a2.traits("segment")
    assert t.compare_x((0, 0), (1, 0)) == -1
    assert t.compare_x((1, 0), (1, 5)) == 0          # compare_x ignores y
    assert t.compare_x((2, 0), (1, 0)) == 1
    assert t.compare_xy((0, 0), (0, 1)) == -1        # lexicographic: y breaks the tie
    assert t.compare_xy((0, 1), (0, 1)) == 0
    assert t.compare_xy((0, 2), (0, 1)) == 1
    assert t.equal((1, 2), (1, 2)) is True
    assert t.equal((1, 2), (1, 3)) is False


def test_traits_compare_y_at_x_and_is_in_x_range():
    t = a2.traits("segment")
    base = S((0, 0), (4, 0))
    assert t.compare_y_at_x((2, 1), base) == 1       # above
    assert t.compare_y_at_x((2, 0), base) == 0       # on
    assert t.compare_y_at_x((2, -1), base) == -1     # below
    assert t.is_in_x_range(base, (2, 7)) is True     # x-range only
    assert t.is_in_x_range(base, (5, 0)) is False
    with pytest.raises(ValueError):                  # CGAL precondition: x must be in range
        t.compare_y_at_x((9, 0), base)


def test_traits_compare_y_at_x_left_and_right():
    t = a2.traits("segment")
    rising, falling = S((0, 0), (4, 4)), S((0, 4), (4, 0))
    # the two diagonals of [0,4]^2 cross at (2,2); left of it the rising one is lower
    assert t.compare_y_at_x_left(rising, falling, (2, 2)) == -1
    assert t.compare_y_at_x_right(rising, falling, (2, 2)) == 1


def test_traits_orientation_predicates():
    t = a2.traits("segment")
    assert t.is_vertical(S((1, 0), (1, 5))) is True
    assert t.is_vertical(S((0, 0), (4, 0))) is False
    assert t.compare_endpoints_xy(S((0, 0), (4, 0))) == -1     # directed right
    assert t.compare_endpoints_xy(S((4, 0), (0, 0))) == 1      # directed left
    assert t.curves_equal(S((0, 0), (1, 1)), S((1, 1), (0, 0))) is True   # direction-blind


def test_traits_intersect_at_a_point():
    t = a2.traits("segment")
    hits = t.intersect(S((0, 0), (4, 4)), S((0, 4), (4, 0)))
    assert len(hits) == 1
    point, multiplicity = hits[0]
    assert point == P(2, 2)                          # the diagonals of [0,4]^2 meet there
    assert multiplicity == 1                         # transversal crossing


def test_traits_intersect_with_an_overlap():
    t = a2.traits("segment")
    hits = t.intersect(S((0, 0), (4, 4)), S((1, 1), (6, 6)))
    assert len(hits) == 1
    overlap = hits[0]
    assert isinstance(overlap, a2.Curve) and not isinstance(overlap, tuple)
    assert overlap == S((1, 1), (4, 4))              # the shared piece
    assert t.intersect(S((0, 0), (1, 0)), S((0, 5), (1, 5))) == []


def test_traits_make_x_monotone_and_construct_x_monotone_curve():
    t = a2.traits("segment")
    pieces = t.make_x_monotone(S((0, 0), (4, 4)))
    assert pieces == [S((0, 0), (4, 4))]             # a segment is already x-monotone
    assert t.construct_x_monotone_curve((0, 0), (4, 4)) == S((0, 0), (4, 4))
    with pytest.raises(ValueError):
        t.construct_x_monotone_curve((1, 1), (1, 1))  # the two points must differ


def test_traits_split_and_merge():
    t = a2.traits("segment")
    left, right = t.split(S((0, 0), (4, 4)), (2, 2))
    assert left == S((0, 0), (2, 2)) and right == S((2, 2), (4, 4))
    assert t.are_mergeable(left, right) is True
    assert t.are_mergeable(S((0, 0), (2, 2)), S((3, 3), (4, 4))) is False
    assert t.merge(left, right) == S((0, 0), (4, 4))
    with pytest.raises(ValueError):
        t.merge(S((0, 0), (1, 1)), S((3, 3), (4, 4)))


def test_traits_trim_and_opposite():
    t = a2.traits("segment")
    assert t.trim(S((0, 0), (4, 4)), (1, 1), (3, 3)) == S((1, 1), (3, 3))
    assert t.opposite(S((0, 0), (4, 4))) == S((4, 4), (0, 0))
    assert t.compare_endpoints_xy(t.opposite(S((0, 0), (4, 4)))) == 1
    with pytest.raises(ValueError):
        t.trim(S((0, 0), (4, 4)), (1, 1), (1, 1))


def test_traits_min_and_max_vertex():
    t = a2.traits("segment")
    reversed_curve = S((4, 4), (0, 0))               # stored right-to-left
    assert t.min_vertex(reversed_curve) == P(0, 0)   # lexicographically smallest end
    assert t.max_vertex(reversed_curve) == P(4, 4)


def test_traits_parameter_space_and_approximation():
    t = a2.traits("segment")
    curve = S((0, 0), (4, 4))
    assert t.parameter_space_in_x(curve, "min") == "interior"    # bounded kind
    assert t.parameter_space_in_y(curve, "max") == "interior"
    assert t.approximate(curve) == [(0.0, 0.0), (4.0, 4.0)]      # endpoints, exactly
    # 1/3 rounds to the nearest double, not to CGAL's to_double() result
    assert t.approximate_point(P("1/3", 2), 0) == float.fromhex("0x1.5555555555555p-2")
    assert t.approximate_point(P("1/3", 2), 1) == 2.0


def test_traits_raises_a_precondition_error_from_cgal():
    """A CGAL precondition that arr2d does not pre-check propagates unchanged."""
    t = a2.traits("segment")
    with pytest.raises(a2.PreconditionError) as info:
        t.trim(S((0, 0), (4, 4)), (0, 0), (9, 9))    # outside the curve's x-range
    assert str(info.value).startswith("CGAL precondition violation:")


def test_traits_split_validates_the_point_itself():
    """REGRESSION: ``Split_2`` requires the point to be strictly inside the curve, and arr2d
    checks that for EVERY kind (the geodesic Split_2 has no such precondition at all), so the
    error is a plain ``ValueError`` rather than a CGAL ``PreconditionError``."""
    t = a2.traits("segment")
    with pytest.raises(ValueError, match="endpoint"):
        t.split(S((0, 0), (4, 4)), (0, 0))           # an endpoint, not an interior point
    with pytest.raises(ValueError, match="does not lie on the curve"):
        t.split(S((0, 0), (4, 4)), (1, 3))           # off the curve


def test_traits_rejects_bad_argument_types():
    t = a2.traits("segment")
    with pytest.raises(TypeError):
        t.compare_xy("nonsense", (0, 0))
    with pytest.raises(TypeError):
        t.compare_xy(S((0, 0), (1, 1)), (0, 0))      # a curve where a point is required
    with pytest.raises(TypeError, match="cannot interpret 'NoneType'"):
        t.compare_xy(None, (0, 0))
    with pytest.raises(ValueError, match="need 2 coordinates"):
        t.compare_x((1, 2, 3), (0, 0))               # 3 coordinates for a planar kind


# ===========================================================================
# 11. error translation
# ===========================================================================

def test_error_class_hierarchy():
    assert issubclass(a2.PreconditionError, (a2.CGALError, ValueError))
    assert issubclass(a2.InvalidHandleError, (a2.CGALError, ValueError))
    assert issubclass(a2.KindMismatchError, (a2.CGALError, TypeError))
    assert issubclass(a2.NotXMonotoneError, (a2.CGALError, ValueError))
    assert issubclass(a2.NotRepresentableError, (a2.CGALError, ValueError))
    assert issubclass(a2.UnsupportedError, (a2.CGALError, NotImplementedError))


@pytest.mark.parametrize("operation", [
    # modify_vertex requires the new point to be geometrically equal to the old one
    lambda arr: arr.modify_vertex(vertex_at(arr, 0, 0), (9, 9)),
    # modify_edge requires an equivalent curve
    lambda arr: arr.modify_edge(edge_with(arr, S((0, 0), (4, 0))), S((0, 0), (9, 9))),
    # insert_at_vertices needs the curve endpoints to *be* the two vertices
    lambda arr: arr.insert_at_vertices(S((0, 0), (4, 4)),
                                       vertex_at(arr, 0, 0), vertex_at(arr, 4, 0)),
    # insert_from_left_vertex needs the curve's left endpoint at the vertex
    lambda arr: arr.insert_from_left_vertex(S((1, 1), (3, 3)), vertex_at(arr, 0, 0)),
    # insert_non_intersecting: CGAL checks that neither endpoint lies on an existing edge
    lambda arr: arr.insert_non_intersecting(S((2, 0), (2, 3))),
])
def test_cgal_preconditions_become_precondition_errors(square, operation):
    with pytest.raises(a2.PreconditionError) as info:
        operation(square)
    assert str(info.value).startswith("CGAL precondition violation:")
    assert isinstance(info.value, ValueError)


@pytest.mark.parametrize("operation, message", [
    # split_edge needs a point strictly inside the edge -- checked by arr2d (the geodesic
    # Split_2 has no precondition at all and would corrupt the arrangement)
    (lambda arr: arr.split_edge(edge_with(arr, S((0, 0), (4, 0))), (0, 0)), "interior"),
    (lambda arr: arr.split_edge(edge_with(arr, S((0, 0), (4, 0))), (7, 7)), "interior"),
    # remove_isolated_vertex needs an isolated vertex -- checked by arr2d so that the check
    # also holds with CGAL's assertions compiled out
    (lambda arr: arr.remove_isolated_vertex(vertex_at(arr, 0, 0)), "isolated"),
])
def test_arr2d_preconditions_become_value_errors(square, operation, message):
    """These used to surface as CGAL ``PreconditionError``s; arr2d now checks them itself, so
    they are plain ``ValueError``s (``PreconditionError`` is a ``ValueError`` too, so code
    catching ``ValueError`` is unaffected)."""
    with pytest.raises(ValueError, match=message):
        operation(square)


def test_insert_non_intersecting_only_validates_the_endpoints(square):
    """Documented CGAL 6.1 behaviour: only the *endpoints* of the curve are checked.

    ``Arrangement_on_surface_2_global.h:685`` locates each endpoint and refuses it when it
    falls on an existing edge; the curve's *interior* is never tested (that would cost a
    full intersection query).  A curve that crosses existing edges is therefore inserted
    silently and leaves the arrangement structurally invalid -- which is why
    :meth:`Arrangement.is_valid` is the only way to detect the mistake.
    """
    # (-1,2)-(5,2) crosses the left and right edges of the square, but both of its
    # endpoints lie in the unbounded face, so no precondition fires.
    square.insert_non_intersecting(S((-1, 2), (5, 2)))
    assert square.is_valid() is False
    # V = 4 corners + the 2 new endpoints, E = 4 + 1: the crossings were not computed
    assert (square.number_of_vertices, square.number_of_edges) == (6, 5)


def test_kind_mismatch_errors(square):
    with pytest.raises(a2.KindMismatchError):
        square.overlay(a2.Arrangement("polyline"))
    with pytest.raises(a2.KindMismatchError):
        square.assign(a2.Arrangement("circle_segment"))
    # KindMismatchError is a TypeError, so ordinary `except TypeError` code keeps working
    with pytest.raises(TypeError):
        square.assign(a2.Arrangement("conic"))


def test_handles_of_a_foreign_arrangement_are_rejected(square):
    other = a2.Arrangement("segment")
    other.insert(list(SQUARE))
    with pytest.raises(a2.InvalidHandleError, match="different arrangement"):
        square.remove_vertex(other.vertices()[0])
    with pytest.raises(a2.InvalidHandleError, match="different arrangement"):
        square.remove_edge(other.edges()[0])
    with pytest.raises(a2.InvalidHandleError, match="different arrangement"):
        square.insert_point_in_face_interior((1, 1), other.faces()[0])
    with pytest.raises(a2.InvalidHandleError, match="different arrangement"):
        square.remove_curve(other.curves()[0])


def test_not_x_monotone_error():
    """Every ``segment`` curve is x-monotone, so the error is shown on a full circle.

    ``NotXMonotoneError`` is raised by the shared (kind-independent) x-monotone guard in
    the core, which is why it belongs in this file even though the ``segment`` kind can
    never trigger it.
    """
    circle = a2.CircleSegment.circle((0, 0), 2)
    assert circle.is_x_monotone is False
    arr = a2.Arrangement("circle_segment")
    with pytest.raises(a2.NotXMonotoneError):
        arr.insert_non_intersecting(circle)
    with pytest.raises(a2.NotXMonotoneError):
        arr.insert_in_face_interior(circle, arr.faces()[0])
    with pytest.raises(a2.NotXMonotoneError):
        a2.traits("circle_segment").split(circle, (2, 0))
    # ... and it is a ValueError, as documented
    with pytest.raises(ValueError):
        arr.insert_non_intersecting(circle)


def test_type_errors_for_bad_inputs(square):
    with pytest.raises(TypeError, match="expected a curve"):
        square.insert(42)
    with pytest.raises(TypeError, match="expected a curve"):
        square.insert("a string")
    with pytest.raises(TypeError, match="expected a Halfedge"):
        square.remove_edge(square.vertices()[0])
    with pytest.raises(TypeError, match="expected a Vertex"):
        square.remove_vertex(square.edges()[0])
    with pytest.raises(TypeError, match="expected a Face"):
        square.insert_point_in_face_interior((1, 1), square.vertices()[0])
    with pytest.raises(TypeError, match="expected a CurveHandle"):
        square.remove_curve(square.edges()[0])
    with pytest.raises(TypeError, match="expected a point"):
        square.locate("x")
    with pytest.raises(TypeError):
        square.overlay(None)


def test_value_errors_for_bad_kinds_and_shapes():
    with pytest.raises(ValueError, match="unknown geometry kind"):
        a2.Arrangement("no-such-kind")
    arr = a2.Arrangement("segment")
    with pytest.raises(ValueError, match="need 2 coordinates"):
        arr.locate((1, 2, 3))


def test_unsupported_errors_for_the_wrong_kind(square):
    with pytest.raises(a2.UnsupportedError, match="fictitious face"):
        square.fictitious_face                       # only the unbounded `linear` kind has one
    with pytest.raises(a2.UnsupportedError, match="spherical_face"):
        square.spherical_face
    with pytest.raises(a2.UnsupportedError):
        square.unbounded_face.polygon()              # no outer CCB -> not a bounded polygon
    assert isinstance(a2.UnsupportedError("x"), NotImplementedError)


def test_value_errors_from_face_and_vertex_accessors(square):
    with pytest.raises(ValueError, match="no outer CCB"):
        square.unbounded_face.outer_ccb()
    with pytest.raises(ValueError, match="isolated vertex"):
        square.vertices()[0].face                    # only isolated vertices have a `.face`


# ===========================================================================
# 12. repr / str
# ===========================================================================

def test_arrangement_repr_and_str(square):
    assert repr(square) == "Arrangement(kind='segment', vertices=4, edges=4, faces=2, curves=4)"
    assert str(square) == repr(square)
    empty = a2.Arrangement("segment")
    assert repr(empty) == "Arrangement(kind='segment', vertices=0, edges=0, faces=1, curves=0)"


def test_handle_repr_formats(square):
    v = vertex_at(square, 0, 0)
    assert repr(v) == "Vertex(id=%d, point=Point(0, 0), degree=2)" % v.id
    assert str(v) == repr(v)

    h = edge_with(square, S((0, 0), (4, 0)))
    assert repr(h) == "Halfedge(id=%d, curve=Segment((0, 0), (4, 0)), direction='%s')" % (
        h.id, h.direction)
    assert h.direction in ("left_to_right", "right_to_left")

    f = square.bounded_faces()[0]
    assert repr(f) == ("Face(id=%d, unbounded=False, outer_ccbs=1, holes=0, isolated=0)" % f.id)
    assert repr(square.unbounded_face) == (
        "Face(id=%d, unbounded=True, outer_ccbs=0, holes=1, isolated=0)"
        % square.unbounded_face.id)

    c = square.curves()[0]
    assert repr(c) == "CurveHandle(id=%d, curve=%r, induced_edges=1)" % (c.id, c.curve)


# ===========================================================================
# 13. copy / deepcopy / weakref
# ===========================================================================

def test_copy_duplicates_structure_and_history(square):
    square.faces()[0].data = "tag"
    clone = square.copy()
    assert clone is not square
    assert clone.kind == square.kind
    assert (clone.number_of_vertices, clone.number_of_edges, clone.number_of_faces,
            clone.number_of_curves) == (4, 4, 2, 4)
    assert clone.is_valid()
    # mutating the clone leaves the original alone
    clone.remove_edge(clone.edges()[0])
    assert clone.number_of_edges == 3 and square.number_of_edges == 4


def test_copy_module_functions_delegate_to_copy(square):
    for clone in (_copy.copy(square), _copy.deepcopy(square)):
        assert clone is not square
        assert (clone.number_of_vertices, clone.number_of_edges) == (4, 4)
        assert clone.is_valid()


def test_deepcopy_shares_element_data(square):
    """Documented: ``__deepcopy__`` duplicates the DCEL but *shares* the payloads."""
    payload = {"deep": True}
    for f in square.faces():
        f.data = payload
    clone = _copy.deepcopy(square)
    assert all(f.data is payload for f in clone.faces())


def test_copy_does_not_carry_observers_or_point_location(square):
    log = record_all(square)
    square.attach_point_location("walk")
    clone = square.copy()
    assert clone.observers() == []
    assert clone.has_point_location("walk") is False
    before = len(log)
    clone.insert(S((10, 10), (12, 12)))
    assert len(log) == before                        # the observer is not attached to the clone


def test_arrangement_supports_weak_references():
    """``Arrangement`` has a weakref slot, so observers can hold one without a cycle."""
    arr = a2.Arrangement("segment")
    arr.insert(list(SQUARE))
    ref = weakref.ref(arr)
    assert ref() is arr
    holder = []
    weakref.finalize(arr, holder.append, "gone")
    del arr
    gc.collect()
    assert ref() is None
    assert holder == ["gone"]


def test_dcel_handles_are_not_weak_referenceable(square):
    """Handles are lightweight ``(arrangement, pointer, id)`` records with no weakref slot."""
    for handle in (square.vertices()[0], square.edges()[0], square.faces()[0],
                   square.curves()[0]):
        with pytest.raises(TypeError, match="weak reference"):
            weakref.ref(handle)


# ===========================================================================
# 14. sizes / iteration invariants (cross-checks used by the tests above)
# ===========================================================================

def test_size_properties_are_consistent(square_arr):
    """The fixture is [0,4]^2 plus the chord y=2 from x=-1 to x=5: V=8 E=9 F=3."""
    assert square_arr.number_of_vertices == 8
    assert square_arr.number_of_edges == 9
    assert square_arr.number_of_halfedges == 18
    assert square_arr.number_of_faces == 3
    assert square_arr.number_of_unbounded_faces == 1
    assert square_arr.number_of_isolated_vertices == 0
    assert square_arr.number_of_vertices_at_infinity == 0
    assert square_arr.number_of_curves == 5
    assert len(square_arr) == square_arr.number_of_edges
    # Euler for a connected planar arrangement including the unbounded face: V - E + F = 2
    assert (square_arr.number_of_vertices - square_arr.number_of_edges
            + square_arr.number_of_faces) == 2


def test_iteration_snapshots_match_the_counters(square_arr):
    assert len(square_arr.vertices()) == 8
    assert len(square_arr.halfedges()) == 18
    assert len(square_arr.edges()) == 9
    assert len(square_arr.faces()) == 3
    assert len(square_arr.unbounded_faces()) == 1
    assert len(square_arr.bounded_faces()) == 2
    assert len(square_arr.curves()) == 5
    assert square_arr.unbounded_faces() == [square_arr.unbounded_face]
    assert set(square_arr.bounded_faces()) | set(square_arr.unbounded_faces()) == set(
        square_arr.faces())
    # edges() picks exactly one halfedge per twin pair
    assert {h.edge_id for h in square_arr.edges()} == {h.edge_id for h in square_arr.halfedges()}


def test_curve_handles_link_input_curves_to_the_edges_they_induce():
    """Two overlapping collinear inputs: the shared middle edge has two originators."""
    arr = a2.Arrangement("segment")
    c1 = arr.insert(S((0, 0), (4, 4)))
    c2 = arr.insert(S((1, 1), (6, 6)))
    assert c1.curve == S((0, 0), (4, 4)) and c2.curve == S((1, 1), (6, 6))
    # c1 covers (0,0)-(1,1) and (1,1)-(4,4); c2 covers (1,1)-(4,4) and (4,4)-(6,6)
    assert c1.number_of_induced_edges == 2
    assert c2.number_of_induced_edges == 2
    assert len(c1.induced_edges()) == 2 and len(c2.induced_edges()) == 2
    shared = edge_with(arr, S((1, 1), (4, 4)))
    assert shared.number_of_originating_curves == 2
    assert set(shared.originating_curves()) == {c1, c2}
    assert edge_with(arr, S((0, 0), (1, 1))).originating_curves() == [c1]
    assert edge_with(arr, S((4, 4), (6, 6))).originating_curves() == [c2]
    # the twin reports the same originators (history is per edge, not per halfedge)
    assert set(shared.twin.originating_curves()) == {c1, c2}


def test_insert_dispatches_on_the_argument_type():
    arr = a2.Arrangement("segment")
    assert isinstance(arr.insert(P(1, 1)), a2.Vertex)
    assert isinstance(arr.insert((2, 2)), a2.Vertex)          # a bare coordinate tuple
    assert isinstance(arr.insert(S((0, 0), (4, 4))), a2.CurveHandle)
    handles = arr.insert([S((0, 4), (4, 0))])
    assert isinstance(handles, list) and isinstance(handles[0], a2.CurveHandle)
    assert arr.insert([]) == []
    assert len(arr.insert(iter([S((7, 7), (8, 8))]))) == 1     # any iterable works
