#include "slang-emit-ispc.h"

namespace Slang
{
ISPCSourceEmitter::ISPCSourceEmitter(const Desc& desc)
    : Super(desc)
{
}

// void ISPCSourceEmitter::emitModuleImpl(IRModule* module, DiagnosticSink* sink)
// {
//     m_uniformityAnalysis.analyzeModule(module, sink);

//     Super::emitModuleImpl(module, sink);
// }

// void ISPCSourceEmitter::emitVarDecorationsImpl(IRInst* var)
// {
//     if (m_uniformityAnalysis.isUniform(var))
//     {
//         m_writer->emit("uniform ");
//     }
//     else
//     {
//         m_writer->emit("varying ");
//     }

//     Super::emitVarDecorationsImpl(var);
// }
} // namespace Slang
