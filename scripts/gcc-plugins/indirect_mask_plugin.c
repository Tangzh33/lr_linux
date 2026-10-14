// SPDX-License-Identifier: GPL-2.0-only

#include "gcc-common.h"

#include "df.h"
#include "optabs.h"

__visible int plugin_is_GPL_compatible;

static void mask_reg(rtx reg, rtx_insn *before)
{
	rtx masked;
	rtx_insn *insns;

	start_sequence();

	masked = expand_simple_binop(DImode, IOR, reg, GEN_INT(-1 << 30), reg,
				     0, OPTAB_DIRECT);
	gcc_assert(masked == reg);

	insns = end_sequence();

	emit_insn_before(insns, before);
}

// https://gcc.gnu.org/onlinedocs/gccint/Calls.html
static rtx find_call(rtx x)
{
	if (GET_CODE(x) == CALL)
		return x;

	if (GET_CODE(x) == SET)
		return find_call(SET_SRC(x));

	if (GET_CODE(x) == PARALLEL) {
		for (int i = 0; i < XVECLEN(x, 0); ++i) {
			rtx y = find_call(XVECEXP(x, 0, i));
			if (y)
				return y;
		}
	}

	return NULL_RTX;
}

// https://gcc.gnu.org/onlinedocs/gccint/Insns.html#index-jump_005finsn
static rtx find_pc_set(rtx x)
{
	if (GET_CODE(x) == SET && SET_DEST(x) == pc_rtx)
		return x;

	if (GET_CODE(x) == PARALLEL) {
		for (int i = 0; i < XVECLEN(x, 0); ++i) {
			rtx y = find_pc_set(XVECEXP(x, 0, i));
			if (y)
				return y;
		}
	}

	return NULL_RTX;
}

static void instrument_call(rtx_insn *insn)
{
	rtx call, mem, target;

	call = find_call(PATTERN(insn));
	gcc_assert(call != NULL_RTX);

	mem = XEXP(call, 0);
	gcc_assert(MEM_P(mem));

	target = XEXP(mem, 0);

	if (GET_CODE(target) == SYMBOL_REF)
		return;
	gcc_assert(REG_P(target));

	mask_reg(target, insn);
}

static void instrument_jump(rtx_insn *insn)
{
	rtx pat, pc_set, target;

	pat = PATTERN(insn);
	if (ANY_RETURN_P(pat))
		return;

	pc_set = find_pc_set(pat);
	gcc_assert(pc_set != NULL_RTX);

	target = SET_SRC(pc_set);

	if (GET_CODE(target) == LABEL_REF || GET_CODE(target) == IF_THEN_ELSE)
		return;
	gcc_assert(REG_P(target));

	mask_reg(target, insn);
}

static unsigned int indirect_mask_execute(void)
{
	rtx_insn *insn;

	for (insn = get_insns(); insn; insn = NEXT_INSN(insn)) {
		if (CALL_P(insn))
			instrument_call(insn);
		else if (JUMP_P(insn))
			instrument_jump(insn);
	}

	return 0;
}

#define PASS_NAME indirect_mask
#define NO_GATE
#include "gcc-generate-rtl-pass.h"

__visible int plugin_init(struct plugin_name_args *plugin_info,
			  struct plugin_gcc_version *version)
{
	struct register_pass_info pass_info;

	if (!plugin_default_version_check(version, &gcc_version))
		return 1;

	if (getenv("UNSAFE_BYPASS_INDIRECT_MASK"))
		return 0;

	pass_info.pass = make_indirect_mask_pass();
	pass_info.reference_pass_name = "*all-late_compilation";
	pass_info.ref_pass_instance_number = 1;
	pass_info.pos_op = PASS_POS_INSERT_BEFORE;

	register_callback(plugin_info->base_name, PLUGIN_PASS_MANAGER_SETUP,
			  NULL, &pass_info);

	return 0;
}
