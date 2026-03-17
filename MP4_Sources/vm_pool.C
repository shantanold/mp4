/*
 File: vm_pool.C

 Author:
 Date  : 2024/09/20

 */

/*--------------------------------------------------------------------------*/
/* DEFINES */
/*--------------------------------------------------------------------------*/

/* -- (none) -- */

/*--------------------------------------------------------------------------*/
/* INCLUDES */
/*--------------------------------------------------------------------------*/

#include "vm_pool.H"
#include "console.H"
#include "utils.H"
#include "assert.H"

/*--------------------------------------------------------------------------*/
/* DATA STRUCTURES */
/*--------------------------------------------------------------------------*/

/* -- (none) -- */

/*--------------------------------------------------------------------------*/
/* CONSTANTS */
/*--------------------------------------------------------------------------*/

/* -- (none) -- */

/*--------------------------------------------------------------------------*/
/* FORWARDS */
/*--------------------------------------------------------------------------*/

/* -- (none) -- */

/*--------------------------------------------------------------------------*/
/* METHODS FOR CLASS   V M P o o l */
/*--------------------------------------------------------------------------*/

VMPool::VMPool(unsigned long  _base_address,
               unsigned long  _size,
               ContFramePool *_frame_pool,
               PageTable     *_page_table)
    : base_address(_base_address), size(_size),
      frame_pool(_frame_pool), page_table(_page_table),
      allocated_count(0), free_count(0)
{
    // must register before touching pool memory
    page_table->register_pool(this);

    // metadata arrays live in the first pool page
    allocated_regions = (Region *) base_address;
    free_regions       = (Region *)(base_address + MAX_REGIONS * sizeof(Region));

    // first page holds metadata — mark it allocated
    allocated_regions[0].base_page  = base_address >> 12;
    allocated_regions[0].page_count = 1;
    allocated_count = 1;

    // rest of pool is free
    free_regions[0].base_page  = (base_address >> 12) + 1;
    free_regions[0].page_count = (size >> 12) - 1;
    free_count = 1;

    Console::puts("Constructed VMPool object.\n");
}

unsigned long VMPool::allocate(unsigned long _size) {
    unsigned long pages = (_size + 4095) >> 12; // round up

    for (int i = 0; i < free_count; i++) {
        if (free_regions[i].page_count >= pages) {
            unsigned long start_page = free_regions[i].base_page;

            assert(allocated_count < MAX_REGIONS);
            allocated_regions[allocated_count].base_page  = start_page;
            allocated_regions[allocated_count].page_count = pages;
            allocated_count++;

            // shrink or remove free region
            free_regions[i].base_page  += pages;
            free_regions[i].page_count -= pages;
            if (free_regions[i].page_count == 0) {
                free_regions[i] = free_regions[--free_count];
            }

            Console::puts("Allocated region of memory.\n");
            return start_page << 12; // frames allocated lazily on fault
        }
    }

    Console::puts("VMPool::allocate: out of virtual memory!\n");
    return 0;
}

void VMPool::release(unsigned long _start_address) {
    unsigned long start_page = _start_address >> 12;

    for (int i = 0; i < allocated_count; i++) {
        if (allocated_regions[i].base_page == start_page) {
            unsigned long count = allocated_regions[i].page_count;

            for (unsigned long p = 0; p < count; p++) {
                page_table->free_page(start_page + p);
            }

            assert(free_count < MAX_REGIONS);
            free_regions[free_count].base_page  = start_page;
            free_regions[free_count].page_count = count;
            free_count++;

            // replace with last entry to compact
            allocated_regions[i] = allocated_regions[--allocated_count];

            Console::puts("Released region of memory.\n");
            return;
        }
    }

    Console::puts("VMPool::release: address not found in allocated list!\n");
}

bool VMPool::is_legitimate(unsigned long _address) {
    // range check only — walking allocated_regions would fault during bootstrap
    return (_address >= base_address) && (_address < base_address + size);
}
