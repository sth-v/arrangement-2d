export const meta = {
  name: 'arr2d-impl-stage3',
  description: 'Stage 3: integrate & build the extension, write tests per area, fix loop until green, adversarial review, docs/helpers',
  phases: [
    { title: 'Integrate', detail: 'build the extension, C++ tests, smoke pytest' },
    { title: 'Tests', detail: 'parallel test writers per area (no rebuilds)' },
    { title: 'Fix', detail: 'fixer applies reported failures, rebuilds, reruns all tests (loop)' },
    { title: 'Review', detail: 'adversarial reviewers + refuters' },
    { title: 'Finish', detail: 'fix confirmed findings; docs, regions.py, plot.py' },
  ],
}

const REPO = '/Users/sthv/PycharmProjects/arrangement-2d'
const SCRATCH = '/private/tmp/claude-501/-Users-sthv-PycharmProjects-arrangement-2d/caeba100-f0a3-4bc9-8340-691c4b0ddc3d/scratchpad'
const PY = `${REPO}/.venv/bin/python`

const COMMON = `
Project: arrangement_2d — Cython bindings for the CGAL 6.1 2D Arrangements package. Repo: ${REPO}. Python: ${PY} (3.14, venv with Cython 3.2, numpy, pytest).
Read first: docs/dev/DESIGN.md (architecture + Python API spec §3), docs/dev/CGAL_TRAPS_CHECKLIST.md, docs/dev/STAGE1_NOTES.md and docs/dev/STAGE2_NOTES.md (conventions and open issues reported by the implementers), then the code you need: src/arr2d/include/arr2d/*.hpp, src/arr2d/include/arr2d/impl/*.hpp, src/arr2d/src/*.cpp, arrangement_2d/*.pyx|pxd|pxi|py. The CGAL 6.1 API maps live in docs/dev/cgal61_api/ (compile-verified; trust them over memory).
Build: \`cd ${REPO} && ${PY} setup.py build_ext --inplace -j 8\` (env ARR2D_OPT=-O1 speeds iteration; the final build must use the default flags). A full build takes several minutes (CGAL-heavy TUs). NEVER run two builds concurrently. Run tests with \`${PY} -m pytest -x -q tests/<file>\`. C++ tests: see the header comments of src/arr2d/tests/*.cpp for their compile+run commands. Scratch dir for temporaries: ${SCRATCH}/<your-label>/ (create it); never write temporaries into the repo. Never background a test binary or a python process without \`timeout\` (an orphaned probe once ate 14 GB here).
Rules: C++17; API misuse -> arr2d::Error; CGAL exceptions propagate to Python as errors.py classes. Keep the interface contracts documented in the headers/DESIGN.md unless a change is unavoidable (then update the doc comment and mention it in your report). Be exhaustive; never silently skip a failing case: either fix it or report it with the exact error text. Do not stop early: decide, document, continue.
Final report (structured): summary; files_written; verified; open_issues; conventions.`

const REPORT = { type: 'object', properties: {
  summary: { type: 'string' }, files_written: { type: 'array', items: { type: 'string' } }, verified: { type: 'string' },
  open_issues: { type: 'array', items: { type: 'string' } }, conventions: { type: 'string' } },
  required: ['summary', 'files_written', 'verified', 'open_issues', 'conventions'] }

const TEST_REPORT = { type: 'object', properties: {
  summary: { type: 'string' }, test_file: { type: 'string' }, passed: { type: 'integer' }, failed: { type: 'integer' },
  failures: { type: 'array', items: { type: 'object', properties: {
    test: { type: 'string' }, error: { type: 'string' }, suspected_cause: { type: 'string' }, proposed_fix: { type: 'string' }, file_hint: { type: 'string' } },
    required: ['test', 'error', 'suspected_cause', 'proposed_fix', 'file_hint'] } },
  api_gaps: { type: 'array', items: { type: 'string' } } },
  required: ['summary', 'test_file', 'passed', 'failed', 'failures', 'api_gaps'] }

const FIX_REPORT = { type: 'object', properties: {
  summary: { type: 'string' }, fixed: { type: 'array', items: { type: 'string' } }, remaining: { type: 'array', items: { type: 'object', properties: {
    test: { type: 'string' }, error: { type: 'string' }, analysis: { type: 'string' } }, required: ['test', 'error', 'analysis'] } },
  full_suite: { type: 'string' } }, required: ['summary', 'fixed', 'remaining', 'full_suite'] }

// ---------------------------------------------------------------- Integrate
phase('Integrate')
const integ = await agent(`${COMMON}

TASK integrate: make the whole extension build, import and pass a smoke test. Steps:
1. Build: \`${PY} setup.py build_ext --inplace -j 8\` (start with ARR2D_OPT=-O1 to iterate faster, finish with a default-flag build). Fix every compile/link error anywhere in the repo (C++ core, kind TUs, BSO TUs, Cython files, setup.py). Typical integration problems to expect: signature drift between headers and implementations, duplicate symbols (obs_event_name is defined in registry.cpp; Types::traits() must be defined exactly once per kind), missing includes, Cython <-> .pxd mismatches (e.g. IntersectionResult::multiplicity is size_t; NumberKind members are Rational/SqrtExt/Algebraic), the CORE memory-pool exit abort (leaked singletons), and macOS link flags. Keep fixes minimal and documented; when you change behaviour, update the relevant doc comment.
2. Import check: \`${PY} -c "import arrangement_2d as a2; print(a2.build_info()); print([k for k in a2.Kind if a2.kind_available(k)])"\` must list all seven kinds; \`${PY} -c "import arrangement_2d as a2; a2.BezierCurve([(0,0),(1,1),(2,0)]); a2.ConicArc.circle((0,0), 1)"\` must exit 0 (no CORE abort at exit).
3. C++ tests: compile+run every src/arr2d/tests/test_*.cpp per its header comment (they link a subset of TUs). Fix failures.
4. Write tests/test_smoke.py (pytest) covering every kind end to end: construct curves, insert (single, list, point), counts (hand-verified V/E/F), iteration (vertices/halfedges/edges/faces/curves), handle accessors, .data round trip, locate/ray_shoot per supported strategy, zone, decompose (planar kinds), batched_locate order, overlay with a callback, observer receiving events, remove_edge/remove_curve/split/merge, handle invalidation (InvalidHandleError after removal), copy(), exact()/approx/interval of points, approximate() of curves, PolygonSet ops for the four kinds with Boolean ops (join/intersection/difference/symmetric_difference/complement/oriented_side/to_arrangement), error translation (PreconditionError, KindMismatchError, NotXMonotoneError, UnsupportedError), and repr() of everything. Run \`${PY} -m pytest -q tests/test_smoke.py\` until it passes; if a failure reveals a bug, fix the bug (not the test) unless the test's expectation was wrong.
5. Apply these cross-cutting fixes reported by the stage-2 implementers (see docs/dev/STAGE2_NOTES.md for details) in the generic code, each with a C++ or pytest regression check: (a) kind_ops_base_impl.hpp: make_x_monotone and intersect must CLEAR their output vector first (the ops.hpp contract now says every output vector is cleared); (b) kind_ops_base_impl.hpp: approximate_coordinate must return the correctly rounded value (delegate to point_approx) instead of CGAL's raw Approximate_2 functor, for every kind; (c) arr_impl.hpp, Linear kind: before inserting an UNBOUNDED curve (ops().curve_is_bounded == false) with insert_curve / insert_curves / insert_non_intersecting_curve, detect an overlap with an existing edge (zone of the curve + ops().intersect against each halfedge's curve, any overlap result) and throw Error(Unsupported, "CGAL 6.1 cannot insert an unbounded curve overlapping an existing edge") — this prevents the cv.has_left() abort; (d) arr_impl.hpp, Sphere kind: batched_locate must route query points whose sphere::point_location != NO_BOUNDARY (poles / identification curve) through the naive locate individually (CGAL's batched locate segfaults at the north pole); decompose must throw Error(Unsupported, ...) when any vertex has parameter_space_in_x/y != ARR_INTERIOR; remove_vertex / remove_isolated_vertex must throw Error(Unsupported, ...) for such boundary vertices (dangling topology pointers otherwise); (e) KindPolicy<LinearTypes>::supports_trapezoid and KindPolicy<SegmentTypes>::supports_triangulation are now false (done) — make sure the Python `point_location_strategies()` / supports_point_location reflect it and tests do not assume them. (f) Also install CGAL::set_warning_handler silencing in the module (BSO validation prints warnings otherwise) if not already done.
6. Report exactly what was built, what passed, and every remaining problem with error text.`, { label: 'integrate', phase: 'Integrate', model: 'opus', schema: REPORT })
log(`integration: ${integ ? integ.summary.slice(0, 200) : 'NO REPORT'}`)

// ---------------------------------------------------------------- Tests
phase('Tests')
const AREAS = [
  ['segment_kind', 'tests/test_segment.py', 'the segment kind: Segment construction/accessors/exact/approx, Arrangement of segments: all insertion forms (insert single/list/point, insert_non_intersecting, insert_in_face_interior, insert_from_left/right_vertex, insert_at_vertices), all modification forms (modify_vertex/edge, split_edge both forms, merge_edge both forms, remove_edge with/without vertex removal, remove_vertex, remove_isolated_vertex, remove_curve), history queries (curves, induced_edges, originating_curves), counts after each step verified by hand, is_valid, Euler characteristic V-E+F = 1+C on random inputs (C = connected components computed via a union-find over the edges plus isolated vertices), traits functors via arr.traits and traits(kind), bulk exports (vertex_coordinates, edge_vertex_indices, face_boundaries, approximate_edges), bbox'],
  ['linear_kind', 'tests/test_linear.py', 'the linear kind (lines/rays/segments, unbounded topology): construction, which/is_line/is_ray, supporting_line, direction, arrangement counts incl. number_of_vertices_at_infinity and unbounded faces, fictitious halfedges in CCB walks (Halfedge.curve raises for fictitious, is_fictitious), fictitious_face, unbounded_faces, face_polygon/boundary_points for unbounded faces (open chains), approximate with clip bbox, directed_curve semantics for rays (as stored + direction), point location (walk/trapezoid/landmarks), decompose reporting fictitious halfedges, overlay of two line arrangements, the CGAL overlap-of-unbounded-curves trap (inserting the same line twice must raise a clean error, not crash)'],
  ['circle_kind', 'tests/test_circle_segment.py', 'the circle_segment kind: circles (radius vs squared_radius), arcs (from center/radius/endpoints, three points), segments, make_x_monotone piece counts, sqrt-extension coordinates (exact() -> SqrtExtension, is_rational False, Point.from_sqrt_extension round trip), two intersecting circles V/E/F, tangency, approximate() within tolerance (check sample points lie on the circle), PolygonSet of circles (union/intersection of two discs: number of polygons, oriented_side, area of approximation ~ expected), to_arrangement contained faces'],
  ['polyline_kind', 'tests/test_polyline.py', 'the polyline kind: construction from points/segments, x_monotone(), points/segments/number_of_subcurves, arrangement of crossing polylines (counts), history (one input curve induces many edges), approximate, conversion from Segment kind, errors for consecutive duplicate points'],
  ['bezier_kind', 'tests/test_bezier.py', 'the bezier kind: construction (quadratic, cubic, degree 5, rational control points), control_points/degree/curve_id, make_x_monotone pieces and parameter_range, evaluate exact (Fraction t) vs approximate (float t), sample, self-intersecting curve arrangement (counts), two crossing cubics (V/E/F), point exact() -> Algebraic with interval containing the approx and refine() tightening, point_originators/parameter_at, split precondition (off-curve point raises), approximate() within tolerance of evaluate, from_rational -> ConicArc for degree 2, PolygonSet of Bezier polygons (a closed curve made of two Bezier arcs: orientation fix, union with a second), process exits cleanly after using Bezier objects (subprocess test)'],
  ['conic_kind', 'tests/test_conic.py', 'the conic kind: circle/ellipse/segment/from_coefficients/from_points/from_rational_bezier/from_circle_segment constructors, coefficients/orientation/conic_type/is_full, arrangement of an ellipse and a circle and a segment (counts), exact() -> Algebraic, approximate within tolerance (verify points satisfy the conic equation approximately), hyperbolic safety predicate (a safe hyperbola arc builds; an unsafe one raises UnsupportedError unless conic_allow_hyperbolic(True)), rational quadratic Bezier exactness (points evaluated from the Bezier definition satisfy the conic equation), PolygonSet of conic polygons (ellipse ∪ circle), clean process exit (subprocess)'],
  ['sphere_kind', 'tests/test_sphere.py', 'the sphere kind: Point(x,y,z) (location, exact unnormalised rationals, approx normalised), GeodesicArc constructors (from_points, with normal, great_circle), is_full/is_vertical/is_meridian, make_x_monotone piece counts (great circle -> 2), arrangement of a spherical triangle (V=3 E=3 F=2, spherical face with 0 outer ccbs contains the north pole, unbounded_face/spherical_face), an octahedron from three great circles (V=6 E=12 F=8), history, locate (naive only; other strategies raise UnsupportedError), aggregate insert of arcs crossing the identification curve, overlay of two spherical arrangements, approximate() points on the unit sphere within tolerance, decompose raising a clean error when a vertex is on a pole, remove_vertex on a pole vertex raising UnsupportedError'],
  ['arrangement_api', 'tests/test_arrangement_api.py', 'kind-independent arrangement API on the segment kind: handle identity/equality/hash and invalidation after every removal/merge (InvalidHandleError, is_valid False), .data on vertices/halfedges/faces incl. copy() sharing and overlay callbacks setting data, Observer receiving the documented events with the documented arguments for insert/split/merge/remove (record the sequence and assert key events and handle validity inside callbacks; exceptions raised in a callback propagate after the operation), OverlayCallbacks all ten methods invoked with correct handle types, on_face/on_vertex/on_edge convenience, point location strategies attach/detach/has/supports, batched_locate with duplicate and shuffled points, zone ordering, decompose below/above correctness on a small example, Traits functor wrappers (compare_xy, compare_y_at_x, intersect incl. overlap, split, merge, trim, opposite, make_x_monotone, construct_x_monotone_curve), errors (PreconditionError from a CGAL precondition, KindMismatchError mixing kinds, NotXMonotoneError, TypeError for bad inputs), repr/str formats, deepcopy, weakref'],
  ['polygon_set_api', 'tests/test_polygon_set.py', 'Boolean set operations on the segment kind (and briefly circle_segment): Polygon/PolygonWithHoles construction (points, curves, auto-close, chaining error), orientation()/reverse()/area()/approximate()/points, PolygonSet insert with fix_orientation, all Boolean ops (member, operator, in-place, module-level) with hand-verified polygon counts and vertex counts on squares (overlapping, disjoint, touching, nested with a hole), complement/is_plane, oriented_side/locate/do_intersect, polygons_with_holes round trip, to_arrangement (faces/edges counts and contained faces), is_valid_polygon on CW/CCW/self-intersecting input, from an Arrangement face via Face.polygon() into a PolygonSet and back'],
  ['numbers_and_geometry', 'tests/test_numbers_geometry.py', 'the number and geometry layer: Point construction from int/float/Fraction/Decimal/str/numpy, exact() Fractions, float exactness (0.1 -> its exact binary Fraction), to_kind conversions between all kinds where rational, KindMismatch for planar->sphere, SqrtExtension and Algebraic arithmetic/comparison/hash/repr/interval semantics, Curve base API on Segment (bbox, approximate, length, opposite, split/trim/merge/intersect, is_in_x_range, compare_y_at_x), curve conversions between kinds (segment -> linear/polyline/circle_segment/conic/bezier and back where documented; NotRepresentableError otherwise), Polygon.to_kind, repr of every geometry class'],
]
const testReports = await parallel(AREAS.map(([key, file, scope]) => () => agent(`${COMMON}

TASK test_${key}: write ${file} (pytest) covering ${scope}. Rules: use only the public API (import arrangement_2d as a2); every expected number must be hand-derived (explain in a comment); use tests/conftest.py fixtures where useful; keep each test small and independent; mark tests for known CGAL limitations with pytest.xfail(strict=True) ONLY when the limitation is documented in docs/dev/CGAL_TRAPS_CHECKLIST.md, otherwise a failure is a bug to report. Do NOT rebuild the extension and do NOT modify any file outside your test file (other writers run concurrently). Run \`${PY} -m pytest -q ${file}\` (with \`timeout 900\`). For every failing test decide whether the test or the code is wrong: fix the test if the expectation was wrong; otherwise leave the test failing and report it in the 'failures' field with the exact error text, the suspected cause (file:line) and a concrete proposed fix. Also list API gaps (things DESIGN.md promises that are missing). Aim for at least 40 test functions for your area (more where the area is rich).`, { label: `test_${key}`, phase: 'Tests', model: 'opus', schema: TEST_REPORT })))
const tr = testReports.filter(Boolean)
log(`tests written: ${tr.length}/${AREAS.length}; failing tests reported: ${tr.reduce((n, r) => n + (r.failed || 0), 0)}`)

// ---------------------------------------------------------------- Fix loop
let failures = tr.flatMap(r => r.failures.map(f => ({ ...f, test_file: r.test_file })))
let round = 0
let lastFix = null
while (failures.length > 0 && round < 3) {
  round++
  phase('Fix')
  lastFix = await agent(`${COMMON}

TASK fix (round ${round}): the test writers reported ${failures.length} failing tests. Reports (test, error, suspected cause, proposed fix, file hint):
${JSON.stringify(failures, null, 1).slice(0, 60000)}

For each: reproduce with \`${PY} -m pytest -q <file>::<test>\`, decide root cause, fix the CODE (C++ core, kind TUs, Cython, Python) — fix the TEST only when its expectation is provably wrong (say so). Group fixes, rebuild ONCE per group (\`${PY} setup.py build_ext --inplace -j 8\`, never concurrent builds), then run the FULL suite \`timeout 1800 ${PY} -m pytest -q tests\` and report its final summary line in full_suite. List what you fixed and what remains (with analysis).`, { label: `fix_${round}`, phase: 'Fix', model: 'opus', schema: FIX_REPORT })
  failures = lastFix ? lastFix.remaining.map(r => ({ test: r.test, error: r.error, suspected_cause: r.analysis, proposed_fix: '', file_hint: '', test_file: '' })) : []
  log(`fix round ${round}: remaining ${failures.length}`)
}

// ---------------------------------------------------------------- Review
phase('Review')
const FINDINGS = { type: 'object', properties: { findings: { type: 'array', items: { type: 'object', properties: {
  title: { type: 'string' }, file: { type: 'string' }, line: { type: 'integer' }, severity: { type: 'string' }, description: { type: 'string' }, repro: { type: 'string' }, fix: { type: 'string' } },
  required: ['title', 'file', 'line', 'severity', 'description', 'repro', 'fix'] } } }, required: ['findings'] }
const LENSES = [
  ['memory_safety', 'memory safety and handle validity: stale handles, use-after-free of DCEL records, PyRef refcount balance (copy/clone/overlay/data set), exceptions thrown mid-operation, observer re-entrancy, callbacks touching handles CGAL says are unsafe (before_split_face e->face()), CORE static-destruction aborts, process exit cleanliness'],
  ['exactness', 'exactness and numerics: any place that converts through double when an exact path exists, non-correctly-rounded to_double, wrong interval certification, Sqrt_extension/CORE::Expr handling, hyperbolic conic predicate correctness, rational Bezier -> conic derivation correctness, ellipse coefficient derivation'],
  ['cgal_preconditions', 'CGAL preconditions and traps: every CGAL call whose precondition can be violated by Python input without our own check (leading to a CGAL exception in the middle of a mutation, or UB under NDEBUG), plus every item of docs/dev/CGAL_TRAPS_CHECKLIST.md that the code does not handle'],
  ['api_contract', 'Python API contract vs docs/dev/DESIGN.md §3 and the docstrings: missing/renamed methods, wrong return types, inconsistent argument conventions (kind, orientation, tolerance), error classes, repr formats, numpy/no-numpy paths, kind conversions matrix, PolygonSet semantics, Observer/Overlay argument mapping'],
  ['cython_layer', 'the Cython layer specifically: reference leaks (PyRef get/set), exception propagation from noexcept callbacks, GIL assumptions, object lifetime of Arrangement vs handles vs Traits, unique_ptr moves, _pending handling, enum/cname traps, thread-safety claims'],
]
const rawFindings = await parallel(LENSES.map(([key, lens]) => () => agent(`${COMMON}

TASK review_${key}: adversarially review the whole implementation through the lens of ${lens}. Read the code (not just the docs). Every finding must be concrete: file, line, what goes wrong, a minimal reproduction (Python or C++), and a fix. Do not report style issues. Verify each finding by reproducing it when feasible (you may compile scratch C++ / run Python against the built module, but do NOT rebuild the extension and do not modify repo files). Return the findings ranked by severity (critical/high/medium/low).`, { label: `review_${key}`, phase: 'Review', model: 'opus', schema: FINDINGS })))
const findings = rawFindings.filter(Boolean).flatMap(r => r.findings)
log(`review findings: ${findings.length}`)

const VERDICT = { type: 'object', properties: { refuted: { type: 'boolean' }, reason: { type: 'string' } }, required: ['refuted', 'reason'] }
const verified = await parallel(findings.map(f => () => agent(`${COMMON}

TASK refute: a reviewer claims the following defect. Try hard to REFUTE it by reading the code and reproducing (Python against the built module or scratch C++; do not modify repo files or rebuild). If the defect is real and reproducible (or clearly real from the code), set refuted=false; if it is mistaken, already handled elsewhere, or not reproducible, set refuted=true. Default to refuted=true when uncertain.
Finding: ${JSON.stringify(f)}`, { label: `refute:${f.title.slice(0, 30)}`, phase: 'Review', model: 'opus', schema: VERDICT }).then(v => ({ f, v }))))
const confirmed = verified.filter(x => x.v && !x.v.refuted).map(x => x.f)
log(`confirmed findings: ${confirmed.length}/${findings.length}`)

// ---------------------------------------------------------------- Finish
phase('Finish')
const finalFix = await agent(`${COMMON}

TASK final_fix: apply fixes for these CONFIRMED review findings (ranked by severity), add a regression test for each in tests/test_regressions.py, rebuild once (\`${PY} setup.py build_ext --inplace -j 8\`), and run the full suite (\`timeout 1800 ${PY} -m pytest -q tests\`), reporting its summary line:
${JSON.stringify(confirmed, null, 1).slice(0, 60000)}`, { label: 'final_fix', phase: 'Finish', model: 'opus', schema: FIX_REPORT })

const docs = await agent(`${COMMON}

TASK docs_and_helpers: (1) write arrangement_2d/regions.py — high-level helpers documented in DESIGN.md §3 (bounded_faces(arr), merge_faces(arr, faces) removing shared edges, union_outline(arr) -> PolygonSet for kinds with Boolean ops, split_face(arr, face, curve), faces_polygons(arr, tolerance), face_containing(arr, point), connected_components(arr), extract_regions(arr, predicate) etc.) with docstrings and tests/test_regions.py; (2) write arrangement_2d/plot.py — matplotlib helpers (plot_arrangement(arr, ax=None, tolerance, show_vertices, face_colors by data or index, clip bbox for unbounded kinds; plot_polygon_set) that import matplotlib lazily; a test that skips if matplotlib is missing (matplotlib may not be installed: check; if not, use \`${REPO}/.venv/bin/python -m pip install matplotlib\` only if pip is available — otherwise just skip); (3) write docs/user_guide.md (concepts, every kind with examples, history, data, observers, overlay, point location, Boolean ops, regions helpers, exactness model, error model, known CGAL 6.1 limitations from docs/dev/CGAL_TRAPS_CHECKLIST.md in user terms) and docs/api_reference.md (generated from the docstrings via a small script docs/gen_api_reference.py that imports the built module and walks the public names); update README.md (features, install, quick start, links). Run \`timeout 900 ${PY} -m pytest -q tests\` at the end and report the summary line.`, { label: 'docs', phase: 'Finish', model: 'opus', schema: REPORT })

return { integration: integ, tests: tr, fix_rounds: round, last_fix: lastFix, findings_total: findings.length, confirmed, final_fix: finalFix, docs }
