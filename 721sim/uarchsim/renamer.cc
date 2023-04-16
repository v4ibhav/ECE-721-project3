#include "renamer.h"

renamer::renamer(uint64_t n_log_regs,uint64_t n_phys_regs,uint64_t n_branches,uint64_t n_active)
{   
    ///////////////assertion///////////////////////
    assert(n_phys_regs>n_log_regs);
    assert(n_branches >= 1 && n_branches <= 64);
    assert(n_active > 0);


    ////////////////Free list allocation//////////////
    uint64_t flsize = n_phys_regs - n_log_regs;
    FL.FL_Size = flsize;
    FL.FL_entries.resize(flsize);
    FL.head =   FL.tail   =   0;
    FL.h_phase  =    0;
    FL.t_phase  =    1 ;
    foru(j,flsize)
    {
        FL.FL_entries[j] = j+n_log_regs; 
    }

    ////////////////Active list allocation/////////////
    AL.AL_size  =   n_active;
    AL.AL_entries.resize(n_active);
    AL.head    =    AL.tail    =   0;
    AL.h_phase  =   AL.t_phase  =   0;

    //////////////PRF and PRF bits allocation///////////
    PRF.resize(n_phys_regs);
    PRF_bits.resize(n_phys_regs);
    foru(i,n_phys_regs)
    {
        PRF[i] = 0;
        //all usage is 0 and all are not mapped 
        usage_Counter[i] = 0;
        unmapped_Bit[i]  = 1;
        PRF_bits[i] = 1;
    }

    ///////////////vector allocation/////////////////
    RMT.resize(n_log_regs);
    AMT.resize(n_log_regs);
    foru(i,n_log_regs)
    {
        RMT[i] = AMT[i] =    i;
        usage_Counter[i]=    1;
        unmapped_Bit[i] =    0;
    }
    //////////////GBM set///////////////////////////////
    GBM = 0;

    ///////////////Private variables////////////////////
    number_of_branches      =   n_branches;
    number_of_logical_reg   =   n_log_regs;
    number_of_physical_reg  =   n_phys_regs;
    total_active_instruction=   n_active;
    
    /////////////////Branch Checkpoint allocation///////
    Branch_CheckPoint.resize(number_of_branches);

    ////////////////checkpoint initializaiton//////////
    CPR_BUFFER.checkPointInfo.resize(n_branches);
    for(int i =0 ; i<n_branches;i++)
    {
        CPR_BUFFER.checkPointInfo[i].Checkpoint_of_rmt.resize(n_log_regs);
    }
        
}

bool renamer::stall_reg(uint64_t bundle_dst)
{
    uint64_t n_freelist_enteries = enteries_in_freelist();
    return (bundle_dst>n_freelist_enteries);
}

bool renamer::stall_branch(uint64_t bundle_branch)
{
    //check number of set bits of GBM (number of 1)
    uint64_t count_set_bits =    0;
    uint64_t t_GBM =    GBM;
    foru(i,number_of_branches)
    {
        if((t_GBM & 1) == true)   count_set_bits++;
        t_GBM  >>= 1;
    }   
    return((number_of_branches-count_set_bits)<bundle_branch);
}

uint64_t renamer::get_branch_mask()
{
    return GBM;
}

uint64_t renamer::rename_rsrc(uint64_t log_reg)
{
    // usage_Counter[RMT[log_reg]]++;
    inc_usage_counter(RMT[log_reg]);
    return RMT[log_reg];
}

uint64_t renamer::rename_rdst(uint64_t log_reg)
{
    //goto freelist head-->get the index 
    uint64_t rmt_value = FL.FL_entries[FL.head];
    //this is popped from the freelist map it
    renamer::map(rmt_value);
    //increment the head 
    FL.head++;
    
    //wrap around
    if(FL.head == FL.FL_Size)
    {
        FL.head = 0;
        FL.h_phase = !FL.h_phase;
    }
    //RMT[log_reg] is displaced from the RMT unmap it
    renamer::unmap(RMT[log_reg]);
    RMT[log_reg] = rmt_value;
    // usage_Counter[rmt_value]++;
    inc_usage_counter(rmt_value);
    unmapped_Bit[rmt_value] = 0;
    return rmt_value;

}

void renamer::checkpoint()
{
    //find the branch id position inside gbm if there is one
    // uint64_t pos = 0;

    // foru(i,checkPointBuffer_t.size)
    // {
    //     if((checkPointData & 1) == 0 )
    //     {
    //         pos = i;
    //         break;
    //     }
    //     t_GBM  >>= 1;
    // }
    
    // GBM = (GBM | (1<<pos)); //set the bit to 1
    // Branch_CheckPoint[pos].SMT = RMT;
    // Branch_CheckPoint[pos].checkpoint_freelist_head = FL.head;
    // Branch_CheckPoint[pos].checkpoint_freelist_head_phase = FL.h_phase;
    // Branch_CheckPoint[pos].checkpoint_GBM = GBM;
    //return  pos;


    //check space inside the checkpoint

    //3.3.5
    int tail = CPR_BUFFER.checkPointTail;
    int head = CPR_BUFFER.checkPointHead;
    //checkpoint the rmt
    CPR_BUFFER.checkPointInfo[tail].Checkpoint_of_rmt = RMT;
    //update the usage counter
    foru(i,number_of_logical_reg)
    {
        inc_usage_counter(CPR_BUFFER.checkPointInfo[tail].Checkpoint_of_rmt[i]);
    }
    //upate the tail to next position
    CPR_BUFFER.checkPointTail++;
    if(CPR_BUFFER.checkPointTail == CPR_BUFFER.size)
    {
        CPR_BUFFER.checkPointTail = 0;
    }
}

bool renamer::stall_dispatch(uint64_t bundle_inst)
{
    uint64_t AL_free_space = space_in_activelist();
    return (AL_free_space<bundle_inst);  
}

//dispatch the instruction
uint64_t renamer::dispatch_inst(bool dest_valid,uint64_t log_reg,uint64_t phys_reg,bool load,bool store,bool branch,bool amo,bool csr,uint64_t PC)
{
    uint64_t index_of_instruction = AL.tail;
    
    //dest_valid if true then the instr. has a destination register.
    if(dest_valid)
    {
        AL.AL_entries[AL.tail].log_dest = log_reg;
        AL.AL_entries[AL.tail].phy_dest = phys_reg;
    }

    //new instruction will clear all the old active list garbage values
    AL.AL_entries[AL.tail].load_flag    =   load;
    AL.AL_entries[AL.tail].store_flag   =   store;
    AL.AL_entries[AL.tail].branch_flag  =   branch;
    AL.AL_entries[AL.tail].atomic_flag  =   amo;
    AL.AL_entries[AL.tail].CSR_flag     =   csr;
    AL.AL_entries[AL.tail].dest_flag    =   dest_valid;
    AL.AL_entries[AL.tail].complete_bit =   0;
    AL.AL_entries[AL.tail].branch_misp_bit = 0 ;
    AL.AL_entries[AL.tail].load_viol_bit = 0;
    AL.AL_entries[AL.tail].exception_bit = 0;
    AL.AL_entries[AL.tail].value_misp_bit = 0;

    AL.AL_entries[AL.tail].prog_counter =   PC;


    //increment the tail
    AL.tail++;
    //wrap around
    if(AL.tail == AL.AL_size)
    {
        AL.tail = 0;
        AL.t_phase = !AL.t_phase;
    }

    return index_of_instruction;
}

bool renamer::is_ready(uint64_t phys_reg)
{
    return(PRF_bits[phys_reg]);
}

void renamer::clear_ready(uint64_t phys_reg)
{
    PRF_bits[phys_reg] = false;
}

uint64_t renamer::read(uint64_t phys_reg)
{
    dec_usage_counter(phys_reg);
    return PRF[phys_reg];
}

void renamer::set_ready(uint64_t phys_reg)
{
    PRF_bits[phys_reg]   =   true;
}

void renamer::write(uint64_t phys_reg, uint64_t value)
{
    dec_usage_counter(phys_reg);
    PRF[phys_reg] = value;
}

void renamer::set_complete(unsigned int checkPoint_ID)
{
    // AL.AL_entries[AL_index].complete_bit = true;
    CPR_BUFFER.checkPointInfo[checkPoint_ID].instr_Counter--;
}

// void renamer::resolve(uint64_t AL_index,uint64_t branch_ID,bool correct)
// {
//     if(correct)
//     {
//         // clear the branch bit in the gbm
//         // and clear the branch bit in all the branch checkpoints
//         GBM &= (~(1<<branch_ID));
//         foru(i, number_of_branches)
//         {
//             Branch_CheckPoint[i].checkpoint_GBM &= (~(1<<branch_ID));
//         }

//     }
//     else
//     {
//         // * Restore the GBM from the branch's checkpoint.
//         Branch_CheckPoint[branch_ID].checkpoint_GBM &= (~(1<<branch_ID));
//         GBM = Branch_CheckPoint[branch_ID].checkpoint_GBM;

//         //restore the rmt from branch checkpoint
//         RMT = Branch_CheckPoint[branch_ID].SMT;

//         //restore the free list head and the phase
//         FL.head = Branch_CheckPoint[branch_ID].checkpoint_freelist_head;
//         FL.h_phase = Branch_CheckPoint[branch_ID].checkpoint_freelist_head_phase;
        
//         //restore the active list tail and its phase bit
//         AL.tail = AL_index+1;
//         if(AL.tail == AL.AL_size)
//         {
//             AL.tail = 0;
//         }

//         //restore phase
//         AL.t_phase  =   !AL.h_phase;
//         if(AL.tail>AL.head)
//         {
//             AL.t_phase = AL.h_phase;
//         }
//     }
// }

// bool renamer::precommit(bool &completed,bool &exception, bool &load_viol, bool &br_misp, bool &val_misp,bool &load, bool &store, bool &branch, bool &amo, bool &csr,uint64_t &PC)
// {
//     if((AL.head == AL.tail) && (AL.h_phase == AL.t_phase))
//     {
//         return false;
//     }
//     else
//     {
//         completed   =   AL.AL_entries[AL.head].complete_bit; 
//         exception   =   AL.AL_entries[AL.head].exception_bit; 
//         load_viol   =   AL.AL_entries[AL.head].load_viol_bit;
//         br_misp     =   AL.AL_entries[AL.head].branch_misp_bit;
//         val_misp    =   AL.AL_entries[AL.head].value_misp_bit;
//         load        =   AL.AL_entries[AL.head].load_flag;
//         store       =   AL.AL_entries[AL.head].store_flag;
//         branch      =   AL.AL_entries[AL.head].branch_flag;
//         amo         =   AL.AL_entries[AL.head].atomic_flag;
//         csr         =   AL.AL_entries[AL.head].CSR_flag;
//         PC          =   AL.AL_entries[AL.head].prog_counter;
//         return true;

//     }
// }
//3.9.6 adding rollback function
void renamer::rollback(uint64_t chkpt_id, bool next, uint64_t &total_loads, 
                            uint64_t &total_stores, uint64_t &total_branches)
{
    int current_chkpt  = chkpt_id;
    if(next)
    {
        current_chkpt = chkpt_id+1;
    }

}

//3.9.1 
bool renamer::precommit(uint64_t &chkpt_id, uint64_t &num_loads, uint64_t &num_stores, uint64_t &num_branches, 
                            bool &amo, bool &csr, bool &exception)
{
    //That is, it returns true if
    // there exists a checkpoint after the oldest checkpoint and if all instructions between
    // them have completed.
    //oldest checkpoint is CPR_BUFFER.checkPointInfo[head]
    //newest checkpoint is CPR_BUFFER.checkPointInfo[tail-1]
    int oldest_CheckPoint = CPR_BUFFER.checkPointHead;
    int newest_CheckPoint = CPR_BUFFER.checkPointTail-1;
    if(CPR_BUFFER.checkPointTail ==-1)
    {
        newest_CheckPoint = CPR_BUFFER.size-1;
    }

    if((CPR_BUFFER.checkPointHead == CPR_BUFFER.checkPointTail-1) 
        || (CPR_BUFFER.checkPointInfo[oldest_CheckPoint].instr_Counter > 0))
    {
        return false;
    }
    chkpt_id    =   oldest_CheckPoint;
    num_loads   =   CPR_BUFFER.checkPointInfo[oldest_CheckPoint].load_Counter;
    num_stores  =   CPR_BUFFER.checkPointInfo[oldest_CheckPoint].store_Counter;
    num_branches=   CPR_BUFFER.checkPointInfo[oldest_CheckPoint].branch_Counter;
    amo         =   CPR_BUFFER.checkPointInfo[oldest_CheckPoint].amo;
    csr         =   CPR_BUFFER.checkPointInfo[oldest_CheckPoint].csr;
    exception   =   CPR_BUFFER.checkPointInfo[oldest_CheckPoint].exception;
    return true;
}

void renamer::commit(uint64_t log_reg)
{
    //goto logreg in usagecounter vector and decrement it
    // usage_Counter[log_reg]--;
    //find the physical register mapped to logical register inside the olderst checkpoint
    int oldest_CheckPoint = CPR_BUFFER.checkPointHead;
    int phys_reg = CPR_BUFFER.checkPointInfo[oldest_CheckPoint].Checkpoint_of_rmt[log_reg];
    dec_usage_counter(phys_reg);
    assert(usage_Counter[log_reg]>=0);
}

void renamer::squash()
{
    //squashing all instruction first thing AMT will be copied to RMT
    RMT = CPR_BUFFER.checkPointInfo[CPR_BUFFER.checkPointHead].Checkpoint_of_rmt;

    //unamapped bit roll back
    unmapped_Bit.assign(unmapped_Bit.size(),1);
    usage_Counter.assign(usage_Counter.size(),0);

    //unmapped bit and usage counter reinstialized
    foru(i,RMT.size())
    {
        unmapped_Bit[CPR_BUFFER.checkPointInfo[CPR_BUFFER.checkPointHead].Checkpoint_of_rmt[i]] = 0;
        usage_Counter[CPR_BUFFER.checkPointInfo[CPR_BUFFER.checkPointHead].Checkpoint_of_rmt[i]] = 1;
    }

    //reinitialize free list
    foru(i,PRF.size())
    {
        if((unmapped_Bit[i] == 1) && (usage_Counter[i] == 0))
        {
            FL.FL_entries.push_back(i);
        }
    }

    foru(i,PRF_bits.size())
    {
        PRF_bits[i] = 1;
    }
    
    // Active List will be emptied and phase are matched 
    // AL.tail = AL.head ;
    // AL.t_phase = AL.h_phase;
    
    //freelist is filled and phase are mismatched
    // FL.head = FL.tail;
    // FL.h_phase = !FL.t_phase;
    //3.9.4 modifying renamer squash


    // GBM  = 0;
    // foru(i,number_of_branches)
    // {
    //     Branch_CheckPoint[i].checkpoint_GBM = 0;
    // }

}
//3.7
void renamer::set_exception(unsigned int checkPoint_ID)
{
    CPR_BUFFER.checkPointInfo[checkPoint_ID].exception = 1;
}

void renamer::set_load_violation(uint64_t AL_index)
{
    AL.AL_entries[AL_index].load_viol_bit = 1;
}
void renamer::set_branch_misprediction(uint64_t AL_index)
{
    AL.AL_entries[AL_index].branch_misp_bit =1;
}

void renamer::set_value_misprediction(uint64_t AL_index)
{
    AL.AL_entries[AL_index].value_misp_bit = 1;
}

bool renamer::get_exception(uint64_t AL_index)
{
    return AL.AL_entries[AL_index].exception_bit;
}

uint64_t renamer::space_in_activelist()
{
    if(AL.h_phase == AL.t_phase)
    {
        return AL.AL_size - AL.tail + AL.head;   
    }
    else
    {
        return AL.head - AL.tail;
    }
}

uint64_t renamer::enteries_in_freelist()
{
    if(FL.h_phase == FL.t_phase)
    {
        return FL.tail-FL.head;
    }
    else
    {
        return FL.FL_Size-FL.head+FL.tail;
    }
}





//change 3.3.2
bool renamer::stall_checkpoint(uint64_t bundle_chkpts)
{
    //should stall until there is not enough space
    int capacity;
    if(CPR_BUFFER.checkPointHeadPhase == CPR_BUFFER.checkPointTailPhase)
    {
        capacity =  CPR_BUFFER.size - CPR_BUFFER.checkPointTail + CPR_BUFFER.checkPointHead;   
    }
    else
    {
        capacity = CPR_BUFFER.checkPointHead - CPR_BUFFER.checkPointTail;
    }
    return (capacity<bundle_chkpts);
}



//3.6
uint64_t renamer::get_checkpoint_ID(bool load , bool store, bool branch, bool amo, bool csr)
{
    uint64_t checkpoint_ID = CPR_BUFFER.checkPointTail-1;
    if(checkpoint_ID < 0)
    {
        checkpoint_ID = CPR_BUFFER.size-1;
    }

    //check if load,store,branch,amo,csr is set
    //if yes then increment the CPR_BUFFER.amo and CPR_BUFFER.csr
    CPR_BUFFER.checkPointInfo[checkpoint_ID].instr_Counter++;
    if(load)
    {
        CPR_BUFFER.checkPointInfo[checkpoint_ID].load_Counter++;
    }
    if(store)
    {
        CPR_BUFFER.checkPointInfo[checkpoint_ID].store_Counter++;
    }
    if(branch)
    {
        CPR_BUFFER.checkPointInfo[checkpoint_ID].branch_Counter++;
    }
    if(amo)
    {
        CPR_BUFFER.checkPointInfo[checkpoint_ID].amo++;
    }
    if(csr)
    {
        CPR_BUFFER.checkPointInfo[checkpoint_ID].csr++;
    }
    return checkpoint_ID;

}




//3.9.4
void renamer::free_checkpoint()
{
    //freeing the checkpoint
    CPR_BUFFER.checkPointInfo[CPR_BUFFER.checkPointHead].load_Counter = 0;
    CPR_BUFFER.checkPointInfo[CPR_BUFFER.checkPointHead].store_Counter = 0;
    CPR_BUFFER.checkPointInfo[CPR_BUFFER.checkPointHead].branch_Counter = 0;
    CPR_BUFFER.checkPointInfo[CPR_BUFFER.checkPointHead].amo = 0;
    CPR_BUFFER.checkPointInfo[CPR_BUFFER.checkPointHead].csr = 0;
    CPR_BUFFER.checkPointInfo[CPR_BUFFER.checkPointHead].exception = 0;
    CPR_BUFFER.checkPointInfo[CPR_BUFFER.checkPointHead].instr_Counter = 0;
    CPR_BUFFER.checkPointInfo[CPR_BUFFER.checkPointHead].Checkpoint_of_rmt.assign(CPR_BUFFER.checkPointInfo[CPR_BUFFER.checkPointHead].Checkpoint_of_rmt.size(),0);
    //update the head of the checkpoint buffer
    CPR_BUFFER.checkPointHead++;
    if(CPR_BUFFER.checkPointHead == CPR_BUFFER.size)
    {
        CPR_BUFFER.checkPointHead = 0;
        CPR_BUFFER.checkPointHeadPhase = !CPR_BUFFER.checkPointHeadPhase;
    }
}

void renamer::inc_usage_counter(uint64_t phys_reg)
{
    usage_Counter[phys_reg]++;
}

void renamer::dec_usage_counter(uint64_t phys_reg)
{
    assert(usage_Counter[phys_reg]>0);
    usage_Counter[phys_reg]--;
    if((usage_Counter[phys_reg] == 0) && (unmapped_Bit[phys_reg] == 1))
    {
        FL.FL_entries[FL.tail] = phys_reg;
        FL.tail++;
        //check if tail is at the end of the FL
        if(FL.tail == FL.FL_Size)
        {
            FL.tail = 0;
            FL.t_phase = !FL.t_phase;
        }
    } 
}

void renamer:: unmap(uint64_t phys_reg)
{
    unmapped_Bit[phys_reg] = 1;
    if(usage_Counter[phys_reg] == 0)
    {
        FL.FL_entries[FL.tail] = phys_reg;
        FL.tail++;
        //check if tail is at the end of the FL
        if(FL.tail == FL.FL_Size)
        {
            FL.tail = 0;
            FL.t_phase = !FL.t_phase;
        }
    }
}

void renamer:: map(uint64_t phys_reg)
{
    unmapped_Bit[phys_reg] = 0;
}
