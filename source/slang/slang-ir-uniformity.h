// slang-ir-uniformity.h
#pragma once

namespace Slang
{
struct IRModule;
class DiagnosticSink;

void validateUniformity(IRModule* module, DiagnosticSink* sink);

struct UniformityAnalysis
{
    IRModule* module = nullptr;
    HashSet<IRInst*> nonUniformInsts;

    void analyzeModule(IRModule* inModule, DiagnosticSink* sink = nullptr)
    {
        module = inModule;
        validateUniformity(inModule, sink);
    };

    bool isUniform(IRInst* inst) const { return !nonUniformInsts.contains(inst); };
    bool isVarying(IRInst* inst) const { return nonUniformInsts.contains(inst); };
};
} // namespace Slang
