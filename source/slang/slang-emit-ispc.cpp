#include "slang-emit-ispc.h"

#include "slang-ir-util.h"

namespace Slang
{
ISPCSourceEmitter::ISPCSourceEmitter(const Desc& desc)
    : Super(desc)
{
    desc.sourceWriter->supressLineDirective();
}

void ISPCSourceEmitter::_emitISPCVectorDispatchLoop(IRFunc* func, const String& funcName)
{
    SLANG_UNUSED(func);
    m_writer->emit("export void ");
    m_writer->emit(funcName);
    m_writer->emit("(uniform uint32 startGroupID[3], uniform uint32 endGroupID[3], ");
    m_writer->emit("void* uniform entryPointParams, void* uniform globalParams)\n{\n");
    m_writer->indent();

    m_writer->emit(
        "for (uniform uint32 z_dim = startGroupID[2]; z_dim < endGroupID[2]; ++z_dim)\n{\n");
    m_writer->indent();

    m_writer->emit(
        "for (uniform uint32 y_dim = startGroupID[1]; y_dim < endGroupID[1]; ++y_dim)\n{\n");
    m_writer->indent();

    m_writer->emit("foreach (x_dim = startGroupID[0] ... endGroupID[0])\n{\n");
    m_writer->indent();

    m_writer->emit(
        "varying uint32<3> dispatchThreadID;\ndispatchThreadID[0] = x_dim;\ndispatchThreadID[1] = "
        "y_dim;\ndispatchThreadID[2] = z_dim;\n\n");

    m_writer->emit("_");
    m_writer->emit(funcName);
    m_writer->emit("(entryPointParams, (void* uniform)&dispatchThreadID, globalParams);\n");

    m_writer->dedent();
    m_writer->emit("}\n");

    m_writer->dedent();
    m_writer->emit("}\n");

    m_writer->dedent();
    m_writer->emit("}\n");

    m_writer->dedent();
    m_writer->emit("}\n");
}

void ISPCSourceEmitter::emitModuleImpl(IRModule* module, DiagnosticSink* sink)
{
    m_uniformityAnalysis.analyzeModule(module, sink);

    SLANG_UNUSED(sink);

    List<EmitAction> actions;
    computeEmitActions(module, actions);

    // Forward declarations
    _emitForwardDeclarations(actions);

    // Output thread-locals / global variables
    for (auto action : actions)
    {
        if (action.level == EmitAction::Level::Definition &&
            action.inst->getOp() == kIROp_GlobalVar)
        {
            emitGlobalInst(action.inst);
        }
    }

    // Output all functions
    for (auto action : actions)
    {
        if (action.level == EmitAction::Level::Definition && action.inst->getOp() == kIROp_Func)
        {
            emitGlobalInst(action.inst);
        }
    }

    _emitWitnessTableDefinitions();
    return;
    for (auto action : actions)
    {
        if (action.level == EmitAction::Level::Definition && action.inst->getOp() == kIROp_Func)
        {
            IRFunc* func = as<IRFunc>(action.inst);

            if (auto entryPointDecor = func->findDecoration<IREntryPointDecoration>())
            {
                if (entryPointDecor->getProfile().getStage() == Stage::Compute)
                {
                    String funcName = getName(func);
                    _emitISPCVectorDispatchLoop(func, funcName);
                }
            }
        }
    }
}

void ISPCSourceEmitter::emitSimpleFuncParamImpl(IRParam* param)
{
    IRType* paramType = param->getDataType();

    if (auto constantBufferType = as<IRConstantBufferType>(paramType))
    {
        IRType* elementType = constantBufferType->getElementType();
        String typeName = getName(elementType);

        // Verify if this is the EntryPointParams struct parameter
        if (typeName.startsWith("EntryPointParams") || typeName.startsWith("GlobalParams"))
        {
            // Emit type name followed by ISPC reference '&' instead of '*' for ISPC supports
            // reference passing for exports
            m_writer->emit("uniform ");
            emitType(elementType);
            m_writer->emit("& ");
            m_writer->emit(getName(param));
            return;
        }
    }

    Super::emitSimpleFuncParamImpl(param);
}

void ISPCSourceEmitter::emitSimpleFuncImpl(IRFunc* func)
{
    bool isEntryPoint = func->findDecoration<IREntryPointDecoration>() != nullptr;

    if (isEntryPoint)
    {
        m_writer->emit("export ");
    }

    // Emit decorations and visibility static qualifier as standard
    emitFuncDecorations(func);

    auto resultType = func->getResultType();
    auto name = getName(func);

    if (!isPublicOrExportedFunc(func))
    {
        m_writer->emit("static ");
    }

    emitType(resultType, name);

    m_writer->emit("(");

    bool isFirstEmittedParam = true;

    String dispatchThreadIDVarName;
    for (auto pp = func->getFirstParam(); pp; pp = pp->getNextParam())
    {
        if (as<IRTypeType>(pp->getFullType()))
        {
            continue;
        }

        if (isEntryPoint)
        {
            if (auto semanticDecor = pp->findDecoration<IRSemanticDecoration>())
            {
                UnownedStringSlice paramSemName = semanticDecor->getSemanticName();
                if (paramSemName == "SV_DispatchThreadID")
                {
                    // Rename thread index with a local variable to get rid of gathers/scatters
                    m_writer->emit(
                        ", uniform uint32 SLANG_START_GROUP_ID[3], uniform uint32 "
                        "SLANG_END_GROUP_ID[3]");

                    dispatchThreadIDVarName = getName(pp);
                    continue;
                }
            }
        }

        if (!isFirstEmittedParam)
        {
            m_writer->emit(", ");
        }

        emitSimpleFuncParamImpl(pp);
        isFirstEmittedParam = false;
    }
    m_writer->emit(")");

    emitSemantics(func);

    if (isDefinition(func))
    {
        m_writer->emit("\n{\n");
        m_writer->indent();

        if (isEntryPoint)
        {
            m_writer->emit(
                "for (uniform uint32 SLANG_THREAD_INDEX_Z = SLANG_START_GROUP_ID[2]; "
                "SLANG_THREAD_INDEX_Z < "
                "SLANG_END_GROUP_ID[2]; ++SLANG_THREAD_INDEX_Z)\n{\n");
            m_writer->indent();
            m_writer->emit(
                "for (uniform uint32 SLANG_THREAD_INDEX_Y = SLANG_START_GROUP_ID[1]; "
                "SLANG_THREAD_INDEX_Z < "
                "SLANG_END_GROUP_ID[1]; ++SLANG_THREAD_INDEX_Y)\n{\n");
            m_writer->indent();
            m_writer->emit(
                "foreach (SLANG_THREAD_INDEX_X = SLANG_START_GROUP_ID[0] ... "
                "SLANG_END_GROUP_ID[0])\n{\n");
            m_writer->indent();

            m_writer->emit("uint32<3> ");
            m_writer->emit(dispatchThreadIDVarName);
            m_writer->emit(
                " = { SLANG_THREAD_INDEX_X, SLANG_THREAD_INDEX_Y, "
                "SLANG_THREAD_INDEX_Z };\n");
        }

        emitFunctionBody(func);

        if (isEntryPoint)
        {
            m_writer->dedent();
            m_writer->emit("}\n");
            m_writer->dedent();
            m_writer->emit("}\n");
            m_writer->dedent();
            m_writer->emit("}\n");
        }

        m_writer->dedent();
        m_writer->emit("}\n\n");
    }
    else
    {
        m_writer->emit(";\n\n");
    }
}

UnownedStringSlice ISPCSourceEmitter::getBuiltinTypeName(IROp op)
{
    switch (op)
    {
    case kIROp_VoidType:
        return UnownedStringSlice("void");
    case kIROp_BoolType:
        return UnownedStringSlice("bool");

    case kIROp_Int8Type:
        return UnownedStringSlice("int8");
    case kIROp_Int16Type:
        return UnownedStringSlice("int16");
    case kIROp_IntType:
        return UnownedStringSlice("int32");
    case kIROp_Int64Type:
        return UnownedStringSlice("int64");
    case kIROp_IntPtrType:
        return UnownedStringSlice("intptr_t");
    case kIROp_UInt8Type:
        return UnownedStringSlice("uint8");
    case kIROp_UInt16Type:
        return UnownedStringSlice("uint16");
    case kIROp_UIntType:
        return UnownedStringSlice("uint32");
    case kIROp_UInt64Type:
        return UnownedStringSlice("uint64");
    case kIROp_UIntPtrType:
        return UnownedStringSlice("uintptr_t");
    case kIROp_HalfType:
        return UnownedStringSlice("float16"); // ISPC uses float16 for half
    case kIROp_FloatType:
        return UnownedStringSlice("float");
    case kIROp_DoubleType:
        return UnownedStringSlice("double");
    case kIROp_CharType:
        return UnownedStringSlice("uint8"); // ISPC has no 'char', maps to int8/uint8

    default:
        return UnownedStringSlice();
    }
}

SlangResult ISPCSourceEmitter::calcTypeName(IRType* type, CodeGenTarget target, StringBuilder& out)
{
    switch (type->getOp())
    {
    case kIROp_VectorType:
        {
            auto vecType = static_cast<IRVectorType*>(type);
            auto vecCount = int(getIntVal(vecType->getElementCount()));
            auto elemType = vecType->getElementType();

            out << _getTypeName(elemType) << "<" << vecCount << ">";
            return SLANG_OK;
        }
    case kIROp_NativeStringType:
        {
            out << "uint8*";
            return SLANG_OK;
        }
    case kIROp_ClassType:
    case kIROp_ComPtrType:
        {
            // Unwrap the underlying payload type and emit as a raw pointer
            auto baseType = cast<IRType>(type->getOperand(0));

            out << _getTypeName(baseType) << "* ";
            return SLANG_OK;
        }
    default:
        {
            if (IRBasicType::isaImpl(type->getOp()))
            {
                out << getBuiltinTypeName(type->getOp());
                return SLANG_OK;
            }

            if (auto structuredBuf = as<IRHLSLStructuredBufferTypeBase>(type))
            {
                // StructuredBuffer<T> or RWStructuredBuffer<T> lowers to a direct element pointer
                // T*
                auto elementType = structuredBuf->getElementType();

                if (as<IRHLSLStructuredBufferType>(type))
                {
                    out << "const ";
                }

                StringBuilder typeBuilder;
                emitType(elementType, typeBuilder);
                out << typeBuilder.produceString() << "*";

                return SLANG_OK;
            }

            if (as<IRByteAddressBufferTypeBase>(type))
            {
                // ByteAddressBuffer lowers to a raw uint32 pointer
                out << "uint32*";
                return SLANG_OK;
            }

            return Super::calcTypeName(type, target, out);
        }
    }

    SLANG_DIAGNOSE_UNEXPECTED(getSink(), SourceLoc(), "unhandled type for ISPC emit");
    return SLANG_FAIL;
}

void ISPCSourceEmitter::_emitType(IRType* type, DeclaratorInfo* declarator)
{
    switch (type->getOp())
    {

    case kIROp_MatrixType:
        {
            auto matType = cast<IRMatrixType>(type);
            auto elementType = matType->getElementType();
            const auto rowCount = int(getIntVal(matType->getRowCount()));
            const auto colCount = int(getIntVal(matType->getColumnCount()));

            _emitType(elementType, nullptr);
            m_writer->emit("<");
            m_writer->emit(colCount);
            m_writer->emit("> ");

            emitDeclarator(declarator);

            m_writer->emit("[");
            m_writer->emit(rowCount);
            m_writer->emit("]");
            break;
        }

    case kIROp_PtrType:
    case kIROp_BorrowInOutParamType:
    case kIROp_OutParamType:
        {
            auto ptrType = cast<IRPtrTypeBase>(type);
            IRType* valueType = ptrType->getValueType();

            if (valueType->getOp() == kIROp_VoidType)
            {
                m_writer->emit("void* ");
                emitDeclarator(declarator);
                break;
            }

            PtrDeclaratorInfo ptrDeclarator(declarator);
            _emitType(valueType, &ptrDeclarator);
            break;
        }
    case kIROp_ArrayType:
        {
            auto arrayType = static_cast<IRArrayType*>(type);
            auto elementType = arrayType->getElementType();
            int elementCount = int(getIntVal(arrayType->getElementCount()));

            // Emit standard C array declaration syntax for ISPC
            _emitType(elementType, nullptr);
            emitDeclarator(declarator);
            m_writer->emit("[");
            m_writer->emit(elementCount);
            m_writer->emit("]");
            break;
        }
    default:
        Super::_emitType(type, declarator);
        break;
    }
}

bool ISPCSourceEmitter::tryEmitInstExprImpl(IRInst* inst, const EmitOpInfo& inOuterPrec)
{
    switch (inst->getOp())
    {
    default:
        {
            // m_writer->emit("// UNHANDLED_OP:");
            // m_writer->emit(getIROpInfo(inst->getOp()).name);
            // m_writer->emit(" \n");
            // SLANG_DIAGNOSE_UNEXPECTED(
            //     getSink(),
            //     SourceLoc(),
            //     "unexpected inst expr passed to ISPCSourceEmitter");
            return Super::tryEmitInstExprImpl(inst, inOuterPrec);
        }
    case kIROp_FieldAddress:
    case kIROp_FieldExtract:
        {
            auto base = inst->getOperand(0);
            emitOperand(base, getInfo(EmitOp::Postfix));

            IRType* baseType = base->getDataType();
            if (as<IRPtrTypeBase>(baseType) || baseType->getOp() == kIROp_RawPointerType)
            {
                m_writer->emit("->");
            }
            else
            {
                m_writer->emit(".");
            }

            auto fieldKey = inst->getOperand(1);
            m_writer->emit(getName(fieldKey));
            return true;
        }

    case kIROp_InOutImplicitCast:
    case kIROp_OutImplicitCast:
        {
            // We'll just the LValue to be the desired type
            m_writer->emit("(");
            emitType(inst->getDataType());
            m_writer->emit(")(");

            emitOperand(inst->getOperand(0), getInfo(EmitOp::General));

            m_writer->emit(")");
            return true;
        }
    case kIROp_MakeVector:
        {
            // IRType* retType = inst->getFullType();
            // emitType(retType);
            m_writer->emit("{"); // Use `{` instead of super's `(` for vector
                                 // initializers in ISPC

            bool isFirst = true;
            for (UInt i = 0; i < inst->getOperandCount(); i++)
            {
                auto arg = inst->getOperand(i);
                if (auto vectorType = as<IRVectorType>(arg->getDataType()))
                {
                    // Component extraction for nested vectors
                    int count = (int)cast<IRIntLit>(vectorType->getElementCount())->getValue();
                    for (int j = 0; j < count; j++)
                    {
                        if (!isFirst)
                            m_writer->emit(", ");
                        isFirst = false;

                        auto outerPrec = getInfo(EmitOp::General);
                        auto prec = getInfo(EmitOp::Postfix);
                        emitOperand(arg, leftSide(outerPrec, prec));

                        // Element indexing in ISPC vector types
                        m_writer->emit("[");
                        m_writer->emit(String(j));
                        m_writer->emit("]");
                    }
                }
                else
                {
                    if (!isFirst)
                        m_writer->emit(", ");
                    isFirst = false;

                    emitOperand(arg, getInfo(EmitOp::General));
                }
            }
            m_writer->emit("}");

            return true;
        }
    case kIROp_CastFloatToInt:
    case kIROp_CastIntToFloat:
    case kIROp_FloatCast:
    case kIROp_IntCast:
        {
            m_writer->emit("(");
            emitType(inst->getDataType());
            m_writer->emit(")(");
            emitOperand(inst->getOperand(0), getInfo(EmitOp::General));
            m_writer->emit(")");
            return true;
        }
    case kIROp_VectorReshape:
        {
            if (auto vectorType = as<IRVectorType>(inst->getDataType()))
            {
                m_writer->emit("(");
                emitType(vectorType);
                m_writer->emit(")(");
                emitOperand(inst->getOperand(0), getInfo(EmitOp::General));
                m_writer->emit(")");
                return true;
            }
            return false;
        }
    case kIROp_GetElement:
        {
            auto getElementInst = static_cast<IRGetElement*>(inst);

            IRInst* baseInst = getElementInst->getBase();
            IRType* baseType = baseInst->getDataType();

            if (as<IRVectorType>(baseType))
            {
                auto outerPrec = getInfo(EmitOp::General);
                auto prec = getInfo(EmitOp::Postfix);

                emitOperand(baseInst, leftSide(outerPrec, prec));
                m_writer->emit("[");
                emitOperand(getElementInst->getIndex(), getInfo(EmitOp::General));
                m_writer->emit("]");
                return true;
            }
            else if (as<IRMatrixType>(baseType))
            {
                auto outerPrec = getInfo(EmitOp::General);
                auto prec = getInfo(EmitOp::Postfix);

                emitOperand(baseInst, leftSide(outerPrec, prec));
                m_writer->emit(".rows[");
                emitOperand(getElementInst->getIndex(), getInfo(EmitOp::General));
                m_writer->emit("]");
                return true;
            }
            return false;
        }
    case kIROp_GetElementPtr:
        {
            auto getElementInst = static_cast<IRGetElement*>(inst);

            IRInst* baseInst = getElementInst->getBase();
            IRType* baseType = (IRType*)unwrapAttributedType(
                as<IRPtrTypeBase>(baseInst->getDataType())->getValueType());

            if (as<IRVectorType>(baseType))
            {
                m_writer->emit("&((*");
                emitOperand(baseInst, getInfo(EmitOp::General));
                m_writer->emit(")[");
                emitOperand(getElementInst->getIndex(), getInfo(EmitOp::General));
                m_writer->emit("])");
                return true;
            }
            else if (as<IRMatrixType>(baseType))
            {
                m_writer->emit("(");
                auto outerPrec = getInfo(EmitOp::General);
                auto prec = getInfo(EmitOp::Postfix);
                emitOperand(baseInst, leftSide(outerPrec, prec));
                m_writer->emit("->rows + (");
                emitOperand(getElementInst->getIndex(), getInfo(EmitOp::General));
                m_writer->emit("))");
                return true;
            }
            return false;
        }
    case kIROp_RWStructuredBufferGetElementPtr:
        {
            m_writer->emit("&(");

            auto base = inst->getOperand(0);
            auto index = inst->getOperand(1);
            auto outerPrec = getInfo(EmitOp::General);

            emitOperand(base, outerPrec);
            m_writer->emit("[");
            emitOperand(index, getInfo(EmitOp::General));
            m_writer->emit("])");

            return true;
        }
    case kIROp_Swizzle:
        {
            auto swizzleInst = static_cast<IRSwizzle*>(inst);
            IRInst* baseInst = swizzleInst->getBase();
            IRType* baseType = baseInst->getDataType();

            if (as<IRBasicType>(baseType))
            {
                IRType* dstType = swizzleInst->getDataType();
                if (as<IRBasicType>(dstType))
                {
                    emitOperand(baseInst, inOuterPrec);
                    return true;
                }
            }

            const Index elementCount = Index(swizzleInst->getElementCount());

            if (elementCount == 1)
            {
                auto outerPrec = getInfo(EmitOp::General);
                auto prec = getInfo(EmitOp::Postfix);
                emitOperand(baseInst, leftSide(outerPrec, prec));

                IRInst* irElementIndex = swizzleInst->getElementIndex(0);
                SLANG_RELEASE_ASSERT(irElementIndex->getOp() == kIROp_IntLit);
                UInt elementIndex = (UInt)((IRConstant*)irElementIndex)->value.intVal;

                m_writer->emit("[");
                m_writer->emit(String(elementIndex));
                m_writer->emit("]");
                return true;
            }

            // Multi-element swizzle (e.g., v.zyx) -> emit VectorType{ base[2], base[1], base[0]
            // }
            IRType* retType = swizzleInst->getFullType();
            emitType(retType);
            m_writer->emit("{");

            for (Index i = 0; i < elementCount; ++i)
            {
                if (i > 0)
                    m_writer->emit(", ");

                auto outerPrec = getInfo(EmitOp::General);
                auto prec = getInfo(EmitOp::Postfix);
                emitOperand(baseInst, leftSide(outerPrec, prec));

                IRInst* irElementIndex = swizzleInst->getElementIndex(i);
                SLANG_RELEASE_ASSERT(irElementIndex->getOp() == kIROp_IntLit);
                UInt elementIndex = (UInt)((IRConstant*)irElementIndex)->value.intVal;

                m_writer->emit("[");
                m_writer->emit(String(elementIndex));
                m_writer->emit("]");
            }

            m_writer->emit("}");
            return true;
        }
    case kIROp_FRem:
        {
            m_writer->emit("fmod(");
            emitOperand(inst->getOperand(0), getInfo(EmitOp::General));
            m_writer->emit(", ");
            emitOperand(inst->getOperand(1), getInfo(EmitOp::General));
            m_writer->emit(")");
            return true;
        }
    case kIROp_Call:
        {
            auto funcValue = inst->getOperand(0);

            // Does this function declare any requirements.
            handleRequiredCapabilities(funcValue);

            auto sliceContains = [](UnownedStringSlice slice, const char* str) -> bool
            { return slice.indexOf(UnownedStringSlice(str)) != -1; };

            // Check if the callee is an intrinsic function or target function
            if (auto func = as<IRFunc>(funcValue))
            {
                auto funcName = getName(func);
                UnownedStringSlice nameSlice = funcName.getUnownedSlice();

                if (sliceContains(nameSlice, "asfloat"))
                {
                    m_writer->emit("floatbits(");
                    emitOperand(inst->getOperand(1), getInfo(EmitOp::General));
                    m_writer->emit(")");
                    return true;
                }

                if (sliceContains(nameSlice, "asuint") || sliceContains(nameSlice, "asint"))
                {
                    m_writer->emit("intbits(");
                    emitOperand(inst->getOperand(1), getInfo(EmitOp::General));
                    m_writer->emit(")");
                    return true;
                }
            }

            // Fallback to standard call emission if it's a regular user function
            return false;
        }
    case kIROp_LookupWitnessMethod: // WARN: Unsure if the compiler will lower this into C-capable
                                    // code (along with the ones below)
        {
            emitInstExpr(inst->getOperand(0), inOuterPrec);
            m_writer->emit("->");
            m_writer->emit(getName(inst->getOperand(1)));
            return true;
        }
    case kIROp_GetSequentialID:
        {
            emitInstExpr(inst->getOperand(0), inOuterPrec);
            m_writer->emit("->sequentialID");
            return true;
        }
    case kIROp_WitnessTable:
        {
            m_writer->emit("(&");
            m_writer->emit(getName(inst));
            m_writer->emit(")");
            return true;
        }
    case kIROp_GetAddress:
        {
            // Once we clean up the pointer emitting logic, we can
            // just use GetElementAddress instruction in place of
            // getAddr instruction, and this case can be removed.
            m_writer->emit("(&(");
            emitInstExpr(inst->getOperand(0), EmitOpInfo::get(EmitOp::General));
            m_writer->emit("))");
            return true;
        }
    case kIROp_RTTIObject:
        {
            m_writer->emit(getName(inst));
            return true;
        }
    case kIROp_Alloca:
        {
            m_writer->emit("alloca(");
            emitOperand(inst->getOperand(0), EmitOpInfo::get(EmitOp::Postfix));
            m_writer->emit("->typeSize)");
            return true;
        }
    case kIROp_BitCast:
        {
            IRType* dstType = inst->getDataType();
            IRType* srcType = inst->getOperand(0)->getDataType();

            // Pointer to Pointer cast -> C-style pointer cast
            if (as<IRPtrTypeBase>(dstType) || as<IRPtrTypeBase>(srcType))
            {
                m_writer->emit("((");
                emitType(dstType);
                m_writer->emit(")(");
                emitOperand(inst->getOperand(0), getInfo(EmitOp::General));
                m_writer->emit("))");
                return true;
            }

            // Value bitcasts (float <-> uint/int)
            auto dstBasic = as<IRBasicType>(dstType);
            auto srcBasic = as<IRBasicType>(srcType);

            if (dstBasic && srcBasic)
            {
                // Scalar cast (int32)(val)
                m_writer->emit("((");
                emitType(dstType);
                m_writer->emit(")(");
                emitOperand(inst->getOperand(0), getInfo(EmitOp::General));
                m_writer->emit("))");
                return true;
            }

            // Vector bitcast / conversion handling
            if (auto dstVec = as<IRVectorType>(dstType))
            {
                int elementCount = 0;
                if (auto intLit = as<IRIntLit>(dstVec->getElementCount()))
                {
                    elementCount = (int)intLit->getValue();
                }

                m_writer->emit("{");
                for (int i = 0; i < elementCount; ++i)
                {
                    if (i > 0)
                        m_writer->emit(", ");

                    m_writer->emit("(");
                    emitType(dstVec->getElementType());
                    m_writer->emit(")(");

                    // Emit source operand with postfix precedence for [] indexing
                    auto outerPrec = getInfo(EmitOp::General);
                    auto prec = getInfo(EmitOp::Postfix);
                    emitOperand(inst->getOperand(0), leftSide(outerPrec, prec));

                    m_writer->emit("[");
                    m_writer->emit(String(i));
                    m_writer->emit("])");
                }
                m_writer->emit("}");
                return true;
            }
            return false;
        }
    case kIROp_StringLit:
        {
            auto handler = StringEscapeUtil::getHandler(StringEscapeUtil::Style::Cpp);

            StringBuilder buf;
            const auto slice = as<IRStringLit>(inst)->getStringSlice();
            StringEscapeUtil::appendQuoted(handler, slice, buf);

            if (m_hasString)
            {
                m_writer->emit("Slang::toTerminatedSlice(");
                m_writer->emit(buf);
                m_writer->emit(")");
            }
            else
            {
                m_writer->emit(buf);
            }

            return true;
        }
    case kIROp_PtrLit:
        {
            auto ptrVal = as<IRPtrLit>(inst)->value.ptrVal;
            if (ptrVal == nullptr)
            {
                m_writer->emit("NULL");
            }
            else
            {
                m_writer->emit("(");
                emitType(inst->getFullType());
                m_writer->emit(")(");
                m_writer->emitUInt64((uint64_t)ptrVal);
                m_writer->emit(")");
            }
            return true;
        }
    case kIROp_MakeExistential:
    case kIROp_MakeExistentialWithRTTI: // INFO: probably never activated for ISPC
        {
            m_writer->emit("((void*)(");
            emitOperand(inst->getOperand(0), getInfo(EmitOp::General));
            m_writer->emit("))");
            return true;
        }
    case kIROp_GetValueFromBoundInterface:
        {
            m_writer->emit("((");
            emitType(inst->getFullType());
            m_writer->emit("*)");

            emitOperand(inst->getOperand(0), getInfo(EmitOp::General));
            m_writer->emit(")");
            return true;
        }
    case kIROp_Select:
        {
            m_writer->emit("select(");
            emitOperand(inst->getOperand(0), getInfo(EmitOp::General));
            m_writer->emit(", ");
            emitOperand(inst->getOperand(1), getInfo(EmitOp::General));
            m_writer->emit(", ");
            emitOperand(inst->getOperand(2), getInfo(EmitOp::General));
            m_writer->emit(")");
            return true;
        }
    }
}

bool ISPCSourceEmitter::tryEmitInstStmtImpl(IRInst* inst)
{
    switch (inst->getOp())
    {
    case kIROp_StructuredBufferGetDimensions:
        {
            // ISPC doesn't have .GetDimensions() object methods.
            // Structured buffers in ISPC are usually passed with an explicit count/stride
            // inside a kernel context struct, or accessed via buffer layout metadata.

            emitInstResultDecl(inst);
            m_writer->emit("(uint32<2>){ ");
            emitOperand(inst->getOperand(0), getInfo(EmitOp::General));
            m_writer->emit(".count, sizeof(");

            auto bufferType = as<IRHLSLStructuredBufferType>(inst->getOperand(0)->getDataType());
            if (bufferType)
                emitType(bufferType->getElementType());
            else
                m_writer->emit("uint32");

            m_writer->emit(") };\n");
            return true;
        }
    case kIROp_AtomicAdd:
        {
            // ISPC provides native atomic intrinsics: atomic_add(pointer, value)
            // It automatically returns the old value prior to the addition.
            emitInstResultDecl(inst);
            m_writer->emit("atomic_add(");

            emitOperand(inst->getOperand(0), getInfo(EmitOp::General));
            m_writer->emit(", ");

            emitOperand(inst->getOperand(1), getInfo(EmitOp::General));
            m_writer->emit(");\n");

            return true;
        }
    default:
        return false;
    }
}

void ISPCSourceEmitter::emitSimpleValueImpl(IRInst* inst)
{
    switch (inst->getOp())
    {
    case kIROp_IntLit:
        {
            auto litInst = static_cast<IRConstant*>(inst);
            m_writer->emit(String(litInst->value.intVal));
            return;
        }
    case kIROp_FloatLit:
        {
            auto litInst = static_cast<IRConstant*>(inst);
            switch (litInst->getFloatKind())
            {
            case IRConstant::FloatKind::Nan:
                m_writer->emit("(0.0f / 0.0f)");
                return;
            case IRConstant::FloatKind::PositiveInfinity:
                m_writer->emit("(1.0f / 0.0f)");
                return;
            case IRConstant::FloatKind::NegativeInfinity:
                m_writer->emit("(-1.0f / 0.0f)");
                return;
            default:
                // Standard floating point output (e.g. 3.5f)
                m_writer->emit(String(litInst->value.floatVal));
                m_writer->emit("f");
                return;
            }
        }

    default:
        Super::emitSimpleValueImpl(inst);
        break;
    }
}

void ISPCSourceEmitter::emitSimpleTypeImpl(IRType* inType)
{
    // No point in over complicating this; the ISPC compiler is smart
    // enough to optimize it into uniforms and consts where possible
    // if (m_uniformityAnalysis.isUniform(inType))
    // {
    //     m_writer->emit("uniform ");
    // }
    // else
    // {
    //     m_writer->emit("varying ");
    // }

    Super::emitSimpleTypeImpl(inType);
}

bool ISPCSourceEmitter::shouldFoldInstIntoUseSites(IRInst* inst)
{
    switch (inst->getOp())
    {
    case kIROp_FieldAddress:
    case kIROp_GetElementPtr:
        // Return false here so it emits named variables instead of giant inline casts
        // ISPC doesn't enjoy pointers in that style very much
        return false;

    default:
        break;
    }

    return CLikeSourceEmitter::shouldFoldInstIntoUseSites(inst);
};
} // namespace Slang
