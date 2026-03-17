/*
 File: ContFramePool.C
 
 Author:
 Date  : 
 
 */

/*--------------------------------------------------------------------------*/
/* 
 POSSIBLE IMPLEMENTATION
 -----------------------

 The class SimpleFramePool in file "simple_frame_pool.H/C" describes an
 incomplete vanilla implementation of a frame pool that allocates 
 *single* frames at a time. Because it does allocate one frame at a time, 
 it does not guarantee that a sequence of frames is allocated contiguously.
 This can cause problems.
 
 The class ContFramePool has the ability to allocate either single frames,
 or sequences of contiguous frames. This affects how we manage the
 free frames. In SimpleFramePool it is sufficient to maintain the free 
 frames.
 In ContFramePool we need to maintain free *sequences* of frames.
 
 This can be done in many ways, ranging from extensions to bitmaps to 
 free-lists of frames etc.
 
 IMPLEMENTATION:
 
 One simple way to manage sequences of free frames is to add a minor
 extension to the bitmap idea of SimpleFramePool: Instead of maintaining
 whether a frame is FREE or ALLOCATED, which requires one bit per frame, 
 we maintain whether the frame is FREE, or ALLOCATED, or HEAD-OF-SEQUENCE.
 The meaning of FREE is the same as in SimpleFramePool. 
 If a frame is marked as HEAD-OF-SEQUENCE, this means that it is allocated
 and that it is the first such frame in a sequence of frames. Allocated
 frames that are not first in a sequence are marked as ALLOCATED.
 
 NOTE: If we use this scheme to allocate only single frames, then all 
 frames are marked as either FREE or HEAD-OF-SEQUENCE.
 
 NOTE: In SimpleFramePool we needed only one bit to store the state of 
 each frame. Now we need two bits. In a first implementation you can choose
 to use one char per frame. This will allow you to check for a given status
 without having to do bit manipulations. Once you get this to work, 
 revisit the implementation and change it to using two bits. You will get 
 an efficiency penalty if you use one char (i.e., 8 bits) per frame when
 two bits do the trick.
 
 DETAILED IMPLEMENTATION:
 
 How can we use the HEAD-OF-SEQUENCE state to implement a contiguous
 allocator? Let's look a the individual functions:
 
 Constructor: Initialize all frames to FREE, except for any frames that you 
 need for the management of the frame pool, if any.
 
 get_frames(_n_frames): Traverse the "bitmap" of states and look for a 
 sequence of at least _n_frames entries that are FREE. If you find one, 
 mark the first one as HEAD-OF-SEQUENCE and the remaining _n_frames-1 as
 ALLOCATED.

 release_frames(_first_frame_no): Check whether the first frame is marked as
 HEAD-OF-SEQUENCE. If not, something went wrong. If it is, mark it as FREE.
 Traverse the subsequent frames until you reach one that is FREE or 
 HEAD-OF-SEQUENCE. Until then, mark the frames that you traverse as FREE.
 
 mark_inaccessible(_base_frame_no, _n_frames): This is no different than
 get_frames, without having to search for the free sequence. You tell the
 allocator exactly which frame to mark as HEAD-OF-SEQUENCE and how many
 frames after that to mark as ALLOCATED.
 
 needed_info_frames(_n_frames): This depends on how many bits you need 
 to store the state of each frame. If you use a char to represent the state
 of a frame, then you need one info frame for each FRAME_SIZE frames.
 
 A WORD ABOUT RELEASE_FRAMES():
 
 When we releae a frame, we only know its frame number. At the time
 of a frame's release, we don't know necessarily which pool it came
 from. Therefore, the function "release_frame" is static, i.e., 
 not associated with a particular frame pool.
 
 This problem is related to the lack of a so-called "placement delete" in
 C++. For a discussion of this see Stroustrup's FAQ:
 http://www.stroustrup.com/bs_faq2.html#placement-delete
 
 */
/*--------------------------------------------------------------------------*/


/*--------------------------------------------------------------------------*/
/* DEFINES */
/*--------------------------------------------------------------------------*/

/* -- (none) -- */

/*--------------------------------------------------------------------------*/
/* INCLUDES */
/*--------------------------------------------------------------------------*/

#include "cont_frame_pool.H"
#include "console.H"
#include "utils.H"
#include "assert.H"

/*--------------------------------------------------------------------------*/
/* DATA STRUCTURES */
/*--------------------------------------------------------------------------*/


ContFramePool* ContFramePool::head = nullptr;

/*--------------------------------------------------------------------------*/
/* CONSTANTS */
/*--------------------------------------------------------------------------*/

/* -- (none) -- */

/*--------------------------------------------------------------------------*/
/* FORWARDS */
/*--------------------------------------------------------------------------*/

/* -- (none) -- */

/*--------------------------------------------------------------------------*/
/* METHODS FOR CLASS   C o n t F r a m e P o o l */
/*--------------------------------------------------------------------------*/

ContFramePool::FrameState ContFramePool::get_state(unsigned long frame_no){
    unsigned long index = frame_no / 4;
    unsigned long bit_position = (frame_no % 4) * 2; 

    char state = (bitmap[index] >> bit_position) & 0x03;
    if(state==0) return FrameState::Free;
    if(state==1) return FrameState::Used;
    return FrameState::HoS;

}

void ContFramePool::set_state(unsigned long _frame_no, FrameState _state){
    unsigned long index = _frame_no / 4;
    unsigned long bit_position = (_frame_no % 4)* 2; 
    char state_val = 0;

    switch(_state) {
        case FrameState::Used:
        state_val = 1;
        break;
      case FrameState::Free:
        state_val = 0;
        break;
      case FrameState::HoS:
        state_val = 2;
        break;
    }
    bitmap[index] &= ~(0x03 << bit_position);
    bitmap[index] |= (state_val  << bit_position);
}

ContFramePool::ContFramePool(unsigned long _base_frame_no,
                             unsigned long _n_frames,
                             unsigned long _info_frame_no)
{
    base_frame_no = _base_frame_no;
    n_frames = _n_frames;
    info_frame_no = _info_frame_no;
    if(info_frame_no == 0) {
        bitmap = (unsigned char *) (base_frame_no * FRAME_SIZE);
    } else {
        bitmap = (unsigned char *) (info_frame_no * FRAME_SIZE);
    }
    unsigned long bitmap_size = (n_frames * 2 + 7 )/8;
    for(unsigned long i = 0;i < bitmap_size;i++){
        bitmap[i] = 0;
    }
    if(info_frame_no == 0) {
        set_state(0, FrameState::Used);  
    }
    next = head;
    head = this;
    Console::puts("ContframePool::Constructor initialized!\n");
}

unsigned long ContFramePool::get_frames(unsigned int _n_frames)
{
    /*
    Iterate through the bitmap, going frame by frame
    do a linear search for a gap of size _n_frames that is free
    update the state of those frames and allocates them as used and return the start index

    */
    unsigned long start_index = 0;
    unsigned long consecutive_frames = 0;
    for (unsigned long i = 0; i < n_frames;i++){
        if(get_state(i) == FrameState::Free){
            if(consecutive_frames == 0){
                start_index = i;
            }
            consecutive_frames++;
            if (consecutive_frames == _n_frames){
                //mark and return
                mark_inaccessible(base_frame_no + start_index, _n_frames);
                return base_frame_no + start_index;
            }
        }
        else{
            consecutive_frames = 0;

        }
    }
    return 0;
}

void ContFramePool::mark_inaccessible(unsigned long _base_frame_no,
                                      unsigned long _n_frames)
{
    if(_base_frame_no < base_frame_no || 
        _base_frame_no + _n_frames > base_frame_no + n_frames) {
         Console::puts("ContframePool::mark_inaccessible: Frame out of bounds!\n");
         assert(false);
     }
    unsigned long rel_index = _base_frame_no - base_frame_no;
    set_state(rel_index, FrameState::HoS);
    for(unsigned long j = 1; j < _n_frames; j++){
        set_state(rel_index + j, FrameState::Used);
    }
}

void ContFramePool::release_frames(unsigned long _first_frame_no)
{
    ContFramePool* current = head;
    while(current != nullptr) {
        // Check if this frame belongs to the current pool
        if(_first_frame_no >= current->base_frame_no && 
           _first_frame_no < current->base_frame_no + current->n_frames) {
            // Found the pool that owns this frame
            current->release_frames_helper(_first_frame_no);
            return;
        }
        current = current->next;
    }
    
    // Frame doesn't belong to any pool - this is an error
    Console::puts("ContframePool::release_frames: Frame not found in any pool!\n");
    assert(false);
}
void ContFramePool::release_frames_helper(unsigned long _first_frame_no)
{
    // Get relative index
    unsigned long rel_index = _first_frame_no - base_frame_no;
    
    // Check if the first frame is HoS
    if(get_state(rel_index) != FrameState::HoS) {
        Console::puts("ContframePool::release_frames_helper: First frame is not HoS!\n");
        assert(false);
    }
    
    set_state(rel_index, FrameState::Free);
    
    // Continue until we hit Free or HoS
    rel_index++;
    while(rel_index < n_frames) {
        FrameState state = get_state(rel_index);
        if(state == FrameState::Free || state == FrameState::HoS) {
            break;
        }
        set_state(rel_index, FrameState::Free);
        rel_index++;
    }
}


unsigned long ContFramePool::needed_info_frames(unsigned long _n_frames)
{
    unsigned long frames_per_info = (FRAME_SIZE *8 ) / 2;
    return (_n_frames / frames_per_info) + ((_n_frames % frames_per_info) > 0 ? 1 : 0);
}
