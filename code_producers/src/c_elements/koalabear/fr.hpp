#ifndef __FR_H
#define __FR_H

#include <gmp.h>
#include <stdlib.h>
#include <sstream>
#include <string.h>
#include <assert.h>
#include <stdint.h>

#define Fr_N64 1
#define Fr_N8 4
#define Fr_prime 2130706433u
#define Fr_prime_str "2130706433"
#define Fr_half 1065353216u

typedef uint32_t FrElement;

inline FrElement Fr_one() {
  return 1u;
}

inline FrElement Fr_reduce64(uint64_t x) {
  const uint64_t q = (((__uint128_t)x * 8657571868ull) >> 64);
  uint64_t r = x - q * (uint64_t)Fr_prime;
  if (r >= (uint64_t)Fr_prime) r -= (uint64_t)Fr_prime;
  return (FrElement)r;
}

#define Fr_copy(r, a) r = a

inline void Fr_copyn(FrElement r[], const FrElement a[], int n){
  for (int i = 0; i < n; i++) {
    r[i] = a[i];
  }
}

inline int Fr_toInt(const FrElement & a) {
  if (a > Fr_half) return -((int)(Fr_prime - a));
  return (int)a;
}

inline FrElement Fr_str2element(const char *s, uint base) {
  mpz_t q;
  mpz_init_set_ui(q, Fr_prime);
  mpz_t mr;
  mpz_init_set_str(mr, s, base);
  mpz_fdiv_r(mr, mr, q);
  FrElement v = (FrElement)mpz_get_ui(mr);
  mpz_clear(mr);
  mpz_clear(q);
  return v;
}

inline char *Fr_element2str(const FrElement & a) {
  std::stringstream ss;
  ss << a;
  std::string str = ss.str();
  char * cstr = new char [str.length()+1];
  strcpy (cstr, str.c_str());
  return cstr;
}

inline FrElement Fr_add (const FrElement & a, const FrElement & b) {
  uint32_t r = a + b;
  return r >= Fr_prime ? r - Fr_prime : r;
}

inline FrElement Fr_sub (const FrElement & a, const FrElement & b) {
  return (b <= a)? a - b : Fr_prime - (b - a);
}

inline FrElement Fr_mul(const FrElement & a, const FrElement & b) {
  return Fr_reduce64((uint64_t)a * (uint64_t)b);
}

inline FrElement Fr_inv(const FrElement & a) {
  mpz_t ma;
  mpz_init_set_ui(ma, a);
  mpz_t mr;
  mpz_init(mr);
  mpz_t mpz_prime;
  mpz_init_set_ui(mpz_prime, Fr_prime);
  mpz_invert(mr, ma, mpz_prime);
  FrElement ra = (FrElement)mpz_get_ui(mr);
  mpz_clear(ma);
  mpz_clear(mr);
  mpz_clear(mpz_prime);
  return ra;
}

inline FrElement Fr_div(const FrElement & a, const FrElement & b) {
  FrElement ib = Fr_inv(b);
  return Fr_mul(a, ib);
}

inline FrElement Fr_idiv(const FrElement & a, const FrElement & b) {
  return a / b;
}

inline FrElement Fr_mod(const FrElement & a, const FrElement & b) {
  return a % b;
}

inline FrElement Fr_pow(const FrElement & a, const FrElement & b) {
  FrElement p = 1u;
  FrElement ao = a;
  FrElement bo = b;
  while (bo > 0) {
    if ((bo & 1u) == 0)  {
      ao = Fr_mul(ao, ao);
      bo = bo >> 1;
    } else {
      p = Fr_mul(p, ao);
      bo = bo - 1;
    }
  }
  return p;
}

FrElement Fr_shr(const FrElement & a, const FrElement & b);

inline FrElement Fr_shl(const FrElement & a, const FrElement & b) {
  if (b > Fr_half) return Fr_shr(a, Fr_prime - b);
  if (b >= 32) return 0u;
  return Fr_reduce64((uint64_t)a << b);
}

inline FrElement Fr_shr(const FrElement & a, const FrElement & b) {
  if (b > Fr_half) return Fr_shl(a, Fr_prime - b);
  if (b >= 32) return 0u;
  return a >> b;
}

inline FrElement Fr_leq(const FrElement & a, const FrElement & b) {
  if (a <= Fr_half) {
    if (b <= Fr_half) return a <= b;
    else return 0u;
  } else {
    if (b <= Fr_half) return 1u;
    else return a <= b;
  }
}

inline FrElement Fr_geq(const FrElement & a, const FrElement & b) {
  if (a <= Fr_half) {
    if (b <= Fr_half) return a >= b;
    else return 1u;
  } else {
    if (b <= Fr_half) return 0u;
    else return a >= b;
  }
}

inline FrElement Fr_lt(const FrElement & a, const FrElement & b) {
  if (a <= Fr_half) {
    if (b <= Fr_half) return a < b;
    else return 0u;
  } else {
    if (b <= Fr_half) return 1u;
    else return a < b;
  }
}

inline FrElement Fr_gt(const FrElement & a, const FrElement & b) {
  if (a <= Fr_half) {
    if (b <= Fr_half) return a > b;
    else return 1u;
  } else {
    if (b <= Fr_half) return 0u;
    else return a > b;
  }
}

inline FrElement Fr_eq(const FrElement & a, const FrElement & b) {
  return a == b;
}

inline FrElement Fr_eq(const FrElement a[], const FrElement b[], int n) {
  for (int i = 0; i < n; i++) {
    if (a[i] != b[i]) return 0u;
  }
  return 1u;
}

inline FrElement Fr_neq(const FrElement & a, const FrElement & b) {
  return a != b;
}

inline FrElement Fr_lor(const FrElement & a, const FrElement & b) {
  return (a == 0u) && (b == 0u) ? 0u : 1u;
}

inline FrElement Fr_land(const FrElement & a, const FrElement & b) {
  return (a == 0u) || (b == 0u) ? 0u : 1u;
}

inline FrElement Fr_bor(const FrElement & a, const FrElement & b) {
  return Fr_reduce64((uint64_t)(a | b));
}

inline FrElement Fr_band(const FrElement & a, const FrElement & b) {
  return a & b;
}

inline FrElement Fr_bxor(const FrElement & a, const FrElement & b) {
  return Fr_reduce64((uint64_t)(a ^ b));
}

inline FrElement Fr_neg(const FrElement & a) {
  if (a == 0u) return a;
  return Fr_prime - a;
}

inline FrElement Fr_lnot(const FrElement & a) {
  return a == 0u ? 1u : 0u;
}

inline int Fr_isTrue(const FrElement & a) {
  return a == 0u ? 0 : 1;
}

inline FrElement Fr_bnot(const FrElement & a) {
  return Fr_reduce64((uint64_t)(~a));
}

#endif // __FR_H
