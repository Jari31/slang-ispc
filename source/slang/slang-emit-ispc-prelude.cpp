// slang-emit-ispc-prelude.cpp
#include "core/slang-string.h"
#include "slang-emit-ispc.h"

Slang::String get_slang_ispc_prelude()
{
    return R"(
// Base ISPC Prelude
typedef int8   int8_t;
typedef int16  int16_t;
typedef int32  int32_t;
typedef int64  int64_t;
typedef uint8  uint8_t;
typedef uint16 uint16_t;
typedef uint32 uint32_t;
typedef uint64 uint64_t;
)";
}

namespace Slang
{

} // namespace Slang
