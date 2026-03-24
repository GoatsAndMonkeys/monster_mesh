// Compile Peanut-GB implementation here — exactly once.
//
// All other .cpp files include peanut_gb.h with PEANUT_GB_HEADER_ONLY defined
// (set globally in platformio.ini), so they only see declarations.
// This file deliberately undefines the flag before including, so the full
// implementation is compiled into this single translation unit.
#undef PEANUT_GB_HEADER_ONLY
#include "peanut_gb.h"
