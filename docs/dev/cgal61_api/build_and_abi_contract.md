# CGAL 6.1 — Build & ABI contract for the `arrangement_2d` extension module

Scope: the rules every translation unit of the deliverable must obey. The deliverable is a
**single** CPython extension module built by
`/Users/sthv/PycharmProjects/arrangement-2d/setup.py` from

* `arrangement_2d/_core.pyx` → one generated `.cpp`,
* every `src/arr2d/src/*.cpp` (today: `registry.cpp`; tomorrow: one TU per kind),
* all of them including the headers under `src/arr2d/include/arr2d/`, which include CGAL.

Source of truth: the **installed** headers under `/opt/homebrew/include/CGAL` (CGAL **6.1**,
`CGAL_VERSION_NR 1060101000`, git `b26b07a1242`, release date 2025-09-29).
Quoted code is verbatim, with `file:line`.

**`[verified]`** marks a fact that was *measured* — compiled, linked and run in this session.
Everything else was read out of the headers. The probe compile line used by the other maps in this
directory is

```
/usr/bin/clang++ -std=c++17 -O0 -DCGAL_USE_CORE -DCGAL_USE_GMP -DCGAL_USE_MPFR \
  -I/opt/homebrew/include -L/opt/homebrew/lib -lgmp -lmpfr -o test test.cpp
```

but the measurements in this document additionally use multi-TU `.dylib`/`.so` builds and a real
CPython extension module (`PyInit_abicore`) imported into the project venv's Python 3.14.

---

## 0. Gotchas / surprises

1. **`CGAL_HAS_THREADS` is the killer.** It is *not* something you set — it is derived from
   `BOOST_HAS_THREADS` in `config.h:377-381`. It selects a **different class** for the internals of
   every `Epeck::FT`: `sizeof(CGAL::Lazy_rep_0<Interval_nt<false>, Exact_rational, To_interval>)` is
   **48 bytes with it, 40 bytes without** `[verified]`. Both definitions carry the *same mangled
   name*. Two TUs disagreeing link with **no warning**, import into Python fine, and then
   **segfault the interpreter** `[verified: exit 139 on `abicore.symdiff_area()`]`. At `-O0` the
   same mismatch survives silently — so it passes your debug build and crashes the release wheel.
   `-DBOOST_DISABLE_THREADS` on one TU is enough to flip it `[verified]`.

2. **`CGAL_ALWAYS_LEFT_TO_RIGHT` does not compile in CGAL 6.1.** The two references in
   `traits_segment_linear_polyline.md` (lines 759, 1162) describe a macro that is *dead code*:
   `Arr_polycurve_traits_2.h` lines 218, 251, 355, 388 reference an undeclared identifier `x_seg`,
   and line 386 is a syntax error (`x_polycurve (cond) ? a : b;`). Defining it gives 4+ hard
   compile errors the moment `Arr_polyline_traits_2`'s `Make_x_monotone_2` is instantiated
   `[verified]`. Never define it — and Boolean set operations *require* it to stay off anyway
   (§4).

3. **`-DCGAL_NO_ASSERTIONS` on its own does not compile either** — 13 errors, first at
   `Arr_segment_traits_2.h:424` (`use of undeclared identifier 'p'`), because the parameter is
   declared with `CGAL_assertion_code(p)` and *used* inside `CGAL_precondition(...)` `[verified]`.
   The four `CGAL_NO_*` macros are only safe as a **group**, i.e. through `NDEBUG`/`CGAL_NDEBUG`.

4. **`-DNDEBUG -DCGAL_DEBUG` really does re-enable CGAL's checks** (`assertions.h:34-38` undefines
   `CGAL_NDEBUG`), and it leaves C `assert()` disabled — unlike `-UNDEBUG`. `[verified]` in the
   real extension: `precondition_probe()` returns `CGAL::Failure_exception` under both
   `-DNDEBUG -UNDEBUG` and `-DNDEBUG -DCGAL_DEBUG`, and returns
   `boost::wrapexcept<std::overflow_error>` under plain `-DNDEBUG`.
   Cost of keeping checks on: **2.6×** wall time on a 120-segment arrangement
   (0.350 s vs 0.136 s per build) `[verified]`.

5. **`-ffast-math` / `-Ofast` / `-ffinite-math-only` produce WRONG GEOMETRY, not slow geometry.**
   `CGAL::Bbox_2()` is `(+inf, +inf, -inf, -inf)` (`Bbox_2.h:43-48`); under `-ffinite-math-only`
   it comes out as denormal garbage and **`CGAL::do_overlap(empty_box, unit_box)` returns `true`
   when the answer is `false`** `[verified]`. `bbox + bbox` returns garbage too. Clang even warns:
   `CGAL/Bbox_2.h:45: warning: use of infinity is undefined behavior due to the currently enabled
   floating-point options [-Wnan-infinity-disabled]`.

6. **The FPU rounding-mode machinery is *not* what `-ffast-math` breaks on this toolchain.**
   On arm64 CGAL sets the rounding mode with inline asm (`FPU.h:456-462`,
   `asm volatile ("MSR FPCR, %0")`) and blocks the optimiser with `asm volatile` opacifiers
   (`FPU.h:142-208`). `Protect_FPU_rounding` and CGAL's own rounding self-test pass under
   *every* flag tested, including `-Ofast` `[verified]`. `-frounding-math` is **not needed** on
   Apple clang/arm64 (accepted silently, changes nothing) `[verified]`.

7. **`-ffp-contract` defaults to `fast` on Apple clang 17, even at `-O0`** (`a*b+c` → `fmadd`)
   `[verified]`, and it is **harmless for CGAL** — `IA_opacify` sits between every interval
   operation, and the static-filter epsilon in
   `Filtered_kernel/internal/Static_filters/Orientation_2.h` is derived for the *un*fused case, so
   fusing only makes the filter more conservative. 200 000 near-degenerate `orientation` triples:
   0 disagreements between Epick and Epeck under `-ffp-contract=fast` `[verified]`. Still pin
   `-ffp-contract=off` for cross-platform reproducibility of any `double` arithmetic *you* write.

8. **`-fvisibility=hidden` (already in `setup.py`) is safe inside one `.so` and fatal across two.**
   Throw in TU A / catch in TU B of the **same** `.so`: `catch (const CGAL::Failure_exception&)`
   works. Across **two** `.dylib`s compiled with hidden visibility: the catch **misses** and the
   exception is only caught as `std::logic_error` `[verified]`. Likewise
   `CGAL::set_error_handler()` installed in one hidden-visibility `.so` **does not apply** to
   another `[verified]`. One extension module ⇒ fine; a second CGAL-using extension ⇒ each needs
   its own `set_error_handler` call and must not exchange CGAL exceptions.

9. **`-DCGAL_USE_CORE -DCGAL_USE_GMP -DCGAL_USE_MPFR` in `setup.py` are redundant no-ops.**
   `Installation/internal/enable_third_party_libraries.h` defines all of
   `CGAL_USE_GMP`, `CGAL_USE_MPFR`, `CGAL_USE_BOOST_MP`, `CGAL_USE_CORE` unconditionally in the
   header-only path `[verified: a build with none of those `-D` flags reports all four defined]`.
   Keep them for documentation value; do not rely on them to *force* anything —
   `-DCGAL_NO_GMP` overrides them (and then fails to compile) `[verified]`.

10. **`CGAL_DO_NOT_USE_BOOST_MP` changes `sizeof(CGAL::Exact_rational)` from 32 to 8**
    (`mpq_rational` → `CGAL::Gmpq`) `[verified]`. `arr2d/common.hpp` passes `Exact_rational`
    **by value** at the core boundary, so a mismatch here is a silent, total ABI break. Never set
    it on some TUs only; preferably never set it at all.

11. **Header-only is real: there is no `libCGAL` in `/opt/homebrew/lib`** `[verified: `ls` is
    empty]`. A linked CGAL binary needs exactly `libgmp.10.dylib`, `libmpfr.6.dylib`,
    `libc++.1.dylib`, `libSystem.B.dylib` — **zero Boost libraries** `[verified via `otool -L`]`.
    Dropping `-lmpfr` is a link error (`_mpfr_set_q` from
    `RET_boost_mp<mpq_rational>::To_interval`) `[verified]`.

12. **`-mmacosx-version-min=11.0` in `setup.py` is a lie on this machine.** Homebrew's gmp/mpfr are
    built for macOS 15.0, and the linker says so:
    `ld: warning: building for macOS-11.0, but linking with dylib '.../libmpfr.6.dylib' which was
    built for newer version 15.0` `[verified]`. The `.so` also records absolute
    `/opt/homebrew/opt/...` install names, so the wheel is not relocatable without
    `delocate`/`install_name_tool`.

13. **C++17 is a hard floor** (`config.h:152-154`: `#error "CGAL requires C++ 17"`). All of
    `-std=c++17`, `c++20`, `c++23` compile every traits class we use (segment, linear, polyline,
    circle-segment, conic, Bézier, geodesic-sphere, `Arrangement_with_history_2`, trapezoid-RIC and
    landmarks PL, `General_polygon_set_2`) `[verified]`. `c++23` adds 15
    `float_denorm_style`/`denorm_absent` deprecation warnings. The `-std` level still changes
    `CGAL_CXX20`, `CGAL_ASSUME`, `CGAL_UNREACHABLE`, `CGAL_CPP20_REQUIRE_CLAUSE` and
    `cpp11::result_of`, so it must be uniform across TUs.

---

## 1. The installation being targeted `[verified]`

| Item | Value |
|---|---|
| CGAL | 6.1, `CGAL_VERSION_NR 1060101000`, `CGAL_RELEASE_DATE 20250929`, git `b26b07a1242` |
| CGAL libraries | **none** — `ls /opt/homebrew/lib \| grep -i cgal` is empty; header-only |
| CORE | `/opt/homebrew/include/CGAL/CORE/` + `CGAL/CORE_*.h`, compiled into your TUs |
| GMP | 6.3.0 — `libgmp.10.dylib` (+ `libgmpxx`, unused) |
| MPFR | 4.2.2 — `libmpfr.6.dylib` |
| Boost | 1.89.0, **headers only** for this workload |
| Compiler | Apple clang 17.0.0 (clang-1700.4.4.1), target `arm64-apple-darwin25.6.0` |
| Python | 3.14.0, `EXT_SUFFIX=.cpython-314-darwin.so`, `Py_GIL_DISABLED=0` |
| Python `CFLAGS` | `-fno-strict-overflow -Wsign-compare -Wunreachable-code -DNDEBUG -g -O3 -Wall -O3 -arch arm64 -mmacosx-version-min=11.0 … -fPIC` |
| Python `LDSHARED` | `cc -bundle -undefined dynamic_lookup -arch arm64 -mmacosx-version-min=11.0 …` |

Note the `-DNDEBUG` in Python's own `CFLAGS`: setuptools emits it **before** `extra_compile_args`,
so anything you put in `extra_compile_args` wins (§8).

Auto-derived macro state for the default probe build `[verified]`:

```
CGAL_HEADER_ONLY              = 1
CGAL_USE_GMP / MPFR / CORE / BOOST_MP  = all defined (even with no -D flags)
CGAL_HAS_THREADS              = defined      (from BOOST_HAS_THREADS)
CGAL_NO_ATOMIC                = undefined
CGAL_USE_SSE2                 = undefined    (arm64)
CGAL_FPU_HAS_EXCESS_PRECISION = undefined    (arm64)
CGAL_ALWAYS_ROUND_TO_NEAREST  = undefined
CGAL_EIGEN3_ENABLED           = undefined    (no Eigen installed)
```

Reference sizes on this platform, **stable** across every macro tested `[verified]`:

```
sizeof(Interval_nt<false>)        = 16      sizeof(Epeck::FT)            = 16
sizeof(Epeck::Point_2)            =  8      sizeof(Arr_segment_traits_2<Epeck>::X_monotone_curve_2) = 32
sizeof(Arrangement_2<SegTr>)      = 248     ::Vertex 48   ::Halfedge 72   ::Face 104
sizeof(Arrangement_on_surface_2<SphTr, Arr_spherical_topology_traits_2>) = 288
sizeof(FPU_CW_t)                  =  8      sizeof(Protect_FPU_rounding<true>) = 8
sizeof(CGAL::Exact_rational)      = 32      (mpq_rational)
```

The *public* types therefore give you no protection: a macro mismatch does **not** show up as a
`sizeof` difference in anything you can see from the binding layer. The break is in the internal
rep classes (§3).

---

## 2. ODR-critical macros

An "ODR-critical" macro is one that changes **class layout**, **member presence**, **which
template specialisation is selected**, or **the body of an inline/template function**, while
leaving the *mangled name* unchanged. Mismatch ⇒ the linker silently keeps one definition and
discards the other (`-O0`), or each TU keeps its own inlined copy (`-O2`), or both, unpredictably.

### 2.1 Summary table

| Macro | Set by | What it changes | Mismatch symptom | Verdict |
|---|---|---|---|---|
| `CGAL_HAS_THREADS` | auto (`config.h:377`), flipped by `-DCGAL_HAS_NO_THREADS` or `-DBOOST_DISABLE_THREADS` | `Lazy_rep_selector<AT>::value` 2→1 ⇒ different `Lazy_rep` specialisation; `std::once_flag once` member appears/disappears; `tss.h` `thread_local` → `static` | **48 vs 40-byte `Lazy_rep_0`, heap corruption, SIGSEGV** `[verified]` | **MUST match. Never set.** |
| `NDEBUG` | Python `CFLAGS`, `-DNDEBUG` | ⇒ `CGAL_NDEBUG` ⇒ all four `CGAL_NO_*`; changes `Interval_nt()` ctor body; removes `Check_FPU_rounding_mode_is_restored`; erases every `CGAL_precondition` from inline/template code | preconditions vanish from the TU that asked for them; wrong exception type reaches Python | **MUST match** |
| `CGAL_NDEBUG` | `-DCGAL_NDEBUG`, or derived from `NDEBUG` | as above | as above | **MUST match** |
| `CGAL_DEBUG` | `-DCGAL_DEBUG` | undefines `CGAL_NDEBUG` (`assertions.h:34-38`) | as above | **MUST match** |
| `CGAL_NO_ASSERTIONS` `_PRECONDITIONS` `_POSTCONDITIONS` `_WARNINGS` | derived, or `-D` | individually: change inline bodies **and break the build** | `-DCGAL_NO_ASSERTIONS` alone: 13 compile errors `[verified]` | **Never set individually** |
| `CGAL_NO_STATIC_FILTERS` | `-D` | template **default argument** `Filtered_kernel<CK, UseStaticFilters=true/false>` (`Filtered_kernel_fwd.h:21-24`, `Filtered_kernel.h:108-113`); `constexpr bool CGAL::epeck_use_static_filter` 1→0; `Epick::Orientation_2` changes from `Static_filters_predicates::Orientation_2<…>` to `Filtered_predicate_RT_FT<…>` while `class CGAL::Epick` keeps its name `[verified]` | same-named class with different base/members in two TUs | **MUST match. Leave undefined.** |
| `CGAL_NO_STATIC_FILTERS_FOR_LAZY_KERNEL` | `-D` | same, for `Lazy_kernel.h:84,133` (this is the one that actually reaches **Epeck**) | ditto | **MUST match. Leave undefined.** |
| `CGAL_DO_NOT_USE_BOOST_MP` | `-D` | `CGAL::Exact_rational` `mpq_rational` (32 B) → `CGAL::Gmpq` (8 B) `[verified]` | every by-value `Exact_rational` in `arr2d` changes size | **MUST match. Leave undefined.** |
| `CGAL_NO_GMP` / `CGAL_DISABLE_GMP` / `CGAL_NO_MPFR` | `-D` | `#undef CGAL_USE_GMP/MPFR` (`enable_third_party_libraries.h:19-26`) — but leaves `CGAL_USE_BOOST_MP`/`CGAL_USE_CORE` set | **hard compile error** here (`incomplete type 'boost::multiprecision::backends::gmp_int'`) `[verified]` | **Never set** |
| `CGAL_NO_ATOMIC` | `-D` | `std::atomic<T>` → plain `T` for CORE's global state (`CoreDefs.h:33-52`) and for the curve-index counters in `Arr_conic_traits_2.h:160-164`, `Arr_circle_segment_traits_2.h:83-87`, `Arr_counting_traits_2.h:813-817`, `Box_intersection_d/Box_d.h:38-42` | inline functions with the same mangled name return `std::atomic<T>&` in one TU and `T&` in the other; function-local statics of different type collide | **MUST match. Leave undefined.** |
| `CGAL_DISABLE_ROUNDING_MATH_CHECK` | `-D` | removes the `static const Test_runtime_rounding_modes tester` **member** of `Interval_nt<Protected>` (`Interval_nt.h:294-296, 865-868`) and the per-TU FPU-restore checker (`test_FPU_rounding_mode_impl.h`) | member presence differs between TUs | **MUST match. Leave undefined** (it is a cheap, useful self-test) |
| `CGAL_USE_SSE2` | auto, x86-64 only (`FPU.h:117-121`) | `Interval_nt` stores `__m128d val` instead of `double _inf,_sup`; `Lazy_rep_selector<Interval_nt<b>>::value` 2→1 (`Lazy.h:274-285`) | different `Interval_nt` representation and `Lazy_rep` size | derived from `-march`/`-msse2` — **keep arch flags uniform** (matters for x86-64 wheels, not arm64) |
| `CGAL_ALWAYS_ROUND_TO_NEAREST` | `-D` | `CGAL_FE_PROTECTED` becomes `CGAL_FE_TONEAREST`; `IA_up` becomes `nextafter`; disables `CGAL_USE_SSE2` | different interval arithmetic in different TUs | **Never set** |
| `CGAL_ENABLE_DISABLE_ASSERTIONS_AT_RUNTIME` | `-D` | `CGAL::get_use_assertions()` becomes a thread-local `bool&` instead of `constexpr true` (`assertions.h:47-68`) — changes **every** assertion macro expansion | half the TUs ignore the runtime switch | **MUST match** (see §8 if you want a Python-visible "strict mode") |
| `CGAL_HEADER_ONLY` | auto (`config.h:24-31`) | selects `assertions_impl.h`, `CoreDefs.h`'s `inline` global-state accessors, `Check_FPU_rounding_mode_is_restored` | if one TU had `CGAL_NOT_HEADER_ONLY`, `CGAL::assertion_fail` etc. become undefined externals ⇒ **loud link error**, not silent | **MUST match** (leave alone; there is no library to link anyway) |
| `CGAL_CHECK_EXACTNESS`, `CGAL_CHECK_EXPENSIVE` | `-D` | enable whole extra families of `CGAL_expensive_*` checks in inline code | inline bodies differ | **MUST match. Never ship either** — some arrangement checks are O(n²) |
| `-std=c++17/20/23` | `-std` | `CGAL_CXX20`, `CGAL_CXX23`, `CGAL_ASSUME`, `CGAL_UNREACHABLE`, `CGAL_CPP20_REQUIRE_CLAUSE`, `CGAL_TYPE_CONSTRAINT`, `cpp11::result_of`, `__cpp_lib_*`-guarded bodies in `assertions.h` | inline bodies differ (no break observed in a narrow mixed c++17/c++20 test `[verified]`, but it is still UB) | **MUST match** |
| `BOOST_ALL_NO_LIB` | `-D` | **MSVC only.** Suppresses `#pragma comment(lib, …)` in `boost/config/auto_link.hpp:458-478`. On this toolchain no CGAL header we use pulls `boost/config/auto_link.hpp` at all `[verified: include-trace count = 0]`, and `config.h:29-31` already sets `CGAL_NO_AUTOLINK 1` because `CGAL_HEADER_ONLY` | none on macOS/Linux | **Harmless. Define it for the Windows build**; irrelevant here |
| `CGAL_USE_CORE`, `CGAL_USE_GMP`, `CGAL_USE_MPFR` | `-D` **and** auto | already `1` from `enable_third_party_libraries.h:16-17,56-62`; the `-D` is a same-token redefinition ⇒ no warning, no effect `[verified]` | none | Harmless; keep or drop |

### 2.2 The assertion family, exactly (`assertions.h:24-45`)

```cpp
#ifdef NDEBUG
#  ifndef CGAL_NDEBUG
#    define CGAL_NDEBUG
#  endif
#endif

// The macro `CGAL_DEBUG` allows to force CGAL assertions, even if `NDEBUG`
// is defined,
#ifdef CGAL_DEBUG
#  ifdef CGAL_NDEBUG
#    undef CGAL_NDEBUG
#  endif
#endif

#ifdef CGAL_NDEBUG
#  define CGAL_NO_ASSERTIONS
#  define CGAL_NO_PRECONDITIONS
#  define CGAL_NO_POSTCONDITIONS
#  define CGAL_NO_WARNINGS
#endif
```

Measured truth table `[verified]`:

| flags | `CGAL_NDEBUG` | `CGAL_NO_PRECONDITIONS` | `CGAL_PRECONDITIONS_ENABLED` | builds? |
|---|---|---|---|---|
| *(none)* | no | no | 1 | yes |
| `-DNDEBUG` | yes | yes | 0 | yes |
| `-DCGAL_NDEBUG` | yes | yes | 0 | yes |
| `-DNDEBUG -DCGAL_DEBUG` | **no** | **no** | **1** | yes |
| `-DCGAL_NO_ASSERTIONS` | no | no | 1 | **NO — 13 errors** |
| `-DCGAL_NO_PRECONDITIONS` | no | yes | 0 | yes (but do not) |
| `-DCGAL_NO_POSTCONDITIONS` / `-DCGAL_NO_WARNINGS` | no | no | 1 | yes (but do not) |

The `-DCGAL_NO_ASSERTIONS` failure is structural, not incidental —
`Arr_segment_traits_2.h:422-436`:

```cpp
    Comparison_result operator()(const X_monotone_curve_2& cv1,
                                 const X_monotone_curve_2& cv2,
                                 const Point_2& CGAL_assertion_code(p)) const
    {
      ...
      CGAL_precondition_code(auto compare_xy = kernel.compare_xy_2_object());
      CGAL_precondition((m_traits.compare_y_at_x_2_object()(p, cv1) == EQUAL) &&
                        (m_traits.compare_y_at_x_2_object()(p, cv2) == EQUAL));
```

`p` is named by `CGAL_assertion_code` but used by `CGAL_precondition`. Turning assertions off
without turning preconditions off leaves the parameter unnamed and the use dangling.

Also note `config.h:52-54`: `#if defined(CGAL_TEST_SUITE) && defined(NDEBUG)` → `#error`.

### 2.3 `CGAL_HAS_THREADS` — the layout-changing one

`config.h:377-381`:

```cpp
// If CGAL_HAS_THREADS is not defined, then CGAL code assumes
// it can do any thread-unsafe things (like using static variables).
#if !defined CGAL_HAS_THREADS && !defined CGAL_HAS_NO_THREADS
#  if defined BOOST_HAS_THREADS || defined _OPENMP
#    define CGAL_HAS_THREADS
#  endif
#endif
```

`Lazy.h:271-288` — the specialisation selector:

```cpp
#ifdef CGAL_HAS_THREADS
template<class AT>struct Lazy_rep_selector { static constexpr int value = 0; };
# if defined CGAL_USE_SSE2 && !defined __SANITIZE_THREAD__ && !__has_feature(thread_sanitizer)
template<bool b>struct Lazy_rep_selector<Interval_nt<b>> { static constexpr int value = 1; };
...
# else
template<bool b>struct Lazy_rep_selector<Interval_nt<b>> { static constexpr int value = 2; };
# endif
#else
template<class AT>struct Lazy_rep_selector { static constexpr int value = 1; };
#endif
```

`Lazy.h:435-439` — the member that appears and disappears:

```cpp
  mutable AT at;
  mutable std::atomic<ET*> ptr_ { nullptr };
#ifdef CGAL_HAS_THREADS
  mutable std::once_flag once;
#endif
```

and `Lazy.h:475-484`, the matching body change:

```cpp
  const ET & exact() const
  {
#ifdef CGAL_HAS_THREADS
    std::call_once(once, [this](){this->update_exact();});
#else
    if (is_lazy())
      this->update_exact();
#endif
    return exact_unsafe();
  }
```

Measured on arm64 (`CGAL_USE_SSE2` undefined) `[verified]`:

| build | `Lazy_rep_selector<Interval_nt<false>>::value` | `sizeof(Lazy_rep_0<AT,Exact_rational,To_interval>)` |
|---|---|---|
| default (`CGAL_HAS_THREADS`) | **2** | **48** |
| `-DCGAL_HAS_NO_THREADS` | **1** | **40** |
| `-DBOOST_DISABLE_THREADS` | **1** | **40** |
| `-DCGAL_NO_ATOMIC` | 2 | 48 |

`offsetof(Lazy_rep_0::ptr_)` happens to be 32 in both — so a mismatched read of `ptr_` *works*,
which is exactly why the corruption is deferred until something touches `once` past the end of the
shorter allocation.

### 2.4 `CGAL_NO_ATOMIC` and CORE global state

`CoreDefs.h:33-52` (header-only branch):

```cpp
#ifdef CGAL_HEADER_ONLY
  #define CGAL_GLOBAL_STATE_VAR(TYPE, NAME, VALUE)  \
    inline TYPE & get_static_##NAME()               \
    {                                               \
      static TYPE NAME(VALUE);                      \
      return NAME;                                  \
    }
```

used as e.g.

```cpp
#ifdef CGAL_NO_ATOMIC
CGAL_GLOBAL_STATE_VAR(bool, AbortFlag, true)
#else
CGAL_GLOBAL_STATE_VAR(std::atomic<bool>, AbortFlag, true)
#endif
```

Two consequences for the binding:

* The Itanium ABI does **not** mangle return types, so `CORE::get_static_AbortFlag()` has the same
  symbol whether it returns `bool&` or `std::atomic<bool>&`. A mismatch is invisible to the linker.
* These are **function-local statics in inline functions**, not `thread_local`. CORE's precision
  knobs (`get_static_defRelPrec()`, `get_static_defAbsPrec()`, `get_static_EscapePrec()`,
  `get_static_AbortFlag()`, `get_static_InvalidFlag()`, …) are **process-global shared state** for
  the whole `.so`. If you expose CORE precision to Python (Bézier/conic kinds), it is a global
  setting, not per-arrangement — and it is shared with every thread.

`tss.h:15-21` shows the other half of the `CGAL_HAS_THREADS` story:

```cpp
#if defined( CGAL_HAS_THREADS )
#  define CGAL_STATIC_THREAD_LOCAL_VARIABLE_0(TYPE, VAR)       \
  static thread_local TYPE VAR
```

so any CGAL singleton built with `CGAL_STATIC_THREAD_LOCAL_VARIABLE*` is per-thread in one TU and
process-global in the other.

### 2.5 `CGAL_NO_STATIC_FILTERS`

`Filtered_kernel_fwd.h:21-26` and `Filtered_kernel.h:108-113` make it a **template default
argument**:

```cpp
#ifdef CGAL_NO_STATIC_FILTERS
template < typename CK, bool UseStaticFilters = false >
#else
template < typename CK, bool UseStaticFilters = true >
#endif
struct Filtered_kernel;
```

and `Exact_predicates_exact_constructions_kernel.h:31-37`:

```cpp
constexpr bool epeck_use_static_filter =
#ifdef CGAL_NO_STATIC_FILTERS
    false;
#else
    true;
#endif
```

Measured `[verified]`:

| | default | `-DCGAL_NO_STATIC_FILTERS` |
|---|---|---|
| `CGAL::epeck_use_static_filter` | 1 | 0 |
| `Epeck::Has_static_filters` | 0 | 0 |
| `sizeof(Epick::Orientation_2)` | 9 | 9 |
| `Epick::Orientation_2` demangled | `CGAL::internal::Static_filters_predicates::Orientation_2<CGAL::Filtered_kernel_base<…>>` | `CGAL::Filtered_predicate_RT_FT<…Simple_cartesian<CGAL::Mpzf>…>` |
| `Arr_segment_traits_2<Epeck>::Compare_y_at_x_2` mangled name | *identical* | *identical* |

That last row is the point: the outer names are stable, the member types are not. The compiler
cannot warn, and the linker cannot warn.

---

## 3. Proof of the two most dangerous mismatches

Harness (all in the scratchpad, not in the repo): two TUs `tu_a.cpp` / `tu_b.cpp` including
`<CGAL/Exact_predicates_exact_constructions_kernel.h>` and `<CGAL/Lazy.h>`, each exporting
`extern "C"` probes; a driver; and a real CPython extension
(`tu1_arr.cpp` = arrangements, `tu2_bso.cpp` = Boolean set ops, `module.cpp` = `PyInit_abicore`).

### 3.1 `CGAL_HAS_THREADS` mismatch → SIGSEGV inside CPython `[verified]`

Build, one `.so`, `-O2 -fvisibility=hidden`, TU2 gets `-DCGAL_HAS_NO_THREADS`:

```
### build 'bad1'  TU1 extra=[] TU2 extra=[-DCGAL_HAS_NO_THREADS]
linked abicore.cpython-314-darwin.so          <- no warning at compile or link
info: {'tu1': 'tu1:threads,checks,std2017',   'tu1_lazy_rep_size': 48,
       'tu2': 'tu2:nothreads,checks,std2017', 'tu2_lazy_rep_size': 40}
faces(6): 36                                   <- TU1 alone: fine
probe:    1                                    <- TU1 alone: fine
Segmentation fault: 11                         <- TU2's symmetric_difference()
python exit=139
```

Same mismatch at `-O0`: **no crash**, everything prints correct answers. The mismatch is a
release-build-only failure.

Same mismatch in a plain (non-Python) single `.dylib`:

| build | result |
|---|---|
| `-O0`, no visibility flags | SURVIVED (linker collapsed everything to TU-A's definition) |
| `-O0 -fvisibility=hidden` | SURVIVED |
| `-O2`, no visibility flags | **exit 139** |
| `-O2 -fvisibility=hidden` | **exit 139** |

Mechanism: at `-O2` TU-B inlines the 40-byte `Lazy_rep_0` layout and writes `at` at an offset that,
in TU-A's 48-byte layout, lands on other members; the following `delete` walks a corrupted heap.

### 3.2 `NDEBUG` mismatch → preconditions silently vanish `[verified]`

`CGAL::operator/(Lazy_exact_nt, Lazy_exact_nt)` is a template (`Lazy_exact_nt.h:674-682`):

```cpp
template <typename ET1, typename ET2>
Lazy_exact_nt< typename Coercion_traits<ET1, ET2>::Type >
operator/(const Lazy_exact_nt<ET1>& a, const Lazy_exact_nt<ET2>& b)
{
  CGAL_PROFILER(std::string("calls to    : ") + std::string(CGAL_PRETTY_FUNCTION));
  CGAL_precondition(b != 0);
  ...
}
```

Two TUs, one dylib, `FT(1)/FT(0)` in each:

| build | TU-A result | TU-B result |
|---|---|---|
| both checks-on (control) | `CGAL::Precondition_exception` | `CGAL::Precondition_exception` |
| `-O0`, `-DNDEBUG` on **A** only | `boost::wrapexcept<std::overflow_error>` "Division by zero." | **`boost::wrapexcept<std::overflow_error>`** — B lost its precondition |
| `-O0`, `-DNDEBUG` on **B** only | `Precondition_exception` | **`Precondition_exception`** — B kept a check it asked to remove |
| `-O2`, `-DNDEBUG` on **B** only | `Precondition_exception` | `boost::wrapexcept<std::overflow_error>` |

i.e. **which behaviour you get depends on link order and optimisation level.** Here CGAL happened
to have a downstream `std::overflow_error` to fall back on; most CGAL preconditions have no such
net and their absence is straight UB.

### 3.3 What did *not* break

* `-std=c++17` TU + `-std=c++20` TU, one dylib, `-O2`: SURVIVED, all values correct `[verified]`.
  Still forbidden — it is UB that this test did not trigger.
* `-DCGAL_NO_ATOMIC`, `-DCGAL_NO_STATIC_FILTERS`, `-DCGAL_DISABLE_ROUNDING_MATH_CHECK` did not
  change `sizeof(Arrangement_2<…>)`, `sizeof(Epeck::FT)` or `sizeof(Lazy_rep_0)` `[verified]` —
  their damage is in member types and inline bodies, which no `sizeof` probe can see.

---

## 4. `CGAL_ALWAYS_LEFT_TO_RIGHT`

### 4.1 Every use in the installed headers

```
Arr_polycurve_basic_traits_2.h:704    Compare_y_at_x_left_2 / compare_vertical
Arr_polycurve_basic_traits_2.h:1217   Construct_x_monotone_curve_2(X_monotone_subcurve_2)
Arr_polycurve_basic_traits_2.h:1336   Construct_x_monotone_curve_2(ForwardIterator, ForwardIterator)
Arr_polyline_traits_2.h:478           Construct_x_monotone_curve_2(Point_2, Point_2)
Arr_polyline_traits_2.h:580           Construct_x_monotone_curve_2 from a point range
Arr_polycurve_traits_2.h:199,217,249  Make_x_monotone_2::operator_impl(…, Arr_all_sides_oblivious_tag)
Arr_polycurve_traits_2.h:287,295      ditto
Arr_polycurve_traits_2.h:336,354,386,431,443,451  Make_x_monotone_2::operator_impl(…, Arr_not_all_sides_oblivious_tag)
Arr_polycurve_traits_2.h:711          Intersect_2  (CGAL_assertion(consistent))
```

Nothing else in the installed tree mentions it — it is a polycurve/polyline-only switch.

### 4.2 What it is supposed to do

**Off (the default): an x-monotone polycurve keeps the direction of its input.** A polyline built
from points `(3,0),(2,1),(1,0),(0,1)` stays right-to-left; `compare_endpoints_xy_2_object()(xcv)`
returns `LARGER`, `construct_min_vertex_2` gives `(0,1)` and `construct_max_vertex_2` gives `(3,0)`
`[verified]`.

**On: every constructor normalises to left-to-right.** E.g. `Arr_polyline_traits_2.h:472-482`:

```cpp
      X_monotone_subcurve_2 seg =  this->m_poly_traits.subcurve_traits_2()->
        construct_x_monotone_curve_2_object()(p, q);

#ifdef CGAL_ALWAYS_LEFT_TO_RIGHT
      if (this->m_poly_traits.subcurve_traits_2()->compare_xy_2_object()(p,q) ==
          LARGER)
        seg = this->m_poly_traits.subcurve_traits_2()->
          construct_opposite_2_object()(seg);
#endif
```

and `Arr_polycurve_basic_traits_2.h:1246-1252` for a single subcurve,
`:1365-1372` for a range, plus the `Make_x_monotone_2` variants that flip each subcurve and
`push_front` instead of `push_back` (`Arr_polycurve_traits_2.h:287-300`). In
`compare_vertical` (`Arr_polycurve_basic_traits_2.h:701-717`) it lets the traits skip the direction
query entirely:

```cpp
#ifdef CGAL_ALWAYS_LEFT_TO_RIGHT
      const Comparison_result l2r_smaller = SMALLER;
      const Comparison_result l2r_larger = LARGER;
#else
      auto cmp_endpints_xy = m_poly_traits.compare_endpoints_xy_2_object();
      const bool l2r = cmp_endpints_xy(xcv[0]) == SMALLER;
      const Comparison_result l2r_smaller = l2r ? SMALLER : LARGER;
      const Comparison_result l2r_larger = l2r ? LARGER : SMALLER;
#endif
```

and in `Intersect_2` it hardens an assumption into an assertion
(`Arr_polycurve_traits_2.h:710-713`):

```cpp
  const bool consistent = (dir1 == dir2);
#ifdef CGAL_ALWAYS_LEFT_TO_RIGHT
  CGAL_assertion(consistent);
#endif
```

### 4.3 It does not build `[verified]`

```
$ clang++ -std=c++17 -O0 -DCGAL_ALWAYS_LEFT_TO_RIGHT -I/opt/homebrew/include … l2r.cpp
/opt/homebrew/include/CGAL/Arr_polycurve_traits_2.h:218:28: error: use of undeclared identifier 'x_seg'
/opt/homebrew/include/CGAL/Arr_polycurve_traits_2.h:251:56: error: use of undeclared identifier 'x_seg'
/opt/homebrew/include/CGAL/Arr_polycurve_traits_2.h:355:28: error: use of undeclared identifier 'x_seg'
/opt/homebrew/include/CGAL/Arr_polycurve_traits_2.h:388:56: error: use of undeclared identifier 'x_seg'
```

The guarded code was never updated when the surrounding code moved from `CGAL::Object` to
`std::variant` + `std::get_if` (the variable is now `x_seg_p`, a pointer). Line 386 is worse — it
is not even valid syntax:

```cpp
          x_polycurve (cmp_seg_endpts(*x_seg_p) == LARGER) ?
            ctr_x_curve(ctr_seg_opposite(*x_seg_p)) : ctr_x_curve(*x_seg_p);
```

The identical build without the macro compiles and runs `[verified]`.

### 4.4 Must it be on or off for Boolean set operations? **OFF.**

Beyond "it does not compile", BSO is *built around* directed curves.
`Boolean_set_operations_2/Gps_agg_op_visitor.h:113-131`:

```cpp
  void insert_edge_to_hash(Halfedge_handle he, const X_monotone_curve_2& cv)
  {
    const Comparison_result he_dir =
      ((Arr_halfedge_direction)he->direction() == ARR_LEFT_TO_RIGHT) ?
      SMALLER : LARGER;

    const Comparison_result cv_dir =
      this->m_arr_access.arrangement().geometry_traits()->
            compare_endpoints_xy_2_object()(cv);

    if (he_dir == cv_dir) {
      (*m_edges_hash)[he] = cv.data().bc();
      (*m_edges_hash)[he->twin()] = cv.data().twin_bc();
    }
    else {
      (*m_edges_hash)[he] = cv.data().twin_bc();
      (*m_edges_hash)[he->twin()] = cv.data().bc();
    }
  }
```

The *curve's own direction* is what tells the aggregated Boolean engine which side of the halfedge
carries which boundary condition — that is exactly the polygon-orientation information. That is
also why the BSO traits are required to model `AosDirectionalXMonotoneTraits_2` (see the class
comment of `Arr_directional_non_caching_segment_basic_traits_2.h:23-29`). Normalising every curve
to left-to-right would erase it. Conclusion for `arr2d`: **never define
`CGAL_ALWAYS_LEFT_TO_RIGHT`; treat "curve direction is meaningful and must survive the binding
round-trip" as a core invariant.**

---

## 5. Floating-point flags

### 5.1 How CGAL protects itself on arm64

`FPU.h:455-462` — the rounding mode is set with inline asm, so no compiler FP flag can remove it:

```cpp
# elif defined  __aarch64__
#define CGAL_IA_SETFPCW(CW) asm volatile ("MSR FPCR, %0" : :"r" (CW))
#define CGAL_IA_GETFPCW(CW) asm volatile ("MRS %0, FPCR" : "=r" (CW))
typedef unsigned long FPU_CW_t;
#define CGAL_FE_TONEAREST    (0x0)
#define CGAL_FE_TOWARDZERO   (0xC00000)
#define CGAL_FE_UPWARD       (0x400000)
#define CGAL_FE_DOWNWARD     (0x800000)
```

`FPU.h:535-560`:

```cpp
template <bool Protected = true> struct Protect_FPU_rounding;

template <>
struct Protect_FPU_rounding<true>
{
  Protect_FPU_rounding(FPU_CW_t r = CGAL_FE_PROTECTED)
    : backup( FPU_get_and_set_cw(r) ) {}
  ~Protect_FPU_rounding() { FPU_set_cw(backup); }
private:
  FPU_CW_t backup;
};
```

and the optimiser barrier that makes interval arithmetic sound, `FPU.h:142-152` +
`FPU.h:295-302`:

```cpp
inline double IA_opacify(double x)
{
#ifdef __llvm__
# ...
# elif (defined __VFP_FP__ && !defined __SOFTFP__) || defined __aarch64__
  asm volatile ("" : "+w"(x) );
# ...
  return x;
```

```cpp
#elif 1
// LLVM doesn't have -frounding-math so needs extra protection.
// GCC also migrates fesetround calls over FP instructions, so protect
// everyone.
#  define CGAL_IA_FORCE_TO_DOUBLE(x) CGAL::IA_opacify(x)
```

That comment is the answer to the `-frounding-math` question: CGAL assumes clang has no such
option and inserts asm barriers instead.

`Interval_nt.h:860-868` contains CGAL's own runtime self-test, instantiated as a static member of
every `Interval_nt<Protected>` unless `CGAL_DISABLE_ROUNDING_MATH_CHECK`:

```cpp
      typename Interval_nt<>::Internal_protector P;
      CGAL_assertion_msg(-CGAL_IA_MUL(-1.1, 10.1) != CGAL_IA_MUL(1.1, 10.1),
                         "Wrong rounding: did you forget the  -frounding-math  option if you use GCC (or  -fp-model=strict  for Intel)?");
```

### 5.2 Measured flag matrix `[verified]`

Test program: FPCR readback around `Protect_FPU_rounding`; CGAL's own `CGAL_IA_MUL`/`CGAL_IA_DIV`
self-test; NaN/Inf predicates; 200 000 random `Interval_nt<false>` evaluations of `a*b-c` checked
against exact `mpq_rational`; 200 000 near-degenerate `orientation` triples Epick-vs-Epeck; the
`Bbox_2` empty-box test.

| flags | FPCR set to `CGAL_FE_UPWARD` inside protector | CGAL rounding self-test | interval `a*b-c` unsound / 200 000 | Epick vs Epeck orientation wrong / 200 000 | `is_valid(NaN)` (must be 0) | `isfinite(inf)` (must be 0) | `do_overlap(Bbox_2(), unit)` (must be 0) |
|---|---|---|---|---|---|---|---|
| `-O2` | yes (0x400000) | OK | 0 | 0 | 0 | 0 | 0 |
| `-O2 -ffp-contract=off` | yes | OK | 0 | 0 | 0 | 0 | 0 |
| `-O2 -ffp-contract=fast` | yes | OK | 0 | 0 | 0 | 0 | 0 |
| `-O2 -fno-math-errno` | yes | OK | 0 | 0 | 0 | 0 | — |
| `-O2 -funsafe-math-optimizations` | yes | OK | 0 | 0 | 0 | 0 | **0** |
| `-O2 -frounding-math` | yes | OK | 0 | 0 | 0 | 0 | — |
| `-O2 -ffinite-math-only` | yes | OK | 0 | 0 | **1 (WRONG)** | **1 (WRONG)** | **1 (WRONG)** |
| `-O2 -ffast-math` | yes | OK | 0 | 0 | **1 (WRONG)** | **1 (WRONG)** | **1 (WRONG)** |
| `-Ofast` | yes | OK | 0 | 0 | **1 (WRONG)** | **1 (WRONG)** | **1 (WRONG)** |

The wrong geometric answer, verbatim `[verified]`:

```
======== -O2
empty  = [inf,-inf]x[inf,-inf]
do_overlap(empty, unit) (must be 0) = 0
empty+unit (must be [0,1]x[0,1]) = [0,1]x[0,1]
sqrt([-1,-1]) = [0,nan] is_valid=0 (must be invalid/NaN)

======== -Ofast
empty  = [4.18531e-314,3.00916e-314]x[4.94066e-324,4.18531e-314]
do_overlap(empty, unit) (must be 0) = 1
empty+unit (must be [0,1]x[0,1]) = [4.94066e-324,3.00916e-314]x[4.94066e-324,4.18531e-314]
sqrt([-1,-1]) -> CGAL error: assertion violation!
     (!is_valid(i)) || (!is_valid(s)) || (!(i>s))   Interval_nt.h:172
```

The culprit is `-ffinite-math-only` (a component of `-ffast-math`, itself a component of `-Ofast`),
which makes `std::numeric_limits<double>::infinity()` undefined behaviour. `Bbox_2.h:43-48`:

```cpp
  Bbox_2()
    : rep(CGAL::make_array(std::numeric_limits<double>::infinity(),
                           std::numeric_limits<double>::infinity(),
                           - std::numeric_limits<double>::infinity(),
                           - std::numeric_limits<double>::infinity() ))
  {}
```

Bounding boxes are used by the arrangement sweep's box filters, by
`Arr_Bezier_curve_traits_2`/`Arr_conic_traits_2` bounding logic, and by `Box_intersection_d`.
`is_valid`/NaN checks are used inside `Interval_nt`'s constructor assertion and its `is_valid()`.

Clang tells you, if you read the warnings: `-Wnan-infinity-disabled` fires on
`CGAL/Bbox_2.h:45`, `:46` and `boost/math/policies/error_handling.hpp:326,354` `[verified]`.

### 5.3 The exact flag set `setup.py` must force

```
-ffp-contract=off  -fno-fast-math  -fno-finite-math-only  -fno-unsafe-math-optimizations
-fno-associative-math  -fno-reciprocal-math  -fhonor-nans  -fhonor-infinities
```

Justification per flag:

* `-ffp-contract=off` — Apple clang's default is `fast` (`fmadd` emitted even at `-O0`)
  `[verified]`. Not needed for CGAL itself, but any `double` arithmetic **you** write in
  `arr2d` (bbox pre-filters, tolerance comparisons, `to_double` post-processing) becomes
  architecture-dependent otherwise, and results stop being bit-reproducible between arm64 and
  x86-64 wheels.
* `-fno-finite-math-only` / `-fhonor-nans` / `-fhonor-infinities` — the measured wrong-answer flag.
  These are the ones that actually matter.
* `-fno-fast-math`, `-fno-unsafe-math-optimizations`, `-fno-associative-math`,
  `-fno-reciprocal-math` — defensive: they neutralise a user who sets `ARR2D_OPT=-Ofast` or
  `CFLAGS=-ffast-math`, because later flags win on the clang command line and
  `extra_compile_args` comes after `CFLAGS`.
* `-frounding-math` is **not** required and does nothing measurable here `[verified]`. Add it only
  for a GCC build (where CGAL's `Interval_nt` self-test message explicitly asks for it).
* Do **not** add `-DCGAL_DISABLE_ROUNDING_MATH_CHECK`: keeping it off costs one static object per
  TU and gives you a runtime abort if the FPU state is ever left dirty.

**Scope: every TU that includes any CGAL header, plus the Cython-generated `.cpp`.** The FP flags
are not an ODR problem (they do not change declarations), but they are a *correctness* problem for
whichever TU has them wrong — and `Bbox_2`, `Interval_nt` and `is_valid` are header-inline, so
whichever TU instantiates them decides. In a single-extension build the cheapest correct policy is
"one flag list, applied to the whole `Extension`", which is what `setup.py` already does
structurally.

`-Ofast` must be rejected outright: `ARR2D_OPT` is user-settable, and `ARR2D_OPT=-Ofast` silently
produces a wrong-answers build.

---

## 6. Link and runtime requirements

### 6.1 Libraries `[verified]`

```
$ otool -L <any CGAL binary built here>
	/opt/homebrew/opt/mpfr/lib/libmpfr.6.dylib   (current version 9.2.0)
	/opt/homebrew/opt/gmp/lib/libgmp.10.dylib    (current version 16.0.0)
	/usr/lib/libc++.1.dylib
	/usr/lib/libSystem.B.dylib
```

* **No CGAL library** — `config.h:24-31` sets `CGAL_HEADER_ONLY 1` and therefore
  `CGAL_NO_AUTOLINK 1`. There is no `libCGAL*` in `/opt/homebrew/lib` at all.
* **GMP and MPFR are both mandatory at link and at runtime.** MPFR is not optional: omitting
  `-lmpfr` fails with undefined `_mpfr_set_q`, `_mpfr_subnormalize`, … referenced from
  `CGAL::RET_boost_mp<mpq_rational,…>::To_interval::operator()`, i.e. from
  `CGAL::to_interval(Exact_rational)` — the innermost loop of every filtered predicate.
* **No Boost library** is needed (Boost.Multiprecision, Boost.Container, Boost.Variant usages here
  are header-only). `BOOST_ALL_NO_LIB` is therefore a no-op on this platform; define it on Windows
  where `boost/config/auto_link.hpp:458-478` emits `#pragma comment(lib, …)`.
* Link order used and verified: `-lmpfr -lgmp` (mpfr depends on gmp).

### 6.2 macOS specifics

* `-stdlib=libc++` is the Apple clang default; do not pass `-stdlib=libstdc++`. Mixing standard
  libraries across TUs is an even coarser ABI break than anything in §2.
* `-mmacosx-version-min=11.0` (current `setup.py`) is **inconsistent with the Homebrew
  dependencies**, which are built for macOS 15.0 — the linker warns on every link `[verified]`.
  Either build gmp/mpfr for your floor, or take the deployment target from
  `sysconfig.get_config_var("MACOSX_DEPLOYMENT_TARGET")` (here: `11.0`) *and* accept that the
  Homebrew-linked wheel will not actually run on 11.0.
* The module is a **bundle**: `LDSHARED = cc -bundle -undefined dynamic_lookup …`. Python symbols
  are resolved at load; CGAL/GMP/MPFR symbols are not, so `-lgmp -lmpfr` must be on the link line.
* `arm64` only on this machine. For a universal2 wheel every `-arch` slice is a separate
  compilation — `CGAL_USE_SSE2` becomes defined for the x86-64 slice
  (`FPU.h:117-121`), which changes `Interval_nt`'s representation and
  `Lazy_rep_selector<Interval_nt<b>>::value` from 2 to 1. That is fine as long as it is uniform
  *within* a slice, which `-arch` guarantees.
* `-fPIC` comes from Python's `CFLAGS`; keep it.

### 6.3 C++ standard `[verified]`

`config.h:152-154`:

```cpp
#if !(__cplusplus >= 201703L || _MSVC_LANG >= 201703L)
#error "CGAL requires C++ 17"
#endif
```

A single TU instantiating `Arrangement_2` over segment / polyline / circle-segment / conic /
Bézier traits, `Arrangement_on_surface_2` + `Arr_spherical_topology_traits_2`,
`Arrangement_with_history_2`, `Arr_trapezoid_ric_point_location`,
`Arr_landmarks_point_location`, `General_polygon_set_2<Gps_traits_2<Arr_polyline_traits_2<…>>>`
and `CORE_algebraic_number_traits`:

| `-std` | compiles | errors | warnings | `sizeof(Arrangement_2<SegTr>)` | compile time |
|---|---|---|---|---|---|
| `c++17` | yes | 0 | 0 | 248 | 3.9 s |
| `c++20` | yes | 0 | 0 | 248 | 4.2 s |
| `c++23` | yes | 0 | 15 (`float_denorm_style` / `denorm_absent` deprecations, from CORE/Boost) | 248 | 4.3 s |

**Use `-std=c++17`** — it is the minimum CGAL supports, the `std::variant`/`std::optional` API
that CGAL 6.x exposes is fully available there, CORE builds clean, and it avoids the C++23
deprecation noise. Whatever you pick, it must be identical in every TU including the Cython output
(`setup.py` already puts `-std=c++17` in `extra_compile_args`, which applies to all sources).

---

## 7. Symbol visibility and exceptions across the boundary

### 7.1 Inside one `.so`: `-fvisibility=hidden` is safe `[verified]`

Throw `CGAL_error_msg(...)` in TU-A, catch `const CGAL::Failure_exception&` in TU-B, both compiled
with `-fvisibility=hidden -fvisibility-inlines-hidden`, linked into **one** dylib:

```
throw in B, catch in A: code=1 [caught Failure_exception: CGAL ERROR: assertion violation! …]
throw in A, catch in B: code=1 [caught Failure_exception: …]
```

The vague-linkage `typeinfo` symbols are still merged by the static linker within one image, so
RTTI matching works. `PyMODINIT_FUNC` already carries
`__attribute__((visibility("default")))`, so the module still imports `[verified]`.

### 7.2 Across two `.so`s: it breaks `[verified]`

Same code, TU-A and TU-B in **separate** dylibs, both `-fvisibility=hidden`:

```
throw in B, catch in A: code=2 [caught as std::logic_error only: CGAL ERROR: assertion violation! …]
throw in A, catch in B: code=2 [caught as std::logic_error only: …]
```

`catch (const CGAL::Failure_exception&)` **misses**; only the `std::logic_error` base matches
(its `typeinfo` lives in `libc++.dylib` with default visibility). With default visibility on both
dylibs, the specific catch works.

### 7.3 The error-handler singleton is per-image too `[verified]`

`CGAL::set_error_handler()` / `set_error_behaviour()` store into function-local statics of `inline`
functions in `assertions_impl.h` (included because `assertions.h:352-354` +
`CGAL_HEADER_ONLY`).

| layout | handler installed in A applies to B? |
|---|---|
| one dylib, `-fvisibility=hidden` | **yes** (hit count 1 → 2) |
| one dylib, default visibility | yes |
| two dylibs, default visibility | yes |
| two dylibs, `-fvisibility=hidden` | **no** — B printed the default `CGAL error:` block to `stderr` |

**Rules for `arr2d`:**

1. Keep `-fvisibility=hidden`; it is correct for a single-module build and shrinks the export
   table.
2. Keep the whole CGAL core in **one** extension module. If a second CGAL-using module is ever
   added, (a) it needs its own `arrangement2d_init_error_handling()` call, (b) CGAL exceptions must
   never cross between them, and (c) CGAL objects must never cross between them either (§2 applies
   even more strongly, since hidden visibility stops the linker from merging the mismatched
   definitions).
3. Catch and translate CGAL exceptions **inside the core**, at the `extern "C"` / Cython boundary —
   never let a `CGAL::Failure_exception` propagate out of the module. See
   `number_types_and_errors.md` §8.

---

## 8. Recommended flag sets — exact strings for `setup.py`

### 8.1 Release (default)

```python
def compile_args():
    args = [
        "-std=c++17",
        os.environ.get("ARR2D_OPT", "-O2"),          # see the guard below
        # --- floating point: MUST be identical in every TU, and must not be
        #     overridable by CFLAGS (later flags win on the clang command line)
        "-ffp-contract=off",
        "-fno-fast-math",
        "-fno-finite-math-only",
        "-fno-unsafe-math-optimizations",
        "-fno-associative-math",
        "-fno-reciprocal-math",
        "-fhonor-nans",
        "-fhonor-infinities",
        # --- CGAL third-party switches (redundant here, kept as documentation)
        "-DCGAL_USE_CORE", "-DCGAL_USE_GMP", "-DCGAL_USE_MPFR",
        # --- assertion policy: C assert() off (Python's -DNDEBUG stays),
        #     CGAL preconditions ON so misuse reaches Python as an exception.
        "-DCGAL_DEBUG",
        # --- visibility
        "-fvisibility=hidden", "-fvisibility-inlines-hidden",
        # --- warnings
        "-Wno-unused-parameter", "-Wno-deprecated-declarations",
        "-Wno-unused-local-typedef", "-Wno-unused-function",
        "-Wno-unused-variable", "-Wno-unused-but-set-variable",
        "-Wno-unknown-warning-option", "-Wno-sign-compare",
    ]
    if sys.platform == "darwin":
        args += ["-mmacosx-version-min=" +
                 (sysconfig.get_config_var("MACOSX_DEPLOYMENT_TARGET") or "11.0"),
                 "-Wno-unused-command-line-argument"]
    return args
```

`ARR2D_NDEBUG=1` (all CGAL checks off, ~2.6× faster) replaces `"-DCGAL_DEBUG"` with:

```python
        "-DNDEBUG", "-DCGAL_NDEBUG",
```

Guard `ARR2D_OPT` so a user cannot smuggle in a broken FP mode:

```python
_BANNED = ("-Ofast", "-ffast-math", "-funsafe-math-optimizations",
           "-ffinite-math-only", "-fno-honor-nans", "-fno-honor-infinities",
           "-ffp-contract=fast", "-ffp-contract=on")
opt = os.environ.get("ARR2D_OPT", "-O2")
if any(b in opt for b in _BANNED):
    raise SystemExit(f"ARR2D_OPT={opt!r} enables unsafe floating point; "
                     "CGAL produces WRONG geometric answers (verified: "
                     "do_overlap(Bbox_2(), unit) returns True under -Ofast). Use -O2 or -O3.")
```

### 8.2 Debug (`ARR2D_DEBUG=1`)

```python
    args = [
        "-std=c++17", "-O0", "-g", "-fno-omit-frame-pointer",
        "-ffp-contract=off", "-fno-fast-math", "-fno-finite-math-only",
        "-fno-unsafe-math-optimizations", "-fno-associative-math",
        "-fno-reciprocal-math", "-fhonor-nans", "-fhonor-infinities",
        "-DCGAL_USE_CORE", "-DCGAL_USE_GMP", "-DCGAL_USE_MPFR",
        "-UNDEBUG",                       # C assert() AND CGAL checks both on
        "-fvisibility=hidden", "-fvisibility-inlines-hidden",
        # optional, when hunting a specific bug — NEVER in a shipped build:
        # "-DCGAL_CHECK_EXPENSIVE", "-DCGAL_CHECK_EXACTNESS",
    ]
```

Add `-fsanitize=address` only if you rebuild **every** TU with it (it is itself an ABI-affecting
flag, and it changes `Lazy_rep_selector` on x86-64 via `__SANITIZE_THREAD__`/`thread_sanitizer`
feature checks in `Lazy.h:274`).

### 8.3 Link args

```python
def link_args():
    args = []
    if sys.platform == "darwin":
        args += ["-mmacosx-version-min=" +
                 (sysconfig.get_config_var("MACOSX_DEPLOYMENT_TARGET") or "11.0")]
    return args
# libraries = ["mpfr", "gmp"]   # order matters: mpfr depends on gmp
```

`setup.py` currently lists `["mpfr", "gmp"]` — correct.

### 8.4 Diff against the current `setup.py`

| current | change | why |
|---|---|---|
| no FP flags at all | **add the 8-flag FP block** | `-Ofast`/`-ffast-math` anywhere in `CFLAGS` or `ARR2D_OPT` silently yields wrong geometry (§5.2) |
| `args += ["-UNDEBUG"]` in the default path | replace with `"-DCGAL_DEBUG"` | same CGAL behaviour `[verified]`, but leaves C `assert()` disabled as CPython expects |
| `ARR2D_OPT` unchecked | add the `_BANNED` guard | user-settable path to a wrong-answers build |
| `-mmacosx-version-min=11.0` hardcoded | take it from `sysconfig` | current value contradicts the Homebrew deps (linker warns) |
| `-DCGAL_USE_CORE/GMP/MPFR` | keep (no-ops) | `enable_third_party_libraries.h` already defines them `[verified]` |
| `-fvisibility=hidden` | keep | verified safe for a single module (§7.1) |
| one `Extension`, one flag list | **document that this is load-bearing** | any second `Extension`, or any pre-built object file, must use byte-identical macro/`-std` flags (§2, §3) |

---

## 9. A self-check worth shipping

The mismatches in §3 are invisible to the compiler and the linker, but trivial to detect at
runtime. Extend `arr2d::build_info()` (`src/arr2d/src/registry.cpp`) so **each** TU contributes its
own view, and assert they agree at module init:

```cpp
// arr2d/abi_check.hpp  — included by EVERY TU of the core
#pragma once
#include <CGAL/version.h>
#include <CGAL/Lazy.h>
#include <CGAL/Interval_nt.h>
#include <CGAL/Exact_rational.h>
#include <cstddef>

namespace arr2d {
struct AbiStamp {
  long        cgal_version_nr;
  long        cplusplus;
  std::size_t sizeof_exact_rational;   // 32 = boost mpq_rational, 8 = CGAL::Gmpq
  std::size_t sizeof_lazy_rep_0;       // 48 = CGAL_HAS_THREADS, 40 = without
  int         lazy_rep_selector;       // 2 = threads/no-SSE2, 1 = no threads or SSE2
  int         has_threads;
  int         checks_on;               // CGAL_PRECONDITIONS_ENABLED
  int         no_atomic;
  int         static_filters;          // CGAL::epeck_use_static_filter
  bool operator==(const AbiStamp&) const = default;
};

// Must be a static inline in a header so each TU gets its own evaluation.
inline AbiStamp abi_stamp_of_this_tu() {
  using AT  = CGAL::Interval_nt<false>;
  using ET  = CGAL::Exact_rational;
  using E2A = CGAL::To_interval<ET>;
  return AbiStamp{
    (long)CGAL_VERSION_NR,
    (long)__cplusplus,
    sizeof(ET),
    sizeof(CGAL::Lazy_rep_0<AT, ET, E2A>),
    CGAL::Lazy_rep_selector<AT>::value,
#ifdef CGAL_HAS_THREADS
    1,
#else
    0,
#endif
    (int)CGAL_PRECONDITIONS_ENABLED,
#ifdef CGAL_NO_ATOMIC
    1,
#else
    0,
#endif
    (int)CGAL::epeck_use_static_filter
  };
}
void abi_register(const char* tu_name, const AbiStamp& s);  // in registry.cpp
void abi_verify();  // throws if any two registered stamps differ
}  // namespace arr2d
```

Each kind TU calls `abi_register("segment", abi_stamp_of_this_tu());` from its existing static
registrar, and `_core.pyx` calls `abi_verify()` at module import. On the exact configuration that
segfaulted in §3.1 this reports, before any geometry runs:

```
tu1: sizeof_lazy_rep_0=48 lazy_rep_selector=2 has_threads=1
tu2: sizeof_lazy_rep_0=40 lazy_rep_selector=1 has_threads=0   <-- ABI mismatch
```

Expose the stamp from Python (`arrangement_2d.build_info()`) so bug reports carry it.

---

## 10. One-page checklist

* [ ] Every TU: same `-std`, same `-D`/`-U` macro set, same `-arch`, same `-stdlib`.
* [ ] Never define: `CGAL_ALWAYS_LEFT_TO_RIGHT`, `CGAL_NO_ASSERTIONS` (alone), `CGAL_HAS_NO_THREADS`,
      `BOOST_DISABLE_THREADS`, `CGAL_NO_ATOMIC`, `CGAL_NO_STATIC_FILTERS`,
      `CGAL_NO_STATIC_FILTERS_FOR_LAZY_KERNEL`, `CGAL_DO_NOT_USE_BOOST_MP`, `CGAL_NO_GMP`,
      `CGAL_ALWAYS_ROUND_TO_NEAREST`, `CGAL_CHECK_EXPENSIVE`, `CGAL_CHECK_EXACTNESS`,
      `CGAL_DISABLE_ROUNDING_MATH_CHECK`, `CGAL_TEST_SUITE`.
* [ ] Assertion policy is a *build-wide* decision: `-DCGAL_DEBUG` (checks on) **or**
      `-DNDEBUG -DCGAL_NDEBUG` (checks off). Never a per-file mix.
* [ ] FP: `-ffp-contract=off -fno-fast-math -fno-finite-math-only
      -fno-unsafe-math-optimizations -fno-associative-math -fno-reciprocal-math
      -fhonor-nans -fhonor-infinities` on **every** TU. Reject `-Ofast`.
* [ ] Link `-lmpfr -lgmp`. No CGAL library. No Boost library.
* [ ] `-fvisibility=hidden` is fine for one module; do not ship a second CGAL module that exchanges
      objects or exceptions with it.
* [ ] Translate CGAL exceptions to Python inside the module; call `set_error_handler` once per
      image.
* [ ] Run `abi_verify()` at import.
