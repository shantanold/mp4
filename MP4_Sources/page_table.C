#include "assert.H"
#include "exceptions.H"
#include "console.H"
#include "paging_low.H"
#include "page_table.H"

PageTable * PageTable::current_page_table = nullptr;
unsigned int PageTable::paging_enabled = 0;
ContFramePool * PageTable::kernel_mem_pool = nullptr;
ContFramePool * PageTable::process_mem_pool = nullptr;
unsigned long PageTable::shared_size = 0;



void PageTable::init_paging(ContFramePool * _kernel_mem_pool,
                            ContFramePool * _process_mem_pool,
                            const unsigned long _shared_size)
{
   kernel_mem_pool = _kernel_mem_pool;
   process_mem_pool = _process_mem_pool;
   shared_size = _shared_size;
   Console::puts("Initialized Paging System\n");
}

PageTable::PageTable()
{
   // Allocate the page directory from the process frame pool (Part I change)
   unsigned long pd_frame = process_mem_pool->get_frames(1);
   page_directory_frame = pd_frame;
   page_directory = (unsigned long *)(pd_frame << 12);

   // Clear the entire page directory
   for (unsigned int i = 0; i < ENTRIES_PER_PAGE; i++) {
      page_directory[i] = 0;
   }

   // Identity-map the first shared_size bytes using the kernel pool
   unsigned long bytes_per_pt = ENTRIES_PER_PAGE * PAGE_SIZE;  // 4 MB per page table
   unsigned long num_page_tables = (shared_size + bytes_per_pt - 1) / bytes_per_pt;

   for (unsigned long pt_idx = 0; pt_idx < num_page_tables; pt_idx++) {
      unsigned long pt_frame = kernel_mem_pool->get_frames(1);
      unsigned long *pt = (unsigned long *)(pt_frame << 12);

      unsigned long base_page = pt_idx * ENTRIES_PER_PAGE;
      for (unsigned int i = 0; i < ENTRIES_PER_PAGE; i++) {
         unsigned long frame_no = base_page + i;
         pt[i] = (frame_no << 12) | 3;
      }

      page_directory[pt_idx] = (pt_frame << 12) | 3;
   }

   // Recursive mapping: entry 1023 points to the page directory itself
   page_directory[1023] = (pd_frame << 12) | 0x3;

   Console::puts("Constructed Page Table object\n");
}


void PageTable::load()
{
   current_page_table = this;
   write_cr3(page_directory_frame << 12);
   Console::puts("Loaded page table\n");
}

void PageTable::enable_paging()
{
   unsigned long cr0 = read_cr0();
   cr0 |= (1 << 31);   // PG bit = enable paging
   write_cr0(cr0);
   paging_enabled = 1;
   Console::puts("Enabled paging\n");
}

unsigned long* PageTable::PDE_address(unsigned long addr)
{
   // Bits 31-22 of addr select the PDE index.
   // Logical address: | 1023 | 1023 | pde_index*4 |
   // = 0xFFFFF000 | (pde_index << 2)
   unsigned long pde_index = addr >> 22;
   return (unsigned long*)(0xFFFFF000 | (pde_index << 2));
}

unsigned long* PageTable::PTE_address(unsigned long addr)
{
   // Logical address: | 1023 | pde_index | pte_index*4 |
   // = 0xFFC00000 | (pde_index << 12) | (pte_index << 2)
   unsigned long pde_index = addr >> 22;
   unsigned long pte_index = (addr >> 12) & 0x3FF;
   return (unsigned long*)(0xFFC00000 | (pde_index << 12) | (pte_index << 2));
}

void PageTable::handle_fault(REGS * _r)
{
   unsigned long fault_addr = read_cr2();
   PageTable* cur = current_page_table;

   unsigned long* pde = cur->PDE_address(fault_addr);
   if (!(*pde & 0x1)) {
      // Page table page not present — allocate from process pool
      unsigned long new_pt_frame = process_mem_pool->get_frames(1);
      assert(new_pt_frame != 0);
      *pde = (new_pt_frame << 12) | 0x3;

      // Zero the new page table page via its recursive logical address
      unsigned long pde_index = fault_addr >> 22;
      unsigned long* pt_page = (unsigned long*)(0xFFC00000 | (pde_index << 12));
      for (int i = 0; i < 1024; i++) {
         pt_page[i] = 0;
      }
   }

   unsigned long* pte = cur->PTE_address(fault_addr);
   if (!(*pte & 0x1)) {
      unsigned long new_frame = process_mem_pool->get_frames(1);
      assert(new_frame != 0);
      *pte = (new_frame << 12) | 0x3;
   }
}

