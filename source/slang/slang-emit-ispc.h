// slang-emit-ispc.h
#pragma once

#include "slang-emit-cpp.h"
#include "slang-ir-uniformity.h"

namespace Slang
{
class ISPCSourceEmitter : public CPPSourceEmitter
{
private:
    UniformityAnalysis m_uniformityAnalysis;

public:
    typedef CPPSourceEmitter Super;

    ISPCSourceEmitter(const Desc& desc);

    virtual void emitVarDecorationsImpl(IRInst* var) SLANG_OVERRIDE;
    virtual void emitModuleImpl(IRModule* module, DiagnosticSink* sink) SLANG_OVERRIDE;
};
} // namespace Slang
