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
    vector<uint64_t> AMT;
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

    typedef struct ALRow{
        uint64_t log_dest, phy_dest,prog_counter;
        bool dest_flag,load_flag,store_flag,branch_flag,atomic_flag, CSR_flag;
        bool complete_bit,exception_bit, load_viol_bit,branch_misp_bit,value_misp_bit;
        ALRow():    log_dest(0),
                    phy_dest(0),
                    prog_counter(0),
                    dest_flag(0),
                    load_flag(0),
                    store_flag(0),
                    branch_flag(0),
                    atomic_flag(0),
                    CSR_flag(0),
                    complete_bit(0),
                    exception_bit(0),
                    load_viol_bit(0),
                    branch_misp_bit(0),
                    value_misp_bit(0){};
    }ALRow;

    //active list
    typedef struct ActiveList{
        vector<ALRow> AL_entries;
        uint64_t head;
        uint64_t tail;
        bool h_phase,t_phase;
        uint64_t AL_size;
        ActiveList() :  head(0),
                        tail(0),
                        h_phase(0),
                        t_phase(0),
                        AL_size(0) {};
        
    }ActiveList;
    //prf and prf bits
    vector<uint64_t> PRF;
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
    

    FreeList FL;
    // ALRow AL_entries;
    ActiveList AL;
    vector<CheckPoint>  Branch_CheckPoint;



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
	uint64_t checkpoint();
	bool stall_dispatch(uint64_t bundle_inst);
    uint64_t dispatch_inst(bool dest_valid,
	                       uint64_t log_reg,
	                       uint64_t phys_reg,
	                       bool load,
	                       bool store,
	                       bool branch,
	                       bool amo,
	                       bool csr,
	                       uint64_t PC);
	bool is_ready(uint64_t phys_reg);
	void clear_ready(uint64_t phys_reg);
	uint64_t read(uint64_t phys_reg);
	void set_ready(uint64_t phys_reg);
	void write(uint64_t phys_reg, uint64_t value);
	void set_complete(uint64_t AL_index);
    void resolve(uint64_t AL_index,
		     uint64_t branch_ID,
		     bool correct);
	bool precommit(bool &completed,
                       bool &exception, bool &load_viol, bool &br_misp, bool &val_misp,
	               bool &load, bool &store, bool &branch, bool &amo, bool &csr,
		       uint64_t &PC);
	void commit();
	void squash();
    void set_exception(uint64_t AL_index);
	void set_load_violation(uint64_t AL_index);
	void set_branch_misprediction(uint64_t AL_index);
	void set_value_misprediction(uint64_t AL_index);
	bool get_exception(uint64_t AL_index);
    uint64_t enteries_in_freelist();
    uint64_t space_in_activelist();




};