#ifndef __KERN_MM_PMM_H__
#define __KERN_MM_PMM_H__

#include <assert.h>
#include <defs.h>
#include <memlayout.h>
#include <mmu.h>
#include <riscv.h>

// pmm_manager is a physical memory management class. A special pmm manager -
// XXX_pmm_manager
// only needs to implement the methods in pmm_manager class, then
// XXX_pmm_manager can be used
// by ucore to manage the total physical memory space.
struct pmm_manager {
    const char *name;  // XXX_pmm_manager's name
    void (*init)(
        void);  // initialize internal description&management data structure
                // (free block list, number of free block) of XXX_pmm_manager
    void (*init_memmap)(
        struct Page *base,
        size_t n);  // setup description&management data structcure according to
                    // the initial free physical memory space
    struct Page *(*alloc_pages)(
        size_t n);  // allocate >=n pages, depend on the allocation algorithm
    void (*free_pages)(struct Page *base, size_t n);  // free >=n pages with
                                                      // "base" addr of Page
                                                      // descriptor
                                                      // structures(memlayout.h)
    size_t (*nr_free_pages)(void);  // return the number of free pages
    void (*check)(void);            // check the correctness of XXX_pmm_manager
};
/*
定义了一个物理内存管理器的“标准”或“模板”。
这个结构体本身不包含任何实现，而是包含了一组函数指针。
它规定了任何一个具体的物理内存分配算法（比如 First-Fit, Best-Fit）都必须提供这 6 个标准函数，
才能被操作系统内核集成和使用。
*/

extern const struct pmm_manager *pmm_manager;

void pmm_init(void);

struct Page *alloc_pages(size_t n);
void free_pages(struct Page *base, size_t n);
size_t nr_free_pages(void); // number of free pages

#define alloc_page() alloc_pages(1)
#define free_page(page) free_pages(page, 1)


/* *
 * PADDR - takes a kernel virtual address (an address that points above
 * KERNBASE),
 * where the machine's maximum 256MB of physical memory is mapped and returns
 * the
 * corresponding physical address.  It panics if you pass it a non-kernel
 * virtual address.
 * */
#define PADDR(kva)                                                 \
    ({                                                             \
        uintptr_t __m_kva = (uintptr_t)(kva);                      \
        if (__m_kva < KERNBASE) {                                  \
            panic("PADDR called with invalid kva %08lx", __m_kva); \
        }                                                          \
        __m_kva - va_pa_offset;                                    \
    })

/* *
 * KADDR - takes a physical address and returns the corresponding kernel virtual
 * address. It panics if you pass an invalid physical address.
 * */
/*
#define KADDR(pa)                                                \
    ({                                                           \
        uintptr_t __m_pa = (pa);                                 \
        size_t __m_ppn = PPN(__m_pa);                            \
        if (__m_ppn >= npage) {                                  \
            panic("KADDR called with invalid pa %08lx", __m_pa); \
        }                                                        \
        (void *)(__m_pa + va_pa_offset);                         \
    })
*/
extern struct Page *pages;//指向 struct Page 结构体数组的起始地址
extern size_t npage;//物理内存中的总页数
extern const size_t nbase;//物理内存起始地址 (0x80000000) 对应的页号
extern uint64_t va_pa_offset;//虚实地址映射的固定偏移量

// 将一个 struct Page 结构体的指针转换为它所代表的物理页的物理页号 (ppn)
static inline ppn_t page2ppn(struct Page *page) { return page - pages + nbase; }

// 将 struct Page 指针转换为物理地址 (pa)
static inline uintptr_t page2pa(struct Page *page) {
    return page2ppn(page) << PGSHIFT;
}
// 将物理地址 (pa) 转换为 struct Page 指针
static inline struct Page *pa2page(uintptr_t pa) {
    if (PPN(pa) >= npage) {
        panic("pa2page called with invalid pa");
    }
    return &pages[PPN(pa) - nbase];
}


// 用于操作 struct Page 的 ref (引用计数) 字段
static inline int page_ref(struct Page *page) { return page->ref; }
static inline void set_page_ref(struct Page *page, int val) { page->ref = val; }
static inline int page_ref_inc(struct Page *page) {
    page->ref += 1;
    return page->ref;
}
static inline int page_ref_dec(struct Page *page) {
    page->ref -= 1;
    return page->ref;
}



// 当内核修改了页表之后，必须调用这个函数来刷新 TLB（地址翻译高速缓存），以确保 CPU 使用的是最新的页表映射关系。
// asm volatile 告诉编译器不要优化或移动这条内联汇编指令。
static inline void flush_tlb() { asm volatile("sfence.vm"); }


extern char bootstack[], bootstacktop[]; // defined in entry.S

#endif /* !__KERN_MM_PMM_H__ */
