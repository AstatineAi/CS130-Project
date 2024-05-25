# Project 3: Virtual Memory

!!! warning "No plagiarism"
    If you are enrolled in CS130, you may not copy code from this repository.
    Project 3 强调设计, 具体实现因人而异. 如果你真的没有头绪, 可以先看看 design document 模板.

!!! warning "急了急了急了"
    Project 3 基于 Project 2, 希望你的 Project 2 足够 robust 且有合适的非法地址检测.

!!! note "Overview"
    - Task 1 : paging
    - Task 2 : stack growth
    - Task 3 : memory mapping files
    - Task 4 : accessing user memory

## 文件系统限制依然存在

- 可能并发访问文件, 考虑加锁
- 文件大小固定, 控制文件名长度不超过 14 字符

## 分析

空啊, 很空啊 (指 vm 文件夹).

`Make.vars` 里面添加了宏定义 `VM`, 可以像之前的 `#ifdef USERPROG` 一样使用.

新增的 `.c` 文件需要在 `Makefile.build` 里面添加到 `vm_SRC`.

### 什么是 virtual address

对 virtual address 的支持是从 bare metal 开始的 (到达硬件 (相对来说比较) 底层! 太美丽了 CPU, 哎呀这不 DRAM 吗, 还是看看远处的一生一芯吧家人们), 回看 pintos 的启动过程, `start.S` 里面的汇编最开始直接访问的是物理地址, 但是在启动过程中 `movl %eax, %cr3` 设置了 CR3 寄存器, 即在进入保护模式之后, 一切 "按照地址访问" 实际上都是访问虚拟地址.

```
+------------------+  <- 0xFFFFFFFF (4GB)
|      32-bit      |
|  memory mapped   |
|     devices      |
|                  |
/\/\/\/\/\/\/\/\/\/\
/\/\/\/\/\/\/\/\/\/\
|                  |
|      Unused      |
|                  |
+------------------+  <- depends on amount of RAM
|                  |
|                  |
| Extended Memory  |
|                  |
|                  |
+------------------+  <- 0x00100000 (1MB)
|     BIOS ROM     |
+------------------+  <- 0x000F0000 (960KB)
|  16-bit devices, |
|  expansion ROMs  |
+------------------+  <- 0x000C0000 (768KB)
|   VGA Display    |
+------------------+  <- 0x000A0000 (640KB)
|  pintos kernel   |
+------------------+  <- 0x00020000 (128KB)
|  page tables     |
|  for startup     |
+------------------+  <- 0x00010000 (64KB)
|  page directory  |
|  for startup     |
+------------------+  <- 0x0000f000 (60KB)
|  initial kernel  |
|   thread struct  |
+------------------+  <- 0x0000e000 (56KB)
|        /         |
+------------------+  <- 0x00007e00 (31.5KB)
|   pintos loader  |
+------------------+  <- 0x00007c00 (31KB)
|        /         |
+------------------+  <- 0x00000600 (1536B)
|     BIOS data    |
+------------------+  <- 0x00000400 (1024B)
|     CPU-owned    |
+------------------+  <- 0x00000000
```

除了上面这张图的 "0x....." 是确实在硬件里面的地址, 其他都是虚拟地址 (进入保护模式了).

然后这层地址是 start.S 创造的只映射了几 MB 的 page directory 的状态, 在高级初始化过程中, `paging_init ()` 函数初始化了更完整的 page directory (`init_page_dir`), 然后修改 CR3 寄存器, 使得 CPU 从此开始使用新的 page directory.

这个 page directory 确定了 kernel virtual memory 到 physical memory 的映射, 内核的 virtual memmory 范围为 `PHYS_BASE` 到 `0xFFFFFFFF` (4 GB), 且内核 virtual memory 和 physical memory 映射关系固定为 `vaddr = paddr + PHYS_BASE`.

由于 CPU 必须读取 page table 的物理地址, 但 x86 没有提供直接读写物理地址的指令, 所以需要 kernel virtual memory 的这种线性映射关系, 以实现修改 page directory / page table 的目的.

在 kernel 的 1 GB 内存中, 还保存了 page 分配的信息 (bitmap), 以此实现了 palloc get/free 的功能.

现在的 virtual memory 布局如下:

```
   (4 GB)    +----------------------------------+
             |                                  |
             |       kernel virtual memory      |
             |                                  |
   PHYS_BASE +----------------------------------+
             |            user stack            |
             |                 |                |
             |                 |                |
             |                 V                |
             |          grows downward          |
             |                                  |
             |         LOL, no heap memory      |
             |                                  |
             |                                  |
             |           grows upward           |
             |                 ^                |
             |                 |                |
             |                 |                |
             +----------------------------------+
             | uninitialized data segment (BSS) |
             +----------------------------------+
             |     initialized data segment     |
             +----------------------------------+
             |           code segment           |
  0x08048000 +----------------------------------+
             |                                  |
             |                                  |
             |                                   |
             |                                  |
             |                                  |
           0 +----------------------------------+
```

内核内存全局共享, 在 project 2 使用的 `thread/malloc.c` 里面实现的 `malloc()`, `free()` 和 `palloc_get_page (0)` 都是从此部分内存中分配.

在 `userprog/pagedir.c pagedir_activate()` 通过内联汇编修改 `CR3` 寄存器 (若该进程未设置 page directory, 则使用只映射了 kernel virtual memory 的 `init_page_dir`), 更改 CPU 查找虚拟内存时使用的 page directory, 来实现切换线程时的内存切换.

所有的新的 page directory 都是在 `init_page_dir` 基础上加入了用户内存的映射, 保证可以访问内核内存和用户内存.

page directory 保证了通过 page table 编号可以找到对应 page table.

在一个 page 被切换出去后, 使用这个 page 的进程可能又要用到这个 page, 需要把切出的 page 放到 disk 上, 以便在需要时再次载入到内存.

user virtual memory 布局和 C 程序内存布局相似, 栈区从高地址向低地址增长, 但是不支持动态分配堆区, 全局区在低地址, 加载可执行文件时被初始化.

由于每个 page 的大小都相同, 且每个 page 都是对齐的, 即第 $k$ 个页的虚拟内存范围为 $[k \times pageSize, (k+1) \times pageSize)$.

即用一个地址的低位表示这个地址在对应页相对页开头的偏移量 offset, 高位表示页编号, 恰好可以完成地址到页和页内地址的映射.

Pintos 页大小为 4096 字节, 低位 12 位用于描述 offset, 则前 20 位可以用于页编号.

```
    31               12 11        0
    +-------------------+-----------+
    |    Page Number    |   Offset  |
    +-------------------+-----------+
            Virtual Address
```

页编号用于找到对应的 page table, page table 找 frame, frame 加上 virtual address 中的 offset 部分映射到物理内存.

但是如果需要通过页编号找到对应的 page table, 就需要保证所有的 page table 都在内存中, 但是并不是所有的 page 都存在, 这样会导致 page table 占用大量内存. 引入 page directory, 划分地址的高 10 位为 page directory index, 低 10 位为 page table index, 末尾 12 位为 offset.

```
    31                22 21        12 11          0
    +-------------------+------------+------------+
    | Page Directory Idx| Page Table |   Offset   |
    +-------------------+------------+------------+
                Virtual Address

```

除此之外, 还需要 swap slot, 用于明确如何存储被换出的 page.

### virtual address -> physical address

回顾目前的结构

1. 每个 process 有一个 page directory, 隔离了不同进程的内存
2. page directory 包含许多 page table
3. page table 包含许多 page
4. page 表示一段虚拟内存
5. page 的情况
    - 有的 page 对应了 frame
    - 有的 page 对应了 swap slot
    - 有的 page 地址合法但是没有被分配

现在, 我们有一个 32-bit 的 virtual address, 需要找到对应的 physical address.

1. 通过 `struct thread` 的 `pagedir` 指针找到 page directory
2. 通过 virtual address 的高 10 位在 page directory 中找到指向对应的页目录项 (page table 的物理地址) 的指针  `lookup_page() pde = pd + pd_no (vaddr);`
3. 通过 kernel pool 里面 virtual address 和 physical address 的映射关系找到对应的 page table 的虚拟地址 `lookup_page() pt = pde_get_pt (*pde);`
4. 通过 virtual address 的中间 10 位在 page table 中找到指向对应的页表项 (page 的地址) 的指针 `lookup_page() return &pt[pt_no (vaddr)];`

### `palloc_get_page (PAL_USER)` 在干什么

我们可以认为 `palloc_get_page (PAL_USER)` 是在分配一个 frame, 在 physical memory 的比 kernel 映射到的更高位的地址获得了一个 frame, 不过需要 `install_page` 将这个 frame 写入 page dir & page table 和 virtual address 建立映射关系. (如果是 `palloc_get_page (0)`, 那么映射关系已经在 `init_page_dir` 里面建立好了, 直接按返回的指针使用即可)

也就是说, user program 认为高地址是栈区, 低地址是全局区, 但是实际上这些地址都是虚拟地址, 不一定真的在物理内存的高/低地址.

### page fault

在有虚拟内存时, page fault 有更多种情况

- 访问的虚拟地址合法
    - 没有对应的页
    - 对应的页没有载入内存
- 访问的虚拟地址非法
    - 中止进程, 防止操作系统崩溃

### 总结

Pintos 的内存结构决定了我们需要管理的部分为加载可执行文件时的静态/全局区内存, 以及栈内存 (在访问到时实现 stack growth 而不是结束进程), 这些部分是会被 swap in/out 的, 在 `PHYS_BASE` 之上的内核内存不需要这方面考虑.

用户内存行为包括

1. 静态区内存: 在低地址, 大小在加载可执行文件时可以确定.
2. 栈区: 在高地址, 需要确定一个增长策略.
3. 非法访问: 导致 exit -1

静态区需要实现 lazy loading, 即加载可执行文件时不载入内存, 在访问时出现 page fault 时载入.

栈区需要实现 stack growth, 先判断当前行为是可能导致堆栈还是非法访问, 然后根据情况处理是否要为栈区分配新的 page.

除此之外, 需要实现 swap in/out, 在无法分配新的 frame 时, 将不常用的 page 换出到 disk 上. 有被修改的 page 需要写回 disk (有 dirty bit 的情况), 而没有被修改的 page 可以直接丢弃, 在需要时可以从原来的文件载入.

## Task 1: Paging

### 结构

首先需要建立 page table 结构, 把每个 frame 需要从 user pool 取得 (`palloc_get_page (PAL_USER)`, 需要清空则改为 `palloc_get_page (PAL_USER | PAL_ZERO)` ) 的过程套壳, 方便记录信息. 在分配一个 page 之后, 需要调用 `install_page` 将 page 添加到当前进程的 page directory 中.

```c
struct frame_or_whatever {
    void *kaddr;
};
```

文档提到:

> To simplify your design, you may store these data structures in non-pageable memory. That means that you can be sure that pointers among them will remain valid.

这里的 non-pageable memory 指的是 kernel pool, 也就是说最好在 kernel pool 里面分配 frame.

如果改成加上 ref count, 可以实现共享 frame (想了一下, 好难).

```c
struct frame_table_entry{
    void *kaddr;                        // Kernel address
    struct sup_page_table_entry *spte;  // Supplementary page table entry
    struct thread* owner;               // Owner of the frame
    struct list_elem elem;              // List element for list of all frames
    bool pinned;                        // Is frame pinned
};
```

然后需要实现 supplmentary page table, 用于记录 page 的信息, 状态, 在 page fault 时会以这些东西决定加载 page 方式.

```c
enum sup_page_type {
    PAGE_ZERO,    // All zero page
    PAGE_FILE,    // Page from file
    PAGE_SWAP,    // Page swapped out
    PAGE_MMAP     // Memory mapped file
};

struct sup_page_table_entry {
    void *uaddr;              // User virtual address
    void *kaddr;              // Kernel virtual address

    bool writable;            // Is page writable
    enum sup_page_type type;  // Type of page

    struct lock spte_lock;    // Lock for page

    struct file *file;        // File to load page from
    off_t offset;             // Offset in file
    uint32_t read_bytes;      // Number of bytes to read
    uint32_t zero_bytes;      // Number of bytes to zero

    struct hash_elem elem;    // Hash element for supplementary page table

    size_t swap_index;        // Swap index
};
```

还需要实现 swap, 使用 device/block.h 里面的 block device, 用于读写 swap slot.

在 `swap_init` 里面初始化 swap slot, `block_get_role (BLOCK_SWAP);` 可以获取 swap slot 的 block device.

按照 page 大小决定每个 page 需要多少 sector, 建立一个总 sector 数除以每个 page 需要的 sector 数的 bitmap, 每个 bit 代表这个 swap slot 是否被占用.

### 同步问题

1. 当多个进程申请 frame 时的 race condition
    - `palloc_get_page` 内置了锁, 保证分配是原子的
    - 一个 frame 从 `palloc_get_page` 获取到, 另一个需要 evict frame, 两个进程的 critical section 是修改 frame table 部分, 需要加锁
    - 两个进程同时 evict frame, 需要保证不 evict 同一个 frame
2. 正在被 evict 的 frame 被原来的 frame 持有者访问
    - 保证在换出等等操作之前进入如下状态: 原来持有者通过 page table 访问此 frame 时引起 page fault
    - 需要使用 `pagedir_clear_page` 清除 page table 中的映射关系
3. 一个进程 page fault 并且获取到了 frame, 另一个进程尝试 evict 这个 frame.
4. 正在从 disk 读取 page 时, 此 page 的 frame 不应该被其他进程 evict
    - evict 时从 frame table 移除.
    - 读取完成之前不加入 frame table.
5. 在 syscall 时发生的 page fault
    - 参见 Task 4
6. 多个 page fault 的处理可以并发进行
    - 保证对同一个 frame 的操作是串行的
    - 需要 I/O 的靠 I/O 的锁互斥
    - 需要 swap 的靠 swap block 的锁互斥

### page fault 情况与对策

| 情况编号 | `not_present` | `write` | `user` | 情况 | 对策 |
| ------- | ------------- | ------- | ------ | --- | --- |
|   (1)   | `false` | `false` | `false` | 内核非法访问 | `kill()` |
|   (2)   | `false` | `false` | `true`  | 用户非法访问 | `kill()` (导致 `exit(-1)` ) |
|   (3)   | `false` | `true`  | `false` | 同理 1 (例如写不可写的内存) | 同 |
|   (4)   | `false` | `true`  | `true`  | 同理 2 | 同 |
|   (5)   | `true`  | `false` | `false` | 在 syscall 中遇到 page fault | 尝试载入 & pin, 在 syscall 结束之前 unpin |
|   (6)   | `true`  | `false` | `true`  | 被换出/没有加载/栈增长 | 尝试载入 |
|   (7)   | `true`  | `true`  | `false` | 同 5 | 同 |
|   (8)   | `true`  | `true`  | `true`  | 同 6 | 同 |

## Task 2: Stack growth

在最开始分配一个 page 作为栈, 后续考虑栈增长的情况. 按照 80x86 PUSHA 最多在 esp 低 32 字节的位置引发 page fault, 考虑将 32 字节作为分界, 低于 32 字节时分配新的 page, 否则视为非法访问.

栈增长锁分配的 frame 也可能被 swap out, 此时总是需要保存 frame 的内容, 以便在需要时载入.

可以限制一个进程最大栈区大小, 以防止无限增长.

## Task 3: Memory mapping files

mmap 会将文件映射到连续的页.

mummap 会将文件从内存中移除, 将 dirty 的 page 写回文件.

## Task 4: Accessing user memory

需要保证在进入内核时, 用户内存是合法的, 且需要避免一些持有资源情况下遇到 page fault 的情况.

情景: 进程 A syscall read, 但是 read 的 buffer 不在内存中, 需要为 buffer 分配 frame, 此时 A 持有 `filesys_lock`. 进程 B 引发 page fault, 获取了 `frame_table_lock`, 但是为了 evict frame 需要获取 `filesys_lock` (例如从 B 的可执行文件读 bss 信息, 把 evict 的部分写回文件系统), 此时 A 和 B 互相等待对方释放锁, 造成死锁.

可能比较好的方式是, 通过检验地址是否在 page table 中, 然后把没有加载的 page 加入到 frame table 中且 pin 住. 此时在访问文件系统的部分就不会出现 page fault, 在 syscall 结束之前 unpin 即可.

## 实现

### swap

定义于 `vm/swap.h`, `vm/swap.c`, 用于管理 swap slot.

在 `thread/vaddr.h` 里面定义了 `PGSIZE` (4096), 表示 page 的大小. 在 `devices/block.h` 里面定义了 `BLOCK_SECTOR_SIZE`, 表示 block 的大小 (512), 于是每个换出的 page  占用的 sector 数即为 `PGSIZE / BLOCK_SECTOR_SIZE`.

包含一个 bitmap 用于记录 swap slot 的使用情况. 在初始化时, 通过 `block_get_role (BLOCK_SWAP)` 获取 swap block, 然后计算出 swap slot 的数量, 初始化 bitmap.

由于不确定 block 是否支持并发读写, 所以需要加全局锁 `swap_lock`, 如果可以并发读写, 那么只需要锁 `swap_bitmap` 即可.

在 `swap_out` 时, 找到一个空闲的 swap slot, 设置为占用, 然后将 frame 写入 swap slot, 返回 swap slot 的编号.

在 `swap_in` 时, 读取 swap slot 的内容, 将其写入 frame, 然后释放 swap slot.

### frame

定义于 `vm/frame.h` ,`vm/frame.c`, 用于管理 frame.

```c
struct frame_table_entry {
    void *kaddr;                        // Kernel address
    struct sup_page_table_entry *spte;  // Supplementary page table entry
    struct thread* owner;               // Owner of the frame
    struct list_elem elem;              // List element for list of all frames
    bool pinned;                        // Is frame pinned
};
```

frame 需要实现包含了 eviction 的与 `palloc_get_page (PAL_USER)`, `palloc_free_page` 同功能的函数.

由于使用 `list` 来管理 frame, 所以需要加锁 `frame_list_lock`.

### page

```c
enum sup_page_type
  {
    PAGE_ZERO,    // All zero page
    PAGE_FILE,    // Page from file
    PAGE_SWAP,    // Page swapped out
    PAGE_MMAP     // Memory mapped file
  };

struct sup_page_table_entry
  {
    void *uaddr;              // User virtual address
    void *kaddr;              // Kernel virtual address

    bool writable;            // Is page writable
    enum sup_page_type type;  // Type of page

    struct lock spte_lock;    // Lock for page

    struct file *file;        // File to load page from
    off_t offset;             // Offset in file
    uint32_t read_bytes;      // Number of bytes to read
    uint32_t zero_bytes;      // Number of bytes to zero

    struct hash_elem elem;    // Hash element for supplementary page table

    size_t swap_index;        // Swap index
  };
```

在 `struct thread` 内加上 `struct hash sup_page_table`, 注意 `hash` 初始化时需要分配内存用于创建桶, 需要在系统初始化完成后才能调用, 不在创建 `main` 等内核线程的时候调用即可.

#### 创建 supplementary page table

#### 使用 supplementary page table

### 加载可执行文件

实现 lazy loading

一个 page 有以下几种情况:

- 来自可执行文件 (程序静态区内存)
    - 没有加载 `PAGE_FILE` / `PAGE_ZERO`
    - 加载到了内存里面, dirty / 不 dirty `PAGE_FRAME`
        - 由是否 dirty 决定是否需要换出.
        - 一旦 dirty 就永远被标记为 dirty
    - 被换出, 在 swap slot 里面 `PAGE_SWAP`
- 来自文件系统 (mmap) `PAGE_MMAP`
    - 加载到了内存里面, dirty / 不 dirty
    - 被换出, 即被存回文件里面
- 栈
    - 栈增长新增的 `PAGE_FRAME`
    - 被换出 `PAGE_SWAP`

### 处理 page fault

#### 栈增长

32 位机器, `PUSHA` 指令会将 32 字节的数据压入栈, 所以可以认为最大的栈增长是一次 32 字节的操作, 超过 32 字节的操作视为非法访问.

同时检查当前总计的栈区大小, 如果超过了限制, 也视为非法访问.

但是存在一个问题, 如果在 kernel mode 触发 page fault, 此时没有栈指针的信息, 需要在 syscall 时记录以实现 syscall 中栈增长.

#### 加载

在 kernel 情况下发生的 page fault, 需要判断是否是合法访问, 如果是合法访问, 需要将 page 加入到 frame table 中, 并 pin 住, 在 syscall 结束之前 unpin, 防止死锁

在 user 情况下发生的 page fault 不考虑 pin 的问题.

### mmap/munmap

在 `struct thread` 内加上 `struct list mmap_list`
