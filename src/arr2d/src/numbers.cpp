// arr2d — implementation of numbers.hpp and impl/number_conv.hpp.
//
// Everything here is exact unless a function name says otherwise. The CGAL 6.1 traps this file
// works around (see docs/dev/cgal61_api/number_types_and_errors.md):
//
//   * gotcha #2  CGAL::to_double(Epeck::FT) is interval-derived and NOT correctly rounded, and
//                operator<<(ostream, Epeck::FT) is lossy. Every conversion here goes through
//                Lazy_exact_nt::exact() and our own round-half-even rational -> double.
//   * gotcha #3  `istream >> Exact_rational` silently mis-parses "0.125" as 0 (no failbit), and
//                Exact_rational("0.125") throws. We therefore parse decimals with our own scanner
//                instead of Boost's extractor or CGAL::IO::iformat (iformat also lets Boost throw
//                std::runtime_error("Division by zero.") on "1/0", which we must not leak).
//   * gotcha #4  CORE::Expr::BigRatValue() is an approximation and there is NO safe rationality
//                test (ExprRep internals segfault). number_is_rational() is therefore always false
//                for Algebraic numbers, and Expr enclosures are built from approx() + BigFloat
//                floor/ceil, which are exact dyadic bounds.
//   * traits_circle_segment gotcha #1  the circle-segment coordinate type is CGAL::Sqrt_extension,
//                not the dead _One_root_number.
#include "arr2d/numbers.hpp"

#include "arr2d/impl/number_conv.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <limits>
#include <sstream>
#include <string>

namespace arr2d {

// ===========================================================================
// Local helpers
// ===========================================================================
namespace {

constexpr int kMinSubnormalExp2 = -1074;  ///< exponent of the smallest positive double (2^-1074)
constexpr int kMantissaBits = 53;

inline Integer int_numerator(const Rational& r) { return numerator(r); }
inline Integer int_denominator(const Rational& r) { return denominator(r); }

/// floor(log2(|z|)) for z != 0.
inline long msb_of(const Integer& z) {
  Integer a = (z.sign() < 0) ? Integer(-z) : z;
  return static_cast<long>(boost::multiprecision::msb(a));
}

/// 10^n as an exact integer (n >= 0).
Integer ipow10(unsigned long n) {
  Integer result(1), base(10);
  while (n) {
    if (n & 1u) result *= base;
    n >>= 1;
    if (n) base *= base;
  }
  return result;
}

inline bool is_space(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v'; }

/// Trim ASCII whitespace on both sides.
std::string trimmed(const std::string& s) {
  std::size_t b = 0, e = s.size();
  while (b < e && is_space(s[b])) ++b;
  while (e > b && is_space(s[e - 1])) --e;
  return s.substr(b, e - b);
}

[[noreturn]] void bad_number(const std::string& what, const std::string& s) {
  throw_error(ErrorCode::InvalidArgument, what + ": '" + s + "'");
}

/// Parse an arbitrary-length decimal integer with an optional sign; whitespace tolerated around it.
/// Throws Error(InvalidArgument) on anything else (including an empty digit run).
Integer parse_decimal_integer(const std::string& raw) {
  const std::string s = trimmed(raw);
  std::size_t i = 0;
  bool neg = false;
  if (i < s.size() && (s[i] == '+' || s[i] == '-')) {
    neg = (s[i] == '-');
    ++i;
  }
  const std::size_t first_digit = i;
  while (i < s.size() && s[i] >= '0' && s[i] <= '9') ++i;
  if (i == first_digit || i != s.size()) bad_number("not a decimal integer", raw);
  // Strip leading zeros so that "007" and "-0" are accepted by the Boost string constructor.
  std::size_t z = first_digit;
  while (z + 1 < s.size() && s[z] == '0') ++z;
  Integer v(s.substr(z, s.size() - z));
  return neg ? Integer(-v) : v;
}

/// Grammar (whitespace tolerated at both ends):
///     [sign] { digit } [ '.' { digit } ] [ ('e'|'E') [sign] digits ]
/// with at least one digit in the mantissa. Exact; no rounding anywhere.
Rational parse_decimal_number(const std::string& raw, const std::string& whole) {
  const std::string s = trimmed(raw);
  std::size_t i = 0;
  bool neg = false;
  if (i < s.size() && (s[i] == '+' || s[i] == '-')) {
    neg = (s[i] == '-');
    ++i;
  }
  std::string digits;
  while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
    digits.push_back(s[i]);
    ++i;
  }
  long frac_len = 0;
  if (i < s.size() && s[i] == '.') {
    ++i;
    while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
      digits.push_back(s[i]);
      ++i;
      ++frac_len;
    }
  }
  if (digits.empty()) bad_number("not a number", whole);

  long long exp10 = 0;
  if (i < s.size() && (s[i] == 'e' || s[i] == 'E')) {
    ++i;
    bool eneg = false;
    if (i < s.size() && (s[i] == '+' || s[i] == '-')) {
      eneg = (s[i] == '-');
      ++i;
    }
    const std::size_t e0 = i;
    while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
      if (exp10 < 1000000000LL) exp10 = exp10 * 10 + (s[i] - '0');
      ++i;
    }
    if (i == e0) bad_number("not a number (empty exponent)", whole);
    if (eneg) exp10 = -exp10;
  }
  if (i != s.size()) bad_number("not a number (trailing characters)", whole);

  // Strip leading zeros of the mantissa digit run.
  std::size_t z = 0;
  while (z + 1 < digits.size() && digits[z] == '0') ++z;
  Integer mant(digits.substr(z));

  const long long net = exp10 - static_cast<long long>(frac_len);
  // Refuse absurd exponents rather than trying to allocate 10^10^9.
  constexpr long long kMaxExp = 1000000;
  if (net > kMaxExp || net < -kMaxExp) {
    if (mant.is_zero()) return Rational(0);
    bad_number("decimal exponent out of range", whole);
  }
  Rational v;
  if (net >= 0) {
    const Integer scaled = mant * ipow10(static_cast<unsigned long>(net));
    v = Rational(scaled, Integer(1));
  } else {
    const Integer den = ipow10(static_cast<unsigned long>(-net));
    v = Rational(mant, den);
  }
  if (neg) v = -v;
  return v;
}

/// Exact comparison of a rational against a finite double.
int compare_rational_double(const Rational& r, double d) {
  return rational_compare(r, rational_from_double(d));
}

std::string rational_text(const Rational& r) {
  const Integer n = int_numerator(r), d = int_denominator(r);
  if (d == 1) return n.str();
  return n.str() + "/" + d.str();
}

}  // namespace

// ===========================================================================
// Rational <-> Python-friendly representations
// ===========================================================================

Rational rational_from_double(double d) {
  if (!std::isfinite(d))
    throw_error(ErrorCode::InvalidArgument,
                std::isnan(d) ? "cannot convert NaN to an exact rational"
                              : "cannot convert an infinite value to an exact rational");
  // Boost's mpq_rational(double) is exact w.r.t. the IEEE value (never a decimal
  // reinterpretation); -0.0 yields 0.
  return Rational(d);
}

Rational rational_from_int64(std::int64_t v) {
  // The long long constructor is exact (no double round trip) — verified for INT64_MIN/MAX.
  return Rational(static_cast<long long>(v));
}

Rational rational_from_strings(const std::string& num, const std::string& den) {
  const Integer n = parse_decimal_integer(num);
  const Integer d = parse_decimal_integer(den);
  if (d.is_zero()) throw_error(ErrorCode::InvalidArgument, "zero denominator");
  // number(mpz_int, mpz_int) canonicalises: gcd reduced, sign moved to the numerator.
  return Rational(n, d);
}

Rational rational_from_string(const std::string& s) {
  const std::string t = trimmed(s);
  if (t.empty()) throw_error(ErrorCode::InvalidArgument, "empty number string");
  const std::size_t slash = t.find('/');
  if (slash == std::string::npos) return parse_decimal_number(t, s);
  const Rational n = parse_decimal_number(t.substr(0, slash), s);
  const Rational d = parse_decimal_number(t.substr(slash + 1), s);
  if (d.is_zero()) throw_error(ErrorCode::InvalidArgument, "zero denominator in '" + s + "'");
  return n / d;
}

void rational_to_strings(const Rational& r, std::string& num, std::string& den) {
  num = int_numerator(r).str();
  den = int_denominator(r).str();
}

double rational_to_double(const Rational& r) { return to_double_correctly_rounded(r); }

int rational_sign(const Rational& r) { return r.sign(); }

int rational_compare(const Rational& a, const Rational& b) {
  const int c = a.compare(b);
  return (c < 0) ? -1 : (c > 0 ? 1 : 0);
}

bool rational_is_integer(const Rational& r) { return int_denominator(r) == 1; }

// ===========================================================================
// number_conv.hpp — sqrt helpers
// ===========================================================================

bool rational_exact_sqrt(const Rational& c, Rational& out) {
  const int s = c.sign();
  if (s < 0) throw_error(ErrorCode::InvalidArgument, "sqrt of a negative rational");
  if (s == 0) {
    out = Rational(0);
    return true;
  }
  const Integer p = int_numerator(c), q = int_denominator(c);
  const Integer sp = boost::multiprecision::sqrt(p);
  const Integer sq = boost::multiprecision::sqrt(q);
  if (sp * sp == p && sq * sq == q) {
    out = Rational(sp, sq);
    return true;
  }
  return false;
}

void rational_sqrt_bounds(const Rational& c, int bits, Rational& lo, Rational& hi) {
  const int s = c.sign();
  if (s < 0) throw_error(ErrorCode::InvalidArgument, "sqrt of a negative rational");
  if (s == 0) {
    lo = hi = Rational(0);
    return;
  }
  const unsigned k = static_cast<unsigned>(std::min(1 << 20, std::max(8, bits)));
  const Integer p = int_numerator(c), q = int_denominator(c);
  // sqrt(p/q) = sqrt(p*q)/q.  With N = p*q*2^(2k):  sqrt(p*q) in [t, t+1] / 2^k, t = isqrt(N).
  Integer N = p * q;
  N <<= (2u * k);
  const Integer t = boost::multiprecision::sqrt(N);
  Integer den = q;
  den <<= k;
  lo = Rational(t, den);
  hi = (t * t == N) ? lo : Rational(t + 1, den);
}

bool sqrt_ext_is_rational(const SqrtExt& s, Rational& out) {
  if (s.c.sign() < 0) throw_error(ErrorCode::InvalidArgument, "SqrtExt with a negative radicand");
  if (s.b.is_zero() || s.c.is_zero()) {
    out = s.a;
    return true;
  }
  Rational r;
  if (rational_exact_sqrt(s.c, r)) {
    out = s.a + s.b * r;
    return true;
  }
  return false;
}

void sqrt_ext_bounds(const SqrtExt& s, int bits, Rational& lo, Rational& hi) {
  Rational sl, sh;
  rational_sqrt_bounds(s.c, bits, sl, sh);
  if (s.b.sign() >= 0) {
    lo = s.a + s.b * sl;
    hi = s.a + s.b * sh;
  } else {
    lo = s.a + s.b * sh;
    hi = s.a + s.b * sl;
  }
}

SqrtExtNT to_cgal_sqrt_ext(const SqrtExt& s) {
  if (s.c.sign() < 0) throw_error(ErrorCode::InvalidArgument, "SqrtExt with a negative radicand");
  if (s.b.is_zero() || s.c.is_zero()) return SqrtExtNT(s.a);
  return SqrtExtNT(s.a, s.b, s.c);
}

Algebraic to_core_expr(const SqrtExt& s) {
  if (s.c.sign() < 0) throw_error(ErrorCode::InvalidArgument, "SqrtExt with a negative radicand");
  if (s.b.is_zero() || s.c.is_zero()) return Algebraic(s.a);
  // CORE::sqrt(Expr) is exact (an algebraic node); Expr(BigRat) is the exact rational entry point.
  return Algebraic(s.a) + Algebraic(s.b) * CORE::sqrt(Algebraic(s.c));
}

// ===========================================================================
// number_conv.hpp — boxing
// ===========================================================================

Geom box_rational(const Rational& r) { return make_geom(Kind::Segment, GeomType::Number, r); }

Geom box_sqrt_ext(const SqrtExt& s) {
  Rational v;
  if (sqrt_ext_is_rational(s, v)) return box_rational(v);  // normalise b==0 / c==0 / c a square
  return make_geom(Kind::Segment, GeomType::Number, s);
}

Geom box_epeck_ft(const EpeckFT& x) { return box_rational(to_rational(x)); }

Geom box_core_expr(const Algebraic& e) { return make_geom(Kind::Segment, GeomType::Number, e); }

// ===========================================================================
// number_conv.hpp — correctly rounded double conversion
// ===========================================================================

double to_double_correctly_rounded(const Rational& r) {
  const int s = r.sign();
  if (s == 0) return 0.0;
  const bool neg = (s < 0);
  Integer n = int_numerator(r);
  if (neg) n = -n;
  const Integer d = int_denominator(r);  // > 0

  // Exact binary exponent E with 2^E <= n/d < 2^(E+1).
  long E = msb_of(n) - msb_of(d);
  {
    Integer ln = n, ld = d;
    if (E >= 0)
      ld <<= static_cast<unsigned>(E);
    else
      ln <<= static_cast<unsigned>(-E);
    if (ln < ld) --E;
  }
  // Shortcuts that also bound the shift widths below.
  if (E >= 1024) return neg ? -std::numeric_limits<double>::infinity()
                            : std::numeric_limits<double>::infinity();
  if (E < kMinSubnormalExp2 - 2) return neg ? -0.0 : 0.0;

  long shift = E - (kMantissaBits - 1);
  if (shift < kMinSubnormalExp2) shift = kMinSubnormalExp2;

  Integer N = n, D = d;
  if (shift <= 0)
    N <<= static_cast<unsigned>(-shift);
  else
    D <<= static_cast<unsigned>(shift);

  Integer q = N / D;
  const Integer rem = N - q * D;
  const Integer twice = rem * 2;
  const int cmp = twice.compare(D);
  if (cmp > 0 || (cmp == 0 && boost::multiprecision::bit_test(q, 0))) ++q;  // round half to even

  // q <= 2^53, hence exactly representable.
  const double m = q.convert_to<double>();
  const double v = std::ldexp(m, static_cast<int>(shift));
  return neg ? -v : v;
}

double to_double_correctly_rounded(const EpeckFT& x) { return to_double_correctly_rounded(to_rational(x)); }

double to_double_correctly_rounded(const SqrtExt& s) {
  Rational v;
  if (sqrt_ext_is_rational(s, v)) return to_double_correctly_rounded(v);
  // The value is irrational, so it is never a rounding tie: the loop converges.
  for (int bits = 64; bits <= 4096; bits *= 2) {
    Rational lo, hi;
    sqrt_ext_bounds(s, bits, lo, hi);
    const double dl = to_double_correctly_rounded(lo);
    const double dh = to_double_correctly_rounded(hi);
    if (dl == dh) return dl;
  }
  Rational lo, hi;
  sqrt_ext_bounds(s, 4096, lo, hi);
  const Rational mid = (lo + hi) / 2;
  return to_double_correctly_rounded(mid);
}

namespace {
/// Certified rational enclosure of a CORE::Expr at ~`bits` bits (see number_types_and_errors.md
/// §4.3). approx() only ever refines, so repeated calls with growing `bits` are nested.
void expr_enclosure(const Algebraic& e, int bits, Rational& lo, Rational& hi) {
  const long b = std::min<long>(1 << 20, std::max<long>(2, bits));
  e.approx(CORE::extLong(b), CORE::extLong(b));
  CORE::BigFloat bf = e.BigFloatValue();
  CORE::BigFloat blo = bf;
  blo.makeFloorExact();
  CORE::BigFloat bhi = bf;
  bhi.makeCeilExact();
  lo = blo.BigRatValue();  // BigFloat::BigRatValue is EXACT (a BigFloat is a dyadic rational)
  hi = bhi.BigRatValue();
  if (lo > hi) std::swap(lo, hi);
}
}  // namespace

double to_double_correctly_rounded(const Algebraic& e) {
  for (int bits = 64; bits <= 4096; bits *= 2) {
    Rational lo, hi;
    expr_enclosure(e, bits, lo, hi);
    const double dl = to_double_correctly_rounded(lo);
    const double dh = to_double_correctly_rounded(hi);
    if (dl == dh) return dl;
  }
  // Rational-valued Expr sitting exactly on a rounding tie: fall back to CORE's own conversion.
  return e.doubleValue();
}

// ===========================================================================
// number_conv.hpp — certified intervals
// ===========================================================================

std::pair<double, double> interval_of(const Rational& r) {
  const double d = to_double_correctly_rounded(r);
  const double huge = std::numeric_limits<double>::max();
  if (std::isinf(d)) return (d > 0) ? std::make_pair(huge, d) : std::make_pair(d, -huge);
  const int c = compare_rational_double(r, d);
  if (c == 0) return {d, d};
  if (c > 0) return {d, std::nextafter(d, std::numeric_limits<double>::infinity())};
  return {std::nextafter(d, -std::numeric_limits<double>::infinity()), d};
}

std::pair<double, double> interval_of(const EpeckFT& x) { return interval_of(to_rational(x)); }

std::pair<double, double> interval_of(const SqrtExt& s) {
  Rational v;
  if (sqrt_ext_is_rational(s, v)) return interval_of(v);
  Rational lo, hi;
  sqrt_ext_bounds(s, 128, lo, hi);
  return {interval_of(lo).first, interval_of(hi).second};
}

std::pair<double, double> interval_of(const Algebraic& e, int bits) {
  Rational lo, hi;
  expr_enclosure(e, bits <= 0 ? 53 : bits, lo, hi);
  return {interval_of(lo).first, interval_of(hi).second};
}

// ===========================================================================
// number_conv.hpp — sign / comparison
// ===========================================================================

int sign_of(const Rational& r) { return r.sign(); }
int sign_of(const EpeckFT& x) { return static_cast<int>(CGAL::sign(x)); }
int sign_of(const SqrtExt& s) { return static_cast<int>(to_cgal_sqrt_ext(s).sign()); }
int sign_of(const Algebraic& e) { return e.sign(); }

int compare(const Rational& a, const Rational& b) { return rational_compare(a, b); }
int compare(const EpeckFT& a, const EpeckFT& b) { return static_cast<int>(CGAL::compare(a, b)); }

int compare(const SqrtExt& a, const SqrtExt& b) {
  // ACDE_TAG == Tag_true => Sqrt_extension::compare defaults to in_same_extension == false, i.e.
  // the exact different-roots branch (Sqrt_extension_type.h:557 ff.). No Expr fallback needed.
  return static_cast<int>(to_cgal_sqrt_ext(a).compare(to_cgal_sqrt_ext(b)));
}
int compare(const SqrtExt& a, const Rational& b) { return static_cast<int>(to_cgal_sqrt_ext(a).compare(b)); }
int compare(const Rational& a, const SqrtExt& b) { return -compare(b, a); }

int compare(const Algebraic& a, const Algebraic& b) {
  const int c = a.cmp(b);
  return (c < 0) ? -1 : (c > 0 ? 1 : 0);
}
int compare(const Algebraic& a, const Rational& b) { return compare(a, to_core_expr(b)); }
int compare(const Rational& a, const Algebraic& b) { return -compare(b, a); }
int compare(const Algebraic& a, const SqrtExt& b) { return compare(a, to_core_expr(b)); }
int compare(const SqrtExt& a, const Algebraic& b) { return -compare(b, a); }

// ===========================================================================
// Boxed numbers (numbers.hpp)
// ===========================================================================

NumberKind number_kind(const Geom& n) {
  require_type(n, GeomType::Number, "number");
  if (n.holds<Rational>()) return NumberKind::Rational;
  if (n.holds<SqrtExt>()) return NumberKind::SqrtExt;
  if (n.holds<Algebraic>()) return NumberKind::Algebraic;
  throw_error(ErrorCode::InvalidArgument, "boxed number holds an unknown C++ type");
}

double number_to_double(const Geom& n) {
  switch (number_kind(n)) {
    case NumberKind::Rational: return to_double_correctly_rounded(n.as<Rational>());
    case NumberKind::SqrtExt: return to_double_correctly_rounded(n.as<SqrtExt>());
    default: return to_double_correctly_rounded(n.as<Algebraic>());
  }
}

std::pair<double, double> number_interval(const Geom& n, int bits) {
  switch (number_kind(n)) {
    case NumberKind::Rational: return interval_of(n.as<Rational>());
    case NumberKind::SqrtExt: return interval_of(n.as<SqrtExt>());
    default: return interval_of(n.as<Algebraic>(), bits);
  }
}

bool number_is_rational(const Geom& n) {
  switch (number_kind(n)) {
    case NumberKind::Rational: return true;
    case NumberKind::SqrtExt: {
      Rational v;
      return sqrt_ext_is_rational(n.as<SqrtExt>(), v);
    }
    default:
      // CORE offers no safe rationality test: BigRatValue() is an approximation and the ExprRep
      // internals (ratFlag()/ratValue()) segfault (gotcha #4). Never claim rationality.
      return false;
  }
}

Rational number_to_rational(const Geom& n) {
  const NumberKind k = number_kind(n);
  if (k == NumberKind::Rational) return n.as<Rational>();
  if (k == NumberKind::SqrtExt) {
    Rational v;
    if (sqrt_ext_is_rational(n.as<SqrtExt>(), v)) return v;
    throw_error(ErrorCode::NotRepresentable,
                "number " + number_repr(n) + " is not rational (a + b*sqrt(c) with b != 0)");
  }
  throw_error(ErrorCode::NotRepresentable,
              "algebraic number " + number_repr(n) +
                  " cannot be converted to an exact rational (CORE::Expr has no exact rational view)");
}

SqrtExt number_to_sqrt_ext(const Geom& n) {
  const NumberKind k = number_kind(n);
  if (k == NumberKind::SqrtExt) return n.as<SqrtExt>();
  // Deliberately lenient: box_sqrt_ext() normalises a rational-valued a + b*sqrt(c) into a
  // Rational box, so a strict "SqrtExt only" rule would make this function unusable.
  if (k == NumberKind::Rational) return SqrtExt{n.as<Rational>(), Rational(0), Rational(0)};
  throw_error(ErrorCode::NotRepresentable,
              "algebraic number " + number_repr(n) + " is not of the form a + b*sqrt(c)");
}

int number_sign(const Geom& n) {
  switch (number_kind(n)) {
    case NumberKind::Rational: return sign_of(n.as<Rational>());
    case NumberKind::SqrtExt: return sign_of(n.as<SqrtExt>());
    default: return sign_of(n.as<Algebraic>());
  }
}

int number_compare(const Geom& a, const Geom& b) {
  const NumberKind ka = number_kind(a), kb = number_kind(b);
  if (ka == NumberKind::Rational && kb == NumberKind::Rational)
    return compare(a.as<Rational>(), b.as<Rational>());
  if (ka == NumberKind::SqrtExt && kb == NumberKind::SqrtExt)
    return compare(a.as<SqrtExt>(), b.as<SqrtExt>());
  if (ka == NumberKind::Algebraic && kb == NumberKind::Algebraic)
    return compare(a.as<Algebraic>(), b.as<Algebraic>());
  // Rational vs SqrtExt: exact through Sqrt_extension (cheaper than lifting to CORE).
  if (ka == NumberKind::SqrtExt && kb == NumberKind::Rational)
    return compare(a.as<SqrtExt>(), b.as<Rational>());
  if (ka == NumberKind::Rational && kb == NumberKind::SqrtExt)
    return compare(a.as<Rational>(), b.as<SqrtExt>());
  // Anything mixed with an Algebraic: lift both to CORE::Expr (exact).
  const Algebraic ea = (ka == NumberKind::Algebraic) ? a.as<Algebraic>()
                       : (ka == NumberKind::Rational) ? to_core_expr(a.as<Rational>())
                                                      : to_core_expr(a.as<SqrtExt>());
  const Algebraic eb = (kb == NumberKind::Algebraic) ? b.as<Algebraic>()
                       : (kb == NumberKind::Rational) ? to_core_expr(b.as<Rational>())
                                                      : to_core_expr(b.as<SqrtExt>());
  return compare(ea, eb);
}

std::string number_repr(const Geom& n) {
  switch (number_kind(n)) {
    case NumberKind::Rational:
      return rational_text(n.as<Rational>());
    case NumberKind::SqrtExt: {
      const SqrtExt& s = n.as<SqrtExt>();
      if (s.b.is_zero() || s.c.is_zero()) return rational_text(s.a);
      const bool bneg = (s.b.sign() < 0);
      const Rational ab = bneg ? Rational(-s.b) : s.b;
      const std::string root = "sqrt(" + rational_text(s.c) + ")";
      const std::string term = (ab == 1) ? root : (rational_text(ab) + "*" + root);
      if (s.a.is_zero()) return bneg ? ("-" + term) : term;
      return rational_text(s.a) + (bneg ? " - " : " + ") + term;
    }
    default: {
      // CORE::Expr has no exact textual form we can trust, so we print a 17-significant-digit
      // decimal approximation and mark it with a trailing '~' (documented in numbers.hpp).
      char buf[64];
      std::snprintf(buf, sizeof(buf), "%.17g", to_double_correctly_rounded(n.as<Algebraic>()));
      return std::string(buf) + "~";
    }
  }
}

}  // namespace arr2d
