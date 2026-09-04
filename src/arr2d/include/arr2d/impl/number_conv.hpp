// arr2d — exact / approximate number conversions at the CGAL <-> core boundary.
//
// This header is the ONE place where a kind TU (or the Cython bridge, indirectly through
// numbers.hpp) turns a CGAL number into an arr2d::Geom "boxed number", and back.
//
// The three exact number representations that cross the boundary are
//   NumberKind::Rational   -> arr2d::Rational       (== CGAL::Exact_rational == mpq_rational == CORE::BigRat)
//   NumberKind::SqrtExt    -> arr2d::SqrtExt        (a + b*sqrt(c), all rational, c > 0)   [circle-segment coords]
//   NumberKind::Algebraic  -> CORE::Expr            (stored BY VALUE inside the Geom)      [Bezier / conic coords]
//
// Design notes / CGAL 6.1 gotchas addressed here (see docs/dev/cgal61_api/number_types_and_errors.md):
//   * gotcha #2: CGAL::to_double(Epeck::FT) is NOT correctly rounded and operator<< is lossy.
//     Everything here goes through `x.exact()` and our own round-half-even rational -> double.
//   * gotcha #4: CORE::Expr has no safe rationality test and BigRatValue() is an approximation.
//     We never claim an Expr is rational; enclosures come from approx(rel,abs) + BigFloat bounds.
//   * traits_circle_segment gotcha #1: the circle-segment CoordNT is
//     CGAL::Sqrt_extension<Epeck::FT, Epeck::FT, Tag_true, Boolean_tag<Filter>>, NOT _One_root_number.
//     box_sqrt_extension() below takes exactly that type.
//
// NAMING WARNING for other TUs: this header declares `arr2d::compare(...)` overloads returning
// `int`. Inside `namespace arr2d`, an unqualified `compare(a, b)` on one of the types listed below
// resolves to the (non-template) arr2d overload rather than to the `CGAL::compare` function
// template found by ADL. Both return -1/0/+1, but if you specifically want a
// CGAL::Comparison_result, spell it `CGAL::compare(a, b)`.
#pragma once

#include <utility>

#include <CGAL/Exact_predicates_exact_constructions_kernel.h>
#include <CGAL/Exact_integer.h>
#include <CGAL/Exact_rational.h>
#include <CGAL/Sqrt_extension.h>
#include <CGAL/CORE_BigInt.h>
#include <CGAL/CORE_BigRat.h>
#include <CGAL/CORE_Expr.h>

#include "arr2d/common.hpp"
#include "arr2d/numbers.hpp"

namespace arr2d {

/// mpz_int; identical to CORE::BigInt in this build.
using Integer = CGAL::Exact_integer;
/// The algebraic number type of the Bezier / conic traits.
using Algebraic = CORE::Expr;
/// Lazy_exact_nt<Rational>: the FT of Epeck (segment / linear / polyline / circle-segment / sphere kinds).
using EpeckFT = CGAL::Epeck::FT;

static_assert(std::is_same<Rational, CORE::BigRat>::value,
              "arr2d assumes CGAL::Exact_rational == CORE::BigRat (boost mpq_rational); "
              "see docs/dev/cgal61_api/number_types_and_errors.md gotcha #1");
static_assert(std::is_same<Integer, CORE::BigInt>::value,
              "arr2d assumes CGAL::Exact_integer == CORE::BigInt (boost mpz_int)");

/// a + b*sqrt(c) over arr2d::Rational.
/// ACDE_TAG = Tag_true makes `compare(other)` default to `in_same_extension == false`, i.e. CGAL
/// compares two extensions with *different* roots EXACTLY (Sqrt_extension_type.h:557 ff.), and it
/// also permits root == 0. FP_TAG = Tag_false: no interval cache (we do our own filtering).
using SqrtExtNT = CGAL::Sqrt_extension<Rational, Rational, CGAL::Tag_true, CGAL::Tag_false>;

// ---------------------------------------------------------------------------
// Rational <-> other exact types
// ---------------------------------------------------------------------------

inline Rational to_rational(const Rational& r) { return r; }
/// Epeck::FT -> Rational. Goes through exact() (gotcha #2); the returned reference lives in the
/// shared lazy DAG node, so we copy it out immediately.
inline Rational to_rational(const EpeckFT& x) { return x.exact(); }
/// CORE::BigInt / CGAL::Exact_integer -> Rational (exact, denominator 1).
inline Rational to_rational(const Integer& z) { return Rational(z); }

inline EpeckFT to_epeck_ft(const Rational& r) { return EpeckFT(r); }
inline EpeckFT to_epeck_ft(const EpeckFT& x) { return x; }

/// Rational -> CORE::Expr. Expr(BigRat) is the only EXACT rational entry point of CORE
/// (Expr("0.1") and Expr::BigRatValue() are approximations — gotcha #4).
inline Algebraic to_core_expr(const Rational& r) { return Algebraic(r); }
inline Algebraic to_core_expr(const Integer& z) { return Algebraic(z); }
inline Algebraic to_core_expr(const EpeckFT& x) { return Algebraic(x.exact()); }
inline const Algebraic& to_core_expr(const Algebraic& e) { return e; }
/// a + b*sqrt(c) as an exact CORE::Expr (c >= 0 required; Error(InvalidArgument) otherwise).
Algebraic to_core_expr(const SqrtExt& s);

/// The CGAL Sqrt_extension image of `s` (no normalisation; c >= 0 required).
SqrtExtNT to_cgal_sqrt_ext(const SqrtExt& s);

// ---------------------------------------------------------------------------
// SqrtExt helpers
// ---------------------------------------------------------------------------

/// True if the *value* of `s` is provably rational, i.e. b == 0, or c == 0, or c is the square of
/// a rational. `out` then receives the exact value. Throws Error(InvalidArgument) if c < 0.
bool sqrt_ext_is_rational(const SqrtExt& s, Rational& out);

/// Exact rational enclosure lo <= a + b*sqrt(c) <= hi with a relative precision of ~2^-bits.
/// Throws Error(InvalidArgument) if c < 0.
void sqrt_ext_bounds(const SqrtExt& s, int bits, Rational& lo, Rational& hi);

/// Exact rational enclosure lo <= sqrt(c) <= hi (c >= 0), relative precision ~2^-bits.
/// lo == hi exactly when sqrt(c) is rational.
void rational_sqrt_bounds(const Rational& c, int bits, Rational& lo, Rational& hi);

/// True (and `out` set) iff sqrt(c) is rational. c >= 0 required.
bool rational_exact_sqrt(const Rational& c, Rational& out);

// ---------------------------------------------------------------------------
// Boxing (Geom with GeomType::Number). The Geom's Kind is always Kind::Segment:
// numbers are kind-agnostic, matching the box_number<> template in numbers.hpp.
// ---------------------------------------------------------------------------

/// Epeck::FT -> Rational number box (exact).
Geom box_epeck_ft(const EpeckFT& x);

/// CORE::Expr -> Algebraic number box (the Expr is stored by value inside the Geom).
Geom box_core_expr(const Algebraic& e);

/// Circle-segment CoordNT -> SqrtExt number box, or a Rational box when the value is rational
/// (`!is_extended()`, or a1 == 0 / root == 0 / root a perfect square). NT is Epeck::FT in the
/// circle-segment traits; the template also accepts NT == arr2d::Rational.
template <class NT, class FPTag>
Geom box_sqrt_extension(const CGAL::Sqrt_extension<NT, NT, CGAL::Tag_true, FPTag>& v) {
  if (!v.is_extended()) return box_rational(to_rational(v.a0()));
  SqrtExt s{to_rational(v.a0()), to_rational(v.a1()), to_rational(v.root())};
  return box_sqrt_ext(s);  // normalises b == 0 / c == 0 / perfect-square c to a Rational box
}

// ---------------------------------------------------------------------------
// Approximation
// ---------------------------------------------------------------------------

/// Correctly rounded (round-half-to-even) double nearest to the exact rational value.
/// Overflows to +-inf, underflows to +-0, handles subnormals exactly.
/// NOT CGAL::to_double(Epeck::FT), which is interval-derived and wrong (gotcha #2).
double to_double_correctly_rounded(const Rational& r);
double to_double_correctly_rounded(const EpeckFT& x);
/// Correctly rounded whenever the refinement loop converges (it always does for an irrational
/// value); otherwise the midpoint of a certified 1-ulp enclosure.
double to_double_correctly_rounded(const SqrtExt& s);
double to_double_correctly_rounded(const Algebraic& e);

/// Certified enclosure [lo, hi] with lo <= value <= hi, as tight as doubles allow
/// (degenerate when the value is exactly a double).
std::pair<double, double> interval_of(const Rational& r);
std::pair<double, double> interval_of(const EpeckFT& x);
std::pair<double, double> interval_of(const SqrtExt& s);
/// `bits` = requested relative AND absolute precision handed to CORE::Expr::approx().
/// CORE only ever refines, so repeated calls with growing `bits` yield nested intervals.
std::pair<double, double> interval_of(const Algebraic& e, int bits = 53);

// ---------------------------------------------------------------------------
// Exact sign / comparison. All results are -1 / 0 / +1.
// ---------------------------------------------------------------------------

int sign_of(const Rational& r);
int sign_of(const EpeckFT& x);
int sign_of(const SqrtExt& s);
int sign_of(const Algebraic& e);

int compare(const Rational& a, const Rational& b);
int compare(const EpeckFT& a, const EpeckFT& b);
int compare(const SqrtExt& a, const SqrtExt& b);   ///< exact, even for different roots
int compare(const SqrtExt& a, const Rational& b);
int compare(const Rational& a, const SqrtExt& b);
int compare(const Algebraic& a, const Algebraic& b);
int compare(const Algebraic& a, const Rational& b);
int compare(const Rational& a, const Algebraic& b);
int compare(const Algebraic& a, const SqrtExt& b);
int compare(const SqrtExt& a, const Algebraic& b);

}  // namespace arr2d
