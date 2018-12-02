/*
* Division Algorithm
* (C) 1999-2007,2012,2018 Jack Lloyd
*
* Botan is released under the Simplified BSD License (see license.txt)
*/

#include <botan/divide.h>
#include <botan/internal/mp_core.h>
#include <botan/internal/mp_madd.h>
#include <botan/internal/ct_utils.h>

namespace Botan {

namespace {

/*
* Handle signed operands, if necessary
*/
void sign_fixup(const BigInt& x, const BigInt& y, BigInt& q, BigInt& r)
   {
   if(x.sign() == BigInt::Negative)
      {
      q.flip_sign();
      if(r.is_nonzero()) { --q; r = y.abs() - r; }
      }
   if(y.sign() == BigInt::Negative)
      q.flip_sign();
   }

bool division_check(word q, word y2, word y1,
                    word x3, word x2, word x1)
   {
   // Compute (y3,y2,y1) = (y2,y1) * q

   word y3 = 0;
   y1 = word_madd2(q, y1, &y3);
   y2 = word_madd2(q, y2, &y3);

   // Return (y3,y2,y1) >? (x3,x2,x1)

   if(y3 > x3) return true;
   if(y3 < x3) return false;

   if(y2 > x2) return true;
   if(y2 < x2) return false;

   if(y1 > x1) return true;
   if(y1 < x1) return false;

   return false;
   }

}

void ct_divide(const BigInt& x, const BigInt& y, BigInt& q_out, BigInt& r_out)
   {
   const size_t x_words = x.sig_words();
   const size_t y_words = y.sig_words();

   const size_t x_bits = x.bits();

   BigInt q(BigInt::Positive, x_words);
   BigInt r(BigInt::Positive, y_words);

   for(size_t i = 0; i != x_bits; ++i)
      {
      const size_t b = x_bits - 1 - i;
      const bool x_b = x.get_bit(b);

      r *= 2;
      r.conditionally_set_bit(0, x_b);

      // y <= r -> r >= y
      const auto r_gte_y = bigint_ct_is_lt(y.data(), y_words, r.data(), r.size(), true);

      q.conditionally_set_bit(b, r_gte_y.is_set());
      bigint_cnd_sub(r_gte_y.value(), r.mutable_data(), r.size(), y.data(), y_words);
      }

   sign_fixup(x, y, q, r);
   r_out = r;
   q_out = q;
   }

void ct_divide_u8(const BigInt& x, uint8_t y, BigInt& q_out, uint8_t& r_out)
   {
   const size_t x_words = x.sig_words();
   const size_t x_bits = x.bits();
   const size_t y_words = 1;

   BigInt q(BigInt::Positive, x_words);
   uint32_t r = 0;

   for(size_t i = 0; i != x_bits; ++i)
      {
      const size_t b = x_bits - 1 - i;
      const bool x_b = x.get_bit(b);

      r *= 2;
      r += x_b;

      const auto r_gte_y = CT::Mask<uint32_t>::is_gte(r, y);

      q.conditionally_set_bit(b, r_gte_y.is_set());
      r = r_gte_y.select(r - y, r);
      }

   if(x.sign() == BigInt::Negative)
      {
      q.flip_sign();
      if(r != 0)
         {
         --q;
         r = y - r;
         }
      }

   r_out = static_cast<uint8_t>(r);
   q_out = q;
   }

/*
* Solve x = q * y + r
*/
void divide(const BigInt& x, const BigInt& y_arg, BigInt& q, BigInt& r)
   {
   if(y_arg.is_zero())
      throw BigInt::DivideByZero();

   BigInt y = y_arg;
   const size_t y_words = y.sig_words();

   r = x;
   q = 0;

   r.set_sign(BigInt::Positive);
   y.set_sign(BigInt::Positive);

   int32_t compare = r.cmp(y);

   if(compare == 0)
      {
      q = 1;
      r = 0;
      }
   else if(compare > 0)
      {
      size_t shifts = 0;
      word y_top = y.word_at(y.sig_words()-1);
      while(y_top < MP_WORD_TOP_BIT) { y_top <<= 1; ++shifts; }
      y <<= shifts;
      r <<= shifts;

      const size_t n = r.sig_words() - 1;
      const size_t t = y_words - 1;

      if(n < t)
         throw Internal_Error("BigInt division word sizes");

      q.grow_to(n - t + 1);

      word* q_words = q.mutable_data();

      if(n <= t)
         {
         while(r > y) { r -= y; ++q; }
         r >>= shifts;
         sign_fixup(x, y_arg, q, r);
         return;
         }

      BigInt temp = y << (BOTAN_MP_WORD_BITS * (n-t));

      while(r >= temp) { r -= temp; q_words[n-t] += 1; }

      for(size_t j = n; j != t; --j)
         {
         const word x_j0  = r.word_at(j);
         const word x_j1 = r.word_at(j-1);
         const word y_t  = y.word_at(t);

         if(x_j0 == y_t)
            q_words[j-t-1] = MP_WORD_MAX;
         else
            q_words[j-t-1] = bigint_divop(x_j0, x_j1, y_t);

         while(division_check(q_words[j-t-1],
                              y_t, y.word_at(t-1),
                              x_j0, x_j1, r.word_at(j-2)))
            {
            q_words[j-t-1] -= 1;
            }

         r -= (q_words[j-t-1] * y) << (BOTAN_MP_WORD_BITS * (j-t-1));

         if(r.is_negative())
            {
            r += y << (BOTAN_MP_WORD_BITS * (j-t-1));
            q_words[j-t-1] -= 1;
            }
         }
      r >>= shifts;
      }

   sign_fixup(x, y_arg, q, r);
   }

}
