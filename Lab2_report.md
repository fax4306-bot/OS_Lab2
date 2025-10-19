# 小组成员：付子璇2313715、郝亚蕾2313591、刘玥2312123
# 1.练习1：**理解 First-fit 连续物理内存分配算法**

在 Lab2 中操作系统初步实现了虚拟内存管理，它主要包括两部分，一是成功地为内核自身创建并启用了一个初始的高位虚拟地址空间，并从此在 MMU 开启的虚拟地址模式下运行；然而，这个初始的内核空间只是解决了内核自身的生存问题，为了未来能够创建和管理多个隔离的用户进程，操作系统必须具备一个更基础的能力即第二部分——管理整个物理内存资源。

Lab 2 的代码框架为此引入了物理内存管理器 PMM 的概念，并提供了一个基于 First-Fit 算法的默认实现 `default_pmm.c`，它为内核提供了追踪、分配和回收以页为单位的物理内存的基本功能，下面将以此为基础展开分析：

### PMM**初始化**（`default_init`）

PMM 的生命周期始于 `default_init()`​ 函数，此函数在系统启动早期被调用，其唯一的职责是通过 `list_init(&free_list)`​ 将 `free_list`​ 初始化为一个空链表，并将空闲页计数器 `nr_free` 置零，这个过程为后续的内存管理建立了一个干净的初始状态。

### 连续可用物理内存登记（`default_init_memmap`）

当内核的早期初始化代码 `page_init`​ 探测到一块连续可用的物理内存区域后，PMM 的 `default_init_memmap(struct Page *base, size_t n)`​ 函数被调用，以将这块内存正式纳入管理。该函数首先遍历这 `n`​ 个页对应的 `struct Page`​ 结构体，将它们的 `flags`​ 和 `property`​ 字段清零，确保它们处于一个干净的空闲状态。随后，它采用头页标记法来管理这个新的空闲块：仅在第一个页 `base`​ 的 `property`​ 字段中记录整个块的大小 `n`​，并通过 `SetPageProperty(base)`​ 设置其标志位，以标识它是一个有效空闲块的起始。最后，这个由 `base`​ 代表的新空闲块被按地址顺序插入到 `free_list` 中，完成了内存的初始登记。

```c
// in default_init_memmap
base->property = n;
SetPageProperty(base);
nr_free += n;
// ... (ordered insertion into free_list)
```

这个初始化的设计虽然是在内核空间部分完成，但其根本目的是为了整个系统的未来做准备。通过建立一个有序的、包含所有可用物理内存的 `free_list`，它为后续的用户空间创建、内核动态数据结构（如进程控制块、文件描述符表）的分配提供了唯一的物理内存来源。

‍

### 分配逻辑：首次适应策略（`default_alloc_pages`）

内存的分配由 `default_alloc_pages(size_t n)`​ 函数实现，当接收到一个分配 `n`​ 个连续页的请求时，函数会从 `free_list` 的头部开始进行线性扫描。

```c
// in default_alloc_pages()
list_entry_t *le = &free_list;
while ((le = list_next(le)) != &free_list) {
    struct Page *p = le2page(le, page_link);
    if (p->property >= n) {
        page = p;
        break; // Found the first fit
    }
}
```

在遍历过程中，它会检查每个空闲块头页 `p`​ 的 `p->property`​ 值，一旦找到第一个大小不小于 `n`​ 的块，搜索便立即停止。如果找到了这样的块 `page`​，它将被从 `free_list`​ 中移除。若 `page->property`​ 严格大于 `n`​，则该块会被分裂：前 `n`​ 页被标记为已分配并返回给调用者；剩余的 `page->property - n`​ 页则被构造成一个新的、更小的空闲块，并重新插入回 `free_list`​，这个分裂操作是连续内存管理中平衡空间利用率和分配开销的关键。`default_alloc_pages`​ 的返回值是指向被分配区域第一个页的 `Page` 结构体指针，后续的内核模块将通过它来构建页表，完成虚拟地址到该物理地址的映射。

### 回收逻辑：合并以减轻碎片化（`default_free_pages`）

内存的回收由 `default_free_pages(struct Page *base, size_t n)`​ 函数负责。它的核心目标不仅是归还内存，更重要的是对抗外部碎片。函数首先将被归还的内存块格式化为一个新的空闲块，并按地址顺序插入 `free_list`，然后它会执行合并操作。

```c
// in default_free_pages(), forward coalescing
le = list_next(&(base->page_link));
if (le != &free_list) {
    p = le2page(le, page_link);
    if (base + base->property == p) { // Check for physical adjacency
        base->property += p->property;
        ClearPageProperty(p);
        list_del(&(p->page_link));
    }
}
```

它检查新插入块在链表中的前一个和后一个邻居，并通过指针运算判断它们在物理地址上是否紧密相连。如果是，则将相邻的空闲块合并成一个更大的连续空闲块，并相应地更新头页的 `property` 和链表结构。这个合并机制对于维持系统中大块连续内存的可用性至关重要，直接影响到未来大内存请求能否被成功满足。

### 在**框架中的作用与改进空间**

​`default_pmm_manager`​ 在本次实验中扮演着基础物理内存服务提供者的角色。它在内核初始化期间的首要任务是通过自检函数 `default_check`​ 完成功能验证，以确保自身分配和回收逻辑的可靠性。在此之后，它便作为未来所有动态内存需求的基石而存在。虽然在当前实验中未被后续流程主动调用，但其建立的 `alloc_pages`​ 和 `free_pages` 接口是未来实现用户空间（包括创建进程页表、加载程序、响应缺页异常等核心功能）所不可或缺的底层支撑。

尽管此 First-Fit 实现功能完备，但作为一个基础模型，其在性能和碎片管理上均存在明确的改进方向。

在性能上，当前 O(N) 复杂度的线性扫描是主要瓶颈。一个根本性的改进是采用分离适配思想，通过维护多个按大小分类的空闲链表，使分配请求能直接定位到尺寸合适的链表，从而将查找复杂度显著降低，大幅提升分配效率。

在碎片管理上，当前固定的立即合并策略在高并发场景下可能成为开销。可以引入更具弹性的延迟合并机制，根据系统实时的碎片化程度动态决定何时触发整理，这种自适应策略能在系统负载与碎片整理收益之间取得更好的平衡。

# 2.练习2：实现 Best-Fit 连续物理内存分配算法

为了在 First-Fit 算法框架的基础上实现 Best-Fit 算法，我们对二者的逻辑与实现进行了以下分析：

### First-Fit 与 Best-Fit 共享的逻辑

Best-Fit 算法与 First-Fit 算法共享了相同的基础数据结构：一个按物理地址升序排列的双向链表 `free_list`，用于管理所有空闲内存块。因此，负责维护这个数据结构完整性和有序性的函数，其实现逻辑是相同的，因为它们的功能与如何选择一个空闲块的策略是解耦的。

具体来说，以下函数无需修改：

- ​`best_fit_init()`​: 与 `default_init`​ 相同，其职责仅是初始化一个空的 `free_list` 和计数器。
- ​`best_fit_init_memmap()`​: 与 `default_init_memmap`​ 相同，其职责是将一块新的内存区域作为一个整体，按地址顺序插入到 `free_list` 中。
- ​`best_fit_free_pages()`​: 与 `default_free_pages`​ 相同，其职责是将被回收的块按地址顺序插回 `free_list`，并执行向前和向后合并以减少碎片，内存的合并只依赖于物理地址的邻接关系，与分配策略无关。

### **核心差异：分配策略的修改 (**​**​`best_fit_alloc_pages`​**​ **)**

两种算法的根本区别在于 `alloc_pages` 函数的实现：First-Fit 采用贪心策略，选择它遇到的第一个足够大的块；而 Best-Fit 则需要做出全局最优选择，即找到所有足够大的块中尺寸最小的那一个。

**实现路径的改变：**

​`best_fit_alloc_pages` 函数的实现路径必须从找到即停的线性扫描，改为完整遍历以寻找最优的流程。具体实现如下：

1. 必须完整遍历整个 `free_list`​，不能在找到第一个满足条件的块后就提前 `break`。
2. 在遍历过程中，需要引入两个变量来追踪到目前为止的最佳选择：一个指针 `page`​ 用于记录最佳块的头页，一个变量 `min_size`​ 用于记录该最佳块的大小。`min_size`​ 初始时被设置为一个不可能达到的极大值（如 `nr_free + 1`）。
3. 在循环中，对于每一个大小 `p->property`​ 不小于请求大小 `n`​ 的空闲块 `p`​，都将其与 `min_size`​ 进行比较。如果 `p->property < min_size`​，则说明找到了一个更贴合的块，此时就更新 `page = p`​ 和 `min_size = p->property`。
4. 循环结束后，`page`​ 指针所指向的就是整个 `free_list` 中最适合本次分配的块。后续的分配和分裂逻辑（从链表中移除、处理剩余部分、更新计数器）则与 First-Fit 完全相同。

**核心代码差异对比：**

以下代码展示了 `default_alloc_pages`​ 和 `best_fit_alloc_pages` 在查找逻辑上的不同。

**First-Fit 实现 (**​**​`default_pmm.c`​**​ **)** :

```c
static struct Page *
default_alloc_pages(size_t n) {
    // ... (前置检查)
    struct Page *page = NULL;
    list_entry_t *le = &free_list;
    
    while ((le = list_next(le)) != &free_list) {
        struct Page *p = le2page(le, page_link);
        if (p->property >= n) {
            page = p;
            break; // 关键：找到第一个满足条件的块就立即中断循环
        }
    }

    // ... (后续的分裂和更新逻辑)
    return page;
}
```

**Best-Fit 实现 (**​**​`best_fit_pmm.c`​**​ **)** :

```c
static struct Page *
best_fit_alloc_pages(size_t n) {
    // ... (前置检查)
    struct Page *page = NULL;
    list_entry_t *le = &free_list;
    size_t min_size = nr_free + 1;

    // 关键：完整遍历链表，不提前中断
    while ((le = list_next(le)) != &free_list) {
        struct Page *p = le2page(le, page_link);
        if (p->property >= n) {
            // 关键：如果当前块更优（更小），则更新记录
            if (p->property < min_size) {
                min_size = p->property;
                page = p;
            }
        }
    }

    // ... (后续的分裂和更新逻辑，与 First-Fit 相同)
    return page;
}
```

### Best-Fit 算法的改进空间

虽然 Best-Fit 算法通过选择最紧凑的空闲块来优化内存使用，旨在保留更大的连续内存区域，但其自身的设计也引入了性能瓶颈和特定的碎片化问题。当前基于单一链表的实现，存在以下几个核心的改进方向。

**在性能方面，最显著的瓶颈是其 O(N) 的分配查找复杂度。**  `best_fit_alloc_pages`​ 函数为了找到最佳匹配，必须完整遍历 `free_list`​ 上的所有空闲块。当系统长时间运行后，内存碎片增多，`free_list` 的长度会显著增加，导致每次内存分配的开销都很大。一个根本性的性能优化是采用分离适配的数据结构。通过维护一个按大小（例如，按2的幂次）分类的链表数组，分配请求可以直接定位到尺寸最接近的链表进行查找，甚至在找不到精确匹配时，只需向上查找更大的链表。这种方式避免了对大量不相关尺寸碎片的无效扫描，能将平均查找复杂度显著降低。

**在碎片管理方面，Best-Fit 策略自身倾向于产生大量难以利用的微小碎片。**  由于总是选择最贴合的块，一个大小为 N 的块在满足一个大小为 N-1 的请求后，会留下一个大小仅为 1 的碎片。这些微小的内存块在系统中累积，虽然总空闲内存可能很多，但都无法满足哪怕是最小的分配请求。对此，一个更先进的替代方案是伙伴系统。它将所有内存块的大小约束为2的幂次，并通过结构化的分裂与合并机制来管理。在释放内存时，伙伴系统会积极地、递归地检查伙伴块是否也空闲，如果是则立即合并成一个更大的块。这种强制合并的策略能非常有效地对抗碎片的产生，但代价是可能因大小限制而引入一定的内部碎片。

# 3.扩展练习Challenge1：buddy system（伙伴系统）分配算法
## 一、算法介绍
### 1.背景：连续物理内存管理的挑战
经典的连续分配算法如First-Fit, Best-Fit都面临一个共同的难题：那就是外部碎片。 随着内存的不断分配和释放，会产生大量不连续的小空闲块，即使总空闲内存足够，也可能因为没有足够大的连续块而导致分配失败。
### 2.buddy system 简介
伙伴系统（Buddy System）算法是一种高度规范化的内存管理策略，核心思想可以概括为“规整切分，主动合并”。

首先，伙伴系统将所有内存块的大小强制规定为2的幂次方（如1, 2, 4, 8...个页）。当系统需要分配一块内存时，它会从一个足够大的块开始，像切蛋糕一样，精确地对半分裂，直到得到一个大小最接近且能满足需求的块。这个分裂过程产生的一系列“另一半”，则被分类存放在对应大小的空闲链表中。

该算法的真正精髓在于其合并策略。每一个内存块都有**一个且仅有一个** 伙伴 —— 即当初在同一次分裂中，从同一个“父块”分离出来的另一半。当一个块被释放时，算法的首要任务就是检查它的伙伴是否也处于空闲状态。如果是，系统会将这对伙伴合并恢复成原来的那个更大的父块。这个合并过程是递归的：新合并成的大块会继续检查自己的伙伴，尝试进行更高层次的合并，直至伙伴被占用或已恢复到内存池的最大块。

**伙伴的识别：**
一对伙伴的起始地址在二进制表示上具有绝妙的数学关系：它们的地址值，**仅仅在代表其自身大小的那一个比特位上是相反的**（一个为0，一个为1）。利用这一特性，我们可以通过一次异或位运算，以 O(1) 的时间复杂度算出任何一个块的伙伴地址。在我们具体的实现中，由于管理的内存区域起始地址并非2的幂对齐，我们通过计算块相对于管理区基地址（buddy_base）的偏移量，再与块大小进行异或，最终准确地定位其伙伴，这也是我们实现过程中攻克的一个关键技术点。

注：伙伴系统的 XOR 寻址有一个基本前提：它操作的地址是从 0 开始，并且整个内存池的大小是 2 的 N 次方。这里我们用偏移量解决了第一个前提，有关第二个前提：我们的 init_memmap 通过贪心切割，巧妙地将一个不规则大小的内存区域，分解成了多个符合伙伴系统规范的块。

## 二、数据结构
### 1.空闲块组织：分级空闲链表数组
static free_area_t free_areas[MAX_ORDER];

设计思路： 为了能够快速找到特定大小的空闲块，我们采用一个数组来管理不同大小的块。数组的索引 i 代表块的“阶数”（Order），对应 2^i 个页的大小。

实现细节： 数组的每个元素 free_areas[i] 是一个 free_area_t 结构体，其中包含一个链表头 free_list，用于链接所有 i 阶的空闲块。MAX_ORDER 的选择依据是系统管理的总页数。
### 2.物理页描述符扩展
page->property： 对于一个空闲块的头节点，此字段用于存储该块的阶数 (order)。这是判断伙伴能否合并的关键信息。

page->flags 中的 PG_property 位： 用于标记一个 Page 结构是否是一个空闲块的头节点。这是区分“空闲块头”和“普通空闲页”的关键。
### 3.伙伴地址计算的关键：基地址指针
static struct Page *buddy_base;

我们引入一个静态全局指针 buddy_base，在 init_memmap 时记录下伙伴系统管理的第一页的地址。之后所有的伙伴地址计算都基于相对于 buddy_base 的偏移量来进行，从而解决了地址对齐问题。
## 三、核心算法实现
### 1.初始化 (buddy_init & buddy_init_memmap)
初始化的目标是将一块任意大小的连续物理内存区域，转化为符合伙伴系统规范的、由多个2的幂次方大小的块组成的初始空闲状态。buddy_init 函数首先负责将所有阶数的空闲链表初始化为空。

真正的核心工作在 buddy_init_memmap 中完成。该函数接收内存区域的起始页 base 和总页数 n，并记录下 base 地址作为后续伙伴计算的基准（buddy_base）。随后，它采用一种贪心策略来划分内存：在一个 while 循环中，从当前未处理内存的起始位置 current_offset 开始，计算出剩余空间所能容纳的最大的2的幂次方块（大小为 block_size，阶数为 order）。然后，它将这个块的头节点（base + current_offset）的 property 设置为 order，并将其链接到 free_areas[order] 对应的空闲链表中。最后，更新 current_offset，跳过已划分的区域，继续处理剩余内存。这个循环持续进行，直到所有 n 个页都被高效地组织成一系列大小不一的、符合伙伴系统规范的初始空闲块。
### 2. (buddy_alloc_pages)
内存分配遵循一个“自顶向下，按需分裂”的策略。当收到一个分配 n 页的请求时，函数首先计算出能够容纳 n 页的最小阶数 best_order。

接着，它从 best_order 对应的空闲链表 free_areas[best_order] 开始向上查找，遍历更高阶的链表，直到在 find_order 阶找到第一个非空的链表。如果所有链表都为空，则意味着内存不足，分配失败。

一旦找到了一个足够大的、阶数为 find_order 的块，函数会将其从链表中取出。如果 find_order 恰好等于 best_order，则直接分配。如果 find_order 更大，则进入循环分裂阶段：在循环中，函数将当前块对半分为两个子块。我们始终将伙伴标记为空闲块，把与 block_choose 相同的块继续执行。这个过程反复进行，直到手上的块的阶数恰好等于 best_order。最后，将这个大小正合适的块标记为“已分配”并返回。
### 3.伙伴地址计算 (get_buddy)
get_buddy 函数是伙伴系统高效合并的基础，它以 O(1) 的时间复杂度精确定位任何块的伙伴。其实现巧妙地利用了二进制位运算。函数接收一个块的头指针 page 及其阶数 order。首先，它计算出块的大小 block_size = 1 << order。然后，通过 page - buddy_base 计算出该块相对于我们预存的管理区基地址的相对页偏移量 offset。核心步骤是执行 offset ^ block_size 的异或（XOR）操作。由于一对伙伴的相对偏移量仅在代表其大小 block_size 的那个比特位上不同，XOR 运算恰好能实现对该位的翻转（0变1或1变0），从而得到伙伴的相对偏移量。最后，将这个伙伴的相对偏移量加回到 buddy_base，就得到了伙伴块的绝对 Page 指针，实现了快速而准确的定位。
### 4.(buddy_free_pages)
内存释放采用一个“自底向上，循环合并”的策略，以最大限度地整合内存碎片。当一个块被释放时，函数首先计算出其对应的阶数 order，并立即将其标记为一个合法的空-闲块（设置 property 和 PG_property）。

随后，进入一个循环合并的过程。在每一轮循环中，函数调用 get_buddy 找到当前块的伙伴。紧接着，它会进行严格的合并条件检查：伙伴块必须也存在（未超出管理区），必须是一个空闲块的头节点（PG_property 为1），并且其阶数必须与当前块的阶数完全相同。如果任一条件不满足，则说明无法合并，循环立即终止。

如果满足合并条件，函数会先将伙伴从其所在的空闲链表中移除，然后将两个块合并为一个阶数 +1 的新块（新块的头节点是两个伙伴中地址较小的那个）。重要的是，这个新合并成的大块会继续作为当前块，进入下一轮循环，尝试与它自己的、更高层次的伙伴进行合并。这个递归合并的过程会一直持续，直到合并条件不满足或已达到最大阶数。最后，将这个经过所有可能合并后的、尺寸最大的最终块，添加到其对应阶数的空闲链表中，完成释放过程。
## 四、测试与验证
- 基本功能与内存回收： 测试1和5通过执行一系列不同大小块的分配，以及顺序或交叉的释放操作，最终通过 assert(nr_free_pages() == total_free_start) 严格断言所有内存都被精确地回收，证明了算法不存在内存泄漏。
- 分裂机制验证： 测试2通过申请一个大块内存（129页），触发了从高阶块的连续分裂。测试日志清晰地展示了分裂后，一系列不同阶数的伙伴块被正确地创建并添加到了各自的空闲链表中，验证了 alloc_pages 函数分裂逻辑的正确性。
- 合并机制验证： 测试3是合并功能的核心验证。我们通过精确控制一对伙伴块 p1 和 p2 的生命周期来创造测试条件：首先释放 p1，此时 p2 尚被占用，日志显示合并失败（伙伴不满足关系1）；接着释放 p2，日志则显示 p2 成功找到了已空闲的 p1 作为伙伴（PageProperty=1 且阶数匹配），并触发了合并。最终，我们能成功申请到一个合并后大小的新块，从而验证了合并逻辑的正确性。
- 递归合并验证： 测试4旨在验证算法最具挑战性的部分——递归合并。我们首先分配8个相邻的1页块，然后以特定顺序释放它们。日志显示，当 pg[1] 被释放时，它与 pg[0] 合并成一个2页块；当 pg[3] 被释放时，它与 pg[2] 合并成2页块，这个新块又立刻发现它的伙伴（由pg[0]和pg[1]合并而成）也处于空闲状态，于是再次合并成一个4页的块。这个过程清晰地展示了合并后的块能够继续作为参与者，触发更高阶的合并，有效地将内存碎片“化零为整”。

    下面就测试4详细介绍：
    - 测试前：
        ```
        ===== Buddy System 空闲块状态 =====
        阶数 3 (8 页): 1 个空闲块
        阶数 4 (16 页): 1 个空闲块
        阶数 5 (32 页): 1 个空闲块
        阶数 7 (128 页): 1 个空闲块
        阶数 10 (1024 页): 1 个空闲块
        阶数 11 (2048 页): 1 个空闲块
        阶数 12 (4096 页): 1 个空闲块
        阶数 13 (8192 页): 1 个空闲块
        阶数 14 (16384 页): 1 个空闲块
        ```
    - 连续申请8个1页的块（pg[0] 到 pg[7]）
        ```
        -> 步骤 1: 申请 8 个 1 页的块。
        pg[0] -> 0x87ff7000 (索引: 31920)
        pg[1] -> 0x87ff8000 (索引: 31921)
        pg[2] -> 0x87ff9000 (索引: 31922)
        pg[3] -> 0x87ffa000 (索引: 31923)
        pg[4] -> 0x87ffb000 (索引: 31924)
        pg[5] -> 0x87ffc000 (索引: 31925)
        pg[6] -> 0x87ffd000 (索引: 31926)
        pg[7] -> 0x87ffe000 (索引: 31927)
        ===== Buddy System 空闲块状态 =====
        阶数 4 (16 页): 1 个空闲块
        阶数 5 (32 页): 1 个空闲块
        阶数 7 (128 页): 1 个空闲块
        阶数 10 (1024 页): 1 个空闲块
        阶数 11 (2048 页): 1 个空闲块
        阶数 12 (4096 页): 1 个空闲块
        阶数 13 (8192 页): 1 个空闲块
        阶数 14 (16384 页): 1 个空闲块
        ```
    -  释放 pg[0] (索引 31920)
        ```
        调试：页索引 31920，阶数 0，对应伙伴页索引 31921
        伙伴不满足关系1
        ===== Buddy System 空闲块状态 =====
        阶数 0 (1 页): 1 个空闲块
        阶数 4 (16 页): 1 个空闲块
        阶数 5 (32 页): 1 个空闲块
        阶数 7 (128 页): 1 个空闲块
        阶数 10 (1024 页): 1 个空闲块
        阶数 11 (2048 页): 1 个空闲块
        阶数 12 (4096 页): 1 个空闲块
        阶数 13 (8192 页): 1 个空闲块
        阶数 14 (16384 页): 1 个空闲块
        ```
    -  释放 pg[1] (索引 31921)
        ```
        调试：页索引 31921，阶数 0，对应伙伴页索引 31920    <--------- 两个一页合并为一个两页
        调试：页索引 31920，阶数 1，对应伙伴页索引 31922
        伙伴不满足关系1
        ===== Buddy System 空闲块状态 =====
        阶数 1 (2 页): 1 个空闲块
        阶数 4 (16 页): 1 个空闲块
        阶数 5 (32 页): 1 个空闲块
        阶数 7 (128 页): 1 个空闲块
        阶数 10 (1024 页): 1 个空闲块
        阶数 11 (2048 页): 1 个空闲块
        阶数 12 (4096 页): 1 个空闲块
        阶数 13 (8192 页): 1 个空闲块
        阶数 14 (16384 页): 1 个空闲块
        ```
    -  释放 pg[2] (索引 31922)
        ```
        调试：页索引 31922，阶数 0，对应伙伴页索引 31923
        伙伴不满足关系1
        ===== Buddy System 空闲块状态 =====
        阶数 0 (1 页): 1 个空闲块
        阶数 1 (2 页): 1 个空闲块
        阶数 4 (16 页): 1 个空闲块
        阶数 5 (32 页): 1 个空闲块
        阶数 7 (128 页): 1 个空闲块
        阶数 10 (1024 页): 1 个空闲块
        阶数 11 (2048 页): 1 个空闲块
        阶数 12 (4096 页): 1 个空闲块
        阶数 13 (8192 页): 1 个空闲块
        阶数 14 (16384 页): 1 个空闲块
        ```
    -  释放 pg[3] (索引 31923)
        ```
        调试：页索引 31923，阶数 0，对应伙伴页索引 31922
        调试：页索引 31922，阶数 1，对应伙伴页索引 31920    <--------- 两个两页合并为一个四页
        调试：页索引 31920，阶数 2，对应伙伴页索引 31924
        伙伴不满足关系1
        ===== Buddy System 空闲块状态 =====
        阶数 2 (4 页): 1 个空闲块
        阶数 4 (16 页): 1 个空闲块
        阶数 5 (32 页): 1 个空闲块
        阶数 7 (128 页): 1 个空闲块
        阶数 10 (1024 页): 1 个空闲块
        阶数 11 (2048 页): 1 个空闲块
        阶数 12 (4096 页): 1 个空闲块
        阶数 13 (8192 页): 1 个空闲块
        阶数 14 (16384 页): 1 个空闲块
        ```
    -  释放 pg[4] 至 pg[7]后恢复到原状态
        ```
        ===== Buddy System 空闲块状态 =====
        阶数 3 (8 页): 1 个空闲块
        阶数 4 (16 页): 1 个空闲块
        阶数 5 (32 页): 1 个空闲块
        阶数 7 (128 页): 1 个空闲块
        阶数 10 (1024 页): 1 个空闲块
        阶数 11 (2048 页): 1 个空闲块
        阶数 12 (4096 页): 1 个空闲块
        阶数 13 (8192 页): 1 个空闲块
        阶数 14 (16384 页): 1 个空闲块
        ```
## 五、总结
本次实验成功在 ucore 操作系统中，基于 pmm_manager 接口实现了一个功能完整、经过充分测试的伙伴系统（Buddy System）物理内存管理器。以下是优缺点总结：

优点：
- 有效控制外部碎片： 通过伙伴合并机制，能够快速地将释放的小块内存重新组合成更大的连续可用空间。
- 分配与释放效率较高： 查找特定大小的空闲块（通过分级链表）和计算伙伴地址（通过 O(1) 的位运算）都非常快速。虽然分配时可能涉及多次分裂，释放时可能涉及多次合并，但总体开销是可控的，并且与空闲块的总数无关，只与内存块的阶数（即 log N）相关。
- 
缺点：
- 内部碎片问题显著： 这是伙伴系统固有的、也是最主要的缺点。由于所有内存块的大小都必须是2的幂次方，当应用程序申请的内存大小不是2的幂次方时，系统必须分配一个比需求更大的块。例如，申请33页内存，实际会分配一个64页的块，其中有31页被浪费掉了。这种在已分配块内部的未使用空间，即为内部碎片。
- 内存合并的局限性： 只有互为“伙伴”的两个块才能合并。如果两个同样大小、地址相邻的空闲块不是伙伴关系（例如，它们来自不同的父块），那么它们将永远无法合并成一个更大的块。


# 4.扩展练习Challenge2：任意大小的内存单元slub分配算法
## 4.1 系统架构设计
基于Linux中的slub分配算法的核心思想，本实验中实现的slub分配算法采用两层架构：
- 第一层-页级分配器：以页为单位管理物理内存，提供分配和释放连续n个物理页的接口。本实现中采用first-fit分配算法（也可替换为best first等）。
- 第二层-对象级分配器slub缓存池：以固定大小(小于一页)的对象(object)为单位在页级分配器之上进行内存管理，提供分配、释放多种固定大小的对象的接口。按对象大小维护多个缓存池（cache），每个缓存池对应一种固定大小对象。每个 cache 由若干 slab 构成，每个 slab 对应一页。slab由描述信息、位图、多个固定大小的对象组成。
## 4.2 核心数据结构设计
- slab管理结构`slab_t`
```
//对应一页的物理内存，负责管理多个同样大小的对象
typedef struct SLAB{
    list_entry_t slab_node;//用于插入对应cache的slab链表
    size_t free_cnt;//该slab中空闲对象的数量
    void *obj_area;//该slab中存放对象的起始地址
    unsigned char *bitmap;//指向 slab 内存中存放位图的起始地址，位图用于标识对象是否被占用
}slab_t;
```
- 缓存池结构`kmem_cache`
```
//kmem_cache表示一个缓存池,专门管理固定大小的对象。
typedef struct cache{
    list_entry_t slab;//双向slab链表的起始节点
    size_t object_size;//该缓冲池对应的对象大小
    size_t object_num;//该缓冲池对应的一个slab中的对象数量
}kmem_cache;
```
- 缓存池管理结构`kmalloc_caches`
```
//使用数组统一管理多个缓存池
static kmem_cache kmalloc_caches[4];
```
## 4.3 关键算法实现
### 4.3.1 内存管理器结构初始化
- 第一层页级分配器初始化：沿用static void
default_init(void)函数。
- 第二层slub缓存池初始化：初始化SLUB分配器的四个缓存池，分别管理32、64、128、256字节大小的对象，并设置每个slab可容纳的对象数量(计算公式为(PGSIZE-sizeof(slab_t))/(object_size+1.0/8.0))，且每个缓存池初始化用于管理属于该缓存池的所有slab的链表。
    ```
    static void kmem_cache_init(void){
        cache_n=4;
        size_t sizes[4]={32,64,128,256};
        for(int i=0;i<cache_n;i++)
        {
            kmalloc_caches[i].object_size=sizes[i];
            //设置每个slab可容纳的对象数量
            kmalloc_caches[i].object_num=cal_objs_num(sizes[i]);
            //初始化一个空的slab链表
            list_init(&kmalloc_caches[i].slab);
        }
    }
    ```
- 整个slub分配器的启动函数：先调用 default_init() 初始化底层页分配机制；再调用 kmem_cache_init() 初始化高层对象缓存池。
    ```
    static void slub_init(void){
        default_init();
        kmem_cache_init();
    }
    ``` 
### 4.3.2 分配一个slab
alloc_slab() 为指定大小的对象对应的新的slab分配一个物理页，并在该页内初始化一个新的 slab 结构（包含描述信息、对象和位图），返回其虚拟地址指针。
```
static slab_t* alloc_slab(size_t object_size,size_t object_num)
{
    //为新的slub分配一页
    struct Page* p=default_alloc_pages(1);
    if(p==NULL)
    {
        return NULL;
    }
    //由页的结构体指针转为该物理页的虚拟地址
    //调用关系为KADDR(将物理地址转换为虚拟地址)->pagepa(将页号转为物理地址)->page2pnn(将page结构体指针转为页号)
    void *va=KADDR(page2pa(p));
    //将新分配的slab指针指向给它分配的页的虚拟地址
    slab_t *slab=(slab_t*)va;
    slab->free_cnt=object_num;
    //一个slab的布局：slab_struct|object|bitmap
    //将对象、位图的起始地址分别赋值给对应指针obj_area、bitmap
    slab->obj_area=(void*)slab+sizeof(slab_t);
    slab->bitmap=(unsigned char*)((void*)slab->obj_area+object_size*object_num);
    //将位图全部初始化为0,其中(objs_num + 7) / 8 → 向上取整，保证足够字节存放所有对象位
    memset(slab->bitmap, 0, (object_num + 7) / 8);
    //初始化slab链表节点
    list_init(&slab->slab_node);
    //返回 slab 的虚拟地址指针
    return slab;
}
```
### 4.3.3 分配一个对象
`alloc_obj()` 根据请求的内存大小从合适大小的缓存池`fit_cache`(取kmalloc_caches中第一个对象大小大于请求内存大小的缓存池)中分配一个对象：若对应缓存池中存在空闲 `slab`，则在第一个空闲的`slab`的位图中找到第一个为0的位返回对应对象地址；若无可用 `slab`，则分配一个新的 `slab` 、将其链入对应缓存池的slab链表，并返回新`slab`中的第一个对象地址。在返回对象地址前将对应位图中的位置1，对应`slab`的空闲对象计数减1。特殊情况处理：在请求内存大小不合法(小于等于0)和请求内存大小大于所有缓存池中最大的对象大小时均返回NULL。
```
static void* alloc_obj(size_t size)
{
    //请求内存大小不合法则返回NULL
    if(size<=0)return NULL;
    kmem_cache *fit_cache=NULL;
    for(int i=0;i<cache_n;i++)
    {
        if(kmalloc_caches[i].object_size>=size)
        {
            fit_cache=&kmalloc_caches[i];
            break;
        }
    }
    //请求内存大小大于所有缓存池中最大的对象大小时均返回NULL
    if(fit_cache==NULL)
    {
        return NULL;
    }
    //用指针le遍历对应cache的slab链表，找到第一个有空闲的slab并取下空闲的object
    list_entry_t *le=&fit_cache->slab;
    while((le=list_next(le))!=&fit_cache->slab)
    {
        //根据链表上的节点获取对应的结构体指针来帮助后续信息使用
        slab_t *fit_slab=le2slab(le,slab_node);
        if(fit_slab->free_cnt>0)
        {
            for(size_t i=0;i<fit_cache->object_num;i++)
            {
                //位图按字节存储
                //byte是对象i所在的位图字节下标
                size_t byte=i/8;
                //bit是对象i在该字节内对应的位位置
                size_t bit=i%8;
                //通过该掩码1 << bit找到位图中位的值。
                if((fit_slab->bitmap[byte]&(1<<bit))==0)
                {
                    //将object对应的位图中的位置1
                    fit_slab->bitmap[byte] |= (1 << bit);
                    //将对应slab的空闲对象计数减1
                    fit_slab->free_cnt--;
                    return (void *)((char *)fit_slab->obj_area + i * fit_cache->object_size);
                }
            }
        }
    }
    //若不存在空闲的slab->新建slab并分配第一个object
    slab_t *new_slab=alloc_slab(fit_cache->object_size,fit_cache->object_num);
    if(!new_slab)return NULL;
    //新的slab节点插入缓存池的slab链表
    list_add(&fit_cache->slab,&new_slab->slab_node);
    new_slab->bitmap[0]|=1;
    new_slab->free_cnt--;
    return new_slab->obj_area;
}
```
### 4.3.4释放一个对象
free_obj() 根据对象地址找到其所属的 slab，在位图中标记该对象为空闲、将对应slab的空闲计数加1并清零内容；若该 slab 全部对象均为空闲态，则回收整个物理页并从缓存池链表中移除。
```
static void free_obj(void* object)
{
    //首先应根据object的地址找到其所属的slab
    for(size_t i=0;i<cache_n;i++)
    {
        kmem_cache *cache=&kmalloc_caches[i];
        list_entry_t *le=&cache->slab;
        while((le=list_next(le))!=&cache->slab)
        {
            slab_t *slab=le2slab(le,slab_node);
            //判断object是否在该slab的object内存空间中
            if((object>=slab->obj_area)&&(object<(slab->obj_area+cache->object_size*cache->object_num)))
            {
                //index= object相对于对象区起始的字节偏移/object大小
                size_t index=((char*)object-(char*)slab->obj_area)/cache->object_size;
                size_t byte=index/8;
                size_t bit=index%8;
                if (slab->bitmap[byte] & (1 << bit))
                {
                    slab->bitmap[byte] &= ~(1 << bit); // 位图位标记为未分配
                    slab->free_cnt++;//slab链表的空闲计数加1
                    //将对象区域内存清0
                    memset(object,0,cache->object_size);
                    //若slab全部对象均空闲则回收该物理页
                    if(slab->free_cnt==cache->object_num)
                    {
                        list_del(&slab->slab_node);
                        default_free_pages(pa2page(PADDR(slab)), 1);
                    }
                }
                return;
            }    }    }     }
```
### 简要介绍部分以上关键算法实现代码中调用的函数/宏：

>  static size_t cal_objs_num(size_t object_size)：根据对象大小计算对应一个slab中存放的对象数量。
>  static struct Page *default_alloc_pages(size_t n)：从空闲页链表中分配连续的 n 页
>  static void default_free_pages(struct Page *base, size_t n)：负责释放连续的n页并将它们回收到空闲页链表中，同时尽量合并相邻空闲页以减少内存碎片。
>  le2slab(le, member)：将slab链表节点指针le转换为对应的slab_t结构体指针
>  obj_info_t get_obj_info(void *obj):根据给定对象的指针，查找它所属的 slab 并返回该对象在 slab 中的索引、位图状态、slab指针，如果对象不属于任何已知 slab，则返回默认错误信息。
## 4.4测试设计
对于slub内存管理器的测试均封装在static void slub_check(void)函数中，包括以下六种测试场景。
### 4.4.1 缓冲池初始化测试
验证缓冲池`kmalloc_caches`在初始化时为不同对象大小计算出的每个 slab 中对象数量是否正确。
```
 //sizeof(slab_t)=40,对应正确object数量应为(4096-40)/(object_size+1/8)
    size_t object_nums[4]={126,63,31,15};
    for(size_t i=0;i<cache_n;i++)
    {
        assert(kmalloc_caches[i].object_num==object_nums[i]);
    }
    cprintf("——————————————缓冲池初始化正确——————————————————\n")
```
### 4.4.2 对象级分配器功能测试
slub_basic_check()函数测试指定对象大小的分配与释放的基本正确性:通过分配两个相同大小的对象，检查当 alloc_obj() 被调用时，既能正确创建新的 slab（首次分配），又能在已有 slab 中重复分配空闲对象；同时通过释放对象验证 free_obj() 的行为是否正确，测试其在对象未完全空闲时不释放 slab，而在slab 全部空闲时能正确释放回页分配器。此外，还验证了对象地址合法性、位图标记正确性、slab中的空闲对象计数、空闲页计数（nr_free）的变化是否符合预期。
```
static void slub_basic_check(size_t object_size)
{   
    //申请两个相同大小的对象
    //需要分配新的slab
    void *obj1=alloc_obj(object_size);
    //已有slab上有空闲object
    void *obj2=alloc_obj(object_size);
    size_t nr_free_st=nr_free;
    //验证两个对象所在地址不同
    assert(obj1!=obj2);
    //验证位图对应位为1
    obj_info_t info1 = get_obj_info(obj1);
    assert(info1.bit_value==1);
    //验证分配的object地址系统物理内存范围之内
    assert(PADDR(obj1)<=npage * PGSIZE);
    assert(info1.slab->free_cnt==(int)((((PGSIZE-sizeof(slab_t))/(object_size+1.0/8.0))-2)));
    cprintf("——————————————2个%lu 字节对象分配测试成功————————————\n", object_size);
    //释放并检查空闲计数
    free_obj(obj1);
    //有占用对象时不释放slab
    assert(nr_free_st==nr_free);
    //测试释放的object正确链入slab
    assert(info1.slab->free_cnt==(int)((((PGSIZE-sizeof(slab_t))/(object_size+1.0/8.0))-1)));
    //slab存在时对应位图被正确清0
    assert((info1.slab->bitmap[info1.index / 8] & (1 << (info1.index % 8))) == 0);
    obj_info_t info2 = get_obj_info(obj1);
    assert(info2.bit_value==0);
    free_obj(obj2);
    //对象均空闲时释放slab
    assert(nr_free_st+1==nr_free);
    cprintf("——————————————2个%lu 字节对象释放测试成功————————————\n", object_size);
}
```
在 `slub_check()` 函数中，对每个缓存池所管理的不同对象大小，分别调用 `slub_basic_check()` 函数进行测试，以验证各缓存池在相应对象大小下的分配与释放逻辑是否正确。
```
for(size_t i=32;i<=256;i=i*2)
    {
        slub_basic_check(i);
    }
```
### 4.4.3 slab分配的特殊情况测试
测试在系统无可用物理页时，alloc_slab() 能够正确返回 NULL，即安全地处理内存不足的情况。
```
    //暂存当前全局空闲页链表和空闲页计数
    list_entry_t free_list_store = free_list;
    size_t nr_free_store=nr_free;
    //将全局空闲页计数清零并初始化一个新的空闲链表
    nr_free = 0;
    list_init(&free_list);
    assert(list_empty(&free_list));
    //测试无空闲物理页时分配 slab应返回 NULL
    size_t a=32;
    size_t b=126;
    assert(alloc_slab(a,b)==NULL);
    //恢复空闲链表和空闲页计数
    free_list = free_list_store;
    nr_free=nr_free_store;
    cprintf("——————————————无空闲物理页时slab分配失败情况测试正确——————————————————\n")
```
### 4.4.4 对象分配的特殊情况测试
测试对象分配边界：请求的对象为0或超出已有缓存池的最大大小(256)均应返回NULL。
```
assert(alloc_obj(0)==NULL);
assert(alloc_obj(512)==NULL);
cprintf("——————————————object分配边界测试正确——————————————————\n")
```
### 4.4.5 细粒度交替分配释放测试
slub_fine_grained_check()函数进行多轮细粒度交替分配不同大小的对象（30、60、120、250 字节，每轮分配 4 个对象）并按分配顺序逐个释放的测试。测试过程中，每次分配后检查对象是否非空、所属 slab 位图是否正确标记为已分配，释放时验证位图是否被清零。最后，测试全局空闲页计数 nr_free 在分配和释放完成后恢复到初始值。
```
static void slub_fine_grained_check(void) {
    cprintf("——————————————————细粒度交替分配释放测试开始——————————————\n");
    size_t sizes[4] = {30, 60, 120, 250};
    size_t nums[4]={126,63,31,15};
    size_t nr_free_start = nr_free;
    void* objects[1000];
    // 交替分配 25 轮，每轮分配 4 个对象，每种大小一个
    int offset = 0;
    for(int round = 0; round < 25; round++) {
        for(int s = 0; s < 4; s++) {
            void* obj = alloc_obj(sizes[s]);
            assert(obj != NULL);
            objects[offset++] = obj;
            // 检查对象位图被正确标记为1
            obj_info_t info = get_obj_info(obj);
            assert(info.bit_value == 1);
        }
    }
    // 交替释放，每次释放一个对象，按分配顺序
    for(int i = 0; i < offset; i++) {
        void* obj = objects[i];
        obj_info_t info_before = get_obj_info(obj);
        assert(info_before.bit_value == 1);
        // 保存 slab 指针和索引
        slab_t *slab = info_before.slab;
        size_t index = info_before.index;
        free_obj(obj);
        // 如果 slab 还存在，检查 bitmap 对应位已清零
        if (slab->free_cnt != nums[i%4]) {
            assert((slab->bitmap[index / 8] & (1 << (index % 8))) == 0);
        }
    }
    // 检查空闲页恢复
    assert(nr_free == nr_free_start);
    cprintf("——————————————————细粒度交替分配释放测试正确——————————————————\n");
}
```
之后，在slub_check()中调用slub_fine_grained_check()函数进行测试。
### 4.4.6 大规模分配释放测试
在每个缓存池中分别分配大量非整页大小的对象（30、60、120、250 字节，每种对象各分配 1000 个）。每分配完一类对象后，更新预期的空闲页计数 `nr_free_expect`，并测试全局空闲页计数 `nr_free` 与预期值是否一致。分配完成后，测试再依次释放所有对象，并在释放完成后测试全局空闲页计数恢复到初始值。
```
{
    cprintf("————————————————大规模分配释放测试开始————————————————————\n");
    size_t nr_free_start = nr_free;
    size_t nr_free_expect = nr_free;
    void *objects[10000];
    int allocated = 0;
    //交替分配不同缓冲池中非object_size大小的对象
    size_t sizes[4] = {30, 60, 120, 250};
    size_t nums[4]={126,63,31,15};
    int offset = 0;
    for(int s = 0; s < 4; s++) {
        for(int i = 0; i < 1000; i++) {
            void *obj = alloc_obj(sizes[s]);
            objects[offset] = obj;
            offset++;
            allocated++;
        }
        //测试分配后空闲页计数正确
        nr_free_expect-=(1000+nums[s]-1)/nums[s];
        cprintf("分配1000个 %luB 对象后: nr_free = %d (预期 %d)\n",sizes[s], (int)nr_free, (int)nr_free_expect);
        assert(nr_free_expect==nr_free);            
        }   
    assert(nr_free==nr_free_start-(1000+125)/126-(1000+62)/63-(1000+30)/31-(1000+14)/15);
    cprintf("————————————————大规模分配测试正确————————————————————\n");
    // 释放不同缓冲池的对象
    for(int i = 0; i < allocated; i++) {
        free_obj(objects[i]);
    }
    // 测试释放后nr_free恢复
    assert(nr_free == nr_free_start);
    cprintf("————————————————大规模释放测试正确————————————————————\n");
}  
```
## 4.5测试结果
如下为测试结果，通过对应打印信息可知实验代码通过了设计的六种测试场景。
```
root@DESKTOP-H1M4D3V:/home/work/operating_system/labcode/lab2# make qemu
+ cc kern/mm/slub_pmm.c
+ ld bin/kernel
riscv64-unknown-elf-objcopy bin/kernel --strip-all -O binary bin/ucore.img

OpenSBI v0.4 (Jul  2 2019 11:53:53)
   ____                    _____ ____ _____
  / __ \                  / ____|  _ \_   _|
 | |  | |_ __   ___ _ __ | (___ | |_) || |
 | |  | | '_ \ / _ \ '_ \ \___ \|  _ < | |
 | |__| | |_) |  __/ | | |____) | |_) || |_
  \____/| .__/ \___|_| |_|_____/|____/_____|
        | |
        |_|

Platform Name          : QEMU Virt Machine
Platform HART Features : RV64ACDFIMSU
Platform Max HARTs     : 8
Current Hart           : 0
Firmware Base          : 0x80000000
Firmware Size          : 112 KB
Runtime SBI Version    : 0.1

PMP0: 0x0000000080000000-0x000000008001ffff (A)
PMP1: 0x0000000000000000-0xffffffffffffffff (A,R,W,X)
DTB Init
HartID: 0
DTB Address: 0x82200000
Physical Memory from DTB:
  Base: 0x0000000080000000
  Size: 0x0000000008000000 (128 MB)
  End:  0x0000000087ffffff
DTB init completed
(THU.CST) os is loading ...
Special kernel symbols:
  entry  0xffffffffc02000d8 (virtual)
  etext  0xffffffffc0201b00 (virtual)
  edata  0xffffffffc0206018 (virtual)
  end    0xffffffffc0206100 (virtual)
Kernel executable memory footprint: 25KB
memory management: slub_pmm_manager
physcial memory map:
  memory: 0x0000000008000000, [0x0000000080000000, 0x0000000087ffffff].
——————————————slub内存管理器测试开始——————————————————
——————————————缓冲池初始化正确——————————————————
——————————————2个32 字节对象分配测试成功————————————
——————————————2个32 字节对象释放测试成功————————————
——————————————2个64 字节对象分配测试成功————————————
——————————————2个64 字节对象释放测试成功————————————
——————————————2个128 字节对象分配测试成功————————————
——————————————2个128 字节对象释放测试成功————————————
——————————————2个256 字节对象分配测试成功————————————
——————————————2个256 字节对象释放测试成功————————————
——————————————无空闲物理页时slab分配失败情况测试正确——————————————————
——————————————object分配边界测试正确——————————————————
——————————————————细粒度交替分配释放测试开始——————————————
——————————————————细粒度交替分配释放测试正确——————————————————
————————————————大规模分配释放测试开始————————————————————
分配1000个 30B 对象后: nr_free = 31921 (预期 31921)
分配1000个 60B 对象后: nr_free = 31905 (预期 31905)
分配1000个 120B 对象后: nr_free = 31872 (预期 31872)
分配1000个 250B 对象后: nr_free = 31805 (预期 31805)
————————————————大规模分配测试正确————————————————————
————————————————大规模释放测试正确————————————————————
check_alloc_page() succeeded!
satp virtual address: 0xffffffffc0205000
satp physical address: 0x0000000080205000
```
# 5.扩展练习Challenge3：硬件的可用物理内存范围的获取方法

> 如果 OS 无法提前知道当前硬件的可用物理内存范围，请问你有何办法让 OS 获取可用物理内存范围？

对于这个练习，我们将从三个阶段进行思考：

在**第一个阶段**我们会明确<span data-type="text" style="background-color: var(--b3-card-error-background); color: var(--b3-card-error-color);">操作系统当前是如何得知硬件可用物理范围的，在获取这个信息之后用它去做了什么，以及我们开发者是从哪里得知可用物理内存相关信息并将其作为先验知识进行编码的，以及代码里的哪些部分是我们依靠先验知识编写而成的</span>。

1. <span data-type="text" style="background-color: var(--b3-card-error-background); color: var(--b3-card-error-color);">操作系统当前是如何得知硬件可用物理范围的？</span>

    在当前的ucore实现中，操作系统自身并不具备主动探测物理硬件的能力。它采取了一种被动接收和信任约定的策略，来获取其运行环境中的可用物理内存范围：

    - 起点是Qemu的硬件配置，在QEMU 的内部实现中，通过硬编码的方式规定了DRAM的起始物理地址为`0x80000000`​，同时QEMU 默认或根据 `-m`​ 命令行参数确定了 RAM 的大小（在我们的实验中默认为 128 MB）。因此，物理内存的完整范围 `[0x80000000, 0x88000000)` 在 QEMU 模拟器启动时就已经是一个确定的事实。
    - QEMU 并不直接将这个信息告知操作系统，而是在启动OpenSBI固件之前预先生成DTB并将上述的内存范围信息编码到DTB中。当OpenSBI运行时，它会在完成自身的初始化之后在跳转到内核之前将DTB的物理地址放在`a1`寄存器中。
    - 跳转到内核的第一件事就是将`a1`​寄存器中的DTB物理地址立即保存到一个全局变量`boot_dtb`​中。在`kern_init`​函数中，`dtb_init`​被调用来执行真正的解码工作，它首先通过`boot_dtb`​拿到DTB的物理地址，并将其转换为内核可以访问的虚拟地址。然后，它调用`extract_memory_info`​函数遍历DTB的二进制结构，找到`/memory`​节点下的`reg`​属性，最终从中提取出内存的基地址和大小，并存入静态全局变量`memory_base`​和`memory_size`中，至此，操作系统已经知晓了可用物理内存的范围。
    - <span data-type="text" style="background-color: var(--b3-card-info-background); color: var(--b3-card-info-color);">这个过程正是Challenge思考题否定的核心模块，需要我们自己去实现。</span>
2. <span data-type="text" style="background-color: var(--b3-card-error-background); color: var(--b3-card-error-color);">得知硬件可用物理内存范围之后操作系统用它去做了什么？</span>

    物理内存管理器的初始化函数`page_init`​通过调用`get_memory_base()`​和`get_memory_size()`，获取到这份经过层层传递而来的权威信息，并基于它来计算总页数、初始化Page数组，从而建立起对整个物理内存的管理。
3. <span data-type="text" style="background-color: var(--b3-card-error-background); color: var(--b3-card-error-color);">我们开发者从哪里得知可用物理内存相关信息并将其作为先验知识进行编码？</span>

    作为开发者，我们可以在编码之前获取到关于物理内存范围的信息，这些信息共同构成了我们对系统内存布局的先验知识。首先我们可以在`riscv.h`​中看到DRAM的标准起始地址`#define DRAM_BASE  0x80000000`​定义（或者从Qemu规范得知其DRAM起始地址），此外在我们运行`make qemu`​时可以用 `-m` 命令行参数规定 RAM 的大小。于是可用物理内存作为先验知识成为我们编码的核心依据。

    此外我们了解ucore的标准启动流程，知道OpenSBI会占用 `[0x80000000, 0x80200000)`​ 这 2MB 空间，并将我们的内核加载到紧随其后的 `0x80200000` 地址。同时我们熟悉 RISC-V Sv39 分页机制，知道虚拟地址是如何分解的，页表项的格式是怎样的，以及大大页映射的存在等等。
4. <span data-type="text" style="background-color: var(--b3-card-error-background); color: var(--b3-card-error-color);">代码里的哪些部分是我们依靠先验知识编写而成的？</span>

    基于上述的先验知识，我们在代码的多个关键位置进行了硬编码，将这些约定固化下来。<span data-type="text" style="background-color: var(--b3-card-info-background); color: var(--b3-card-info-color);">这些硬编码正是 Challenge 思考题所要否定的基础。</span>

    - 平台规范的硬编码（`riscv.h`​）：`#define DRAM_BASE    0x80000000`

      这行代码将我们从QEMU规范中得知的DRAM起始地址，直接定义为一个宏，它成为了所有其他地址计算的原点。
    - 内核加载地址的硬编码（`Makefile`​）:`-device loader,file=$(UCOREIMG),addr=0x80200000`

      这行代码基于我们知道可用物理内存实际上是`[0x80000000, 0x88000000)`，并且OpenSBI占据前2MB。
    - 链接基地址的硬编码（`kernel.ld`​）：`BASE_ADDRESS = 0xFFFFFFFFC0200000;`

      我们基于“内核物理加载地址是 0x80200000”这一先验知识，并结合我们自己设计的“高半核”内存模型，计算出了内核的虚拟基地址，并将其硬编码到链接脚本中。链接器将严格按照这个地址来生成最终的内核镜像。
    - 启动页表的硬编码（`entry.S`）：

      ```asm
      boot_page_table_sv39:
          .zero 8 * 511
          .quad (0x80000 << 10) | 0xcf
      ```

      这是先验知识最集中的体现。

      1. 0x80000这个物理页号是怎么来的？它正是 DRAM_BASE >> 12 (0x80000000 / 4096) 的计算结果。我们硬编码了这个值，因为我们确信物理内存从这里开始。
      2. .zero 8 * 511 和 .quad ...这里我们将有效的页表项放在第 512 项，这是基于我们对 Sv39 虚拟地址分解（高 9 位作为索引）的先验知识，以及我们为内核选择的高位虚拟地址 0xFFFFFFFFC0...（其高 9 位正好是 0x1FF，即索引 511）而做出的精确设计。
    - 内存布局常量的硬编码（`memlayout.h`）：

      ```c
      #define KERNBASE            0xFFFFFFFFC0200000
      #define KMEMSIZE            0x7E00000
      #define PHYSICAL_MEMORY_END         0x88000000
      #define PHYSICAL_MEMORY_OFFSET      0xFFFFFFFF40000000
      #define KERNEL_BEGIN_PADDR          0x80200000
      #define KERNEL_BEGIN_VADDR          0xFFFFFFFFC0200000
      ```

      我们基于“DRAM 起始于 `0x80000000`​”、“大小为 128MB”、“内核加载于 `0x80200000`​”这些先验知识，直接将物理内存的末尾地址和内核的加载地址定义为常量，供内核其他部分（如 `page_init`）使用。

在**第二个阶段**我们主要是完成<span data-type="text" style="background-color: var(--b3-card-error-background); color: var(--b3-card-error-color);">否定先验知识并重构启动模型</span>的任务。在第一阶段中，我们明确了当前ucore的实现是一个高度依赖于平台规范和Bootloader信息传递的地址相关模型。Challenge思考题的核心正是要我们打破这种依赖，探索在这些先验知识完全缺失的情况下，内核如何实现自我发现和动态配置。

因此我们否定了Bootloader的信息传递，否定了所有硬编码的绝对地址，否定了固定的内核加载地址。此时内核必须自己探索可用物理内存，并且整个启动模型会从地址相关重构为地址无关，并引入运行时动态探测的机制。为了应对这个挑战，我们的处理办法将围绕两个核心思路展开：

1. 位置无关代码：为了让内核能在任意物理地址 `X`​ 启动，它本身必须被编译和链接成位置无关的形式，这意味着所有内部的函数跳转和数据访问都必须采用PC 相对寻址，而不是绝对地址。在 `entry.S`​ 的启动初期，内核的首要任务将是自我定位即通过读取 PC 寄存器等技巧计算出自己被加载到的物理基地址 `X`​，这个 `X` 将成为后续所有物理地址计算的唯一基址。
2. 带异常处理的物理内存探测：在确定了自身位置后，内核需要在完全的物理地址模式下，自己动手探测可用的 RAM 范围。这个过程不能再依赖任何外部信息，只能通过“写-读-验证”的经验性测试来完成。为了防止因访问不存在的内存或硬件寄存器（MMIO）而导致的系统崩溃，这个探测过程必须被一个异常处理安全网所包裹：内核需要预设一个特殊的异常处理程序，当探测过程中发生访问错误时，它能够捕获异常并安全地返回，而不是让系统 panic。探测算法本身也需要优化，例如采用大步进与二分查找结合的策略，以在广阔的地址空间中高效地定位出所有可用 RAM 块的边界。

整个启动流程将被重构为一个动态的、分两步走的模型：

1. 物理模式下的探测阶段：

    ​`entry.S`​ 首先进行自我定位，确定加载基地址 `X`​，并建立一个位于内核自身 `.bss`​ 段内的临时栈。然后它直接在物理地址模式下跳转到一个 C 语言编写的 `probe_memory()`​ 函数，`probe_memory()` 在异常处理安全网的保护下，对物理地址空间进行扫描，最终生成一份包含所有可用 RAM 块的物理内存地图。
2. 虚拟模式的建立与切换阶段：

    在获取了内存地图后，`page_init`​ 函数的职责将变得更为核心和复杂。它首先根据地图确定 `pages`​ 数组的大小和存放位置，并初始化 PMM。随后，它动态地创建一个最终的内核页表，该页表将实现我们所熟悉的“高半核、线性映射”模型。页表项的内容将根据内核的真实物理地址 `X`​ 和探测到的 RAM 范围实时计算得出，而非硬编码。最后，内核执行一次页表热切换（通过再次写入 `satp` 寄存器），并执行一个从当前物理 PC 到对应高位虚拟 PC 的特殊跳转，从而将自身无缝地迁移到最终的虚拟地址空间中运行。

通过这个“自我定位 -> 主动探测 -> 动态建表 -> 热切换”的流程，内核最终摆脱了对所有先验知识的依赖，实现了一个真正健壮和可移植的启动模型。

**第三个阶段**是我们为了实现上面核心修改的关键伪代码展示：（前提是内核已链接为位置无关可执行文件，基地址为 0x0）

1. ​`entry.S`

    ```asm
    .section .bss
        .align 12
    boot_probe_stack:
        .space 8192 // 在内核镜像内部，为早期探测阶段预留一个静态的栈空间
    boot_probe_stack_top:

    .section .text.entry
    .globl _start
    _start:
    	# 1. 自我定位：确定内核的物理加载基地址 X
        mv   s0, a2 #从 a2 (Bootloader约定) 中获取基地址 X，保存到 s0 寄存器

    	# 2. 建立临时物理栈：为即将到来的 C 函数调用做准备
        la   t0, boot_probe_stack_top # t0 = 栈顶符号相对于基地址 0x0 的偏移
        add  sp, s0, t0 # sp = X + 偏移 = 栈顶的绝对物理地址
        
    	# 3. 传递参数并跳转到 C 语言入口
        mv   a0, s0	# 将物理基地址 X 作为第一个参数传递给 C 函数
        la   t0, early_C_entry	# t0 = C 函数入口的相对偏移
        add  t0, s0, t0	# t0 = X + 偏移 = C 函数入口的绝对物理地址
        jr   t0	# 在物理地址模式下，跳转到 C 代码
    ```
2. ​`pmm.c(新增/重构)`

    ```c
    // ----------- 全局变量 -----------
    jmp_buf probe_env;                 // 用于异常恢复的“存档点”
    volatile bool g_is_probing = false;  // 探测模式的全局开关
    MemoryMap g_memory_map;            // 存储最终探测结果的物理内存地图
    uintptr_t g_kernel_phys_base;        // 存储内核的物理基地址 X

    // ----------- 异常处理 -----------(伪代码)
    void on_access_fault_handler() {
        if (g_is_probing) {// 如果在探测中，则安全返回
            longjmp(probe_env, 1);
        }
        panic("Access Fault!"); // 否则，是真正的内核错误
    }

    // ----------- 探测核心 -----------(伪代码)
    bool SafeTest(uintptr_t addr) {
        if (setjmp(probe_env) == 0) {
            return perform_write_read_verify(addr);// 尝试危险的读写操作
        }
        return false;// 如果发生异常，longjmp 会跳转到这里
    }

    void ProbeMemory() {
        g_is_probing = true;
        // ... 执行“大步进+二分查找”的渐进式探测算法 ...
        // ... 将找到的所有可用 RAM 块 [base, end) 填充到 g_memory_map ...
        g_is_probing = false;
    }

    // ----------- 新的 C 语言入口 -----------
    void early_C_entry(uintptr_t kernel_phys_base) {
        g_kernel_phys_base = kernel_phys_base; // 保存物理基地址 X

        cons_init_early(); // 初始化控制台，以便打印调试信息
        trap_init_for_probe(); // 配置异常处理程序，使其在探测期间能够进行 longjmp

        ProbeMemory(); // 执行内存探测，填充 g_memory_map
        
        trap_init_final(); // 探测完成，恢复常规的异常处理程序

        vm_init();  // 开始虚拟化
    }

    void vm_init() {
    	// page_init 将完成虚拟化的所有核心工作
        page_init();
    }

    void page_init(void) {
        // --- Part A: 在物理地址模式下，准备并切换到虚拟地址模式 ---
    	// 1. 手动为最终的页表分配物理页
        uintptr_t pgdir_pa = allocate_pagetable_page_manually(&g_memory_map);
    	// 2. 动态创建最终的内核页表
    	//    在 pgdir_pa 指向的内存中，填入 512 个 PDE
        //    核心是计算并填入 pgdir[511] 的内容，使其建立高半核和直接映射
        create_final_kernel_pagetable(pgdir_pa, g_kernel_phys_base, &g_memory_map);
    	// 3. 热切换页表，激活 MMU
        uintptr_t new_satp = (8L << 60) | (pgdir_pa >> 12);
        asm volatile("csrw satp, %0" :: "r"(new_satp));
        asm volatile("sfence.vma");
    	// 4. “跃迁”：将执行流从当前物理 PC 跳转到对应的高位虚拟 PC
        //    获取 pmm_init_continue_virtual 的相对偏移
        uint64_t func_offset = (uint64_t)&pmm_init_continue_virtual - 0; 
        //    计算目标虚拟地址
        uint64_t target_va = KERNEL_VBASE + func_offset;
        //    执行跳转
        ((void (*)(void))target_va)();
    	// --- !! 此处之后的所有代码，都在高位虚拟地址空间中执行 !! ---
    	// --- 此函数永不返回 ---
    }
    // -------------------------------------------------------------------
    // 在虚拟地址模式下继续执行的函数
    // -------------------------------------------------------------------
    void pmm_init_continue_virtual(void) {
        // --- Part B: 在虚拟地址模式下，完成 PMM 的最终初始化 ---
    	 // 1. 计算 npage, 并为 pages 数组找到物理位置
        uint64_t max_pa = find_max_addr_in_map(&g_memory_map);
        npage = max_pa / PGSIZE;
        size_t pages_size = sizeof(struct Page) * npage;
        uintptr_t pages_phys_base = find_space_for_pages(&g_memory_map, pages_size);
    	// 2. 设置全局 pages 指针 (现在可以使用虚拟地址了)
        //    KADDR 宏现在可以工作了，因为它依赖的 va_pa_offset 已经确定
        va_pa_offset = KERNEL_VBASE - g_kernel_phys_base; 
        pages = (struct Page *)KADDR(pages_phys_base);
        // 3. 初始化所有 Page 结构，全部标记为保留
        for (size_t i = 0; i < npage; i++) {
            SetPageReserved(&pages[i]);
        }
    	// 4. 初始化 PMM 实例 (选择算法)
        init_pmm_manager();
    	// 5. 将所有探测到的、真正空闲的 RAM 块加入 PMM
        for (each region R in g_memory_map) {
            uintptr_t free_base = R.base;
            size_t free_n = R.length / PGSIZE;
            // ... (复杂的地址计算，扣除内核、pages 数组等已用空间) ...
            if (free_n > 0) {
                init_memmap(pa2page(free_base), free_n);
            }
        }
    	// 6. PMM 完全就绪，运行自检
        check_alloc_page();
        
        // 7. 调用最终的、高层的内核初始化函数
        kern_init();
    }

    // 动态创建最终的内核页表函数
    void create_final_kernel_pagetable(uintptr_t pgdir_pa, uintptr_t kernel_phys_base, MemoryMap *map) {
        // 1. 获取一个指向 L3 PDT 物理页的指针。
        //    因为我们此时仍在物理地址模式下，可以直接使用这个物理地址。
        //    这个 pgdir_pa 是由 allocate_pagetable_page_manually() 分配并清零的。
        pte_t *pgdir = (pte_t *)pgdir_pa;
        
        // 2. 动态计算映射所需的物理基地址和 PPN。
        //    一个大大页(1GB)映射必须在物理上按 1GB 对齐。
        //    我们需要找到包含内核的那个 1GB 对齐的物理区域的起始地址。
        uintptr_t pa_base_for_mapping = ROUNDDOWN(kernel_phys_base, 1UL << 30);

        //    根据这个物理基地址，计算出对应的物理页号 (PPN)。
        uint64_t ppn = pa_base_for_mapping >> 12;

    	// 4. 遍历并填充 512 个页目录表项 (PDE)。
    	for (int i = 0; i < 512; i++) {
    	
    	    // 检查当前是否是第 512 项（索引 511）。
    	    // 这一项对应于最高的 1GB 虚拟地址空间，也是我们计划放置内核的地方。
    	    if (i == 511) {
    	        // 将我们动态计算出的 PPN 和权限组合起来，填入这个槽位。
    	        pgdir[i] = (ppn << 10) | perms;
    	    } 
    	    else {
    	        // 对于所有其他的虚拟地址区域（低 511GB），我们都不使用。
    	        // 将它们的 PDE 设置为 0，表示无效 (invalid)。
    	        pgdir[i] = 0;
    	    }
    	}

    // 函数结束，pgdir_pa 指向的物理页现在已经是一个功能完备的 L3 PDT。
    }
    ```

我们修改后的伪代码从entry.S开始，到进入kern_init之前这一段过程中完成对可用物理内存地址范围的探测和基于物理内存信息的页表初始化设置和MMU激活以及从物理地址跳转到虚拟地址空间继续运行并把DRAM块链入空链表这三步，保证在进入kern_init之前已经开启了sv39的页表机制，后续过程可以正常进行。

# **实验知识点与 OS 原理的理解**

1. 连续物理内存分配（PMM）

    连续物理内存分配 (PMM) 直接对应于操作系统原理中的动态分区分配。理论指出，为有效利用内存，OS 需通过链表或位图等数据结构管理空闲分区，并采用 First-Fit、Best-Fit 等策略进行分配。本实验的 PMM 正是这一原理的精确实现：它通过一个按物理地址排序的双向链表 `free_list`​ 来管理所有空闲物理页块，`alloc_pages`​ 函数实现了分配时的查找与分裂，而 `free_pages` 则负责回收时的插入与合并，从而有效地对抗外部碎片。虽然理论中的分区大小可变，但本实验将其规范化为固定大小的页，这是一种更贴近现代硬件的工程实践。PMM 作为最底层的内存管理工具，为未来所有用户进程的创建和内核自身的动态需求提供了唯一的物理内存来源。
2. 页式内存管理与页表

    页式内存管理与页表的初步实现，是本实验迈向现代操作系统的关键一步。OS 原理阐明了分页机制如何通过页表建立逻辑页到物理页帧的映射，从而允许非连续的物理内存分配。我们在 `entry.S`​ 中手动构建的 `boot_page_table_sv39`​，就是一个最基础的页表实例。通过将其地址加载到 `satp` 寄存器以激活 MMU，我们成功实现了从逻辑地址到物理地址的硬件翻译，这正是原理中硬件辅助的动态重定位的体现。然而，与理论中为稀疏用户空间设计的、需要动态创建的完整三级页表不同，Lab 2 的实现是一个高度简化的特例：我们利用大大页映射特性，静态地创建了一个只有一项有效条目的顶级页目录表，以此绕过了真正的三级查询，一步完成了对内核空间的映射。因此，本次实验实现了分页机制的启用，但其动态管理的完整软件逻辑尚待未来实验完成。
3. 内核地址空间设计

    内核地址空间设计在本实验中体现为典型的高半核模型，这与 OS 原理中逻辑地址与物理地址分离以实现内存保护的思想紧密相关。通过修改链接脚本 `BASE_ADDRESS`​ 并构建相应的初始页表，我们将内核从物理地址 `0x8020...`​ “重定位”到了一个极高的虚拟地址 `0xFFFFFFFFC020...`​。这一设计决策，在虚拟地址空间中清晰地划分出了内核空间（高地址）与用户空间（低地址），不仅为未来的用户进程预留了广阔、干净的低地址空间，更是实现内存保护的基础。在后续的实现中，可以利用页表项中的 `U` 标志位和 MMU 硬件，来严格禁止用户程序访问映射在内核空间中的任何地址，从而保证内核的稳定与安全。
4. 伙伴系统的伙伴识别机制

    原理中，我们学习到伙伴地址可以通过 XOR 位运算快速定位。但实验中，我们遇到了一个关键的现实问题：理论上的 XOR 算法要求地址从0开始且对齐，但 ucore 的物理内存管理区 base 地址并不满足此条件。这让我们深刻理解到，原理给出的是理想模型，而实验则必须处理“地址偏移”这类工程细节。通过引入 buddy_base 并计算相对偏移量，我们将不完美的现实映射到了完美的理论模型上，这是从理论到实践的关键一步。
5. 内存池的初始化与数据结构建立

    原理通常假设内存池的总大小是2的N次方。但实验中，page_init 传递过来的是一块任意大小 n 的内存。buddy_init_memmap 中的贪心算法，即将任意大小的内存分解为多个2的幂次方块的组合，正是为了解决这个理论与现实不匹配的问题。它告诉我们，在真实系统中，初始化过程往往需要一个“适配层”，来将不规整的硬件资源转化为算法所需的规整数据结构。
6. 内存控制块

    原理中，管理空闲内存需要MCB来记录块的大小、状态等信息。在实验中，我们没有定义一个全新的 buddy_MCB 结构，而是创造性地复用了现有的 struct Page。我们将 property 字段 repurposed 为存储“阶数”，将 PG_property 标志位 repurposed 为“是否为空闲块头”。这体现了系统编程中的一种重要思想：在满足功能的前提下，尽可能复用现有数据结构，以降低内存开销和复杂性。
7. 位图管理的对象状态跟踪
   
    位图管理在SLUB分配器中用于跟踪对象的分配状态，是内存分配元数据管理的关键实现技术。实验中使用位图来标记slab中每个对象的占用或空闲状态，每个对象仅占用一个比特位，实现了用极小的元数据开销管理大量小对象的目标。这与操作系统原理中"用少量元数据管理大量数据"的核心思想一致，位图相比链表等传统管理结构不仅大幅节省存储空间，而且通过位运算可以快速定位空闲对象。
8. 按需分配与延迟初始化机制
   
    实验中的SLUB分配器采用按需分配策略，仅在确实需要存储对象时才分配新的slab页面，而非预先分配所有可能的内存空间，这体现了延迟初始化的设计思想。该机制与虚拟内存系统中的按需分页原理契合，两者都遵循"用时分配"的核心原则，通过推迟资源分配时机来避免不必要的内存占用。
9.  SLUB内存管理架构
    
    本实验实现的slub分层内存管理架构将内存分配划分为页级和对象级两个层次，底层页分配器负责大块物理内存管理，上层SLUB分配器则通过固定大小对象池来管理小内存分配。这种设计对应原理中的Slab分配器，这是建立在伙伴系统之上的内存管理机制，用于高效分配小对象。Slab分配器将内存划分为多个缓存（cache），每个缓存存储相同类型的对象。每个缓存由多个slab组成，每个slab是一个连续的内存页，被划分为多个相同大小的对象。Slab分配器通过维护对象的状态（已分配或空闲）来快速分配和释放对象，避免了频繁调用伙伴系统，提高了小内存分配的效率。SLUB分配器则是slab分配器的优化演进版本。

# 

‍
