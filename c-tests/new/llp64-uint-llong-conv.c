/* C11 6.3.1.8: `long long OP unsigned int` has type long long whenever long
   long can represent every unsigned int value -- which includes LLP64
   targets (win64), where long is 32 bits.  arithmetic_conversion used to ask
   LONG_MAX about every signed type of rank long or above, so on win64 this
   operation was done in unsigned int: (long long) 5u - 7u came out as
   4294967294, and a base-1e9 bignum subtraction borrowed wrongly.  */
int main (void) {
  volatile unsigned int a = 5, b = 7;
  volatile long long borrow = 0;
  long long d = (long long) a - b;
  if (d != -2) return 1;
  if (!((long long) a - b < 0)) return 2;
  long long t = (long long) a - b - borrow;
  if ((t < 0 ? t + 1000000000u : t) != 999999998) return 3;
  if (sizeof ((long long) 1 - 1u) != sizeof (long long)) return 4;
  /* unsigned long vs long long: long long when it can hold every unsigned
     long (LLP64), unsigned long long when it cannot (LP64).  Negative either
     way is the discriminating bit only on LLP64, so check the size.  */
  if (sizeof ((long long) 1 - 1ul) != sizeof (long long)) return 5;
  return 0;
}
