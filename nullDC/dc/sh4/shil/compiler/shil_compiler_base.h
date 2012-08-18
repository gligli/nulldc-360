#include "types.h"
#include "dc/sh4/shil/shil.h"
#include "emitter/emitter.h"
#include "emitter/regalloc/ppc_fpregalloc.h"

extern u32 T_jcond_value;
extern u32 reg_pc_temp_value;

void shil_compiler_init(ppc_block* block,IntegerRegAllocator* ira,FloatRegAllocator* fra);
void shil_compile(shil_opcode* op);

struct roml_patch
{
	ppc_Label* p4_access;
	u8 resume_offset;
	ppc_Label* exit_point;
	u32 asz;
	u32 type;

	ppc_reg reg_addr;
	ppc_reg reg_data;
	s32 fast_imm;
	bool is_float;

	u32 sh4_reg_data;
    
	ppc_Label* roml_search_lbl;
};

extern vector<roml_patch> roml_patch_list;
void apply_roml_patches();