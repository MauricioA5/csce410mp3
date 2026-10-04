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

   Console::puts("Initialized Paging System");
}

PageTable::PageTable()
{
   // Allocate frame for page directory
   unsigned long pd_frame = kernel_mem_pool->get_frames(1);
   page_directory = (unsigned long *)(pd_frame * PAGE_SIZE);

   // Allocate frame for first page table
   unsigned long pt_frame = kernel_mem_pool->get_frames(1);
   unsigned long *first_page_table = (unsigned long *)(pt_frame * PAGE_SIZE);

   // Mark all page directory entries as not present
   for(unsigned int i = 0; i < ENTRIES_PER_PAGE; i++)
   {
      page_directory[i] = 0x2;
   }

   // Map 4MB
   for(unsigned int i = 0; i < ENTRIES_PER_PAGE; i++)
   {
      first_page_table[i] = (i * PAGE_SIZE) | 0x3;
   }

   // Directory entry 0 points to our first page table
   page_directory[0] = (pt_frame * PAGE_SIZE) | 0x3;

   Console::puts("Constructed Page Table object\n");
}


void PageTable::load()
{
   current_page_table = this;
   write_cr3((unsigned long) page_directory);
   Console::puts("Loaded page table\n");
}

void PageTable::enable_paging()
{
   unsigned long cr0 = read_cr0();

   cr0 |= 0x80000000;

   write_cr0(cr0);
   paging_enabled = 1;
   Console::puts("Enabled paging");
}

void PageTable::handle_fault(REGS * _r)
{
   // Find the faulting address
   unsigned long fault_address = read_cr2();

   unsigned long directory_index = fault_address >> 22;
   unsigned long table_index = (fault_address >> 12) & 0x3FF;

   PageTable *pt = current_page_table;

   // Does the required page table exist?
   if ((pt->page_directory[directory_index] & 0x1) == 0)
   {
      // Allocate a frame for a new page table
      unsigned long new_pt_frame = kernel_mem_pool->get_frames(1);
      unsigned long *new_page_table =
         (unsigned long *)(new_pt_frame * PAGE_SIZE);

      // Initialize entries as writable, but not present
      for(unsigned int i = 0; i < ENTRIES_PER_PAGE; i++)
      {
         new_page_table[i] = 0x2;
      }

      // Install the new page table into the directory
      pt->page_directory[directory_index] =
         (new_pt_frame * PAGE_SIZE) | 0x3;
   }

   // Locate the page table
   unsigned long *page_table = (unsigned long *)
      (pt->page_directory[directory_index] & 0xFFFFF000);

   // Allocate the process page
   unsigned long page_frame = process_mem_pool->get_frames(1);

   // Map the faulting virtual page to it
   page_table[table_index] = (page_frame * PAGE_SIZE) | 0x3;
}