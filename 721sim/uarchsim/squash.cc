#include "pipeline.h"


void pipeline_t::squash_complete(reg_t jump_PC) {
	unsigned int i, j;

	//////////////////////////
	// Fetch Stage
	//////////////////////////
  
	FetchUnit->flush(jump_PC);

	//////////////////////////
	// Decode Stage
	//////////////////////////

	for (i = 0; i < fetch_width; i++) {
		DECODE[i].valid = false;
	}

	//////////////////////////
	// Rename1 Stage
	//////////////////////////
	
	FQ.flush();

	//////////////////////////
	// Rename2 Stage
	//////////////////////////

	for (i = 0; i < dispatch_width; i++) {
		RENAME2[i].valid = false;
	}

        //
        // FIX_ME #17c
        // Squash the renamer.
        //

        // FIX_ME #17c BEGIN
		
		REN->squash();
		instr_renamed_since_last_checkpoint = 0;
        // FIX_ME #17c END


	//////////////////////////
	// Dispatch Stage
	//////////////////////////

	for (i = 0; i < dispatch_width; i++) {
		DISPATCH[i].valid = false;
	}

	//////////////////////////
	// Schedule Stage
	//////////////////////////

	IQ.flush();

	//////////////////////////
	// Register Read Stage
	// Execute Stage
	// Writeback Stage
	//////////////////////////

	for (i = 0; i < issue_width; i++) {
		Execution_Lanes[i].rr.valid = false;
		for (j = 0; j < Execution_Lanes[i].ex_depth; j++)
		   Execution_Lanes[i].ex[j].valid = false;
		Execution_Lanes[i].wb.valid = false;
	}

	LSU.flush();
}




void pipeline_t::selective_squash(uint64_t squash_mask) {
	unsigned int i, j;

	// Squash all instructions in the Decode through Dispatch Stages.

	// Decode Stage:
	for (i = 0; i < fetch_width; i++) {
		DECODE[i].valid = false;
	}

	// Rename1 Stage:
	FQ.flush();

	// Rename2 Stage:
	for (i = 0; i < dispatch_width; i++) {
		RENAME2[i].valid = false;
	}
	instr_renamed_since_last_checkpoint = 0;

	// Dispatch Stage:
	for (i = 0; i < dispatch_width; i++) {
		if(DISPATCH[i].valid)
		{

			DISPATCH[i].valid = false;
			if(PAY.buf[DISPATCH[i].index].A_valid)
				REN->dec_usage_counter(PAY.buf[DISPATCH[i].index].A_phys_reg);
			if(PAY.buf[DISPATCH[i].index].B_valid)
				REN->dec_usage_counter(PAY.buf[DISPATCH[i].index].B_phys_reg);
			if(PAY.buf[DISPATCH[i].index].C_valid)
				REN->dec_usage_counter(PAY.buf[DISPATCH[i].index].C_phys_reg);
			if(PAY.buf[DISPATCH[i].index].D_valid)
				REN->dec_usage_counter(PAY.buf[DISPATCH[i].index].D_phys_reg);
		}
	}

	// Selectively squash instructions after the branch, in the Schedule through Writeback Stages.

	// Schedule Stage:
	IQ.squash(squash_mask);

	for (i = 0; i < issue_width; i++) {
		// Register Read Stage:
		if (Execution_Lanes[i].rr.valid && BIT_IS_ONE(squash_mask,Execution_Lanes[i].rr.branch_mask)) {
			Execution_Lanes[i].rr.valid = false;
		if(PAY.buf[Execution_Lanes[i].rr.index].A_valid)
			REN->dec_usage_counter(PAY.buf[Execution_Lanes[i].rr.index].A_phys_reg);
		if(PAY.buf[Execution_Lanes[i].rr.index].B_valid)
			REN->dec_usage_counter(PAY.buf[Execution_Lanes[i].rr.index].B_phys_reg);
		if(PAY.buf[Execution_Lanes[i].rr.index].C_valid)
			REN->dec_usage_counter(PAY.buf[Execution_Lanes[i].rr.index].C_phys_reg);
		if(PAY.buf[Execution_Lanes[i].rr.index].D_valid)
			REN->dec_usage_counter(PAY.buf[Execution_Lanes[i].rr.index].D_phys_reg);

		}

		// Execute Stage:
		for (j = 0; j < Execution_Lanes[i].ex_depth; j++) {
			if (Execution_Lanes[i].ex[j].valid && BIT_IS_ONE(squash_mask,Execution_Lanes[i].ex[j].branch_mask)) {
			Execution_Lanes[i].ex[j].valid = false;
			if(PAY.buf[Execution_Lanes[i].ex[j].index].C_valid)
				REN->dec_usage_counter(PAY.buf[Execution_Lanes[i].ex[j].index].C_phys_reg);
			}
			
		}

		// Writeback Stage:
		if (Execution_Lanes[i].wb.valid && BIT_IS_ONE(squash_mask,Execution_Lanes[i].wb.branch_mask)) {
			Execution_Lanes[i].wb.valid = false;
		}
	}
	
}




// void pipeline_t::resolve(unsigned int branch_ID, bool correct) {
// 	unsigned int i, j;

// 	if (correct) {
// 		// Instructions in the Rename2 through Writeback Stages have branch masks.
// 		// The correctly-resolved branch's bit must be cleared in all branch masks.

// 		for (i = 0; i < dispatch_width; i++) {
// 			// Rename2 Stage:
// 			CLEAR_BIT(RENAME2[i].branch_mask, branch_ID);

// 			// Dispatch Stage:
// 			CLEAR_BIT(DISPATCH[i].branch_mask, branch_ID);
// 		}

// 		// Schedule Stage:
// 		IQ.clear_branch_bit(branch_ID);

// 		for (i = 0; i < issue_width; i++) {
// 			// Register Read Stage:
// 			CLEAR_BIT(Execution_Lanes[i].rr.branch_mask, branch_ID);

// 			// Execute Stage:
// 			for (j = 0; j < Execution_Lanes[i].ex_depth; j++)
// 			   CLEAR_BIT(Execution_Lanes[i].ex[j].branch_mask, branch_ID);

// 			// Writeback Stage:
// 			CLEAR_BIT(Execution_Lanes[i].wb.branch_mask, branch_ID);
// 		}
// 	}
// 	else {
// 		// Squash all instructions in the Decode through Dispatch Stages.

// 		// Decode Stage:
// 		for (i = 0; i < fetch_width; i++) {
// 			DECODE[i].valid = false;
// 		}

// 		// Rename1 Stage:
// 		FQ.flush();

// 		// Rename2 Stage:
// 		for (i = 0; i < dispatch_width; i++) {
// 			RENAME2[i].valid = false;
// 		}

// 		// Dispatch Stage:
// 		for (i = 0; i < dispatch_width; i++) {
// 			DISPATCH[i].valid = false;
// 		}

// 		// Selectively squash instructions after the branch, in the Schedule through Writeback Stages.

// 		// Schedule Stage:
// 		IQ.squash(branch_ID);

// 		for (i = 0; i < issue_width; i++) {
// 			// Register Read Stage:
// 			if (Execution_Lanes[i].rr.valid && BIT_IS_ONE(Execution_Lanes[i].rr.branch_mask, branch_ID)) {
// 				Execution_Lanes[i].rr.valid = false;
// 			}

// 			// Execute Stage:
// 			for (j = 0; j < Execution_Lanes[i].ex_depth; j++) {
// 			   if (Execution_Lanes[i].ex[j].valid && BIT_IS_ONE(Execution_Lanes[i].ex[j].branch_mask, branch_ID)) {
// 				Execution_Lanes[i].ex[j].valid = false;
// 			   }
// 			}

// 			// Writeback Stage:
// 			if (Execution_Lanes[i].wb.valid && BIT_IS_ONE(Execution_Lanes[i].wb.branch_mask, branch_ID)) {
// 				Execution_Lanes[i].wb.valid = false;
// 			}
// 		}
// 	}
// }
