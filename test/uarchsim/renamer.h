#include <inttypes.h>
#include <iostream>
#include <assert.h>
#include <vector>
#include <bits/stdc++.h>

using namespace std;

#define foru(i,n) for(uint64_t i = 0; i<n; i++)
// #define uint64_t	uint64_t

class renamer{
    private:
    // all the structure inside this specifier
    uint64_t    number_of_branches,      
            number_of_logical_reg,
            number_of_physical_reg,
            total_active_instruction;
    
    //RMT
    vector<uint64_t> RMT;
    //AMT
    // vector<uint64_t> AMT;
    //free list
    typedef struct FreeList{
        vector<uint64_t> FL_entries;
        uint64_t head;
        uint64_t tail;
        bool h_phase,t_phase;
        uint64_t FL_Size;
        FreeList()  :   head(0),
                        tail(0),
                        h_phase(0),
                        t_phase(1),
                        FL_Size(0){};

    }FreeList;


    //prf and prf bits
    vector<uint64_t> PRF; 
    vector<bool> unmapped_Bit;
    vector<uint64_t> usage_Counter;  
    void map(uint64_t phy_reg);
    void unmap(uint64_t phy_reg);
    vector<bool> PRF_bits;
    //gbm
	uint64_t GBM;
    //checkpoint
    typedef struct CheckPoint
    {
        vector<uint64_t>    SMT;
        uint64_t            checkpoint_freelist_head;
        bool            checkpoint_freelist_head_phase;
        uint64_t            checkpoint_GBM;
        CheckPoint() :  SMT(0),
                        checkpoint_freelist_head(0),
                        checkpoint_freelist_head_phase(0),
                        checkpoint_GBM(0){};
    }CheckPoint;

    typedef struct checkPointInfo_t{
		bool amo;
		bool csr;
		bool exception;
        // uint64_t usage_Counter;
		uint64_t instr_Counter;
		uint64_t load_Counter;
		uint64_t store_Counter;
		uint64_t branch_Counter;
		vector<uint64_t> Checkpoint_of_rmt;
		checkPointInfo_t() : amo(0), 
                             csr(0),
                             exception(0),
                            //  usage_Counter(0),
                             instr_Counter(0),
                             load_Counter(0),
                             store_Counter(0),
                             branch_Counter(0) {}	

	} checkPointInfo_t;
	
	typedef struct checkPoint_Rows{
		int checkPointHead;
		int checkPointTail;
        int checkPointHeadPhase;
        int checkPointTailPhase;
		int size;

		vector<checkPointInfo_t> checkPointInfo;	
		checkPoint_Rows() : checkPointHead(0),
                               checkPointTail(1), //tail always is empty 
                               checkPointHeadPhase(0),
                               checkPointTailPhase(0),
                               size(1) {}
	}checkPointRows;
    
    

    FreeList FL;
    // ALRow AL_entries;
    // ActiveList AL;
    vector<CheckPoint>  Branch_CheckPoint;
    //changes 2
    checkPoint_Rows CPR_BUFFER;




    public:
    renamer(uint64_t n_log_regs,
		uint64_t n_phys_regs,
		uint64_t n_branches,
		uint64_t n_active);
	bool stall_reg(uint64_t bundle_dst);
	bool stall_branch(uint64_t bundle_branch);
	uint64_t get_branch_mask();
	uint64_t rename_rsrc(uint64_t log_reg);
	uint64_t rename_rdst(uint64_t log_reg);
    void  inc_usage_counter();
	
	// bool stall_dispatch(uint64_t bundle_inst);
    bool stall_checkpoint(uint64_t bundle_chkpts);
    // uint64_t dispatch_inst(bool dest_valid,
	//                        uint64_t log_reg,
	//                        uint64_t phys_reg,
	//                        bool load,
	//                        bool store,
	//                        bool branch,
	//                        bool amo,
	//                        bool csr,
	//                        uint64_t PC);
	bool is_ready(uint64_t phys_reg);
	void clear_ready(uint64_t phys_reg);
	uint64_t read(uint64_t phys_reg);
	void set_ready(uint64_t phys_reg);
	void write(uint64_t phys_reg, uint64_t value);
	void set_complete(unsigned int checkPoint_ID);
    // void resolve(uint64_t AL_index,
	// 	     uint64_t branch_ID,
	// 	     bool correct);
	// bool precommit(bool &completed,
    //                    bool &exception, bool &load_viol, bool &br_misp, bool &val_misp,
	//                bool &load, bool &store, bool &branch, bool &amo, bool &csr,
	// 	       uint64_t &PC);

    uint64_t rollback(uint64_t chkpt_id,  bool next, uint64_t
                    &total_loads, uint64_t &total_stores, uint64_t &total_branches);
    bool precommit(uint64_t &chkpt_id, uint64_t &num_loads,
                uint64_t &num_stores, uint64_t &num_branches, bool &amo, bool &csr,
                bool &exception);
	void commit(uint64_t log_reg);

	void squash();
    void set_exception(unsigned int checkpoint_ID);
	// void set_load_violation(uint64_t AL_index);
	// void set_branch_misprediction    (uint64_t AL_index);
	// void set_value_misprediction(uint64_t AL_index);
	// bool get_exception(uint64_t AL_index);
    uint64_t enteries_in_freelist();
    // uint64_t space_in_activelist();
    void checkpoint();

    uint64_t get_checkpoint_ID(bool load, bool store, bool branch, bool amo, bool  csr);

    void free_checkpoint();
    void inc_usage_counter(uint64_t phys_reg);
    void dec_usage_counter(uint64_t phys_reg);
    void squash_checkpoint(uint64_t current);
};