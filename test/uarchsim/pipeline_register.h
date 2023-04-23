#ifndef PIPELINE_REGISTER_H
#define PIPELINE_REGISTER_H

class pipeline_register {

public:

	bool valid;				              // valid instruction
	unsigned int index;			        // index into instruction payload buffer
	//In the IQ and backend execution lanes’ pipeline registers, 
	//replace the instruction’s branch mask (no longer inherited) with the instruction’s inherited checkpoint ID.
	unsigned long long branch_mask;
	//unsigned long long chkpt_id;	// branches that this instruction depends on

	pipeline_register();	// constructor

};

#endif //PIPELINE_REGISTER_H
