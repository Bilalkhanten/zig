/*
 * Copyright (c) 2016 Andrew Kelley
 *
 * This file is part of zig, which is MIT licensed.
 * See http://opensource.org/licenses/MIT
 */

#include "ir.hpp"
#include "ir_print.hpp"

struct IrPrint {
    FILE *f;
    int indent;
    int indent_size;
};

static void ir_print_other_instruction(IrPrint *irp, IrInstruction *instruction);

static void ir_print_indent(IrPrint *irp) {
    for (int i = 0; i < irp->indent; i += 1) {
        fprintf(irp->f, " ");
    }
}

static void ir_print_prefix(IrPrint *irp, IrInstruction *instruction) {
    ir_print_indent(irp);
    const char *type_name = instruction->type_entry ? buf_ptr(&instruction->type_entry->name) : "(unknown)";
    const char *ref_count = ir_has_side_effects(instruction) ?
        "-" : buf_ptr(buf_sprintf("%zu", instruction->ref_count));
    fprintf(irp->f, "#%-3zu| %-12s| %-2s| ", instruction->debug_id, type_name, ref_count);
}

static void ir_print_const_value(IrPrint *irp, TypeTableEntry *type_entry, ConstExprValue *const_val) {
    switch (const_val->special) {
        case ConstValSpecialRuntime:
            zig_unreachable();
        case ConstValSpecialUndef:
            fprintf(irp->f, "undefined");
            return;
        case ConstValSpecialZeroes:
            fprintf(irp->f, "zeroes");
            return;
        case ConstValSpecialStatic:
            break;
    }
    switch (type_entry->id) {
        case TypeTableEntryIdTypeDecl:
            return ir_print_const_value(irp, type_entry->data.type_decl.canonical_type, const_val);
        case TypeTableEntryIdInvalid:
            fprintf(irp->f, "(invalid)");
            return;
        case TypeTableEntryIdVar:
            fprintf(irp->f, "(var)");
            return;
        case TypeTableEntryIdVoid:
            fprintf(irp->f, "{}");
            return;
        case TypeTableEntryIdNumLitFloat:
            fprintf(irp->f, "%f", const_val->data.x_bignum.data.x_float);
            return;
        case TypeTableEntryIdNumLitInt:
            {
                BigNum *bignum = &const_val->data.x_bignum;
                const char *negative_str = bignum->is_negative ? "-" : "";
                fprintf(irp->f, "%s%llu", negative_str, bignum->data.x_uint);
                return;
            }
        case TypeTableEntryIdMetaType:
            fprintf(irp->f, "%s", buf_ptr(&const_val->data.x_type->name));
            return;
        case TypeTableEntryIdInt:
            {
                BigNum *bignum = &const_val->data.x_bignum;
                assert(bignum->kind == BigNumKindInt);
                const char *negative_str = bignum->is_negative ? "-" : "";
                fprintf(irp->f, "%s%llu", negative_str, bignum->data.x_uint);
            }
            return;
        case TypeTableEntryIdFloat:
            {
                BigNum *bignum = &const_val->data.x_bignum;
                assert(bignum->kind == BigNumKindFloat);
                fprintf(irp->f, "%f", bignum->data.x_float);
            }
            return;
        case TypeTableEntryIdUnreachable:
            fprintf(irp->f, "@unreachable()");
            return;
        case TypeTableEntryIdBool:
            {
                const char *value = const_val->data.x_bool ? "true" : "false";
                fprintf(irp->f, "%s", value);
                return;
            }
        case TypeTableEntryIdPointer:
            fprintf(irp->f, "&");
            ir_print_const_value(irp, type_entry->data.pointer.child_type, const_ptr_pointee(const_val));
            return;
        case TypeTableEntryIdFn:
            {
                FnTableEntry *fn_entry = const_val->data.x_fn;
                fprintf(irp->f, "%s", buf_ptr(&fn_entry->symbol_name));
                return;
            }
        case TypeTableEntryIdBlock:
            {
                AstNode *node = const_val->data.x_block->node;
                fprintf(irp->f, "(scope:%zu:%zu)", node->line + 1, node->column + 1);
                return;
            }
        case TypeTableEntryIdArray:
            {
                uint64_t len = type_entry->data.array.len;
                fprintf(irp->f, "%s{", buf_ptr(&type_entry->name));
                for (uint64_t i = 0; i < len; i += 1) {
                    if (i != 0)
                        fprintf(irp->f, ",");
                    ConstExprValue *child_value = &const_val->data.x_array.elements[i];
                    TypeTableEntry *child_type = type_entry->data.array.child_type;
                    ir_print_const_value(irp, child_type, child_value);
                }
                fprintf(irp->f, "}");
                return;
            }
        case TypeTableEntryIdNullLit:
            {
                fprintf(irp->f, "null");
                return;
            }
        case TypeTableEntryIdUndefLit:
            {
                fprintf(irp->f, "undefined");
                return;
            }
        case TypeTableEntryIdMaybe:
            {
                if (const_val->data.x_maybe) {
                    ir_print_const_value(irp, type_entry->data.maybe.child_type, const_val->data.x_maybe);
                } else {
                    fprintf(irp->f, "null");
                }
                return;
            }
        case TypeTableEntryIdNamespace:
            {
                ImportTableEntry *import = const_val->data.x_import;
                fprintf(irp->f, "(namespace: %s)", buf_ptr(import->path));
                return;
            }
        case TypeTableEntryIdBoundFn:
            {
                FnTableEntry *fn_entry = const_val->data.x_bound_fn.fn;
                fprintf(irp->f, "bound %s to ", buf_ptr(&fn_entry->symbol_name));
                ir_print_other_instruction(irp, const_val->data.x_bound_fn.first_arg);
                return;
            }
        case TypeTableEntryIdStruct:
            {
                fprintf(irp->f, "(struct %s constant)", buf_ptr(&type_entry->name));
                return;
            }
        case TypeTableEntryIdEnum:
            {
                fprintf(irp->f, "(enum %s constant)", buf_ptr(&type_entry->name));
                return;
            }
        case TypeTableEntryIdErrorUnion:
            {
                fprintf(irp->f, "(error union %s constant)", buf_ptr(&type_entry->name));
                return;
            }
        case TypeTableEntryIdUnion:
            {
                fprintf(irp->f, "(union %s constant)", buf_ptr(&type_entry->name));
                return;
            }
        case TypeTableEntryIdPureError:
            {
                fprintf(irp->f, "(pure error constant)");
                return;
            }
    }
    zig_unreachable();
}

static void ir_print_const_instruction(IrPrint *irp, IrInstruction *instruction) {
    TypeTableEntry *type_entry = instruction->type_entry;
    ConstExprValue *const_val = &instruction->static_value;
    ir_print_const_value(irp, type_entry, const_val);
}

static void ir_print_var_instruction(IrPrint *irp, IrInstruction *instruction) {
    fprintf(irp->f, "#%zu", instruction->debug_id);
}

static void ir_print_other_instruction(IrPrint *irp, IrInstruction *instruction) {
    if (instruction->static_value.special != ConstValSpecialRuntime) {
        ir_print_const_instruction(irp, instruction);
    } else {
        ir_print_var_instruction(irp, instruction);
    }
}

static void ir_print_other_block(IrPrint *irp, IrBasicBlock *bb) {
    fprintf(irp->f, "$%s_%zu", bb->name_hint, bb->debug_id);
}

static void ir_print_return(IrPrint *irp, IrInstructionReturn *return_instruction) {
    assert(return_instruction->value);
    fprintf(irp->f, "return ");
    ir_print_other_instruction(irp, return_instruction->value);
}

static void ir_print_const(IrPrint *irp, IrInstructionConst *const_instruction) {
    ir_print_const_instruction(irp, &const_instruction->base);
}

static const char *ir_bin_op_id_str(IrBinOp op_id) {
    switch (op_id) {
        case IrBinOpInvalid:
            zig_unreachable();
        case IrBinOpBoolOr:
            return "BoolOr";
        case IrBinOpBoolAnd:
            return "BoolAnd";
        case IrBinOpCmpEq:
            return "==";
        case IrBinOpCmpNotEq:
            return "!=";
        case IrBinOpCmpLessThan:
            return "<";
        case IrBinOpCmpGreaterThan:
            return ">";
        case IrBinOpCmpLessOrEq:
            return "<=";
        case IrBinOpCmpGreaterOrEq:
            return ">=";
        case IrBinOpBinOr:
            return "|";
        case IrBinOpBinXor:
            return "^";
        case IrBinOpBinAnd:
            return "&";
        case IrBinOpBitShiftLeft:
            return "<<";
        case IrBinOpBitShiftLeftWrap:
            return "<<%";
        case IrBinOpBitShiftRight:
            return ">>";
        case IrBinOpAdd:
            return "+";
        case IrBinOpAddWrap:
            return "+%";
        case IrBinOpSub:
            return "-";
        case IrBinOpSubWrap:
            return "-%";
        case IrBinOpMult:
            return "*";
        case IrBinOpMultWrap:
            return "*%";
        case IrBinOpDiv:
            return "/";
        case IrBinOpMod:
            return "%";
        case IrBinOpArrayCat:
            return "++";
        case IrBinOpArrayMult:
            return "**";
    }
    zig_unreachable();
}

static const char *ir_un_op_id_str(IrUnOp op_id) {
    switch (op_id) {
        case IrUnOpInvalid:
            zig_unreachable();
        case IrUnOpBoolNot:
            return "!";
        case IrUnOpBinNot:
            return "~";
        case IrUnOpNegation:
            return "-";
        case IrUnOpNegationWrap:
            return "-%";
        case IrUnOpAddressOf:
            return "&";
        case IrUnOpConstAddressOf:
            return "&const";
        case IrUnOpDereference:
            return "*";
        case IrUnOpMaybe:
            return "?";
        case IrUnOpError:
            return "%";
        case IrUnOpUnwrapError:
            return "%%";
        case IrUnOpUnwrapMaybe:
            return "??";
        case IrUnOpMaybeReturn:
            return "?return";
        case IrUnOpErrorReturn:
            return "%return";
    }
    zig_unreachable();
}

static void ir_print_un_op(IrPrint *irp, IrInstructionUnOp *un_op_instruction) {
    fprintf(irp->f, "%s ", ir_un_op_id_str(un_op_instruction->op_id));
    ir_print_other_instruction(irp, un_op_instruction->value);
}

static void ir_print_bin_op(IrPrint *irp, IrInstructionBinOp *bin_op_instruction) {
    ir_print_other_instruction(irp, bin_op_instruction->op1);
    fprintf(irp->f, " %s ", ir_bin_op_id_str(bin_op_instruction->op_id));
    ir_print_other_instruction(irp, bin_op_instruction->op2);
}

static void ir_print_decl_var(IrPrint *irp, IrInstructionDeclVar *decl_var_instruction) {
    const char *inline_kw = decl_var_instruction->var->is_inline ? "inline " : "";
    const char *var_or_const = decl_var_instruction->var->gen_is_const ? "const" : "var";
    const char *name = buf_ptr(&decl_var_instruction->var->name);
    if (decl_var_instruction->var_type) {
        fprintf(irp->f, "%s%s %s: ", inline_kw, var_or_const, name);
        ir_print_other_instruction(irp, decl_var_instruction->var_type);
        fprintf(irp->f, " = ");
    } else {
        fprintf(irp->f, "%s%s %s = ", inline_kw, var_or_const, name);
    }
    ir_print_other_instruction(irp, decl_var_instruction->init_value);
}

static void ir_print_cast(IrPrint *irp, IrInstructionCast *cast_instruction) {
    fprintf(irp->f, "cast ");
    ir_print_other_instruction(irp, cast_instruction->value);
    fprintf(irp->f, " to %s", buf_ptr(&cast_instruction->dest_type->name));
}

static void ir_print_call(IrPrint *irp, IrInstructionCall *call_instruction) {
    if (call_instruction->fn_entry) {
        fprintf(irp->f, "%s", buf_ptr(&call_instruction->fn_entry->symbol_name));
    } else {
        assert(call_instruction->fn_ref);
        ir_print_other_instruction(irp, call_instruction->fn_ref);
    }
    fprintf(irp->f, "(");
    for (size_t i = 0; i < call_instruction->arg_count; i += 1) {
        IrInstruction *arg = call_instruction->args[i];
        if (i != 0)
            fprintf(irp->f, ", ");
        ir_print_other_instruction(irp, arg);
    }
    fprintf(irp->f, ")");
}

static void ir_print_cond_br(IrPrint *irp, IrInstructionCondBr *cond_br_instruction) {
    const char *inline_kw = cond_br_instruction->is_inline ? "inline " : "";
    fprintf(irp->f, "%sif (", inline_kw);
    ir_print_other_instruction(irp, cond_br_instruction->condition);
    fprintf(irp->f, ") ");
    ir_print_other_block(irp, cond_br_instruction->then_block);
    fprintf(irp->f, " else ");
    ir_print_other_block(irp, cond_br_instruction->else_block);
}

static void ir_print_br(IrPrint *irp, IrInstructionBr *br_instruction) {
    const char *inline_kw = br_instruction->is_inline ? "inline " : "";
    fprintf(irp->f, "%sgoto ", inline_kw);
    ir_print_other_block(irp, br_instruction->dest_block);
}

static void ir_print_phi(IrPrint *irp, IrInstructionPhi *phi_instruction) {
    assert(phi_instruction->incoming_count != 0);
    assert(phi_instruction->incoming_count != SIZE_MAX);
    for (size_t i = 0; i < phi_instruction->incoming_count; i += 1) {
        IrBasicBlock *incoming_block = phi_instruction->incoming_blocks[i];
        IrInstruction *incoming_value = phi_instruction->incoming_values[i];
        if (i != 0)
            fprintf(irp->f, " ");
        ir_print_other_block(irp, incoming_block);
        fprintf(irp->f, ":");
        ir_print_other_instruction(irp, incoming_value);
    }
}

static void ir_print_container_init_list(IrPrint *irp, IrInstructionContainerInitList *instruction) {
    ir_print_other_instruction(irp, instruction->container_type);
    fprintf(irp->f, "{");
    for (size_t i = 0; i < instruction->item_count; i += 1) {
        IrInstruction *item = instruction->items[i];
        if (i != 0)
            fprintf(irp->f, ", ");
        ir_print_other_instruction(irp, item);
    }
    fprintf(irp->f, "}");
}

static void ir_print_container_init_fields(IrPrint *irp, IrInstructionContainerInitFields *instruction) {
    ir_print_other_instruction(irp, instruction->container_type);
    fprintf(irp->f, "{");
    for (size_t i = 0; i < instruction->field_count; i += 1) {
        IrInstructionContainerInitFieldsField *field = &instruction->fields[i];
        const char *comma = (i == 0) ? "" : ", ";
        fprintf(irp->f, "%s.%s = ", comma, buf_ptr(field->name));
        ir_print_other_instruction(irp, field->value);
    }
    fprintf(irp->f, "} // container init");
}

static void ir_print_struct_init(IrPrint *irp, IrInstructionStructInit *instruction) {
    fprintf(irp->f, "%s {", buf_ptr(&instruction->struct_type->name));
    for (size_t i = 0; i < instruction->field_count; i += 1) {
        IrInstructionStructInitField *field = &instruction->fields[i];
        Buf *field_name = field->type_struct_field->name;
        const char *comma = (i == 0) ? "" : ", ";
        fprintf(irp->f, "%s.%s = ", comma, buf_ptr(field_name));
        ir_print_other_instruction(irp, field->value);
    }
    fprintf(irp->f, "} // struct init");
}

static void ir_print_unreachable(IrPrint *irp, IrInstructionUnreachable *instruction) {
    fprintf(irp->f, "unreachable");
}

static void ir_print_elem_ptr(IrPrint *irp, IrInstructionElemPtr *instruction) {
    fprintf(irp->f, "&");
    ir_print_other_instruction(irp, instruction->array_ptr);
    fprintf(irp->f, "[");
    ir_print_other_instruction(irp, instruction->elem_index);
    fprintf(irp->f, "]");
    if (!instruction->safety_check_on) {
        fprintf(irp->f, " // no safety");
    }
}

static void ir_print_var_ptr(IrPrint *irp, IrInstructionVarPtr *instruction) {
    fprintf(irp->f, "&%s", buf_ptr(&instruction->var->name));
}

static void ir_print_load_ptr(IrPrint *irp, IrInstructionLoadPtr *instruction) {
    fprintf(irp->f, "*");
    ir_print_other_instruction(irp, instruction->ptr);
}

static void ir_print_store_ptr(IrPrint *irp, IrInstructionStorePtr *instruction) {
    fprintf(irp->f, "*");
    ir_print_var_instruction(irp, instruction->ptr);
    fprintf(irp->f, " = ");
    ir_print_other_instruction(irp, instruction->value);
}

static void ir_print_typeof(IrPrint *irp, IrInstructionTypeOf *instruction) {
    fprintf(irp->f, "@typeOf(");
    ir_print_other_instruction(irp, instruction->value);
    fprintf(irp->f, ")");
}

static void ir_print_to_ptr_type(IrPrint *irp, IrInstructionToPtrType *instruction) {
    fprintf(irp->f, "@toPtrType(");
    ir_print_other_instruction(irp, instruction->value);
    fprintf(irp->f, ")");
}

static void ir_print_ptr_type_child(IrPrint *irp, IrInstructionPtrTypeChild *instruction) {
    fprintf(irp->f, "@ptrTypeChild(");
    ir_print_other_instruction(irp, instruction->value);
    fprintf(irp->f, ")");
}

static void ir_print_field_ptr(IrPrint *irp, IrInstructionFieldPtr *instruction) {
    fprintf(irp->f, "fieldptr ");
    ir_print_other_instruction(irp, instruction->container_ptr);
    fprintf(irp->f, ".%s", buf_ptr(instruction->field_name));
}

static void ir_print_struct_field_ptr(IrPrint *irp, IrInstructionStructFieldPtr *instruction) {
    fprintf(irp->f, "@StructFieldPtr(&");
    ir_print_other_instruction(irp, instruction->struct_ptr);
    fprintf(irp->f, ".%s", buf_ptr(instruction->field->name));
    fprintf(irp->f, ")");
}

static void ir_print_enum_field_ptr(IrPrint *irp, IrInstructionEnumFieldPtr *instruction) {
    fprintf(irp->f, "@EnumFieldPtr(&");
    ir_print_other_instruction(irp, instruction->enum_ptr);
    fprintf(irp->f, ".%s", buf_ptr(instruction->field->name));
    fprintf(irp->f, ")");
}

static void ir_print_set_fn_test(IrPrint *irp, IrInstructionSetFnTest *instruction) {
    fprintf(irp->f, "@setFnTest(");
    ir_print_other_instruction(irp, instruction->fn_value);
    fprintf(irp->f, ", ");
    ir_print_other_instruction(irp, instruction->is_test);
    fprintf(irp->f, ")");
}

static void ir_print_set_fn_visible(IrPrint *irp, IrInstructionSetFnVisible *instruction) {
    fprintf(irp->f, "@setFnVisible(");
    ir_print_other_instruction(irp, instruction->fn_value);
    fprintf(irp->f, ", ");
    ir_print_other_instruction(irp, instruction->is_visible);
    fprintf(irp->f, ")");
}

static void ir_print_set_debug_safety(IrPrint *irp, IrInstructionSetDebugSafety *instruction) {
    fprintf(irp->f, "@setDebugSafety(");
    ir_print_other_instruction(irp, instruction->scope_value);
    fprintf(irp->f, ", ");
    ir_print_other_instruction(irp, instruction->debug_safety_on);
    fprintf(irp->f, ")");
}

static void ir_print_array_type(IrPrint *irp, IrInstructionArrayType *instruction) {
    fprintf(irp->f, "[");
    ir_print_other_instruction(irp, instruction->size);
    fprintf(irp->f, "]");
    ir_print_other_instruction(irp, instruction->child_type);
}

static void ir_print_slice_type(IrPrint *irp, IrInstructionSliceType *instruction) {
    const char *const_kw = instruction->is_const ? "const " : "";
    fprintf(irp->f, "[]%s", const_kw);
    ir_print_other_instruction(irp, instruction->child_type);
}

static void ir_print_asm(IrPrint *irp, IrInstructionAsm *instruction) {
    assert(instruction->base.source_node->type == NodeTypeAsmExpr);
    AstNodeAsmExpr *asm_expr = &instruction->base.source_node->data.asm_expr;
    const char *volatile_kw = instruction->has_side_effects ? " volatile" : "";
    fprintf(irp->f, "asm%s (\"%s\") : ", volatile_kw, buf_ptr(asm_expr->asm_template));

    for (size_t i = 0; i < asm_expr->output_list.length; i += 1) {
        AsmOutput *asm_output = asm_expr->output_list.at(i);
        if (i != 0) fprintf(irp->f, ", ");

        fprintf(irp->f, "[%s] \"%s\" (",
                buf_ptr(asm_output->asm_symbolic_name),
                buf_ptr(asm_output->constraint));
        if (asm_output->return_type) {
            fprintf(irp->f, "-> ");
            ir_print_other_instruction(irp, instruction->output_types[i]);
        } else {
            fprintf(irp->f, "%s", buf_ptr(asm_output->variable_name));
        }
        fprintf(irp->f, ")");
    }

    fprintf(irp->f, " : ");
    for (size_t i = 0; i < asm_expr->input_list.length; i += 1) {
        AsmInput *asm_input = asm_expr->input_list.at(i);

        if (i != 0) fprintf(irp->f, ", ");
        fprintf(irp->f, "[%s] \"%s\" (",
                buf_ptr(asm_input->asm_symbolic_name),
                buf_ptr(asm_input->constraint));
        ir_print_other_instruction(irp, instruction->input_list[i]);
        fprintf(irp->f, ")");
    }
    fprintf(irp->f, " : ");
    for (size_t i = 0; i < asm_expr->clobber_list.length; i += 1) {
        Buf *reg_name = asm_expr->clobber_list.at(i);
        if (i != 0) fprintf(irp->f, ", ");
        fprintf(irp->f, "\"%s\"", buf_ptr(reg_name));
    }
    fprintf(irp->f, ")");
}

static void ir_print_compile_var(IrPrint *irp, IrInstructionCompileVar *instruction) {
    fprintf(irp->f, "@compileVar(");
    ir_print_other_instruction(irp, instruction->name);
    fprintf(irp->f, ")");
}

static void ir_print_size_of(IrPrint *irp, IrInstructionSizeOf *instruction) {
    fprintf(irp->f, "@sizeOf(");
    ir_print_other_instruction(irp, instruction->type_value);
    fprintf(irp->f, ")");
}

static void ir_print_test_null(IrPrint *irp, IrInstructionTestNull *instruction) {
    fprintf(irp->f, "*");
    ir_print_other_instruction(irp, instruction->value);
    fprintf(irp->f, " == null");
}

static void ir_print_unwrap_maybe(IrPrint *irp, IrInstructionUnwrapMaybe *instruction) {
    fprintf(irp->f, "&??*");
    ir_print_other_instruction(irp, instruction->value);
    if (!instruction->safety_check_on) {
        fprintf(irp->f, " // no safety");
    }
}

static void ir_print_clz(IrPrint *irp, IrInstructionClz *instruction) {
    fprintf(irp->f, "@clz(");
    ir_print_other_instruction(irp, instruction->value);
    fprintf(irp->f, ")");
}

static void ir_print_ctz(IrPrint *irp, IrInstructionCtz *instruction) {
    fprintf(irp->f, "@ctz(");
    ir_print_other_instruction(irp, instruction->value);
    fprintf(irp->f, ")");
}

static void ir_print_switch_br(IrPrint *irp, IrInstructionSwitchBr *instruction) {
    const char *inline_kw = instruction->is_inline ? "inline " : "";
    fprintf(irp->f, "%sswitch (", inline_kw);
    ir_print_other_instruction(irp, instruction->target_value);
    fprintf(irp->f, ") ");
    for (size_t i = 0; i < instruction->case_count; i += 1) {
        IrInstructionSwitchBrCase *this_case = &instruction->cases[i];
        ir_print_other_instruction(irp, this_case->value);
        fprintf(irp->f, " => ");
        ir_print_other_block(irp, this_case->block);
        fprintf(irp->f, ", ");
    }
    fprintf(irp->f, "else => ");
    ir_print_other_block(irp, instruction->else_block);
}

static void ir_print_switch_var(IrPrint *irp, IrInstructionSwitchVar *instruction) {
    fprintf(irp->f, "switchvar ");
    ir_print_other_instruction(irp, instruction->target_value_ptr);
    fprintf(irp->f, ", ");
    ir_print_other_instruction(irp, instruction->prong_value);
}

static void ir_print_switch_target(IrPrint *irp, IrInstructionSwitchTarget *instruction) {
    fprintf(irp->f, "switchtarget ");
    ir_print_other_instruction(irp, instruction->target_value_ptr);
}

static void ir_print_enum_tag(IrPrint *irp, IrInstructionEnumTag *instruction) {
    fprintf(irp->f, "enumtag ");
    ir_print_other_instruction(irp, instruction->value);
}

static void ir_print_static_eval(IrPrint *irp, IrInstructionStaticEval *instruction) {
    fprintf(irp->f, "@staticEval(");
    ir_print_other_instruction(irp, instruction->value);
    fprintf(irp->f, ")");
}

static void ir_print_import(IrPrint *irp, IrInstructionImport *instruction) {
    fprintf(irp->f, "@import(");
    ir_print_other_instruction(irp, instruction->name);
    fprintf(irp->f, ")");
}

static void ir_print_array_len(IrPrint *irp, IrInstructionArrayLen *instruction) {
    ir_print_other_instruction(irp, instruction->array_value);
    fprintf(irp->f, ".len");
}

static void ir_print_ref(IrPrint *irp, IrInstructionRef *instruction) {
    fprintf(irp->f, "ref ");
    ir_print_other_instruction(irp, instruction->value);
}

static void ir_print_instruction(IrPrint *irp, IrInstruction *instruction) {
    ir_print_prefix(irp, instruction);
    switch (instruction->id) {
        case IrInstructionIdInvalid:
            zig_unreachable();
        case IrInstructionIdReturn:
            ir_print_return(irp, (IrInstructionReturn *)instruction);
            break;
        case IrInstructionIdConst:
            ir_print_const(irp, (IrInstructionConst *)instruction);
            break;
        case IrInstructionIdBinOp:
            ir_print_bin_op(irp, (IrInstructionBinOp *)instruction);
            break;
        case IrInstructionIdDeclVar:
            ir_print_decl_var(irp, (IrInstructionDeclVar *)instruction);
            break;
        case IrInstructionIdCast:
            ir_print_cast(irp, (IrInstructionCast *)instruction);
            break;
        case IrInstructionIdCall:
            ir_print_call(irp, (IrInstructionCall *)instruction);
            break;
        case IrInstructionIdUnOp:
            ir_print_un_op(irp, (IrInstructionUnOp *)instruction);
            break;
        case IrInstructionIdCondBr:
            ir_print_cond_br(irp, (IrInstructionCondBr *)instruction);
            break;
        case IrInstructionIdBr:
            ir_print_br(irp, (IrInstructionBr *)instruction);
            break;
        case IrInstructionIdPhi:
            ir_print_phi(irp, (IrInstructionPhi *)instruction);
            break;
        case IrInstructionIdContainerInitList:
            ir_print_container_init_list(irp, (IrInstructionContainerInitList *)instruction);
            break;
        case IrInstructionIdContainerInitFields:
            ir_print_container_init_fields(irp, (IrInstructionContainerInitFields *)instruction);
            break;
        case IrInstructionIdStructInit:
            ir_print_struct_init(irp, (IrInstructionStructInit *)instruction);
            break;
        case IrInstructionIdUnreachable:
            ir_print_unreachable(irp, (IrInstructionUnreachable *)instruction);
            break;
        case IrInstructionIdElemPtr:
            ir_print_elem_ptr(irp, (IrInstructionElemPtr *)instruction);
            break;
        case IrInstructionIdVarPtr:
            ir_print_var_ptr(irp, (IrInstructionVarPtr *)instruction);
            break;
        case IrInstructionIdLoadPtr:
            ir_print_load_ptr(irp, (IrInstructionLoadPtr *)instruction);
            break;
        case IrInstructionIdStorePtr:
            ir_print_store_ptr(irp, (IrInstructionStorePtr *)instruction);
            break;
        case IrInstructionIdTypeOf:
            ir_print_typeof(irp, (IrInstructionTypeOf *)instruction);
            break;
        case IrInstructionIdToPtrType:
            ir_print_to_ptr_type(irp, (IrInstructionToPtrType *)instruction);
            break;
        case IrInstructionIdPtrTypeChild:
            ir_print_ptr_type_child(irp, (IrInstructionPtrTypeChild *)instruction);
            break;
        case IrInstructionIdFieldPtr:
            ir_print_field_ptr(irp, (IrInstructionFieldPtr *)instruction);
            break;
        case IrInstructionIdStructFieldPtr:
            ir_print_struct_field_ptr(irp, (IrInstructionStructFieldPtr *)instruction);
            break;
        case IrInstructionIdEnumFieldPtr:
            ir_print_enum_field_ptr(irp, (IrInstructionEnumFieldPtr *)instruction);
            break;
        case IrInstructionIdSetFnTest:
            ir_print_set_fn_test(irp, (IrInstructionSetFnTest *)instruction);
            break;
        case IrInstructionIdSetFnVisible:
            ir_print_set_fn_visible(irp, (IrInstructionSetFnVisible *)instruction);
            break;
        case IrInstructionIdSetDebugSafety:
            ir_print_set_debug_safety(irp, (IrInstructionSetDebugSafety *)instruction);
            break;
        case IrInstructionIdArrayType:
            ir_print_array_type(irp, (IrInstructionArrayType *)instruction);
            break;
        case IrInstructionIdSliceType:
            ir_print_slice_type(irp, (IrInstructionSliceType *)instruction);
            break;
        case IrInstructionIdAsm:
            ir_print_asm(irp, (IrInstructionAsm *)instruction);
            break;
        case IrInstructionIdCompileVar:
            ir_print_compile_var(irp, (IrInstructionCompileVar *)instruction);
            break;
        case IrInstructionIdSizeOf:
            ir_print_size_of(irp, (IrInstructionSizeOf *)instruction);
            break;
        case IrInstructionIdTestNull:
            ir_print_test_null(irp, (IrInstructionTestNull *)instruction);
            break;
        case IrInstructionIdUnwrapMaybe:
            ir_print_unwrap_maybe(irp, (IrInstructionUnwrapMaybe *)instruction);
            break;
        case IrInstructionIdCtz:
            ir_print_ctz(irp, (IrInstructionCtz *)instruction);
            break;
        case IrInstructionIdClz:
            ir_print_clz(irp, (IrInstructionClz *)instruction);
            break;
        case IrInstructionIdSwitchBr:
            ir_print_switch_br(irp, (IrInstructionSwitchBr *)instruction);
            break;
        case IrInstructionIdSwitchVar:
            ir_print_switch_var(irp, (IrInstructionSwitchVar *)instruction);
            break;
        case IrInstructionIdSwitchTarget:
            ir_print_switch_target(irp, (IrInstructionSwitchTarget *)instruction);
            break;
        case IrInstructionIdEnumTag:
            ir_print_enum_tag(irp, (IrInstructionEnumTag *)instruction);
            break;
        case IrInstructionIdStaticEval:
            ir_print_static_eval(irp, (IrInstructionStaticEval *)instruction);
            break;
        case IrInstructionIdImport:
            ir_print_import(irp, (IrInstructionImport *)instruction);
            break;
        case IrInstructionIdArrayLen:
            ir_print_array_len(irp, (IrInstructionArrayLen *)instruction);
            break;
        case IrInstructionIdRef:
            ir_print_ref(irp, (IrInstructionRef *)instruction);
            break;
    }
    fprintf(irp->f, "\n");
}

void ir_print(FILE *f, IrExecutable *executable, int indent_size) {
    IrPrint ir_print = {};
    IrPrint *irp = &ir_print;
    irp->f = f;
    irp->indent = indent_size;
    irp->indent_size = indent_size;

    for (size_t bb_i = 0; bb_i < executable->basic_block_list.length; bb_i += 1) {
        IrBasicBlock *current_block = executable->basic_block_list.at(bb_i);
        fprintf(irp->f, "%s_%zu:\n", current_block->name_hint, current_block->debug_id);
        for (size_t instr_i = 0; instr_i < current_block->instruction_list.length; instr_i += 1) {
            IrInstruction *instruction = current_block->instruction_list.at(instr_i);
            ir_print_instruction(irp, instruction);
        }
    }
}