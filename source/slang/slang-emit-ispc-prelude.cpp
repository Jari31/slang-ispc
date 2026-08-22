// slang-emit-ispc-prelude.cpp
#include "core/slang-string.h"
#include "slang-emit-ispc.h"

Slang::String get_slang_ispc_prelude()
{
    return R"(
#ifndef SLANG_ISPC_PRELUDE_H
#define SLANG_ISPC_PRELUDE_H

// Basic type aliases to match Slang expectations
// typedef int8 int8_t;
// typedef uint8 uint8_t;
// typedef int16 int16_t;
// typedef uint16 uint16_t;
// typedef int32 int32_t;
// typedef uint32 uint32_t;

#endif // SLANG_ISPC_PRELUDE_H
)";
}

namespace Slang
{

} // namespace Slang
