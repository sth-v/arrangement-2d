# CGAL 6.1 — Number types, exact conversions, error handling

Target: a type-erased C++ core + Cython bindings for the CGAL 2D Arrangement package.

**Installation documented here** (everything below was read out of the installed headers and/or
verified by compiling and running):

* Headers: `/opt/homebrew/include/CGAL` — CGAL **6.1** (`CGAL_VERSION_NR 1060101000`,
  release date 20250929, git `b26b07a1242`), header-only.
* CORE: `/opt/homebrew/include/CGAL/CORE` + `/opt/homebrew/include/CGAL/CORE_*.h`.
* GMP/MPFR: Homebrew, `/opt/homebrew`.
* Compile line used for every verification in this document:

```
/usr/bin/clang++ -std=c++17 -O0 -DCGAL_USE_CORE -DCGAL_USE_GMP -DCGAL_USE_MPFR \
  -I/opt/homebrew/include -L/opt/homebrew/lib -lgmp -lmpfr -o test test.cpp
```

Notation: “VERIFIED” = printed by a compiled test program; quoted signatures are verbatim from
the headers with the file/line given.

---

## Gotchas / surprises vs. older CGAL

1. **`CGAL::Exact_rational` is `boost::multiprecision::mpq_rational`, NOT `CGAL::Gmpq`.**
   Even though we pass only `-DCGAL_USE_CORE -DCGAL_USE_GMP -DCGAL_USE_MPFR`,
   `<CGAL/Installation/internal/enable_third_party_libraries.h>` **auto-defines
   `CGAL_USE_BOOST_MP 1`** (and `CGAL_USE_GMP`, `CGAL_USE_MPFR`, `CGAL_USE_CORE`) purely from
   Boost version checks, so `Default_exact_nt_backend == BOOST_GMP_BACKEND`.
   VERIFIED demangled types:
   * `CGAL::Exact_rational` = `boost::multiprecision::number<backends::gmp_rational, et_on>` (= `mpq_rational`)
   * `CGAL::Exact_integer`  = `boost::multiprecision::number<backends::gmp_int, et_on>` (= `mpz_int`)
   * `CGAL::Epeck_ft`       = same as `Exact_rational`
   * `CGAL::Epeck::FT`      = `CGAL::Lazy_exact_nt<mpq_rational>`
   * `internal::exact_nt_backend_string()` returns `"BOOST_GMP_BACKEND"`.
   Consequence: **`CORE::BigRat` and `CGAL::Exact_rational` are the *same C++ type*** (both
   `mpq_rational`), and `CORE::BigInt == CGAL::Exact_integer == mpz_int`. One conversion path
   covers both. `CGAL::Gmpq`/`CGAL::Gmpz` still exist and work but are *not* what Epeck uses —
   do not write `Gmpq`-specific code. To force the old behaviour you would have to define
   `CGAL_DO_NOT_USE_BOOST_MP` **consistently in every TU** (ABI-breaking if inconsistent).

2. **`operator<<` on `Epeck::FT` is lossy, and `CGAL::to_double(Epeck::FT)` is not correctly
   rounded.** `Lazy_exact_nt.h:1305` defines `os << a` as `os << CGAL_NTS to_double(a)`.
   `to_double` on a `Lazy_exact_nt` returns a value derived from the *interval* approximation and
   only refines when the relative precision is worse than
   `get_relative_precision_of_to_double()` (default **1e-5**). VERIFIED: `to_double(FT(1)/FT(3))`
   returns `0.33333333333333337` while the correctly-rounded double is `0.33333333333333331`,
   and it *stays* wrong even after `set_relative_precision_of_to_double(1e-16)`.
   **Always go through `x.exact()`** for anything a Python user will see:
   `CGAL::to_double(x.exact())`, `os << x.exact()`.

3. **`istream >> Exact_rational` silently mis-parses decimals.** Boost's own extractor reads
   `"0.125"` as `0` **without setting `failbit`** (VERIFIED). And the string constructor
   `Exact_rational("0.125")` **throws** `boost::wrapexcept<std::runtime_error>`
   (`The string "0.125" could not be interpreted as a valid rational number.`). Use CGAL's
   wrapper `is >> CGAL::IO::iformat(q)` (→ `internal::read_float_or_quotient`), which accepts
   `"3"`, `"3/4"`, `"0.125"`, `"-1.25e3"` (VERIFIED).

4. **`CORE::Expr::BigRatValue()` is an *approximation*, not the exact value.** VERIFIED:
   `CORE::Expr(CORE::BigRat(1,10)).BigRatValue()` returns
   `61897001964269013744956211/618970019642690137449562112` (denominator `2^89`), not `1/10`.
   It equals the exact value only when that value is dyadic (`Expr(BigRat(3,4)).BigRatValue()==3/4`).
   Likewise `CORE::Expr("0.1")` is **not** exactly 1/10 despite the header comment.
   There is **no safe public predicate for “is this Expr rational”**: the internals
   (`e.Rep()->ratFlag()`, `ratValue()`) dereference a lazily-allocated `NodeInfo*` and
   **segfault** (VERIFIED crash, even after `e.sign()` and with
   `CORE::setRationalReduceFlag(true)`). Never touch `ExprRep`.

5. **Default error behaviour in 6.1 header-only is `THROW_EXCEPTION`, but the default handler
   still prints to `std::cerr` first.** `assertions_impl.h` initialises
   `_error_behaviour = THROW_EXCEPTION` and `_warning_behaviour = CONTINUE`. On clang the
   `_standard_error_handler` prints the "CGAL error: … violation!" block to `std::cerr`
   *and then* the exception is thrown (the early-return guard is `#if defined(__GNUG__) &&
   !defined(__llvm__)`, so it does **not** apply to Apple clang). For a Python binding you must
   install your own no-op handler with `CGAL::set_error_handler` or the terminal will be spammed.
   VERIFIED both the cerr spam and the thrown `CGAL::Precondition_exception`.

6. **`NDEBUG` kills preconditions.** `assertions.h` maps `NDEBUG → CGAL_NDEBUG →
   CGAL_NO_ASSERTIONS + CGAL_NO_PRECONDITIONS + CGAL_NO_POSTCONDITIONS + CGAL_NO_WARNINGS`.
   A release build with `-DNDEBUG` therefore silently disables *all* CGAL argument validation,
   including the arrangement package's preconditions. Define **`CGAL_DEBUG`** to undo it
   (it `#undef`s `CGAL_NDEBUG`) — see §8.3 for the recommended flag set.

7. Minor but real:
   * `<CGAL/Exact_predicates_exact_constructions_kernel.h>` does **not** pull in
     `<CGAL/Exact_rational.h>` — you must include it yourself (VERIFIED compile error).
   * `Lazy_exact_nt::depth()` returns `0` always unless `CGAL_PROFILE` is defined
     (`Lazy.h:230-248`, `Depth_base`). Useless as a DAG-size heuristic in a normal build.
   * The primary `CGAL::Fraction_traits<T>` template is a **dummy** (`Is_fraction = Tag_false`,
     all functors `Null_functor`). You get a real specialisation only by including the number
     type's own header. Compiles fine and does the wrong thing if you forget.
   * `CGAL::Epeck` is a **`Lazy_kernel`** over `Simple_cartesian<Epeck_ft>` (not a
     `Filtered_kernel<Cartesian<...>>`), unless you define `CGAL_DONT_USE_LAZY_KERNEL`.
   * `sizeof(Epeck::Point_2) == 8` — it is a single refcounted handle; `sizeof(Epeck::FT) == 16`,
     `sizeof(Exact_rational) == 32` (VERIFIED).

---

## 1. Type resolution in this installation

### 1.1 `Exact_type_selector.h` — where the choice is made

File: `/opt/homebrew/include/CGAL/Number_types/internal/Exact_type_selector.h`

```cpp
namespace CGAL { namespace internal {

enum ENT_backend_choice
{
  GMP_BACKEND,
  GMPXX_BACKEND,
  BOOST_GMP_BACKEND,
  BOOST_BACKEND,
  LEDA_BACKEND,
  MP_FLOAT_BACKEND
};

template <ENT_backend_choice> struct Exact_NT_backend;

#if defined (CGAL_USE_BOOST_MP) && defined(CGAL_USE_GMP)
template <>
struct Exact_NT_backend<BOOST_GMP_BACKEND>
  : public BOOST_gmp_arithmetic_kernel
{
  typedef Exact_NT_backend<GMP_BACKEND>::Ring_for_float Ring_for_float;
};
#endif

constexpr ENT_backend_choice Default_exact_nt_backend =
#ifdef CGAL_USE_GMPXX
  GMPXX_BACKEND;
#elif defined(CGAL_USE_GMP)
  #if defined(CGAL_USE_BOOST_MP)
    BOOST_GMP_BACKEND;
  #else
    GMP_BACKEND;
  #endif
#elif BOOST_VERSION > 107900 && defined(CGAL_USE_BOOST_MP)
  BOOST_BACKEND;
#elif defined(CGAL_USE_LEDA)
  LEDA_BACKEND;
#else
  MP_FLOAT_BACKEND;
#endif

template < typename > struct Exact_field_selector;
template < typename > struct Exact_ring_selector;
// specialised for double, float, int to:
//   Exact_ring_selector<X>::Type  = Exact_NT_backend<Default_exact_nt_backend>::Ring_for_float
//   Exact_field_selector<X>::Type = Exact_NT_backend<Default_exact_nt_backend>::Rational

constexpr const char* exact_nt_backend_string();   // returns "BOOST_GMP_BACKEND" here

} } // CGAL::internal
```

`CGAL_USE_BOOST_MP` is **not** in `config.h`. It comes from
`/opt/homebrew/include/CGAL/Installation/internal/enable_third_party_libraries.h`:

```cpp
#define CGAL_USE_GMP  1
#define CGAL_USE_MPFR 1
...
#if !defined CGAL_DO_NOT_USE_BOOST_MP && \
    (!defined _MSC_VER || BOOST_VERSION >= 107000) && \
    (!defined _WIN32 || defined _WIN64) && \
    (BOOST_VERSION >= 108000 || (!defined _ARCH_PPC && !defined _ARCH_PPC64))
#define CGAL_USE_BOOST_MP 1
#endif

#if CGAL_USE_BOOST_MP
#if ! CGAL_NO_CORE
#  define CGAL_USE_CORE 1
#endif
#endif
```

### 1.2 `Exact_rational.h` / `Exact_integer.h`

```cpp
// /opt/homebrew/include/CGAL/Exact_rational.h
using Exact_rational = internal::Exact_NT_backend<internal::Default_exact_nt_backend>::Rational;

// /opt/homebrew/include/CGAL/Exact_integer.h
using Exact_integer  = internal::Exact_NT_backend<internal::Default_exact_nt_backend>::Integer;
```

Models: `Exact_rational` → `Field, RealEmbeddable, Fraction, FromDoubleConstructible`.
`Exact_integer` → `EuclideanRing, RealEmbeddable`.

### 1.3 Resolved types (VERIFIED at runtime)

| Alias | Resolves to | `sizeof` |
|---|---|---|
| `CGAL::Exact_rational` | `boost::multiprecision::number<backends::gmp_rational, et_on>` (`mpq_rational`) | 32 |
| `CGAL::Exact_integer` | `boost::multiprecision::number<backends::gmp_int, et_on>` (`mpz_int`) | 32 |
| `CGAL::Epeck_ft` | same as `Exact_rational` | 32 |
| `CGAL::Epeck::FT` = `Epeck::RT` | `CGAL::Lazy_exact_nt<mpq_rational>` | 16 |
| `CGAL::Epeck::Point_2` | `CGAL::Point_2<CGAL::Epeck>` | 8 |
| `Exact_NT_backend<...>::Ring_for_float` | `CGAL::Mpzf` (`CGAL_HAS_MPZF` is defined) | — |
| `CORE::BigInt` | `boost::multiprecision::mpz_int` — **identical to `Exact_integer`** | — |
| `CORE::BigRat` | `boost::multiprecision::mpq_rational` — **identical to `Exact_rational`** | — |
| `Fraction_traits<Exact_rational>::{Numerator,Denominator}_type` | `mpz_int` | — |
| `Fraction_traits<Epeck::FT>::{Numerator,Denominator}_type` | `Lazy_exact_nt<mpz_int>` | — |
| `Fraction_traits<Epeck::FT>::Is_fraction` | `std::true_type` | — |

Recommended aliases for the C++ core:

```cpp
#include <CGAL/Exact_predicates_exact_constructions_kernel.h>
#include <CGAL/Exact_rational.h>      // NOT pulled in by the kernel header
#include <CGAL/Exact_integer.h>
#include <CGAL/Fraction_traits.h>
#include <CGAL/number_utils.h>
#include <CGAL/IO/io.h>

using EK = CGAL::Epeck;                       // Exact_predicates_exact_constructions_kernel
using FT = EK::FT;                            // Lazy_exact_nt<Exact_rational>
using ER = CGAL::Exact_rational;              // mpq_rational == CORE::BigRat
using EI = CGAL::Exact_integer;               // mpz_int      == CORE::BigInt
static_assert(std::is_same_v<FT::ET, ER>);
static_assert(std::is_same_v<ER, CORE::BigRat>);   // holds in this installation
```

---

## 2. `CGAL::Exact_rational` / `CGAL::Exact_integer` (= boost.multiprecision)

Because these are `boost::multiprecision::number<Backend, et_on>` you get the whole Boost API in
addition to the CGAL traits. The relevant members (all `const` unless noted):

```cpp
// construction
number();
number(const number&);  number(number&&);
template<class V> number(const V& v);                 // int, long, long long, unsigned…, double, const char*, std::string
number(const char* s);                                // THROWS std::runtime_error on bad syntax
number(const std::string& s);
number(const T& num, const T& den);                   // rational only: number(mpz_int, mpz_int), auto-canonicalised

// assignment / parsing
number& assign(const char*);                          // also throws on bad syntax
template<class V> number& operator=(const V&);

// conversion
template<class T> T convert_to() const;               // convert_to<double>(), <long>(), <long long>()…
std::string str(std::streamsize digits = 0,
                std::ios_base::fmtflags f = {}) const; // exact "n/d" for rationals when digits==0
int sign() const;                                     // -1 / 0 / 1
int compare(const number& other) const;               // <0 / 0 / >0
bool is_zero() const;
```

Free functions found by ADL (all in `boost::multiprecision`, usable unqualified):

```cpp
mpz_int numerator  (const mpq_rational&);
mpz_int denominator(const mpq_rational&);
mpq_rational abs(const mpq_rational&);
mpz_int gcd(const mpz_int&, const mpz_int&);
void    divide_qr(const mpz_int& a, const mpz_int& b, mpz_int& q, mpz_int& r);
std::size_t msb(const mpz_int&), lsb(const mpz_int&);
bool    bit_test(const mpz_int&, unsigned);
mpz_int pow(const mpz_int&, unsigned);
```

### 2.1 CGAL traits for these types

From `/opt/homebrew/include/CGAL/boost_mp_type.h`:

* `Algebraic_structure_traits<mpq_rational>` : `Field_tag`, `Is_exact = Tag_true`.
* `Algebraic_structure_traits<mpz_int>` : `Euclidean_ring_tag`, `Is_exact = Tag_true`.
* `Real_embeddable_traits<...>` (base `RET_boost_mp_base`, lines 430–500):

```cpp
struct Abs       : unary_function<Type, Type>          { Type operator()(const T& x) const; };
struct Sgn       : unary_function<Type, ::CGAL::Sign>  { ::CGAL::Sign operator()(Type const& x) const; };  // CGAL::sign(x.sign())
struct Compare   : binary_function<Type, Type, Comparison_result>
                                                       { Comparison_result operator()(const Type& x, const Type& y) const; }; // CGAL::sign(x.compare(y))
struct To_double : unary_function<Type, double>        { double operator()(const Type& x) const; };  // x.convert_to<double>()
struct To_interval: unary_function<Type, std::pair<double,double>>
                                                       { std::pair<double,double> operator()(const Type& x) const; };
```

`To_interval` is **specialised** for `mpz_int` and `mpq_rational` when `CGAL_USE_MPFR` is on
(`boost_mp_type.h:531-604`): it goes through MPFR at 53 bits with `MPFR_RNDA` and
`mpfr_subnormalize`, returning a *tight, certified* interval (a single point when the value is
exactly a double). VERIFIED: `to_interval(ER("1/3")) == [0.33333333333333331, 0.33333333333333337]`.

* `Fraction_traits<mpq_rational>` (`boost_mp_type.h:698-733`):

```cpp
typedef NT                                             Type;
typedef ::CGAL::Tag_true                               Is_fraction;
typedef boost::multiprecision::component_type<NT>::type Numerator_type;   // mpz_int
typedef Numerator_type                                 Denominator_type;
typedef Algebraic_structure_traits<Numerator_type>::Gcd Common_factor;

class Decompose {
public:
    typedef Type              first_argument_type;
    typedef Numerator_type&   second_argument_type;
    typedef Denominator_type& third_argument_type;
    void operator () (const Type& rat, Numerator_type& num, Denominator_type& den);   // NOT const!
};
class Compose {
public:
    typedef Numerator_type    first_argument_type;
    typedef Denominator_type  second_argument_type;
    typedef Type              result_type;
    Type operator ()(const Numerator_type& num, const Denominator_type& den);          // NOT const!
};
```

> Note: `Decompose::operator()` and `Compose::operator()` are **non-const member functions**.
> You must hold the functor in a non-const local (`Fraction_traits<ER>::Decompose dec;`), not a
> `const` one. Same for `Gmpq` and for the `Lazy_exact_nt` wrapper.

* `Coercion_traits` exist between any two boost.mp `number`s that have a `boost::common_type`,
  and to/from `int`, `short`, `float`, `double`.

### 2.2 I/O

* `operator<<(std::ostream&, mpq_rational)` — Boost's, prints exact `"n/d"` (or `"n"` when
  `d==1`). VERIFIED `os << ER("22/7")` → `22/7`; a 30-digit/30-digit rational round-trips.
* `operator>>(std::istream&, mpq_rational)` — Boost's. **Only accepts `n` and `n/d`.**
  On `"0.125"` it yields `0` and leaves the stream good (VERIFIED). Never use it on user input.
* `CGAL::IO::iformat(q)` → `CGAL::Input_rep<mpq_rational>` (`boost_mp_type.h:881-891`) which calls
  `internal::read_float_or_quotient<mpz_int, mpq_rational>`. Implementation in
  `/opt/homebrew/include/CGAL/IO/io.h:894-1004`; grammar:

  ```
  [ws] [ '+' | '-' ] { digit }  [ '/' <integer> | '.' { digit } ] [ ('e'|'E') <int exponent> ]
  ```
  Sets `failbit` if no digits were seen. Accepts `"3"`, `"3/4"`, `"0.125"`, `".5"`,
  `"-1.25e3"` (→ `-1250`), and rejects `"abc"` (VERIFIED). It builds the value with
  `Fraction_traits<Rat>::Compose`, so **`"1/0"` throws `std::runtime_error("Division by zero.")`**
  from Boost — catch it.

---

## 3. `CGAL::Lazy_exact_nt<ET>` — `Epeck::FT`

File: `/opt/homebrew/include/CGAL/Lazy_exact_nt.h`. Base: `CGAL::Lazy<Interval_nt<false>, ET, To_interval<ET>>`,
which derives from `CGAL::Handle` — i.e. it is a **reference-counted handle to a shared,
immutable DAG node**. Copying is O(1) and shares state; `exact()` memoises into that shared node.

```cpp
template <typename ET_>
class Lazy_exact_nt
  : public Lazy<Interval_nt<false>, ET_, To_interval<ET_> >
  , boost::ordered_euclidian_ring_operators2< Lazy_exact_nt<ET_>, int >
  , boost::ordered_euclidian_ring_operators2< Lazy_exact_nt<ET_>, double >
{
public:
  typedef Lazy_exact_nt<ET_> Self;
  typedef Lazy<Interval_nt<false>, ET_, To_interval<ET_> > Base;
  typedef typename Base::Self_rep  Self_rep;

  typedef typename Base::ET ET;                     // undocumented; = ET_ = Exact_rational
  typedef typename Base::AT AT;                     // undocumented; = Interval_nt<false>

  typedef typename Base::Exact_type        Exact_type;        // = ET
  typedef typename Base::Approximate_type  Approximate_type;  // = Interval_nt<false>
```

### 3.1 Constructors

```cpp
  Lazy_exact_nt () {}                                // shares a static thread-local zero node
  Lazy_exact_nt (Self_rep *r);                       // takes ownership of a new'ed rep

  template<class T>                                  // enabled for arithmetic/enum T that is not ET
  Lazy_exact_nt (T i, std::enable_if_t<
      (std::is_arithmetic_v<T> || std::is_enum_v<T>) &&
      !std::is_same_v<T,ET>,void*> = 0);             // implicit; int, long long, double, …

  Lazy_exact_nt (const ET & e);                      // implicit, from Exact_rational
  Lazy_exact_nt (ET&& e);

  template <class ET1>                               // implicit iff ET1 implicitly converts to ET
  Lazy_exact_nt (const Lazy_exact_nt<ET1> &x, …);
  template <class ET1>                               // explicit otherwise
  explicit Lazy_exact_nt (const Lazy_exact_nt<ET1> &x, …);

  friend void swap(Lazy_exact_nt& a, Lazy_exact_nt& b) noexcept;
```

**Exactness of `FT(double)` and `FT(int64)`:** the `T` constructor stores the value in a
`Lazy_exact_Cst<ET,T>` node whose `update_exact()` builds `ET(the stored T)`, i.e. an exact
conversion of the *double's actual binary value*. VERIFIED:

* `FT(0.1).exact()` → `3602879701896397/36028797018963968` (exact value of the double 0.1 — **not** 1/10)
* `FT(9007199254740993LL).exact()` → `9007199254740993` (2^53+1 survives; the `long long` path is exact, no double round-trip)
* `ER(0.1)` → `3602879701896397/36028797018963968`; `ER(-9223372036854775807LL)` and
  `ER(18446744073709551615ULL)` are exact.

So: `NT(double)` is exact **with respect to the double**, never a decimal-literal reinterpretation.
If a Python user types `0.1` and means one tenth, you must build `ER(1,10)` from the decimal
string, not from the C `double`.

### 3.2 Arithmetic / comparison

```cpp
  Self operator+ () const;                           // returns *this
  Self operator- () const;

  Self & operator+=(const Self& b);
  Self & operator-=(const Self& b);
  Self & operator*=(const Self& b);
  Self & operator/=(const Self& b);                  // CGAL_precondition(b != 0)

  Self & operator+=(CGAL_int(ET) b);                 // CGAL_int(ET) == int for ET=mpq_rational
  Self & operator-=(CGAL_int(ET) b);
  Self & operator*=(CGAL_int(ET) b);
  Self & operator/=(CGAL_int(ET) b);                 // CGAL_precondition(b != 0)
  Self & operator+=(CGAL_double(ET) b);              // CGAL_double(ET) == double
  Self & operator-=(CGAL_double(ET) b);
  Self & operator*=(CGAL_double(ET) b);
  Self & operator/=(CGAL_double(ET) b);              // CGAL_precondition(b != 0)

  Self & operator%=(const Self& b);                  // CGAL_precondition(b != 0); forces exact(), kills filtering
  Self & operator%=(int b);                          // CGAL_precondition(b != 0)

  // hidden friends, mixed with int and double (filter first, fall back to exact()):
  friend bool operator<(const Lazy_exact_nt& a, int b);
  friend bool operator>(const Lazy_exact_nt& a, int b);
  friend bool operator==(const Lazy_exact_nt& a, int b);
  friend bool operator<(const Lazy_exact_nt& a, double b);
  friend bool operator>(const Lazy_exact_nt& a, double b);
  friend bool operator==(const Lazy_exact_nt& a, double b);
```

Free binary operators (`Lazy_exact_nt.h:647-685`) return
`Lazy_exact_nt<typename Coercion_traits<ET1,ET2>::Type>`:

```cpp
template <typename ET1, typename ET2>
Lazy_exact_nt<typename Coercion_traits<ET1,ET2>::Type> operator+(const Lazy_exact_nt<ET1>&, const Lazy_exact_nt<ET2>&);
… operator-(…);  … operator*(…);
… operator/(…);            // CGAL_precondition(b != 0)
template <typename ET> Lazy_exact_nt<ET> operator%(const Lazy_exact_nt<ET>&, const Lazy_exact_nt<ET>&);  // precondition b != 0

template <typename ET1, typename ET2> bool operator< (const Lazy_exact_nt<ET1>&, const Lazy_exact_nt<ET2>&);
template <typename ET1, typename ET2> bool operator==(const Lazy_exact_nt<ET1>&, const Lazy_exact_nt<ET2>&);
template <typename ET1, typename ET2> bool operator> (…) { return b < a; }
template <typename ET1, typename ET2> bool operator>=(…) { return !(a < b); }
template <typename ET1, typename ET2> bool operator<=(…) { return b >= a; }
template <typename ET1, typename ET2> bool operator!=(…) { return !(a == b); }
```

All comparisons first test the interval; only on an indeterminate result do they call
`exact()` on both sides. `a.identical(b)` short-circuits equality/comparison for shared nodes.

### 3.3 Approximation / exactness accessors

Inherited from `Lazy` (`Lazy.h:876-975`):

```cpp
  decltype(auto) approx() const;        // -> const Interval_nt<false>&   (no refinement triggered)
  const ET&      exact()  const;        // forces evaluation once (std::call_once), memoised in the node
  unsigned       depth()  const;        // ALWAYS 0 unless CGAL_PROFILE is defined
  void           print_dag(std::ostream& os, int level) const;
```

Own members:

```cpp
  Interval_nt<true>     interval()   const;   // Interval_nt<true>(approx().inf(), approx().sup())
  Interval_nt_advanced  approx_adv() const;   // = Interval_nt<true>, from ptr()->approx()

  static const double & get_relative_precision_of_to_double();     // default 0.00001 (VERIFIED)
  static void set_relative_precision_of_to_double(double d);       // CGAL_assertion(0 < d && d < 1)
                                                                   // stored in a THREAD-LOCAL static

  bool identical(const Self& b) const;        // same DAG node?
  template <typename T> bool identical(const T&) const { return false; }
```

Free functions (`Lazy.h:70-92`, usable on any `Lazy`):

```cpp
template <typename AT, typename ET, typename E2A> decltype(auto) CGAL::approx(const Lazy<AT,ET,E2A>& l);
template <typename AT, typename ET, typename E2A> const ET&      CGAL::exact (const Lazy<AT,ET,E2A>& l);
template <typename AT, typename ET, typename E2A> int            CGAL::depth (const Lazy<AT,ET,E2A>& l);
```

**Ownership / lifetime.** `exact()` returns a `const ET&` **into the refcounted DAG node**, not
into the `Lazy_exact_nt` object you called it on — but the node is kept alive by that object.
The reference stays valid as long as *any* handle to that node lives. In a binding, **copy it
out** (`ER v = x.exact();`) before letting the `FT` die. VERIFIED that `ER copy = c.exact();`
works and that the value is right.

`exact()` is `const` but mutates the shared node (memoisation) under `std::call_once`.
Concurrent `exact()` on the *same* node from two threads is safe; see §9.

### 3.4 `Real_embeddable_traits<Lazy_exact_nt<ET>>` (`Lazy_exact_nt.h:1010-1123`)

```cpp
class Abs        : unary_function<Type, Type>          { Type operator()(const Type& a) const; };
class Sgn        : unary_function<Type, ::CGAL::Sign>  { ::CGAL::Sign operator()(const Type& a) const; };
class Compare    : binary_function<Type, Type, Comparison_result>
                                                       { Comparison_result operator()(const Type& a, const Type& b) const; };
class To_double  : unary_function<Type, double>        { double operator()(const Type& a) const; };
class To_interval: unary_function<Type, std::pair<double,double>>
                                                       { std::pair<double,double> operator()(const Type& a) const; };
class Is_finite  : unary_function<Type, bool>          { bool operator()(const Type& x) const; };
```

Exact bodies matter:

* `Sgn` / `Compare`: try the interval, else `sign(a.exact())` / `compare(a.exact(), b.exact())`.
  `Compare` short-circuits on `a.identical(b)` → `EQUAL`.
* **`To_double`**:

```cpp
double operator()( const Type& a ) const {
    const Interval_nt<false>& app = a.approx();
    double r;
    if (fit_in_double(app, r))
        return r;
    if (has_smaller_relative_precision(app,
         Lazy_exact_nt<ET>::get_relative_precision_of_to_double()))
        return CGAL_NTS to_double(app);
    a.exact();                      // refines the approximation
    return CGAL_NTS to_double(a.approx());
}
```
  `to_double(Interval_nt)` is the interval *midpoint*, so the result is **not** the
  correctly-rounded double of the exact value (see gotcha #2).
* **`To_interval`**: `return a.approx().pair();` — the *current* interval, **without** forcing
  refinement. If the value came out of a long lazy chain the interval can be very wide.
  Call `x.exact()` first if you need a tight certified enclosure, or use
  `CGAL::to_interval(x.exact())`.
* `Is_finite`: `is_finite(x.approx()) || is_finite(x.exact())`.

### 3.5 `Fraction_traits<Lazy_exact_nt<ET>>` (`Lazy_exact_nt.h:1200-1250`)

When `Fraction_traits<ET>::Is_fraction` is `Tag_true` (it is, for `mpq_rational`):

```cpp
typedef Lazy_exact_nt<ET>                 Type;
typedef Tag_true                          Is_fraction;
typedef Lazy_exact_nt<ET_numerator>       Numerator_type;    // Lazy_exact_nt<mpz_int>
typedef Lazy_exact_nt<ET_denominator>     Denominator_type;  // Lazy_exact_nt<mpz_int>

struct Common_factor : binary_function<Denominator_type,Denominator_type,Denominator_type> {
    Denominator_type operator()(const Denominator_type& a, const Denominator_type& b) const;
};
struct Compose : binary_function<Type,Numerator_type,Denominator_type> {
    Type operator()(const Numerator_type& n, const Denominator_type& d) const;   // forces n.exact(), d.exact()
};
struct Decompose {
    typedef void             result_type;
    typedef Type             first_argument_type;
    typedef Numerator_type   second_argument_type;
    typedef Denominator_type third_argument_type;
    void operator()(const Type& f, Numerator_type& n, Denominator_type& d) const; // forces f.exact()
};
```

Note the numerator/denominator come back **wrapped in `Lazy_exact_nt`** — you need
`n.exact()` to reach the `mpz_int`. It is almost always simpler to call `x.exact()` first
and use `Fraction_traits<Exact_rational>` (§4.1).

### 3.6 I/O — both directions are traps

```cpp
template <typename ET>
std::ostream & operator<< (std::ostream & os, const Lazy_exact_nt<ET> & a)
{ return os << CGAL_NTS to_double(a); }                       // LOSSY (Lazy_exact_nt.h:1303-1306)

template <typename ET>
std::istream & operator>> (std::istream & is, Lazy_exact_nt<ET> & a)
{ ET e; internal::read_float_or_quotient(is, e); if (is) a = std::move(e); return is; }
```

Reading is fine (it uses `read_float_or_quotient`, so `"22/7"` and `"0.125"` both work —
VERIFIED `is >> f; f.exact() == 22/7`). **Writing is lossy** — use `os << a.exact()`.

---

## 4. Recipes (all verified by compiling and running)

### 4.1 Exact rational ⇄ decimal `(numerator, denominator)` strings

Because `Exact_rational == CORE::BigRat` here, one implementation covers both. For `Epeck::FT`
just call `.exact()` first.

```cpp
#include <CGAL/Exact_rational.h>
#include <CGAL/Exact_integer.h>
#include <CGAL/Fraction_traits.h>
#include <CGAL/IO/io.h>
#include <sstream>
#include <stdexcept>

using ER = CGAL::Exact_rational;   // == CORE::BigRat == mpq_rational
using EI = CGAL::Exact_integer;    // == CORE::BigInt == mpz_int
using FT = CGAL::Epeck::FT;

// ---- decompose ------------------------------------------------------------
std::pair<std::string,std::string> rat_to_strings(const ER& q) {
    CGAL::Fraction_traits<ER>::Decompose dec;      // non-const: operator() is non-const
    EI n, d;
    dec(q, n, d);                                  // canonical: d > 0, gcd(n,d) == 1
    return { n.str(), d.str() };                   // base-10, exact, arbitrary length
}
// equivalent, no traits:  { numerator(q).str(), denominator(q).str() }   (ADL, boost::multiprecision)

std::pair<std::string,std::string> ft_to_strings(const FT& x) {
    return rat_to_strings(x.exact());              // forces evaluation, memoised
}

// ---- compose --------------------------------------------------------------
ER rat_from_strings(const std::string& num, const std::string& den) {
    EI n(num), d(den);                             // throws std::runtime_error on bad syntax
    if (d.is_zero()) throw std::invalid_argument("zero denominator");
    CGAL::Fraction_traits<ER>::Compose comp;
    return comp(n, d);                             // canonicalises (sign moved to numerator, gcd reduced)
}
FT ft_from_strings(const std::string& n, const std::string& d) { return FT(rat_from_strings(n,d)); }

// ---- decimal / fraction text ---------------------------------------------
// Accepts "3", "-3", "3/4", "0.125", ".5", "-1.25e3".  Never use `is >> q` directly.
bool rat_from_text(const std::string& s, ER& out) {
    std::istringstream is(s);
    is >> CGAL::IO::iformat(out);                  // -> internal::read_float_or_quotient
    if (is.fail()) return false;
    char junk; if (is >> junk) return false;       // reject trailing garbage
    return true;
}                                                  // NB: "1/0" throws std::runtime_error("Division by zero.")

std::string rat_to_text(const ER& q) {             // exact "n/d" (or "n")
    std::ostringstream os; os << q; return os.str();
}
```

VERIFIED outputs: `rat_to_strings(ER("22/7")) == {"22","7"}`; `rat_from_text("-0.125")` → `-1/8`;
`rat_from_text("-1.25e3")` → `-1250`; `rat_from_text("abc")` → `false`;
`ft_to_strings(FT(ER("22/7"))) == {"22","7"}`; a
`-123456789012345678901234567890/987654321098765432109876543211` round-trips through
`operator<<` / string-ctor exactly.

**Do not** use `ER("0.125")` — the string constructor is `mpq_set_str`-like and **throws**
(VERIFIED). Only `n` and `n/d` are valid for the constructor.

`ER::str(digits, flags)` ignores `digits`/`fixed` for rationals — `ER("1/8").str(20, std::ios_base::fixed)`
returns `"1/8"` (VERIFIED). If Python needs a decimal expansion, do it yourself from
numerator/denominator, or via `convert_to<double>()` for a lossy one.

For `CORE::BigRat` the *idiomatic CORE* accessors also exist and are exact:

```cpp
namespace CORE {
  inline BigInt numerator  (const BigRat& q);      // CORE/BigRat.h
  inline BigInt denominator(const BigRat& q);
  inline BigInt BigIntValue(const BigRat& br);     // truncating quotient
  inline BigRat div_exact(const BigRat& x, const BigRat& y);
  inline BigRat gcd(const BigRat& x, const BigRat& y);
}
// and CGAL::CORE_algebraic_number_traits (CORE_algebraic_number_traits.h:48-68):
Integer   numerator  (const Rational& q) const;    // Integer = CORE::BigInt, Rational = CORE::BigRat
Integer   denominator(const Rational& q) const;
Algebraic convert    (const Integer& z) const;     // Algebraic = CORE::Expr
Algebraic convert    (const Rational& q) const;
```

### 4.2 Exact construction from `double` and from `int64`

| Expression | Result | Exact w.r.t. the C++ value? |
|---|---|---|
| `ER(0.1)` | `3602879701896397/36028797018963968` | yes (exact value of the `double`) |
| `FT(0.1)` → `.exact()` | same | yes |
| `ER(9007199254740993LL)` | `9007199254740993` | yes, no double round-trip |
| `FT(9007199254740993LL)` → `.exact()` | `9007199254740993` | yes |
| `ER(-9223372036854775807LL)`, `ER(18446744073709551615ULL)` | exact | yes |
| `CORE::Expr(0.1).BigRatValue()` | `3602879701896397/36028797018963968` | yes for this dyadic case |
| `CORE::Expr("0.1").BigRatValue()` | `61897001964269013744956211/2^89` | **NO** — approximation |

All VERIFIED. Practical rule for the binding:

```cpp
FT ft_from_double(double d)   { return FT(d); }                     // exact w.r.t. the IEEE double
FT ft_from_int64 (long long v){ return FT(v); }                     // exact, no rounding
FT ft_from_ratio (long long n, long long d) {                       // exact "one tenth"
    if (d == 0) throw std::invalid_argument("zero denominator");
    return FT(ER(EI(n), EI(d)));                                    // mpq_rational(mpz_int, mpz_int), auto-canonicalised
}
```

`CORE::Expr` conversions from exact inputs, for reference (`CORE/Expr.h:50-121`):

```cpp
Expr();  Expr(int);  Expr(short);  Expr(unsigned int);
Expr(long);  Expr(unsigned long);
Expr(float);  Expr(double);                        // exact w.r.t. the binary value; CGAL_error_msg on inf/NaN
Expr(const BigInt& I);
Expr(const BigRat& R);                             // EXACT node — the only exact rational entry point
Expr(const BigFloat& F);
Expr(const char *s, const extLong& p = get_static_defInputDigits());   // NOT exact for decimals
Expr(const std::string& s, const extLong& p = get_static_defInputDigits());
Expr(const Real& r);
Expr(ExprRep* p);
template <class NT> Expr(const Polynomial<NT>& p, int n = 0);          // n-th positive root
template <class NT> Expr(const Polynomial<NT>& p, const BFInterval& I); // root isolated in I
```

**Use `Expr(BigRat)`, never `Expr("…")`, when you need an exact rational `Expr`.**

### 4.3 `to_double` and `to_interval`

Generic entry points (`/opt/homebrew/include/CGAL/number_utils.h:288-302`):

```cpp
template< class Real_embeddable >
typename Real_embeddable_traits<Real_embeddable>::To_double::result_type
to_double( const Real_embeddable& x );                       // -> double

template< class Real_embeddable >
typename Real_embeddable_traits<Real_embeddable>::To_interval::result_type
to_interval( const Real_embeddable& x );                     // -> std::pair<double,double>
```

Behaviour per type (VERIFIED numbers for `1/3`, `sqrt(2)`):

| Type | `to_double` | `to_interval` |
|---|---|---|
| `Exact_rational` | `x.convert_to<double>()` — **correctly rounded**, `0.33333333333333331` | MPFR at 53 bits, tight & certified: `[0.33333333333333331, 0.33333333333333337]`, degenerate when exact |
| `Exact_integer` | `convert_to<double>()` | MPFR, tight & certified |
| `Epeck::FT` | interval-derived, **not correctly rounded** (`0.33333333333333337`) | `a.approx().pair()` — the *current* interval, **no refinement forced** |
| `CORE::Expr` | `x.approx(53,1075); return x.doubleValue();` | `x.approx(53,1075); x.doubleInterval(lo,hi);` — certified, `[1.4142135623730949, 1.4142135623730951]` |
| `Interval_nt<b>` | midpoint | itself |

Recommended binding code:

```cpp
double            ft_to_double  (const FT& x) { return CGAL::to_double(x.exact()); }   // correctly rounded
std::pair<double,double> ft_to_interval(const FT& x) {
    x.exact();                                   // refine first, else the interval may be wide
    return CGAL::to_interval(x.exact());         // tight MPFR enclosure of the exact rational
}
```

**Forcing `CORE::Expr` refinement to a certified interval of a chosen width.**
`CORE/Expr.h:282-286`:

```cpp
  /// Compute approximation to combined precision [r, a].
  /** If e is the exact value and ee is the approximate value,
      then  |e - ee| <= 2^{-a} or  |e - ee| <= 2^{-r} |e|. */
  const Real & approx(const extLong& relPrec = get_static_defRelPrec(),   // default 60
                      const extLong& absPrec = get_static_defAbsPrec()) const;  // default +inf
  CGAL_CORE_EXPORT void doubleInterval(double & lb, double & ub) const;
  BigInt   BigIntValue()   const;   // approximate!
  BigRat   BigRatValue()   const;   // approximate!
  BigFloat BigFloatValue() const;   // the current approximation, with an error term
  std::string toString(long prec = get_static_defOutputDigits(), bool sci=false) const;
```

`doubleInterval` can never be narrower than 1 ulp of a `double` — VERIFIED that
`approx(10,10)`, `approx(60,60)` and `approx(200,200)` all give a `doubleInterval` of width
`2.22e-16` for `sqrt(2)` (`approx` only ever *refines*, it never coarsens; a prior
`to_interval` call already pushed it to 53 bits). For a genuinely tight enclosure go through
`BigFloat`:

```cpp
// certified rational enclosure of an Expr at ~`bits` bits.  VERIFIED width 2.21e-75 at bits=120.
std::pair<CORE::BigRat, CORE::BigRat> expr_enclosure(const CORE::Expr& e, long bits) {
    e.approx(bits, bits);                       // refine (relPrec, absPrec) — monotone, never loosens
    CORE::BigFloat bf = e.BigFloatValue();      // value ∈ [(m-err)·2^exp, (m+err)·2^exp]
    CORE::BigFloat lo = bf; lo.makeFloorExact();
    CORE::BigFloat hi = bf; hi.makeCeilExact();
    return { lo.BigRatValue(), hi.BigRatValue() };   // BigFloat::BigRatValue IS exact (dyadic)
}
```

Relevant `CORE::BigFloat` API (`CORE/BigFloat.h`):

```cpp
  BigFloat(const BigRat& R, const extLong& r = get_static_defRelPrec(),
                            const extLong& a = get_static_defAbsPrec());
  explicit BigFloat(const Expr& E, const extLong& r = …, const extLong& a = …);
  const BigInt& m()   const;          // mantissa
  unsigned long err() const;          // error, in units of the last mantissa "chunk"
  long          exp() const;          // exponent (CORE chunks, 14 bits each)
  bool isExact()      const;          // err() == 0
  bool isZeroIn()     const;          // interval contains 0
  BigFloat& makeExact();              // drop the error term (round to nearest representable)
  BigFloat& makeCeilExact();          // exact upper bound
  BigFloat& makeFloorExact();         // exact lower bound
  int    sign()          const;       // CGAL_assertion((err()==0 && m()==0) || !isZeroIn())
  double doubleValue()   const;
  BigInt BigIntValue()   const;
  BigRat BigRatValue()   const;       // EXACT (the BigFloat is a dyadic rational)
  std::string toString(long prec = get_static_defBigFloatOutputDigits(), bool sci=false) const;
  void approx(const BigInt& I,   const extLong& r, const extLong& a);
  void approx(const BigFloat& B, const extLong& r, const extLong& a);
  void approx(const BigRat& R,   const extLong& r, const extLong& a);
```

**Testing whether a `CORE::Expr` is rational / recovering its exact rational: not possible
safely.**
* `Expr::BigRatValue()` is an approximation (gotcha #4) — you cannot compare it to the Expr and
  conclude anything except that the exact value *is* dyadic when `Expr(e.BigRatValue()) == e`.
* The internal machinery — `CORE::setRationalReduceFlag(true)` (default `false`,
  `CORE/CoreDefs.h:195,300-305`) plus `e.Rep()->ratFlag()` / `*e.Rep()->ratValue()`
  (`CORE/ExprRep.h:369-381`) — **segfaults**: `ratFlag()` dereferences a lazily-allocated
  `NodeInfo*` that is still null (VERIFIED crash, both with the flag on and off, and even after
  calling `e.sign()`). Treat `ExprRep` as private.
* The supported route is `CGAL::CORE_algebraic_number_traits::rational_in_interval(x1, x2)`
  (`CORE_algebraic_number_traits.h:91-121`), which returns *some* rational strictly between two
  algebraic numbers (`CGAL_precondition(x1 != x2)`), by bisecting on `BigIntValue()`. Use that
  to hand Python a rational approximation of any prescribed quality; do not pretend it is exact.

**Design recommendation:** keep `CORE::Expr` out of the binding surface. Represent every
coordinate crossing the C↔Python boundary as an exact `Exact_rational` (from `Epeck`), and only
use `CORE::Expr` internally if you later add the conic traits.

### 4.4 Sign and comparison

```cpp
// number_utils.h
template<class T> typename Real_embeddable_traits<T>::Sgn::result_type      CGAL::sign     (const T& x); // CGAL::Sign
template<class A, class B>
typename Real_embeddable_traits<typename Coercion_traits<A,B>::Type>::Compare::result_type
                                                                            CGAL::compare  (const A&, const B&); // Comparison_result
template<class T> typename Real_embeddable_traits<T>::Abs::result_type      CGAL::abs      (const T&);
template<class T> typename Real_embeddable_traits<T>::Is_finite::result_type CGAL::is_finite(const T&);
template<class T> ... CGAL::is_zero(const T&), CGAL::is_positive(const T&), CGAL::is_negative(const T&);
template<class T> ... CGAL::to_double(const T&), CGAL::to_interval(const T&);
template<class T> ... CGAL::square(const T&), CGAL::sqrt(const T&), CGAL::gcd(a,b), CGAL::div(a,b),
                      CGAL::div_mod(x,y,q,r), CGAL::integral_division(x,y), CGAL::inverse(x),
                      CGAL::is_square(x[,y]), CGAL::unit_part(x), CGAL::simplify(x&), CGAL::kth_root(k,x);
template<class NT> typename Same_uncertainty_nt<Comparison_result,NT>::type
                      CGAL::compare_quotients(const NT& xnum, const NT& xden, const NT& ynum, const NT& yden);
```

`CGAL::Sign` values are `NEGATIVE=-1, ZERO=0, POSITIVE=1`; `Comparison_result` is
`SMALLER=-1, EQUAL=0, LARGER=1` (`CGAL_precondition(SMALLER == (Comparison_result)-1)` is
asserted in `number_utils.h`). VERIFIED: `CGAL::sign(ER("-1/3")) == -1`,
`CGAL::compare(ER("1/3"), ER("2/6")) == 0`, `CGAL::compare(FT(1/3), FT(1/2)) == -1`,
`CGAL::compare(CORE::Expr sqrt2, Expr(1.414213562373095)) == 1`.

Native fast paths:

| Type | native sign | native compare |
|---|---|---|
| `Exact_rational`/`Exact_integer` | `q.sign()` → `int` | `q.compare(r)` → `int` |
| `Epeck::FT` | `CGAL::sign(x)` (filtered) | `CGAL::compare(a,b)` / `a < b` (filtered) |
| `CORE::Expr` | `e.sign()` → `int`; `e.isZero()` | `e.cmp(other)` → `int`; `CORE::cmp(a,b)` |
| `CGAL::Gmpq` | `mpq_sgn` via traits | `operator<`, `operator==` |

`Uncertain<bool>` / `Uncertain<Sign>` show up in filtered code. If one leaks out to you:
`CGAL::is_certain(u)`, `CGAL::get_certain(u)`, `u.make_certain()` (throws
`CGAL::Uncertain_conversion_exception : public std::range_error` when indeterminate),
`CGAL::is_indeterminate(u)` (`Uncertain.h:63-245`). **Catch
`CGAL::Uncertain_conversion_exception` in the binding** — it is a `std::range_error`, not a
`CGAL::Failure_exception`, so a `catch (const CGAL::Failure_exception&)` will miss it.

### 4.5 `Epeck::Point_2` — construction from `FT` and extraction of `FT`

`CGAL::Point_2<R>` (`/opt/homebrew/include/CGAL/Point_2.h`):

```cpp
  typedef typename R_::FT FT;                      // = Lazy_exact_nt<Exact_rational>
  typedef typename R_::RT RT;                      // same here

  Point_2() {}                                     // NOT initialised to origin
  Point_2(const Origin& o);                        // Point_2 p(CGAL::ORIGIN);
  Point_2(const RPoint_2& p);  Point_2(RPoint_2&& p);
  Point_2(const Weighted_point_2& wp);
  template <typename T1, typename T2> Point_2(T1&& x, T2&& y);     // anything convertible to FT
  Point_2(const RT& hx, const RT& hy, const RT& hw);               // homogeneous

  decltype(auto) x() const;                        // -> FT (by value for Epeck)
  decltype(auto) y() const;
  decltype(auto) cartesian(int i) const;           // CGAL_kernel_precondition(i == 0 || i == 1)
  decltype(auto) operator[](int i) const;          // == cartesian(i)
  decltype(auto) hx() const;                       // -> FT ; for Epeck hw() == 1
  decltype(auto) hy() const;
  decltype(auto) hw() const;
  decltype(auto) homogeneous(int i) const;         // CGAL_kernel_precondition(i >= 0 || i <= 2)  [sic — the || is a CGAL bug]
  int dimension() const;                           // 2
  Bbox_2 bbox() const;
  Cartesian_const_iterator cartesian_begin() const, cartesian_end() const;
  const Rep& rep() const noexcept;  Rep& rep() noexcept;
  void swap(Point_2&) noexcept(...);
```

Recipes (VERIFIED):

```cpp
EK::Point_2 p( FT(ER("1/3")), FT(ER("-7/2")) );    // exact
EK::Point_2 q( 0.5, 2.0 );                          // exact w.r.t. the doubles: q.x().exact() == 1/2
ER px = p.x().exact();                              // -> 1/3   (copy out of the DAG!)
ER py = p.y().exact();                              // -> -7/2
double dx = CGAL::to_double(p.x().exact());         // correctly rounded -> 0.333333333333333315
// p.hx().exact() == 1/3, p.hw().exact() == 1  (Cartesian kernel)
```

`Point_2` is a refcounted handle (`sizeof == 8`); copies share the coordinates. Do **not**
hold `const FT&` obtained from `x()` — `x()` returns by value here (`decltype(auto)` on
`Compute_x_2()(*this)`), and even where it returns a reference the referent lives in the
point's rep.

**Kernel choice — `Cartesian` vs `Simple_cartesian`:**
`Cartesian<FT>` uses `Handle_for<T>` for its object reps (reference counted);
`Simple_cartesian<FT>` uses `T` directly (`struct Handle { typedef T type; }`) —
see `Cartesian.h:27-45` vs `Simple_cartesian.h:26-46`. `Epeck` is built on
`Simple_cartesian<Epeck_ft>` at the exact layer plus `Simple_cartesian<Interval_nt_advanced>`
at the approximate layer, wrapped by `Lazy_kernel_base` — the refcounting happens once, in
`Lazy`, not twice. **For a CORE-based kernel (`Cartesian<CORE::Expr>` for conic traits) prefer
`CGAL::Cartesian<CORE::Expr>`**: `CORE::Expr` is itself a refcounted handle, but the
*point/segment reps* are not, and `Cartesian` adds the sharing that makes copying geometry
cheap. For `Epeck` never substitute `Cartesian` — use `Epeck` as shipped.

---

## 5. `CGAL::Gmpq` / `CGAL::Gmpz` (still available, not what Epeck uses)

`/opt/homebrew/include/CGAL/GMP/Gmpq_type.h`, wrapped by `<CGAL/Gmpq.h>`.
`class Gmpq : Handle_for<Gmpq_rep>, boost::ordered_field_operators…` — refcounted, copy-on-write.

```cpp
  Gmpq();
  Gmpq(const mpq_t q);
  Gmpq(int n);  Gmpq(unsigned int n);  Gmpq(long n);  Gmpq(unsigned long n);
  Gmpq(long long n);  Gmpq(unsigned long long n);
  Gmpq(const Gmpz& n);
  Gmpq(int n, int d);  Gmpq(signed long n, unsigned long d);  Gmpq(unsigned long n, unsigned long d);
  Gmpq(const Gmpz& n, const Gmpz& d);              // canonicalised
  Gmpq(double d);                                  // exact w.r.t. the double
  Gmpq(const Gmpfr& f);
  Gmpq(const std::string& str, int base = 10);     // mpq_set_str + canonicalize; "22/7" style only

  Gmpz numerator()   const;                        // Gmpz(mpq_numref(mpq()))
  Gmpz denominator() const;                        // Gmpz(mpq_denref(mpq()))
  Gmpq operator+() const;  Gmpq operator-() const;
  Gmpq& operator+=(const Gmpq&); … -= *= /=;       // and int / long / long long / double / Gmpz / Gmpfr overloads
  bool operator==(const Gmpq &q) const noexcept;   // mpq_equal
  bool operator< (const Gmpq &q) const;            // mpq_cmp
  double to_double() const noexcept;
  Sign   sign()      const;
  const mpq_t & mpq() const noexcept;   mpq_t & mpq() noexcept;   // raw GMP handle
  std::size_t size() const;
```

`Fraction_traits<Gmpq>` (`Gmpq.h:145-172`): `Is_fraction = Tag_true`,
`Numerator_type = Denominator_type = Gmpz`, `Common_factor = Algebraic_structure_traits<Gmpz>::Gcd`,
with the same non-const `Decompose::operator()(const Gmpq&, Gmpz&, Gmpz&)` and
`Compose::operator()(const Gmpz&, const Gmpz&) -> Gmpq`.

`CGAL::Gmpz` (`GMP/Gmpz_type.h`): `Gmpz()`, `Gmpz(const mpz_t)`, `Gmpz(int)`, `Gmpz(long)`,
`Gmpz(unsigned long)`, `Gmpz(double)`, `Gmpz(const std::string& str, int base = 10)`;
`size_t bit_size() const`, `size_t size() const`, `size_t approximate_decimal_length() const`,
`double to_double() const`, `Sign sign() const`, `const mpz_t& mpz() const`,
plus the full arithmetic/bitwise operator set and `operator<<=`/`operator>>=`.

VERIFIED: `Gmpq(22,7)` → `22/7`, `numerator()==22`, `denominator()==7`,
`to_double()==3.14285714285714279`, `Gmpq("355/113")` → `355/113`.

Use these only if you deliberately opt out of boost.mp. Mixing `Gmpq` and `mpq_rational` in the
same program is legal (they interconvert via `Gmpz`/strings) but pointless.

---

## 6. `CGAL::Interval_nt<Protected>`

File `/opt/homebrew/include/CGAL/Interval_nt.h`. `Epeck::FT::AT` is `Interval_nt<false>`;
`Interval_nt_advanced` is `Interval_nt<true>`.

```cpp
template <bool Protected = true>
class Interval_nt {
public:
  typedef double                                  value_type;
  typedef Uncertain_conversion_exception          unsafe_comparison;
  typedef Checked_protect_FPU_rounding<Protected> Internal_protector;
  typedef Protect_FPU_rounding<!Protected>        Protector;

  Interval_nt();                                   // [-1,0] in debug builds → detects use-before-init
  Interval_nt(int); Interval_nt(unsigned); Interval_nt(long); Interval_nt(unsigned long);
  Interval_nt(long long); Interval_nt(unsigned long long);
  Interval_nt(double d);                           // [d,d]
  Interval_nt(double i, double s);                 // checked: i <= s
  Interval_nt(double i, double s, no_check_t);
  Interval_nt(const std::pair<double,double>& p);

  bool   is_point()  const;                        // sup() == inf()
  bool   is_same    (const Interval_nt& d) const;
  bool   do_overlap (const Interval_nt& d) const;
  double inf() const;   double sup() const;
  std::pair<double,double> pair() const;
  static Interval_nt largest();                    // [-inf, +inf]
  static Interval_nt smallest();                   // [-MIN_DOUBLE, MIN_DOUBLE]
};
```

Comparisons return `Uncertain<bool>`. `to_double(Interval_nt)` is the midpoint (VERIFIED:
`to_double(Interval_nt<false>(1.0,2.0)) == 1.5`).

**Rounding mode.** Any code that computes on `Interval_nt<true>` must be inside a
`Protector` (`Protect_FPU_rounding<true>` sets round-to-`+inf` and restores on scope exit).
`Interval_nt<false>` protects each operation itself. Relevant to a Cython binding only if you
call NumPy/BLAS between CGAL calls with a leaked rounding mode — CGAL restores it, but if you
ever construct a `Protect_FPU_rounding` yourself, keep it strictly scoped, and never let a
Python callback run inside one.

---

## 7. `CGAL::Sqrt_extension` (note only)

`/opt/homebrew/include/CGAL/Sqrt_extension.h` + `Sqrt_extension/Sqrt_extension_type.h`.

```cpp
template <class NT_, class ROOT_, class ACDE_TAG = Tag_false, class FP_TAG = Tag_false>
class Sqrt_extension {                             // represents a0 + a1 * sqrt(root)
  Sqrt_extension();
  Sqrt_extension(CGAL_int(NT) i);
  Sqrt_extension(const NT& i);
  Sqrt_extension(const ROOT& root, bool);          // 0 + 1*sqrt(root)
  template<class NTX,class NTY,class ROOTX>
  explicit Sqrt_extension(const NTX& a0, const NTY& a1, const ROOTX& root);
  const NT&   a0() const;
  const NT&   a1() const;
  const ROOT& root() const;
  bool        is_extended() const;
};
```

**It IS used by the circle/segment arrangement traits** — contrary to the premise in the task
brief. `Arr_circle_segment_traits_2<Kernel, Filter>::Point_2` is
`_One_root_point_2<Kernel::FT, Filter>` and its coordinate type is

```cpp
// Arr_geometry_traits/Circle_segment_2.h:46
typedef Sqrt_extension<NT, NT, Tag_true, Boolean_tag<Filter_> > CoordNT;   // NT = Kernel::FT
```

So for the circle-segment traits, a point coordinate is **not** an `Epeck::FT`; it is
`Sqrt_extension<Epeck::FT, Epeck::FT, Tag_true, ...>`, i.e. `a0 + a1*sqrt(root)` with
three `Epeck::FT`s. Any binding that exposes circular arcs must decompose coordinates as
`(a0, a1, root, is_extended)` triples of exact rationals — a plain `(num, den)` pair is not
enough. `Arr_circle_segment_traits_2` also exposes
`typedef double Approximate_number_type;` and
`typedef CGAL::Cartesian<double> Approximate_kernel;` for cheap plotting.

---

## 8. Error handling

### 8.1 Exception hierarchy — `/opt/homebrew/include/CGAL/exceptions.h`

```
std::logic_error
 └─ CGAL::Failure_exception
      ├─ CGAL::Error_exception          "failure"                  (CGAL_error / CGAL_error_msg)
      ├─ CGAL::Precondition_exception   "precondition violation"   (CGAL_precondition*)
      ├─ CGAL::Postcondition_exception  "postcondition violation"  (CGAL_postcondition*)
      ├─ CGAL::Assertion_exception      "assertion violation"      (CGAL_assertion*)
      ├─ CGAL::Test_exception           "test in test-suite violation"
      └─ CGAL::Warning_exception        "warning condition failed" (CGAL_warning*)
```

Separately (NOT a `Failure_exception`):
`CGAL::Uncertain_conversion_exception : public std::range_error` (`Uncertain.h:62-69`).

```cpp
class Failure_exception : public std::logic_error {
public:
    Failure_exception( std::string lib, std::string expr, std::string file,
                       int line, std::string msg, std::string kind = "Unknown kind");
    std::string library()     const;   // "CGAL"
    std::string expression()  const;   // the stringified failing expression, may be empty
    std::string filename()    const;   // absolute path of the CGAL header
    int         line_number() const;
    std::string message()     const;   // optional explanation, may be empty
    // what() from std::logic_error
};
```

> **There is no `explanation()` accessor in 6.1 — it is `message()`.** (Older CGAL docs and
> some tutorials say `explanation()`.)

`what()` is assembled in the constructor and looks exactly like this (VERIFIED):

```
CGAL ERROR: precondition violation!
Expr: b != 0
File: /opt/homebrew/include/CGAL/Lazy_exact_nt.h
Line: 679
```
(with a trailing `\nExplanation: <msg>` when `msg` is non-empty, and no `Expr:` line when
`expr` is empty).

Derived-class constructors:

```cpp
Error_exception        (std::string lib, std::string msg,  std::string file, int line);
Precondition_exception (std::string lib, std::string expr, std::string file, int line, std::string msg);
Postcondition_exception(std::string lib, std::string expr, std::string file, int line, std::string msg);
Assertion_exception    (std::string lib, std::string expr, std::string file, int line, std::string msg);
Test_exception         (std::string lib, std::string expr, std::string file, int line, std::string msg);
Warning_exception      (std::string lib, std::string expr, std::string file, int line, std::string msg);
```

Also in `exceptions.h`: `CGAL::internal::Throw_at_output_exception : public std::exception` and
`CGAL::internal::Throw_at_output` (used with `boost::function_output_iterator` for early exit).

### 8.2 Behaviour and handlers — `assertions_behaviour.h` + `assertions_impl.h`

```cpp
namespace CGAL {

enum Failure_behaviour { ABORT, EXIT, EXIT_WITH_SUCCESS, CONTINUE, THROW_EXCEPTION };
// numeric values (VERIFIED): ABORT=0, EXIT=1, EXIT_WITH_SUCCESS=2, CONTINUE=3, THROW_EXCEPTION=4

typedef void (*Failure_function)(const char* what, const char* expr,
                                 const char* file, int line, const char* msg);

Failure_function  set_error_handler  ( Failure_function handler);   // returns the previous one
Failure_function  set_warning_handler( Failure_function handler);
Failure_behaviour set_error_behaviour  (Failure_behaviour eb);      // returns the previous one
Failure_behaviour set_warning_behaviour(Failure_behaviour eb);

// the failure entry points themselves (assertions.h):
[[noreturn]] void assertion_fail    (const char*, const char*, int, const char* = "");
[[noreturn]] void precondition_fail (const char*, const char*, int, const char* = "");
[[noreturn]] void postcondition_fail(const char*, const char*, int, const char* = "");
             void warning_fail      (const char*, const char*, int, const char* = "");
}
```

**Defaults in 6.1 (header-only build)** — `assertions_impl.h`:

```cpp
inline Failure_behaviour& get_static_error_behaviour()
{ static Failure_behaviour _error_behaviour = THROW_EXCEPTION;  return _error_behaviour; }
inline Failure_behaviour& get_static_warning_behaviour()
{ static Failure_behaviour _warning_behaviour = CONTINUE;      return _warning_behaviour; }
```
VERIFIED at runtime: `set_error_behaviour(THROW_EXCEPTION)` returns `4` (already
`THROW_EXCEPTION`); `set_warning_behaviour(...)` returns `3` (`CONTINUE`).

Dispatch (`assertion_fail`/`precondition_fail`/`postcondition_fail`):
1. call the current **error handler** (default `_standard_error_handler`, which prints the
   `CGAL error: …` block to `std::cerr` — the "skip printing when throwing" shortcut is
   `#if defined(__GNUG__) && !defined(__llvm__)`, so **on clang it always prints**);
2. then `switch` on the behaviour: `ABORT` → `std::abort()`, `EXIT` → `std::exit(1)`,
   `EXIT_WITH_SUCCESS` → `std::exit(0)`, `CONTINUE`/`THROW_EXCEPTION`/default →
   `throw <Kind>_exception("CGAL", expr, file, line, msg)`.
   **Note `CONTINUE` falls through to `throw` for errors** (the comment says
   "The CONTINUE case should not be used anymore"). Only `warning_fail` honours `CONTINUE`
   (it does nothing) and `THROW_EXCEPTION` (throws `Warning_exception`).

Recommended binding init (VERIFIED to silence the cerr spam while keeping the exception):

```cpp
static void cgal_silent_handler(const char*, const char*, const char*, int, const char*) {}

void arrangement2d_init_error_handling() {
    CGAL::set_error_behaviour(CGAL::THROW_EXCEPTION);   // already the default, be explicit
    CGAL::set_warning_behaviour(CGAL::CONTINUE);        // already the default
    CGAL::set_error_handler(cgal_silent_handler);       // stop the std::cerr block
    CGAL::set_warning_handler(cgal_silent_handler);
}
```

Cython translation layer:

```cpp
// in the type-erased C++ core, wrap every entry point:
try { ... }
catch (const CGAL::Failure_exception& e) {
    // e.library(), e.expression(), e.filename(), e.line_number(), e.message(), e.what()
}
catch (const CGAL::Uncertain_conversion_exception& e) { /* std::range_error */ }
catch (const std::runtime_error& e) { /* boost.multiprecision parse / division-by-zero */ }
catch (const std::exception& e) { ... }
```
In Cython, `Failure_exception` (a `std::logic_error`) is already mapped by
`except +` to `ValueError`; if you want a dedicated Python exception carrying
`filename`/`line_number`, catch it in C++ and re-throw a small POD-carrying struct, or use
Cython's `except +translate_cgal_error` custom handler.

### 8.3 `NDEBUG`, `CGAL_NDEBUG` and the check-level macros — `assertions.h:24-45`

```cpp
#ifdef NDEBUG
#  ifndef CGAL_NDEBUG
#    define CGAL_NDEBUG
#  endif
#endif

// CGAL_DEBUG forces CGAL assertions even if NDEBUG is defined
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

Macro families (each is a no-op when its guard macro is defined):

| Family | Disabled by | Enabled by default? |
|---|---|---|
| `CGAL_assertion(_msg/_code)` | `CGAL_NO_ASSERTIONS` | yes |
| `CGAL_precondition(_msg/_code)` | `CGAL_NO_PRECONDITIONS` | yes |
| `CGAL_postcondition(_msg/_code)` | `CGAL_NO_POSTCONDITIONS` | yes |
| `CGAL_warning(_msg/_code)` | `CGAL_NO_WARNINGS` | yes |
| `CGAL_exactness_*` | also needs `CGAL_CHECK_EXACTNESS` **defined** | **no** |
| `CGAL_expensive_*` | also needs `CGAL_CHECK_EXPENSIVE` **defined** | **no** |
| `CGAL_expensive_exactness_*` | needs **both** | **no** |
| `CGAL_error()`, `CGAL_error_msg(MSG)` | never disabled | always |
| `CGAL_destructor_assertion(_catch)` | `CGAL_NO_ASSERTIONS` | yes; suppressed during unwinding |

Also exported as compile-time booleans: `CGAL_ASSERTIONS_ENABLED`, `CGAL_PRECONDITIONS_ENABLED`,
`CGAL_NO_ASSERTIONS_BOOL` (`config.h:513-517`).

**"Keep preconditions but drop expensive checks" is the DEFAULT.** `CGAL_CHECK_EXPENSIVE` and
`CGAL_CHECK_EXACTNESS` are opt-in and not defined here. So:

* Release build for the Python wheel — **do NOT pass `-DNDEBUG`** if you want CGAL's argument
  validation to reach Python as exceptions. Use:
  ```
  -O3 -DNDEBUG -DCGAL_DEBUG          # optimised, C asserts off, CGAL checks ON
  ```
  (`CGAL_DEBUG` undefines `CGAL_NDEBUG`, so all four `CGAL_NO_*` stay undefined.)
  Or, more selectively, `-O3 -DNDEBUG -UCGAL_NDEBUG` will not work (the `#ifdef NDEBUG`
  re-derives it inside the header) — use `CGAL_DEBUG`.
* If you want *no* CGAL checks in the hot loop: plain `-O3 -DNDEBUG`.
* Never define `CGAL_CHECK_EXPENSIVE` in a shipped build; some arrangement checks are O(n²).
* There is also a runtime switch, only if you compile with
  `-DCGAL_ENABLE_DISABLE_ASSERTIONS_AT_RUNTIME` (`assertions.h:45-66`):
  ```cpp
  namespace CGAL { void set_use_assertions(bool b); bool& get_use_assertions(); }   // thread-local
  ```
  Without that macro, `get_use_assertions()` is `constexpr true` and `set_use_assertions` is a
  no-op. This is the cleanest way to expose "strict mode on/off" to Python.
* `CGAL_TEST_SUITE` + `NDEBUG` is a hard `#error` (`config.h:52-54`).

---

## 9. Thread safety — can two arrangements run in two threads?

**Short answer: yes, two independent `Arrangement_2` objects in two threads are fine, provided
you (a) build with `CGAL_HAS_THREADS`, (b) never share an arrangement, a `Point_2`, or an `FT`
across threads without your own lock, and (c) keep `CORE::Expr` single-threaded.**

VERIFIED: `CGAL_HAS_THREADS` is defined in this installation (`config.h:377-381`, triggered by
`BOOST_HAS_THREADS`). A test running 4 threads, each summing 200 `Epeck::FT` divisions, taking
signs, building `Epeck::Point_2` and calling `to_double`, completed cleanly.

Details:

* **`Lazy_exact_nt` / `Lazy` — safe for independent values.**
  `Lazy_rep::exact()` memoises under `std::call_once` (`Lazy.h:351-356, 475-483, 585-589`), so
  even two threads racing on the *same shared node* evaluate it once. The default-constructed
  `Lazy::zero()` node is a `CGAL_STATIC_THREAD_LOCAL_VARIABLE` (`Lazy.h:968-973`), i.e.
  `thread_local` when `CGAL_HAS_THREADS`.
  **However the handle refcount (`CGAL::Handle::PTR`) is a plain non-atomic `int`** — copying
  the *same* `FT`/`Point_2` object from two threads concurrently is a data race. Independent
  copies are fine; sharing one object is not.
* `Lazy_exact_nt::relative_precision_of_to_double` is
  `CGAL_STATIC_THREAD_LOCAL_VARIABLE(double, …, 0.00001)` — **per-thread**. If you expose
  `set_relative_precision_of_to_double` to Python you must set it in every worker thread.
* **CORE global state is process-wide and NOT thread-local.** `CORE/CoreDefs.h` declares them
  with `CGAL_GLOBAL_STATE_VAR(TYPE, NAME, VALUE)` → a function-local `static` (header-only
  build). Some are `std::atomic` (`AbortFlag`, `InvalidFlag`, `EscapePrecWarning`,
  `defBigFloatOutputDigits`, `defOutputDigits`, `defBigFloatInputDigits`, `fpFilterFlag`,
  `incrementalEvalFlag`, `progressiveEvalFlag`, `rationalReduceFlag`,
  `defInitialProgressivePrec`) but **`defRelPrec`, `defAbsPrec`, `defInputDigits`,
  `defBFdivRelPrec`, `defBFsqrtAbsPrec`, `EscapePrec`, `EscapePrecFlag` are plain `extLong`**
  with no synchronisation. The setters
  (`setDefaultPrecision`, `setDefaultRelPrecision`, `setDefaultAbsPrecision`,
  `setDefaultInputDigits`, `setDefaultOutputDigits`, `setDefaultBFInputDigits`,
  `setDefaultBFOutputDigits`, `setFpFilterFlag`, `setIncrementalEvalFlag`,
  `setProgressiveEvalFlag`, `setDefInitialProgressivePrec`, `setRationalReduceFlag`,
  `CORE_init(long)`) all mutate global state. Beyond the globals, `CORE::Expr` nodes carry
  mutable, non-atomic `NodeInfo`/refcounts and are **not** safe to share or even to evaluate
  concurrently.
  → **Rule: if you add CORE-based traits (conics), serialise all CORE work behind one lock, or
  keep it on a single thread. Set CORE precision once at import time, never from a worker.**
* `CGAL::set_error_behaviour` / `set_error_handler` mutate function-local `static`s with no
  synchronisation — call them once, during module init, before starting threads.
* `Interval_nt`'s FPU rounding mode is per-thread hardware state; `Protect_FPU_rounding` saves
  and restores it in the same thread, so it composes correctly, but never suspend a thread
  (e.g. re-enter Python) while a `Protector` is alive.
* `boost::multiprecision::mpq_rational` values are plain values with no shared state — safe.
  GMP itself is thread-safe here (Homebrew builds without the custom-allocator caveat), but
  do not call `mp_set_memory_functions` from Python.
* `Arrangement_2` itself has no internal global state, but every handle/iterator is a raw
  pointer into the DCEL arena: **one arrangement per thread, or an external RW lock.** Any
  modification invalidates other threads' handles.

Practical binding policy: release the GIL around long CGAL computations, but give each Python
object its own arrangement, forbid cross-thread mutation, and call
`arrangement2d_init_error_handling()` plus any CORE `setDefault*` exactly once at import.

---

## 10. Quick reference card

```cpp
// aliases
using EK = CGAL::Epeck;  using FT = EK::FT;  using ER = CGAL::Exact_rational;  using EI = CGAL::Exact_integer;
// ER == CORE::BigRat == boost::multiprecision::mpq_rational
// EI == CORE::BigInt == boost::multiprecision::mpz_int

FT   x = ER(EI("22"), EI("7"));         // exact from strings
FT   y(0.5);                            // exact w.r.t. the double
FT   z(9007199254740993LL);             // exact int64

ER   e = x.exact();                     // COPY it out; forces + memoises evaluation
EI   n = numerator(e), d = denominator(e);
std::string ns = n.str(), ds = d.str();

double dv  = CGAL::to_double(x.exact());        // correctly rounded  (NOT to_double(x))
auto   iv  = (x.exact(), CGAL::to_interval(x.exact()));  // tight, certified
std::string txt;
{ std::ostringstream os; os << x.exact(); txt = os.str(); }   // NOT os << x

ER  parsed; { std::istringstream is("-1.25e3"); is >> CGAL::IO::iformat(parsed); }  // NOT is >> parsed

CGAL::Sign          s  = CGAL::sign(x);
CGAL::Comparison_result c = CGAL::compare(x, y);

EK::Point_2 p(x, y);
ER px = p.x().exact(), py = p.y().exact();

// error handling, once at module init
CGAL::set_error_behaviour(CGAL::THROW_EXCEPTION);
CGAL::set_error_handler([](const char*,const char*,const char*,int,const char*){});
// build flags: -O3 -DNDEBUG -DCGAL_DEBUG   (checks ON)  |  -O3 -DNDEBUG  (checks OFF)
```

---

### Appendix — files read for this document

```
/opt/homebrew/include/CGAL/version.h
/opt/homebrew/include/CGAL/config.h
/opt/homebrew/include/CGAL/Installation/internal/enable_third_party_libraries.h
/opt/homebrew/include/CGAL/Exact_rational.h
/opt/homebrew/include/CGAL/Exact_integer.h
/opt/homebrew/include/CGAL/Number_types/internal/Exact_type_selector.h
/opt/homebrew/include/CGAL/Exact_predicates_exact_constructions_kernel.h
/opt/homebrew/include/CGAL/Lazy_exact_nt.h
/opt/homebrew/include/CGAL/Lazy.h
/opt/homebrew/include/CGAL/Fraction_traits.h
/opt/homebrew/include/CGAL/Rational_traits.h
/opt/homebrew/include/CGAL/boost_mp.h , boost_mp_type.h
/opt/homebrew/include/CGAL/Gmpq.h , GMP/Gmpq_type.h , GMP/Gmpz_type.h
/opt/homebrew/include/CGAL/CORE/BigInt.h , BigRat.h , BigFloat.h , Expr.h , ExprRep.h , CoreDefs.h
/opt/homebrew/include/CGAL/CORE_Expr.h , CORE_BigInt.h , CORE_BigRat.h ,
                          CORE_algebraic_number_traits.h , CORE_arithmetic_kernel.h
/opt/homebrew/include/CGAL/assertions.h , assertions_behaviour.h , assertions_impl.h , exceptions.h
/opt/homebrew/include/CGAL/number_utils.h
/opt/homebrew/include/CGAL/Interval_nt.h , FPU.h , Uncertain.h , tss.h
/opt/homebrew/include/CGAL/Sqrt_extension.h , Sqrt_extension/Sqrt_extension_type.h
/opt/homebrew/include/CGAL/Arr_circle_segment_traits_2.h , Arr_geometry_traits/Circle_segment_2.h
/opt/homebrew/include/CGAL/Cartesian.h , Simple_cartesian.h , Point_2.h
/opt/homebrew/include/CGAL/IO/io.h  (read_float_or_quotient)
```
