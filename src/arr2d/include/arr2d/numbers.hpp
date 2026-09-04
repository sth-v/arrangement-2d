// arr2d — exact / approximate number helpers at the core boundary.
//
// Rational: CGAL::Exact_rational (boost::multiprecision::mpq_rational).
// Boxed numbers: a Geom with type == GeomType::Number holding one of
//   NumberKind::Rational  -> arr2d::Rational
//   NumberKind::SqrtExt   -> arr2d::SqrtExt (a + b*sqrt(c), rationals)   [circle-segment coordinates]
//   NumberKind::Algebraic -> CORE::Expr                                  [Bezier / conic coordinates]
// Python sees Fraction / SqrtExtension / Algebraic objects built from these.
#pragma once

#include <string>
#include <utility>
#include <vector>

#include "arr2d/common.hpp"

namespace arr2d {

enum class NumberKind : int { Rational = 0, SqrtExt = 1, Algebraic = 2 };

/// a + b * sqrt(c). Invariant: c >= 0 (helpers throw Error(InvalidArgument) otherwise).
/// Used for the Sqrt_extension coordinates of circle-segment points. Boxing helpers normalise:
/// a provably rational value (b == 0, c == 0, or c a perfect square) is boxed as a Rational.
struct SqrtExt {
  Rational a, b, c;
};

// ---- Rational <-> Python-friendly representations (implemented in numbers.cpp) ----
Rational rational_from_double(double d);                                  ///< exact (throws InvalidArgument on NaN/inf)
Rational rational_from_int64(std::int64_t v);
Rational rational_from_strings(const std::string& num, const std::string& den);  ///< decimal integer strings, den != 0
Rational rational_from_string(const std::string& s);                      ///< "num/den" or "num"
void rational_to_strings(const Rational& r, std::string& num, std::string& den); ///< canonical (den > 0, gcd 1)
double rational_to_double(const Rational& r);
int rational_sign(const Rational& r);
int rational_compare(const Rational& a, const Rational& b);              ///< -1 / 0 / 1
bool rational_is_integer(const Rational& r);

// ---- Boxed numbers ----
Geom box_rational(const Rational& r);
Geom box_sqrt_ext(const SqrtExt& s);
/// Low-level boxing: T must be exactly arr2d::Rational, arr2d::SqrtExt or CORE::Expr (number_kind()
/// discriminates on the stored C++ type). Prefer box_rational / box_sqrt_ext here and
/// box_epeck_ft / box_sqrt_extension / box_core_expr from impl/number_conv.hpp.
template <class T> Geom box_number(NumberKind /*nk*/, T value) { return make_geom(Kind::Segment, GeomType::Number, std::move(value)); }

NumberKind number_kind(const Geom& n);                    ///< throws unless n.type == Number
double number_to_double(const Geom& n);
/// Certified interval containing the value. For Algebraic numbers `bits` controls the
/// requested relative precision (CORE refines on demand); ignored for the other kinds.
std::pair<double, double> number_interval(const Geom& n, int bits = 53);
bool number_is_rational(const Geom& n);                   ///< true if the exact value is known to be rational (Rational: always; SqrtExt: b==0, c==0 or c a perfect square; Algebraic (CORE::Expr): always false — CORE offers no safe rationality test)
Rational number_to_rational(const Geom& n);               ///< throws NotRepresentable if not rational
SqrtExt number_to_sqrt_ext(const Geom& n);                ///< SqrtExt box, or a Rational box as {r, 0, 0}; throws for Algebraic
int number_sign(const Geom& n);
int number_compare(const Geom& a, const Geom& b);         ///< same NumberKind required (or both convertible to rational); -1/0/1
std::string number_repr(const Geom& n);                   ///< exact textual form when possible ("3/4", "1/2 + 3*sqrt(2)", or a CORE expression / decimal approximation)

}  // namespace arr2d
