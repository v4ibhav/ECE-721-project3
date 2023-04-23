#include "renamer.h"
//Shameek was here okok
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

    // ////////////////Active list allocation/////////////
    // AL.AL_size  =   n_active;
    // AL.AL_entries.resize(n_active);
    // AL.head    =    AL.tail    =   0;
    // AL.h_phase  =   AL.t_phase  =   0;

    ////////////////checkpoint initializaiton//////////
    CPR_BUFFER.checkPointInfo.resize(n_branches);
    CPR_BUFFER.size =n_branches;
    for(int i =0 ; i<n_branches;i++)
    {
        CPR_BUFFER.checkPointInfo[i].Checkpoint_of_rmt.resize(n_log_regs);
    }

    //////////////PRF and PRF bits allocation///////////
    PRF.resize(n_phys_regs);
    PRF_bits.resize(n_phys_regs);
    usage_Counter.resize(n_phys_regs);
    unmapped_Bit.resize(n_phys_regs);
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
    // AMT.resize(n_log_regs);
    foru(i,n_log_regs)
    {
        //checkpoitn at head or 0 is like amt
        RMT[i] = CPR_BUFFER.checkPointInfo[0].Checkpoint_of_rmt[i] = i;
        usage_Counter[i]=    1;
        unmapped_Bit[i] =    0;
    }
    //////////////GBM set///////////////////////////////
    // GBM = 0;

    ///////////////Private variables////////////////////
    number_of_branches      =   n_branches;
    number_of_logical_reg   =   n_log_regs;
    number_of_physical_reg  =   n_phys_regs;
    total_active_instruction=   n_active;
    
    /////////////////Branch Checkpoint allocation///////
    // Branch_CheckPoint.resize(number_of_branches);

        
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
    // unmapped_Bit[rmt_value] = 0;
    return rmt_value;

}

void renamer::checkpoint()
{
    //3.3.5
    int tail = CPR_BUFFER.checkPointTail;
    int head = CPR_BUFFER.checkPointHead;
    
    CPR_BUFFER.checkPointInfo[CPR_BUFFER.checkPointTail].amo = false;
    CPR_BUFFER.checkPointInfo[CPR_BUFFER.checkPointTail].csr = false;
    CPR_BUFFER.checkPointInfo[CPR_BUFFER.checkPointTail].exception = false;
    CPR_BUFFER.checkPointInfo[CPR_BUFFER.checkPointTail].branch_Counter = 0;
    CPR_BUFFER.checkPointInfo[CPR_BUFFER.checkPointTail].store_Counter = 0;
    CPR_BUFFER.checkPointInfo[CPR_BUFFER.checkPointTail].load_Counter = 0;
    CPR_BUFFER.checkPointInfo[CPR_BUFFER.checkPointTail].instr_Counter = 0;
    //checkpoint the rmt

    CPR_BUFFER.checkPointInfo[CPR_BUFFER.checkPointTail].Checkpoint_of_rmt = RMT;
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
        CPR_BUFFER.checkPointTailPhase = !CPR_BUFFER.checkPointTailPhase;
    }
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
    assert(CPR_BUFFER.checkPointInfo[checkPoint_ID].instr_Counter>0);
    CPR_BUFFER.checkPointInfo[checkPoint_ID].instr_Counter--;

    // //cout<<"Instruction indside checkpi "<<CPR_BUFFER.checkPointInfo[checkPoint_ID].instr_Counter<<endl;
    //printf("checkpoint_id %u number of instruction after one completion=====> %u \n",checkPoint_ID,CPR_BUFFER.checkPointInfo[checkPoint_ID].instr_Counter);
}





//3.9.1 
bool renamer::precommit(uint64_t &chkpt_id, uint64_t &num_loads, uint64_t &num_stores, uint64_t &num_branches, 
                            bool &amo, bool &csr, bool &exception)
{
    
    int oldest_CheckPoint = CPR_BUFFER.checkPointHead;
    int newest_CheckPoint = CPR_BUFFER.checkPointTail-1;
    int capacity = 0;
    capacity = CPR_BUFFER.checkPointTail - CPR_BUFFER.checkPointHead;
    if(CPR_BUFFER.checkPointHeadPhase != CPR_BUFFER.checkPointTailPhase)
    {
        capacity =  CPR_BUFFER.size - CPR_BUFFER.checkPointHead + CPR_BUFFER.checkPointTail;   
    }
    if(newest_CheckPoint <0)
    {
        newest_CheckPoint = CPR_BUFFER.size-1;
    }
    assert(newest_CheckPoint>=0);
    chkpt_id    =   oldest_CheckPoint;
    num_loads   =   CPR_BUFFER.checkPointInfo[oldest_CheckPoint].load_Counter;
    num_stores  =   CPR_BUFFER.checkPointInfo[oldest_CheckPoint].store_Counter;
    num_branches=   CPR_BUFFER.checkPointInfo[oldest_CheckPoint].branch_Counter;
    amo         =   CPR_BUFFER.checkPointInfo[oldest_CheckPoint].amo;
    // assert(!csr);
    csr         =   CPR_BUFFER.checkPointInfo[oldest_CheckPoint].csr;
    exception   =   CPR_BUFFER.checkPointInfo[oldest_CheckPoint].exception;
    if((CPR_BUFFER.checkPointInfo[oldest_CheckPoint].instr_Counter == 0) && 
        ((capacity>1) || (CPR_BUFFER.checkPointInfo[oldest_CheckPoint].exception == true)))
    {
        return true;
    }
    else
    {
        return false;
    }

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
    uint64_t j = 0;
    foru(i,PRF.size())
    {
        if((unmapped_Bit[i] == 1) && (usage_Counter[i] == 0))
        {
            FL.FL_entries[j] = i;
            j++;
        }
    }
    FL.head = 0;
    FL.h_phase = 0;
    FL.tail = 0;
    FL.t_phase = 1;



    foru(i,PRF_bits.size())
    {
        PRF_bits[i] = 1;
    }
    
    CPR_BUFFER.checkPointInfo[CPR_BUFFER.checkPointHead].amo = false;
    CPR_BUFFER.checkPointInfo[CPR_BUFFER.checkPointHead].csr = false;
    CPR_BUFFER.checkPointInfo[CPR_BUFFER.checkPointHead].exception = false;
    CPR_BUFFER.checkPointInfo[CPR_BUFFER.checkPointHead].branch_Counter = 0;
    CPR_BUFFER.checkPointInfo[CPR_BUFFER.checkPointHead].store_Counter = 0;
    CPR_BUFFER.checkPointInfo[CPR_BUFFER.checkPointHead].load_Counter = 0;
    CPR_BUFFER.checkPointInfo[CPR_BUFFER.checkPointHead].instr_Counter = 0;

    CPR_BUFFER.checkPointTail = CPR_BUFFER.checkPointHead+1;
    CPR_BUFFER.checkPointTailPhase = CPR_BUFFER.checkPointHeadPhase;
    if(CPR_BUFFER.checkPointTail==CPR_BUFFER.size)
    {
        CPR_BUFFER.checkPointTail = 0;
        CPR_BUFFER.checkPointTailPhase = !CPR_BUFFER.checkPointTailPhase;   //check?@TODO
    }    
}
//3.7
void renamer::set_exception(unsigned int checkPoint_ID)
{
    CPR_BUFFER.checkPointInfo[checkPoint_ID].exception = 1;
}

// void renamer::set_load_violation(uint64_t AL_index)
// {
//     // AL.AL_entries[AL_index].load_viol_bit = 1;
// }
// void renamer::set_branch_misprediction(uint64_t AL_index)
// {
//     // AL.AL_entries[AL_index].branch_misp_bit =1;
// }

// void renamer::set_value_misprediction(uint64_t AL_index)
// {
//     // AL.AL_entries[AL_index].value_misp_bit = 1;
// }

// bool renamer::get_exception(uint64_t AL_index)
// {
//     // return AL.AL_entries[AL_index].exception_bit;
// }

// uint64_t renamer::space_in_activelist()
// {
//     if(AL.h_phase == AL.t_phase)
//     {
//         return AL.AL_size - AL.tail + AL.head;   
//     }
//     else
//     {
//         return AL.head - AL.tail;
//     }
// }

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
    // //cout<<"stall_checkpoint called"<<endl;
    int free_space = 0;
    if(CPR_BUFFER.checkPointHeadPhase == CPR_BUFFER.checkPointTailPhase)
    {
        free_space =  CPR_BUFFER.size - CPR_BUFFER.checkPointTail + CPR_BUFFER.checkPointHead;   
    }
    else
    {
        free_space = CPR_BUFFER.checkPointHead - CPR_BUFFER.checkPointTail;
    }
    return (free_space<bundle_chkpts);
}



//3.6
uint64_t renamer::get_checkpoint_ID(bool load , bool store, bool branch, bool amo, bool csr)
{
    
    uint64_t checkpoint_ID = CPR_BUFFER.checkPointTail-1;

    if(CPR_BUFFER.checkPointTail == 0)
    {
        checkpoint_ID = CPR_BUFFER.size-1;
    }

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
        CPR_BUFFER.checkPointInfo[checkpoint_ID].amo = true;
    }
    if(csr)
    {
        CPR_BUFFER.checkPointInfo[checkpoint_ID].csr =true;
    }
    // assert(checkpoint_ID>=0 && checkpoint_ID<CPR_BUFFER.size);
    if(checkpoint_ID<0)
    {
        cout<<"checkpoint_ID<0"<<endl;
    }
    return checkpoint_ID;

}

// void renamer:: inc_usage_counter()
// {
//     uint64_t tail = CPR_BUFFER.checkPointTail-1;
//     if(tail<0)
//     {
//         tail = CPR_BUFFER.size-1;
//     }
//     CPR_BUFFER.checkPointInfo[tail].instr_Counter++;
// }


//3.9.4
void renamer::free_checkpoint()
{
    //freeing the checkpoint
    //cout<<"freeing the checkpoint"<<endl;
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



//3.9.6 adding rollback function
uint64_t renamer::rollback(uint64_t chkpt_id, bool next, uint64_t &total_loads, 
                            uint64_t &total_stores, uint64_t &total_branches)
{
    int rollback_checkpoint  = chkpt_id;
    if(next)
    {
        rollback_checkpoint = (chkpt_id+1) % (CPR_BUFFER.size);//modulo
    }
    
    //asserting valid/allocated(for allocation need to check if it is not null?@TODO)
    if(CPR_BUFFER.checkPointHeadPhase == CPR_BUFFER.checkPointTailPhase)
    {
        assert((rollback_checkpoint >= CPR_BUFFER.checkPointHead) && (rollback_checkpoint < CPR_BUFFER.checkPointTail));  
    }
    if(CPR_BUFFER.checkPointHeadPhase != CPR_BUFFER.checkPointTailPhase)
    {
        //check rollback is greater than equal to head and less than tail
        assert(((rollback_checkpoint<CPR_BUFFER.size)  &&  (rollback_checkpoint >= CPR_BUFFER.checkPointHead))
                                || ((rollback_checkpoint>=0) &&(rollback_checkpoint < CPR_BUFFER.checkPointTail)));
    }

    //restoring RMT
    foru(i,RMT.size()){
        if(usage_Counter[RMT[i]] == 0)
        {
            unmap(RMT[i]);
        }
    }

    RMT = CPR_BUFFER.checkPointInfo[rollback_checkpoint].Checkpoint_of_rmt;
    unmapped_Bit.assign(unmapped_Bit.size(),1);
    foru(i,RMT.size())
    {
        map(RMT[i]);
    }

    //generating squash mask
    uint64_t squash_mask = 0;
    if(CPR_BUFFER.checkPointHeadPhase == CPR_BUFFER.checkPointTailPhase)
    {
        squash_mask = (((uint64_t)1 << (CPR_BUFFER.checkPointTail-rollback_checkpoint)) - 1) << (rollback_checkpoint);
        for(uint64_t i = rollback_checkpoint+1;i<CPR_BUFFER.checkPointTail;i++)
        {
            squash_checkpoint(i);
        }

    }
    else if(CPR_BUFFER.checkPointHeadPhase != CPR_BUFFER.checkPointTailPhase)
    {   if(rollback_checkpoint >= CPR_BUFFER.checkPointHead)
        {
            squash_mask = (((uint64_t)1 << (CPR_BUFFER.size-rollback_checkpoint)) - 1) << (rollback_checkpoint) | ((1U << (CPR_BUFFER.checkPointTail)) - 1);
            for(uint64_t i = rollback_checkpoint+1;i<CPR_BUFFER.size;i++)
            {
                squash_checkpoint(i);
            }
            for(uint64_t i = 0;i<CPR_BUFFER.checkPointTail;i++)
            {
                squash_checkpoint(i);
            }

        }
        else if(rollback_checkpoint< CPR_BUFFER.checkPointTail)
        {
            squash_mask = (((uint64_t)1 << (CPR_BUFFER.checkPointTail-rollback_checkpoint)) - 1) << (rollback_checkpoint);
            for(uint64_t i = rollback_checkpoint+1;i<CPR_BUFFER.checkPointTail;i++)
            {
                squash_checkpoint(i);
            }
        }
    }

    //Reset the uncompleted instruction count, load count, store count, and branch count, all to 0, of the rollback checkpoint
    CPR_BUFFER.checkPointInfo[rollback_checkpoint].load_Counter = 0;
    CPR_BUFFER.checkPointInfo[rollback_checkpoint].store_Counter = 0;
    CPR_BUFFER.checkPointInfo[rollback_checkpoint].branch_Counter = 0;
    CPR_BUFFER.checkPointInfo[rollback_checkpoint].instr_Counter = 0;
    //Reset the amo, csr, and exception flags of the rollback checkpoint.
    CPR_BUFFER.checkPointInfo[rollback_checkpoint].amo = 0;
    CPR_BUFFER.checkPointInfo[rollback_checkpoint].csr = 0;
    CPR_BUFFER.checkPointInfo[rollback_checkpoint].exception = 0;
    CPR_BUFFER.checkPointTail = (rollback_checkpoint+1) % CPR_BUFFER.size;
    if(CPR_BUFFER.checkPointTail > CPR_BUFFER.checkPointHead)
    {
        CPR_BUFFER.checkPointTailPhase = CPR_BUFFER.checkPointHeadPhase;
    }
    else if(CPR_BUFFER.checkPointTail <= CPR_BUFFER.checkPointHead)
    {
        CPR_BUFFER.checkPointTailPhase = !CPR_BUFFER.checkPointHeadPhase;
    }
    return squash_mask;
}
void renamer::squash_checkpoint(uint64_t current)
{
    //first we have to decrement usage counter and unmap bits
    for(uint64_t i=0; i<number_of_logical_reg; i++){
        cout<<"hello" << endl;
        dec_usage_counter(CPR_BUFFER.checkPointInfo[current].Checkpoint_of_rmt[i]);
        //need to check if usage_counter has become 0 => then unmap the bit.
    }
    //current is the checkpoint ID of the checkpoint to be squashed
    CPR_BUFFER.checkPointInfo[current].load_Counter = 0;
    CPR_BUFFER.checkPointInfo[current].store_Counter = 0;
    CPR_BUFFER.checkPointInfo[current].branch_Counter = 0;
    CPR_BUFFER.checkPointInfo[current].amo = 0;
    CPR_BUFFER.checkPointInfo[current].csr = 0;
    CPR_BUFFER.checkPointInfo[current].exception = 0;
    CPR_BUFFER.checkPointInfo[current].instr_Counter = 0;
    CPR_BUFFER.checkPointInfo[current].Checkpoint_of_rmt.assign(CPR_BUFFER.checkPointInfo[current].Checkpoint_of_rmt.size(),0);
}