#ifndef __KERN_MM_MMU_H__
#define __KERN_MM_MMU_H__

#ifndef __ASSEMBLER__
#include <defs.h>
#endif


#define PGSIZE          4096                    // bytes mapped by a page
#define PGSHIFT         12                      // log2(PGSIZE)

// physical/virtual page number of address
#define PPN(la) (((uintptr_t)(la)) >> PGSHIFT)

// Sv39 linear address structure
// +-------9--------+-------9--------+--------9---------+----------12----------+
// |      VPN2      |      VPN1      |       VPN0       |  Offset within Page  |
// +----------------+----------------+------------------+----------------------+

// Sv39 in RISC-V64 uses 39-bit virtual address to access 56-bit physical address!
// Sv39 page table entry:
// +-------10--------+--------26-------+--------9----------+--------9--------+---2----+-------8-------+
// |    Reserved     |      PPN[2]     |      PPN[1]       |      PPN[0]     |Reserved|D|A|G|U|X|W|R|V|
// +-----------------+-----------------+-------------------+-----------------+--------+---------------+

/* page directory and page table constants */
#define SV39_NENTRY          512                     // page directory entries per page directory，每个页目录表有512个页目录表项

#define SV39_PGSIZE          4096                    // bytes mapped by a page
#define SV39_PGSHIFT         12                      // log2(PGSIZE)
#define SV39_PTSIZE          (PGSIZE * SV39_NENTRY)   // bytes mapped by a page directory entry
#define SV39_PTSHIFT         21                      // log2(PTSIZE)

#define SV39_VPN0SHIFT       12                      // offset of VPN0 in a linear address
#define SV39_VPN1SHIFT       21                      // offset of VPN1 in a linear address
#define SV39_VPN2SHIFT       30                      // offset of VPN2 in a linear address
#define SV39_PTE_PPN_SHIFT   10                      // offset of PPN in a physical address， PPN 字段在 PTE 中开始的位偏移

// 用于从一个虚拟地址 la 中提取出特定级别的 VPN，0x1FF二进制是9个1，对应9位
#define SV39_VPN0(la) ((((uintptr_t)(la)) >> SV39_VPN0SHIFT) & 0x1FF)
#define SV39_VPN1(la) ((((uintptr_t)(la)) >> SV39_VPN1SHIFT) & 0x1FF)
#define SV39_VPN2(la) ((((uintptr_t)(la)) >> SV39_VPN2SHIFT) & 0x1FF)
// 更通用的版本，可以通过参数 n (0, 1, 或 2) 来提取任意级别的 VPN
#define SV39_VPN(la, n) ((((uintptr_t)(la)) >> 12 >> (9 * n)) & 0x1FF)

// construct linear address from indexes and offset
// 反向操作的宏，用于根据三级 VPN 和页内偏移 o 来构建一个完整的虚拟地址
#define SV39_PGADDR(v2, v1, v0, o) ((uintptr_t)((v2) << SV39_VPN2SHIFT | (v1) << SV39_VPN1SHIFT | (v0) << SV39_VPN0SHIFT | (o)))

// address in page table or page directory entry
#define SV39_PTE_ADDR(pte)   (((uintptr_t)(pte) & ~0x1FF) << 3)
// 从一个页目录表项 pte 中提取出它所指向的下一级页表的物理地址？？？？？

// 3-level pagetable
#define SV39_PT0                 0
#define SV39_PT1                 1
#define SV39_PT2                 2

// page table entry (PTE) fields，每个宏对应一个独立的二进制位
#define PTE_V     0x001 // Valid
#define PTE_R     0x002 // Read
#define PTE_W     0x004 // Write
#define PTE_X     0x008 // Execute
#define PTE_U     0x010 // User
#define PTE_G     0x020 // Global
#define PTE_A     0x040 // Accessed
#define PTE_D     0x080 // Dirty
#define PTE_SOFT  0x300 // Reserved for Software

#define PAGE_TABLE_DIR (PTE_V)// 一个指向下一级页表的页目录项的标志，此时 R, W, X 位必须都为 0，只有 V 位为 1
#define READ_ONLY (PTE_R | PTE_V)// 只读数据段 (.rodata)
#define READ_WRITE (PTE_R | PTE_W | PTE_V)//可读写数据段 (.data)
#define EXEC_ONLY (PTE_X | PTE_V)// 只执行代码段 (.text)
#define READ_EXEC (PTE_R | PTE_X | PTE_V)// 可读可执行代码段 (.text)
#define READ_WRITE_EXEC (PTE_R | PTE_W | PTE_X | PTE_V)//可读可写可执行代码段 (.text)

#define PTE_USER (PTE_R | PTE_W | PTE_X | PTE_U | PTE_V)// 用户态可读写可执行

#endif /* !__KERN_MM_MMU_H__ */

