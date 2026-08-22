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

    void _emitISPCVectorDispatchLoop(IRFunc* func, const String& funcName);

    virtual void emitSimpleFuncParamImpl(IRParam* param) SLANG_OVERRIDE;
    virtual void emitModuleImpl(IRModule* module, DiagnosticSink* sink) SLANG_OVERRIDE;

    UnownedStringSlice getBuiltinTypeName(IROp op);

    virtual SlangResult calcTypeName(IRType* type, CodeGenTarget target, StringBuilder& out)
        SLANG_OVERRIDE;
    virtual void _emitType(IRType* type, DeclaratorInfo* declarator) SLANG_OVERRIDE;

    virtual bool tryEmitInstExprImpl(IRInst* inst, const EmitOpInfo& inOuterPrec) SLANG_OVERRIDE;

    virtual bool tryEmitInstStmtImpl(IRInst* inst) SLANG_OVERRIDE;

    virtual void emitSimpleValueImpl(IRInst* inst) SLANG_OVERRIDE;

    virtual void emitSimpleTypeImpl(IRType* inType) SLANG_OVERRIDE;
};
} // namespace Slang
