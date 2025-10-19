#ifndef __KERN_MM_MEMLAYOUT_H__
#define __KERN_MM_MEMLAYOUT_H__

/* All physical memory mapped at this address */
#define KERNBASE            0xFFFFFFFFC0200000 // = 0x80200000(物理内存里内核的起始位置, KERN_BEGIN_PADDR) + 0xFFFFFFFF40000000(偏移量, PHYSICAL_MEMORY_OFFSET)
//把原有内存映射到虚拟内存空间的最后一页
#define KMEMSIZE            0x7E00000          // the maximum amount of physical memory
// 0x7E00000 = 0x8000000 - 0x200000
// 在 QEMU 模拟环境中，物理内存（DRAM）从 0x80000000 开始，默认大小为 128MB (0x8000000)。
// 但是，从 0x80000000 到 0x80200000 这 2MB 的空间被 Bootloader (OpenSBI) 占用了。
// 因此，真正可供内核使用的物理内存大小是 128MB - 2MB，即 0x7E00000 字节。
#define KERNTOP             (KERNBASE + KMEMSIZE) // 0x88000000对应的虚拟地址
// 定义了内核所映射的物理内存区域在虚拟地址空间中的顶部（结束地址），对应着物理内存的末尾
// KERNBASE 到 KERNTOP 这个虚拟地址范围，就是整个物理内存（除 OpenSBI 占用部分外）在内核虚拟空间中的一个线性映射窗口

#define PHYSICAL_MEMORY_END         0x88000000
#define PHYSICAL_MEMORY_OFFSET      0xFFFFFFFF40000000
#define KERNEL_BEGIN_PADDR          0x80200000
#define KERNEL_BEGIN_VADDR          0xFFFFFFFFC0200000


#define KSTACKPAGE          2                           // # of pages in kernel stack，内核栈占用 2 个物理页
#define KSTACKSIZE          (KSTACKPAGE * PGSIZE)       // sizeof kernel stack，在多线程/多核环境中，每个核心或线程都会有自己的内核栈。

#ifndef __ASSEMBLER__

#include <defs.h>
#include <list.h>

typedef uintptr_t pte_t;//页表项
typedef uintptr_t pde_t;//页目录表项

/* *
 * struct Page - Page descriptor structures. Each Page describes one
 * physical page. In kern/mm/pmm.h, you can find lots of useful functions
 * that convert Page to other data types, such as physical address.
 * */
struct Page {
    int ref;                        // page frame's reference counter
    // 引用计数。记录有多少个地方（比如多少个页表项）正在使用这个物理页。当 ref 降为 0 时，说明这个页可以被回收了
    uint64_t flags;                 // array of flags that describe the status of the page frame
    // 状态标志位。用一个 64 位整数的每一个 bit 来存储一个布尔状态，比如页面是否被保留 (PG_reserved)，是否是空闲块的头 (PG_property)。
    unsigned int property;          // the num of free block, used in first fit pm manager
    // 属性。这个字段的含义是灵活的，取决于具体的分配算法。在 First-Fit 算法中，它被用来存储从此页开始的连续空闲页的数量。
    list_entry_t page_link;         // free list link
    // 链表节点。这是一个 list_entry_t 类型的成员，它是一个“钩子”，使得这个 Page 结构体可以被串联进一个双向链表中（比如空闲页链表）。
};

/* Flags describing the status of a page frame */
#define PG_reserved                 0       // if this bit=1: the Page is reserved for kernel, cannot be used in alloc/free_pages; otherwise, this bit=0 
// 如果 bit 0 的值是 1，表示这个物理页被内核保留了（比如内核代码、页表等占用的页面），物理内存分配器不能把它分配出去。
// 如果 bit 0 的值是 0，表示这个页面没有被保留，可以参与正常的分配和释放流程。
#define PG_property                 1       // if this bit=1: the Page is the head page of a free memory block(contains some continuous_addrress pages), and can be used in alloc_pages; if this bit=0: if the Page is the the head page of a free memory block, then this Page and the memory block is alloced. Or this Page isn't the head page.
// 如果 bit 1 的值是 1，表示这个物理页是一个连续空闲内存块的第一个页（头页）。它的 property 字段会记录这个空闲块的总大小。只有这样的头页才会出现在 free_list 链表中。
// 如果 bit 1 的值是 0，有两种可能：这个页已经被分配出去了或者这个页是空闲的，但它不是空闲块的头页

#define SetPageReserved(page)       ((page)->flags |= (1UL << PG_reserved))//PG_reserved=1
#define ClearPageReserved(page)     ((page)->flags &= ~(1UL << PG_reserved))//PG_reserved=0
#define PageReserved(page)          (((page)->flags >> PG_reserved) & 1)//返回page的PG_reserved状态
#define SetPageProperty(page)       ((page)->flags |= (1UL << PG_property))//PG_property=1
#define ClearPageProperty(page)     ((page)->flags &= ~(1UL << PG_property))//PG_property=0
#define PageProperty(page)          (((page)->flags >> PG_property) & 1)//返回page的PG_property状态

// convert list entry to page
#define le2page(le, member)                 \
    to_struct((le), struct Page, member)

/* free_area_t - maintains a doubly linked list to record free (unused) pages */
typedef struct {
    list_entry_t free_list;         // the list header
    unsigned int nr_free;           // number of free pages in this free list
} free_area_t;//空闲链表的管理器

#endif /* !__ASSEMBLER__ */

#endif /* !__KERN_MM_MEMLAYOUT_H__ */
