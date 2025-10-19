#include <mmu.h>
#include <memlayout.h>

    .section .text,"ax",%progbits
    .globl kern_entry
kern_entry:
    # a0: hartid
    # a1: dtb physical address
    # save hartid and dtb address
    la t0, boot_hartid
    sd a0, 0(t0)
    la t0, boot_dtb
    sd a1, 0(t0)

    # t0 := 三级页表的虚拟地址
    lui     t0, %hi(boot_page_table_sv39)
    # t1 := 0xffffffff40000000 即虚实映射偏移量
    li      t1, 0xffffffffc0000000 - 0x80000000
    # t0 减去虚实映射偏移量 0xffffffff40000000，变为三级页表的物理地址
    sub     t0, t0, t1
    # t0 >>= 12，变为三级页表的物理页号
    srli    t0, t0, 12

    # t1 := 8 << 60，设置 satp 的 MODE 字段为 Sv39
    li      t1, 8 << 60
    # 将刚才计算出的预设三级页表物理页号附加到 satp 中
    or      t0, t0, t1
    # 将算出的 t0(即新的MODE|页表基址物理页号) 覆盖到 satp 中
    csrw    satp, t0
    
    # 使用 sfence.vma 指令刷新 TLB
    sfence.vma
    # 从此，我们给内核搭建出了一个完美的虚拟内存空间！
    #nop # 可能映射的位置有些bug。。插入一个nop
    
    # 我们在虚拟内存空间中：随意将 sp 设置为虚拟地址！
    lui sp, %hi(bootstacktop)

    # 我们在虚拟内存空间中：随意跳转到虚拟地址！
    # 跳转到 kern_init
    lui t0, %hi(kern_init)
    addi t0, t0, %lo(kern_init)
    jr t0

.section .data
    # .align 2^12
    .align PGSHIFT
    .global bootstack
bootstack:
    .space KSTACKSIZE
    .global bootstacktop
bootstacktop:

# 用于内核自举的顶级页目录表boot_page_table_sv39
.section .data
    # 由于我们要把这个页表放到一个页里面，因此必须 12 位对齐
    .align PGSHIFT
    .global boot_page_table_sv39
# 分配 4KiB 内存给预设的三级页表
boot_page_table_sv39:
    # 0xffffffff_c0000000 map to 0x80000000 (1G)
    # 前 511 个页表项均设置为 0 ，因此 V=0 ，意味着是空的(unmapped)
    # 虚拟地址空间的低区部分（绝大部分地址）目前是完全未映射的
    .zero 8 * 511
    # 设置最后一个页表项，PPN=0x80000，标志位 VRWXAD 均为 1
    .quad (0x80000 << 10) | 0xcf # VRWXAD
    # 0x80000: 这是我们想要映射到的目标物理页号 (PPN)。它对应物理地址 0x80000000 (因为 0x80000000 >> 12 = 0x80000)。
    # << 10: 在 RISC-V 的页表项 (PTE) 结构中，PPN 字段是从第 10 位开始的。所以我们将 PPN 的值左移 10 位，把它放到正确的位置上。
    # 0xcf: 这是标志位。0xcf 的二进制是 1100 1111。从右到左分别对应：
    # V=1 (Valid): 该表项有效。
    # R=1 (Read): 可读。
    # W=1 (Write): 可写。
    # X=1 (Execute): 可执行。
    # U=0 (User): 用户态不可访问（这是内核映射）。
    # G=0 (Global): 非全局。
    # A=1 (Accessed): 访问位置 1。
    # D=1 (Dirty): 脏位置 1。
    # 重要特性：因为 R, W, X 位不全为 0，这个顶级页目录表项被硬件解释为一个叶子节点，它直接映射一个 1GB 大小的大大页 (Gigapage)。
    # 它建立的映射关系是：虚拟地址区间 [0xffffffff_c0000000, ...) 映射到物理地址区间 [0x80000000, ...)。我们的内核正好位于这个区间内。

    .global boot_hartid
    #  CPU 核心号
boot_hartid:
    .quad 0
    .global boot_dtb
    # 设备树 (DTB) 的物理地址
boot_dtb:
    .quad 0
