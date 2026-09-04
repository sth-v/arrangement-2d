"""Tolerance-based cleanup of messy segment input before building an exact arrangement.

CGAL's arrangement is *exact*: it computes every intersection, splits every curve at every
intersection point, merges exactly overlapping pieces into single edges and records which
input curves induced each edge. What it deliberately does not do is guess: two endpoints
that differ by 1e-9 are two different points, and an endpoint that misses another segment
by 1e-9 is a gap, not a T-junction. Real-world data (CAD exports, GIS, floating-point
computations) is full of such near misses, and every one of them opens a face.

This module closes those gaps *before* the exact arrangement is built:

* :func:`near_miss_report` measures how many endpoints almost touch other endpoints or the
  interior of other segments, at several tolerances, so you can pick a tolerance that is
  larger than the noise and smaller than the real geometry.
* :func:`snap_segments` clusters endpoints within a tolerance, snaps endpoints onto nearby
  segments (splitting them there), removes degenerate and duplicate segments, and iterates
  until nothing moves.
* :func:`clean_arrangement` runs :func:`snap_segments`, builds the exact arrangement and
  optionally removes dangling edges (edges with the same face on both sides), leaving only
  the edges that bound faces.
* :func:`remove_dangling_edges` does the last step on any arrangement.

Everything here is plain Python + NumPy over the public API; coordinates are handled as
``float`` (the snapped positions become exact rationals when inserted).

Example::

    import json, arrangement_2d as a2
    from arrangement_2d import cleanup

    segs = [((p[0], p[1]), (q[0], q[1])) for p, q in json.load(open("segments.json"))]
    print(cleanup.near_miss_report(segs))          # choose a tolerance from the histogram
    arr = cleanup.clean_arrangement(segs, tolerance=1e-3)
    faces = a2.regions.bounded_faces(arr)
"""
from __future__ import annotations

import math
from dataclasses import dataclass, field
from typing import Any, Iterable, Sequence

import numpy as np

from . import Arrangement, Segment, Kind

__all__ = [
    "near_miss_report",
    "NearMissReport",
    "snap_segments",
    "SnapResult",
    "clean_arrangement",
    "remove_dangling_edges",
    "segments_from_polylines",
]

Point = tuple[float, float]
SegmentT = tuple[Point, Point]


# ---------------------------------------------------------------------------
# helpers
# ---------------------------------------------------------------------------

def _as_segment_array(segments: Iterable[Any]) -> np.ndarray:
    """Accept ((x, y), (x, y)) tuples, :class:`Segment` objects or (4,) rows -> float array (n, 4)."""
    rows = []
    for s in segments:
        if isinstance(s, Segment):
            (x1, y1), (x2, y2) = s.source.approx, s.target.approx
        else:
            p, q = s
            if hasattr(p, "approx"):
                p = p.approx
            if hasattr(q, "approx"):
                q = q.approx
            x1, y1 = float(p[0]), float(p[1])
            x2, y2 = float(q[0]), float(q[1])
        rows.append((x1, y1, x2, y2))
    return np.asarray(rows, dtype=float).reshape(-1, 4)


def segments_from_polylines(polylines: Iterable[Sequence[Any]]) -> list[SegmentT]:
    """Explode polylines (sequences of points) into segments, dropping zero-length pieces."""
    out: list[SegmentT] = []
    for pl in polylines:
        pts = [(float(p[0]), float(p[1])) for p in pl]
        for a, b in zip(pts, pts[1:]):
            if a != b:
                out.append((a, b))
    return out


class _UnionFind:
    __slots__ = ("parent",)

    def __init__(self, n: int):
        self.parent = np.arange(n)

    def find(self, i: int) -> int:
        p = self.parent
        while p[i] != i:
            p[i] = p[p[i]]
            i = p[i]
        return int(i)

    def union(self, a: int, b: int) -> None:
        ra, rb = self.find(a), self.find(b)
        if ra != rb:
            self.parent[max(ra, rb)] = min(ra, rb)


def _grid_pairs_within(points: np.ndarray, tol: float) -> Iterable[tuple[int, int]]:
    """Yield (i, j), i < j, for all point pairs closer than ``tol`` (uniform grid, cell = tol)."""
    if len(points) == 0 or tol <= 0:
        return
    cells = np.floor(points / tol).astype(np.int64)
    buckets: dict[tuple[int, int], list[int]] = {}
    for i, (cx, cy) in enumerate(map(tuple, cells)):
        buckets.setdefault((cx, cy), []).append(i)
    tol2 = tol * tol
    for (cx, cy), idx in buckets.items():
        for dx in (-1, 0, 1):
            for dy in (-1, 0, 1):
                other = buckets.get((cx + dx, cy + dy))
                if other is None:
                    continue
                if (dx, dy) == (0, 0):
                    for a in range(len(idx)):
                        pa = points[idx[a]]
                        for b in range(a + 1, len(idx)):
                            d = pa - points[idx[b]]
                            if d[0] * d[0] + d[1] * d[1] <= tol2:
                                yield idx[a], idx[b]
                elif (dx, dy) > (0, 0):   # visit each unordered cell pair once
                    for ia in idx:
                        pa = points[ia]
                        for ib in other:
                            d = pa - points[ib]
                            if d[0] * d[0] + d[1] * d[1] <= tol2:
                                yield ia, ib


# ---------------------------------------------------------------------------
# near-miss report
# ---------------------------------------------------------------------------

@dataclass
class NearMissReport:
    """Result of :func:`near_miss_report`."""
    n_segments: int
    n_zero_length: int
    n_exact_duplicates: int
    n_endpoints: int
    n_exact_endpoint_coincidences: int
    #: tolerance -> number of endpoints whose nearest OTHER endpoint is within (0, tol]
    endpoint_gaps: dict[float, int] = field(default_factory=dict)
    #: tolerance -> number of endpoints within (0, tol] of a foreign segment's interior (T-junction candidates)
    t_junction_gaps: dict[float, int] = field(default_factory=dict)
    n_exact_t_junctions: int = 0

    def __str__(self) -> str:
        lines = [
            f"{self.n_segments} segments ({self.n_zero_length} zero-length, {self.n_exact_duplicates} exact duplicates)",
            f"{self.n_endpoints} endpoints, {self.n_exact_endpoint_coincidences} exactly coincident with another endpoint",
            f"{self.n_exact_t_junctions} endpoints exactly on another segment's interior",
            "near misses (endpoint gaps / T-junction gaps) within tolerance:",
        ]
        for tol in self.endpoint_gaps:
            lines.append(f"  <= {tol:g}: {self.endpoint_gaps[tol]:6d} / {self.t_junction_gaps.get(tol, 0):6d}")
        return "\n".join(lines)


def near_miss_report(segments: Iterable[Any],
                     tolerances: Sequence[float] = (1e-9, 1e-6, 1e-3, 1e-2, 1e-1, 1.0)) -> NearMissReport:
    """Measure how far the input is from being exactly connected.

    For every endpoint the distance to the nearest *other* endpoint and to the nearest
    *foreign* segment interior is computed (O(n^2) in NumPy chunks; fine up to ~50k segments).
    """
    S = _as_segment_array(segments)
    n = len(S)
    zero = int(np.all(S[:, :2] == S[:, 2:], axis=1).sum())
    keyed = {tuple(sorted([(r[0], r[1]), (r[2], r[3])])) for r in S.tolist()}
    dups = n - len(keyed)
    P = np.vstack([S[:, :2], S[:, 2:]])
    owner = np.concatenate([np.arange(n), np.arange(n)])
    A, B = S[:, :2], S[:, 2:]
    AB = B - A
    L2 = (AB ** 2).sum(1)
    L2[L2 == 0] = np.inf
    tols = sorted(set(float(t) for t in tolerances))
    d_pt = np.full(len(P), np.inf)
    d_seg = np.full(len(P), np.inf)
    coincide = 0
    chunk = max(1, min(512, int(2e7 // max(1, len(P)))))
    for i0 in range(0, len(P), chunk):
        Q = P[i0:i0 + chunk]
        D = np.sqrt(((Q[:, None, :] - P[None, :, :]) ** 2).sum(2))
        D[np.arange(len(Q)), np.arange(i0, i0 + len(Q))] = np.inf
        zero_mask = D == 0
        coincide += int(zero_mask.any(1).sum())
        D[zero_mask] = np.inf
        d_pt[i0:i0 + chunk] = D.min(1)
        t = ((Q[:, None, :] - A[None, :, :]) * AB[None, :, :]).sum(2) / L2[None, :]
        interior = (t > 1e-12) & (t < 1 - 1e-12)
        proj = A[None, :, :] + t[:, :, None] * AB[None, :, :]
        Ds = np.sqrt(((Q[:, None, :] - proj) ** 2).sum(2))
        Ds[~interior] = np.inf
        Ds[np.arange(len(Q)), owner[i0:i0 + len(Q)]] = np.inf
        d_seg[i0:i0 + chunk] = Ds.min(1)
    rep = NearMissReport(n, zero, dups, len(P), coincide)
    for tol in tols:
        rep.endpoint_gaps[tol] = int(((d_pt > 0) & (d_pt <= tol)).sum())
        rep.t_junction_gaps[tol] = int(((d_seg > 0) & (d_seg <= tol)).sum())
    rep.n_exact_t_junctions = int((d_seg == 0).sum())
    return rep


# ---------------------------------------------------------------------------
# snapping
# ---------------------------------------------------------------------------

@dataclass
class SnapResult:
    """Result of :func:`snap_segments`."""
    segments: list[SegmentT]
    iterations: int
    endpoints_merged: int
    t_junctions_snapped: int
    removed_degenerate: int
    removed_duplicates: int

    def __str__(self) -> str:
        return (f"{len(self.segments)} segments after {self.iterations} iteration(s): "
                f"{self.endpoints_merged} endpoint clusters merged, {self.t_junctions_snapped} T-junctions snapped, "
                f"{self.removed_degenerate} degenerate and {self.removed_duplicates} duplicate segments removed")


def _cluster_endpoints(S: np.ndarray, tol: float) -> tuple[np.ndarray, int]:
    """Merge endpoints closer than ``tol`` (union-find); each cluster snaps to its first point."""
    P = np.vstack([S[:, :2], S[:, 2:]])
    uf = _UnionFind(len(P))
    for i, j in _grid_pairs_within(P, tol):
        uf.union(i, j)
    roots = np.array([uf.find(i) for i in range(len(P))])
    Pn = P[roots]
    merged = int(np.any(Pn != P, axis=1).sum())   # endpoints that actually MOVED (coincident points do not count)
    n = len(S)
    return np.hstack([Pn[:n], Pn[n:]]), merged


def _snap_to_edges(S: np.ndarray, tol: float) -> tuple[np.ndarray, int]:
    """Split every segment at the endpoints (of other segments) that lie within ``tol`` of its interior."""
    P = np.unique(np.vstack([S[:, :2], S[:, 2:]]), axis=0)
    if len(P) == 0:
        return S, 0
    lo, hi = P.min(0), P.max(0)
    extent = float(max(hi[0] - lo[0], hi[1] - lo[1], tol))
    cell = max(tol * 4.0, extent / 1024.0)
    cells = np.floor((P - lo) / cell).astype(np.int64)
    buckets: dict[tuple[int, int], list[int]] = {}
    for i, c in enumerate(map(tuple, cells)):
        buckets.setdefault(c, []).append(i)
    out_rows: list[tuple[float, float, float, float]] = []
    snapped = 0
    tol2 = tol * tol
    for row in S:
        a = row[:2]
        b = row[2:]
        ab = b - a
        l2 = float(ab @ ab)
        if l2 == 0:
            out_rows.append(tuple(row))
            continue
        cmin = np.floor((np.minimum(a, b) - tol - lo) / cell).astype(np.int64)
        cmax = np.floor((np.maximum(a, b) + tol - lo) / cell).astype(np.int64)
        hits: list[tuple[float, np.ndarray]] = []
        for cx in range(int(cmin[0]), int(cmax[0]) + 1):
            for cy in range(int(cmin[1]), int(cmax[1]) + 1):
                for i in buckets.get((cx, cy), ()):
                    p = P[i]
                    if (p[0] == a[0] and p[1] == a[1]) or (p[0] == b[0] and p[1] == b[1]):
                        continue
                    t = float(((p - a) @ ab) / l2)
                    if t <= 0.0 or t >= 1.0:
                        continue
                    proj = a + t * ab
                    d = p - proj
                    d2 = d[0] * d[0] + d[1] * d[1]
                    if 0.0 < d2 <= tol2:          # exact incidences are left to the arrangement
                        hits.append((t, p))
        if not hits:
            out_rows.append(tuple(row))
            continue
        hits.sort(key=lambda h: h[0])
        # a vertex may hit the same segment through several grid cells: keep each point once
        uniq: list[tuple[float, np.ndarray]] = []
        for h in hits:
            if not uniq or not (uniq[-1][1][0] == h[1][0] and uniq[-1][1][1] == h[1][1]):
                uniq.append(h)
        hits = uniq
        chain = [a] + [h[1] for h in hits] + [b]
        for u, v in zip(chain, chain[1:]):
            if not (u[0] == v[0] and u[1] == v[1]):
                out_rows.append((float(u[0]), float(u[1]), float(v[0]), float(v[1])))
        snapped += len(hits)
    return np.asarray(out_rows, dtype=float).reshape(-1, 4), snapped


def _dedupe(S: np.ndarray) -> tuple[np.ndarray, int, int]:
    keep: list[tuple[float, float, float, float]] = []
    seen: set[tuple] = set()
    degenerate = dup = 0
    for x1, y1, x2, y2 in S.tolist():
        if x1 == x2 and y1 == y2:
            degenerate += 1
            continue
        key = ((x1, y1), (x2, y2)) if (x1, y1) <= (x2, y2) else ((x2, y2), (x1, y1))
        if key in seen:
            dup += 1
            continue
        seen.add(key)
        keep.append((x1, y1, x2, y2))
    return np.asarray(keep, dtype=float).reshape(-1, 4), degenerate, dup


def snap_segments(segments: Iterable[Any], tolerance: float, *, snap_endpoints: bool = True,
                  snap_to_edges: bool = True, max_iterations: int = 8) -> SnapResult:
    """Close near misses smaller than ``tolerance`` in a set of segments.

    Each iteration (1) merges endpoint clusters closer than ``tolerance`` (every endpoint of a
    cluster moves to the cluster's first point), (2) splits every segment at the foreign
    endpoints within ``tolerance`` of its interior, moving the split to the endpoint (this
    turns near T-junctions into exact ones), and (3) drops zero-length and exactly duplicated
    segments. Iterations stop when nothing changes (snapping can create new near misses).

    Overlapping, collinear segments are *not* merged here: after snapping their endpoints
    onto each other they overlap exactly, and the exact arrangement merges exact overlaps
    into single edges by itself.
    """
    if tolerance <= 0:
        raise ValueError("tolerance must be positive")
    S = _as_segment_array(segments)
    S, degenerate, dup = _dedupe(S)
    merged_total = snapped_total = 0
    it = 0
    for it in range(1, max_iterations + 1):
        changed = False
        if snap_endpoints:
            S2, merged = _cluster_endpoints(S, tolerance)
            if merged:
                changed = True
                merged_total += merged
                S = S2
        if snap_to_edges:
            S2, snapped = _snap_to_edges(S, tolerance)
            if snapped:
                changed = True
                snapped_total += snapped
                S = S2
        S, d, u = _dedupe(S)
        degenerate += d
        dup += u
        if not changed:
            break
    segs = [((r[0], r[1]), (r[2], r[3])) for r in S.tolist()]
    return SnapResult(segs, it, merged_total, snapped_total, degenerate, dup)


# ---------------------------------------------------------------------------
# arrangement-level cleanup
# ---------------------------------------------------------------------------

def remove_dangling_edges(arr: Arrangement, *, remove_vertices: bool = True) -> int:
    """Remove every edge that has the same face on both sides (antennas, isolated pieces),
    repeating until none is left. Returns the number of removed edges.

    After this, every remaining edge separates two different faces, so the arrangement is
    exactly the set of region boundaries. Vertices left with degree 0 are removed too when
    ``remove_vertices`` is true.
    """
    removed = 0
    while True:
        dangling = [he for he in arr.edges() if he.face == he.twin.face]
        if not dangling:
            break
        for he in dangling:
            if not he.is_valid:
                continue
            arr.remove_edge(he, remove_source=remove_vertices, remove_target=remove_vertices)
            removed += 1
    return removed


def clean_arrangement(segments: Iterable[Any], tolerance: float, *, kind: Any = "segment",
                      remove_dangling: bool = True, snap_endpoints: bool = True,
                      snap_to_edges: bool = True, max_iterations: int = 8) -> Arrangement:
    """:func:`snap_segments` + exact arrangement (+ :func:`remove_dangling_edges`).

    ``kind`` may be ``"segment"`` (default) or any kind that accepts segments.
    """
    res = snap_segments(segments, tolerance, snap_endpoints=snap_endpoints,
                        snap_to_edges=snap_to_edges, max_iterations=max_iterations)
    arr = Arrangement(kind)
    arr.insert([Segment(p, q) for p, q in res.segments])
    if remove_dangling:
        remove_dangling_edges(arr)
    return arr
