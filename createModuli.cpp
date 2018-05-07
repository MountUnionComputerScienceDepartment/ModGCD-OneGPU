/* createModuli.cpp -- Program to generate moduli.h.

  Prepares a declaration for an array of moduli and ancillary data needed to compute inverses.

  See Cavagnino & Werbrouck
      Efficient Algorithms for Integer Division by Constants Using Multiplication
      The Computer Journal
      Vol. 51 No. 4, 2008.
      
  Based on initial work by
  Authors: Justin Brew, Anthony Rizzo, Kenneth Weber
           Mount Union College
           June 25, 2009
           
  Further revisions by 
  K. Weber  University of Mount Union
            weberk@mountunion.edu
            
  See GmpCudaDevice.cu for revision history.
*/

#include <iostream>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <stdint.h>
#include <gmp.h>
#include "GmpCudaDevice.h"

using namespace GmpCuda;

const size_t MAX_NUM_MODULI = 1 << 27;  //  can go up to 1<<27 with current declarations.
uint32_t moduliList[MAX_NUM_MODULI];
uint64_t mInvList[MAX_NUM_MODULI];

const unsigned int SIEVE_SZ = 1 << 31;  //  Should handle all 32 bit primes.
static char sieve[SIEVE_SZ];

using namespace std;

static void mark_composite(uint32_t limit, char* sieve, size_t sieve_sz)
{
  uint32_t d = static_cast<uint32_t>(floor(sqrt((double)limit)));

  if ((d&1) == 0)
    d -= 1;                /*  Make d odd.  */

  while (d > 2)
    {
      uint32_t g = uint32_t{3}*5*7*11*13*17*19*23*29;
      uint32_t tmp = d;
      while (g != tmp)
          if (g > tmp)
            {
              g -= tmp;
              while ((g&1) == 0)
                  g >>= 1;
            }
          else
            {
              tmp -= g;
              while ((tmp&1) == 0)
                  tmp >>= 1;
            }
      switch (g)
        {
          size_t i, end;
          case  3: case  5: case  7: case 11:
          case 13: case 17: case 19: case 23: case 29:
              if (g != d)
                  break;
          case 1:
              i = limit % d;
              if (i & 1)
                  i += d;
              i >>= 1;
              end = (limit - d)>>1;
              if (sieve_sz < end)
              end = sieve_sz;
              while (i < end)
                  sieve[i] = !0, i += d;
        }
      d -= 2;
    }
}

/*
    Generate a list of the N largest k-bit odd primes.  If there will
    not be enough values to generate the whole list whatever is
    generated will be returned in LIST; it is assumed that LIST has at
    least N slots.  The return value of the function is how many slots were
    left empty in LIST.  The larger primes will be placed at the lower
    indices of LIST.
*/

size_t primes(uint32_t * list, size_t n, int k)
{
  uint32_t limit = (uint64_t{1} << k) - 1;
  uint32_t lowerLimit = 1 << (k - 1);

  size_t sieve_sz, inv_density;
  
  if ((limit&1) == 0)                /*  If limit is even,  */
      limit -= 1;                /*  make limit next smaller odd number.  */

  if (n == 0 || limit < 3)
      return n;

  inv_density = ceil(log((double)limit)/2.0);

  /*  The sieve should be large enough to find n primes.  */

  sieve_sz = (n > static_cast<uint32_t>(-1)/inv_density) ? static_cast<uint32_t>(-1) : n * inv_density;
  
  if (sieve_sz > SIEVE_SZ)
    {
      cerr << "Not enough memory for sieve"  << endl;
      std::exit(1);
    }

  /*  Now strike non-primes from the sieve and harvest primes.  */
  do
    {
      size_t i;
      uint32_t d;

      if (sieve_sz > limit/2)
          sieve_sz = limit/2;

      memset(sieve, 0, sieve_sz);

      mark_composite(limit, sieve, sieve_sz);

      /*  Harvest primes.  */
      for (i = 0; i < sieve_sz; i++)
        {
          if (sieve[i])
            continue;                         /*  Composite.  */
          *list = limit - (i << 1);
          if (*list++ < lowerLimit)
            return n;
          n -= 1;
          if (n == 0)
            return n;
        }

      limit -= 2*sieve_sz;
      if (sieve_sz/n > inv_density)
          sieve_sz = n * inv_density;
    }
  while (limit >= 3);

  return n;
}

int main(int argc, char *argv[])
{
  size_t   mListSize = MAX_NUM_MODULI - primes(moduliList, MAX_NUM_MODULI, L);

  size_t mListSizeOriginal = mListSize;
  mListSize = 0;
  mpz_t FC, J, DJ_FC, Qcr, Ncr;
  mpz_init_set_ui(FC,  2);
  mpz_pow_ui(FC, FC, W + L - 1);  //  FC <-- 2^(W + L - 1)
  mpz_init(J);
  mpz_init(DJ_FC);
  mpz_init(Qcr);
  mpz_init(Ncr);
  for (size_t i = 0; i < mListSizeOriginal; i += 1)
    {
      uint32_t D = moduliList[i];
      mpz_fdiv_q_ui(J, FC, D);
      mpz_add_ui(J, J, 1);          //  J <-- FC / D + 1
      mpz_mul_ui(DJ_FC, J, D);
      mpz_sub(DJ_FC, DJ_FC, FC);    //  DJ_FC <-- D * J - FC
      mpz_cdiv_q(Qcr, J, DJ_FC);    //  Qcr <-- ceil(J / DJ_FC)
      mpz_mul_ui(Ncr, Qcr, D);
      mpz_sub_ui(Ncr, Ncr, 1);      //  Ncr <-- Qcr * D - 1
      if (mpz_sizeinbase(Ncr, 2) > W)
        {
          moduliList[mListSize] = moduliList[i];    //  Compress list.
          mpz_export(mInvList + mListSize++, NULL, -1, sizeof(uint64_t), 0, 0, J);
        }
    }

  cerr   << "There are " << mListSize << " usable " << L << "-bit primes." << endl;
  cerr   << "Percentage of primes usable as moduli = " << 100 * mListSize/mListSizeOriginal << endl;
  
  cout   << "//  AUTOMATICALLY GENERATED by createModuli: do not edit" << endl
         << endl;
  cout   << "//  A list of " << L << "-bit primes, selected so that DBM_a(N, J) will always be accurate." << endl
         << "//  See Cavagnino & Werbrouck," << endl
         << "//      Efficient Algorithms for Integer Division by Constants Using Multiplication," << endl
         << "//      The Computer Journal, Vol. 51 No. 4, 2008." << endl
         << endl
         << "#include <stdint.h>" << endl
         << "#include \"GmpCudaDevice.h\"" << endl
         << "using namespace GmpCuda;" << endl
//         << "const int GmpCuda::NUM_MODULI = " << mListSize << ";" << endl
         << "__device__ const modulus_t GmpCuda::moduliList[] = " << endl
         << "{" << endl;
         
  if (mListSize < NUM_MODULI)
    {
      cerr << "There are only " << mListSize << " usable " << L << "-bit primes; ";
      cerr << NUM_MODULI << " are required." << endl;
      std::exit(1);
    }
    
  for (size_t i = 0; i < NUM_MODULI; i += 1)
    cout << "\t{" << moduliList[i] << ", " << mInvList[i] << "}," << endl;
    
  cout   << "};" << endl;
  
}
