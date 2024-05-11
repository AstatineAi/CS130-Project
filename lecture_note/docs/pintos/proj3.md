# Project 3: Virtual Memory

!!! warning "No plagiarism"
    If you are enrolled in CS130, you may not copy code from this repository.

!!! warning "急了急了急了"
    Project 3 基于 Project 2, 希望你的 Project 2 足够 robust.

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

Pintos 的虚拟内存分为 user virtual memory 和 kernel virtual memory, 内核内存全局共享, 在 project 2 使用的 `thread/malloc` 里面实现的 `malloc/free` 和 `palloc_get_page (0)` 都是从此部分内存中分配.

用户的 virtual address 范围为 `0` 到 `PHYS_BASE` (0xc0000000, 3 GB), 内核的 vritual address 范围为 `PHYS_BASE` 到 4 GB, 且内核 virtual memory 和 physical memory 映射关系固定为 `vaddr = paddr + PHYS_BASE`

在 `userprog/pagedir.c pagedir_activate()` 通过内联汇编修改 `CR3` 寄存器 (若该进程未设置 page directory, 则使用只映射了 kernel pool 的 `init_page_dir`), 更改 CPU 查找虚拟内存时使用的 page directory, 来实现切换线程时的内存切换.

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

但是如果需要通过页编号找到对应的 page table, 就需要保证所有的 page table 都在内存中, 但是这样会导致 page table 占用大量内存. 引入 page directory, 划分地址的高 10 位为 page directory index, 低 10 位为 page table index, 末尾 12 位为 offset.

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

首先需要建立 frame 结构, frame 需要从 user pool 取得 (`palloc_get_page (PAL_USER)`).

文档提到:

> Synchronization is also a concern: how do you deal with it if process A faults on a page whose frame process B is in the process of evicting?

可知不能并发访问一个 frame, 需要对每个 frame 加锁.



## Task 2



## Task 3



## Task 4



## Task 5


