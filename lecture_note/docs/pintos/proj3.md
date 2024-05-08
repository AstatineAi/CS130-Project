# Project 3: Virtual Memory

!!! warning "No plagiarism"
    If you are enrolled in CS130, you may not copy code from this repository.

!!! warning "急了急了急了"
    Project 3 基于 Project 2, 希望你的 Project 2 足够 robust.

!!! note "Overview"
    - Task 1 : paging
    - Task 2 : stack growth
    - Task 3 : memory mapping files
    - Task 4 : swapping
    - Task 5 : accessing user memory

## 文件系统限制依然存在

- 可能并发访问一个文件, 考虑加锁
- 文件大小固定, 控制文件名长度不超过 14 字符

## 分析

空啊, 很空啊 (指 vm 文件夹).

`Make.vars` 里面添加了宏定义 `VM`, 可以像之前的 `#ifdef USERPROG` 一样使用.

新增的 `.c` 文件需要在 `Makefile.build` 里面添加到 `vm_SRC`.

在有虚拟内存时, page fault 有更多种情况

- 访问的虚拟地址合法
    - 没有对应的页
    - 对应的页没有载入内存
- 访问的虚拟地址非法
    - 中止, 防止操作系统崩溃

## Task 1



## Task 2



## Task 3



## Task 4



## Task 5


