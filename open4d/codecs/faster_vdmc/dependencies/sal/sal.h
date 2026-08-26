#pragma once
#include "no_sal2.h"

#ifdef __in_ecount
#undef __in_ecount
#endif
#define __in_ecount(x)
#ifdef __out_ecount
#undef __out_ecount
#endif
#define __out_ecount(x)
#ifdef _Analysis_assume_
#undef _Analysis_assume_
#endif
#define _Analysis_assume_ (void)
