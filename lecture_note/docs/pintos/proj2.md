# Project 2: User Program

!!! warning "No plagiarism"
    If you are enrolled in CS130, you may not copy code from this repository.

!!! warning "急了急了急了"
    强烈建议从未修改的框架代码版本开始写 Project 2, 由于 Project 1 涉及到对 `thread`, `semaphore`, `lock` 等的修改, 可能在 Project 2 导致未知错误.

!!! note "Overview"
    - Task1 : print termination messages
    - Task2 : argument passing
    - Task3 : system calls
    - Task4 : deny writes to executables

## 调试

为了将可执行文件写入 pintos 的虚拟硬盘, 需要添加参数 `-p tests/userprog/filename -a filename`, 每个文件增加一组上述形式的参数.

## 分析

在 `tests/userprog/Make.tests` 内有此 project 的自测数据点.

```
tests/userprog/args-none_SRC = tests/userprog/args.c
tests/userprog/args-single_SRC = tests/userprog/args.c
tests/userprog/args-multiple_SRC = tests/userprog/args.c
tests/userprog/args-many_SRC = tests/userprog/args.c
tests/userprog/args-dbl-space_SRC = tests/userprog/args.c
```

前面几个测试用到了 `args.c`

```c
test_name = "args";

msg ("begin");
msg ("argc = %d", argc);
for (i = 0; i <= argc; i++)
if (argv[i] != NULL)
    msg ("argv[%d] = '%s'", i, argv[i]); // 需要把 arg 分隔开
else
    msg ("argv[%d] = null", i);
msg ("end");

return 0; // 我该怎么得到你, 亲爱的返回值?
```

主要内容为打印命令行参数, 也就是需要实现为 user program 传参.

分析调用过程: 在 `threads/init.c` 中, `main() -> run_actions() -> run_task() -> process_wait (process_execute (task))`

看一下 `process_wait()`

```c
int
process_wait (tid_t child_tid UNUSED) 
{
  return -1;
}
```

按照注释里的内容, 这个函数作用是等待编号 `child_tid` 线程的运行结果, 如果 _那个线程被系统中止/传入的 `tid` 不存在/传入的 `tid` 代表的进程不是目前线程的子进程/这个线程已经有一个 `process_wait()` 在等待它_ 则返回 `-1`, 否则返回它通过 `exit` 传递的返回值.

## 文件系统限制

- 可能并发访问文件, 考虑加锁
- 文件大小固定, 控制文件名长度不超过 14 字符
- 没有虚拟内存, 文件必须占用连续的物理内存, 合理分配和回收

## Task 1

在一个进程退出时输出中止信息 (名称, exit code)

在 `process.c` 内查找发现有一个 `process_exit()`, 用于释放一个进程分配的资源, 这个函数在 `thread_exit()` 被调用, 由于 pintos 的一个进程下没有多线程, 所以直接用该线程的 exit 代表.

于是需要在 `process_exit()` 内加上信息打印.

进程名称来自线程名称, 在 `process_excute()` 内打印, 发现 `file_name` 不仅传入了要运行的文件的名称, 还有运行参数, 是一行完整的指令, 需要手动按空格分离函数名和以空格 (可能多个空格) 分隔开的每个 command line args.

`process_execute()` 内, `palloc_get_page(0)` 获得了 kernel pool 的一个 page, 存储完整的命令, 在 `start_process()` 中 `load()` 加载可执行文件完毕后将其释放, `thread` 中的 `name` 使用 `memcpy` 复制到字符数组, 而 command line args 需要直接压到栈里面, 所以说可以通过在前面分配, 在 `start_process()` 内释放的形式临时保存命令行语句.

这样, 在 `process_execute()` 时直接分割出可执行文件名称, 作为线程名称即可.

你说得对, 但是我返回值呢?

做不了一点, 看下一个 task

## Task 2

为  user program 传参.

首先为了让 progress 跑起来, 把 `progress_wait()` 改成 `while(1)` 死循环, 防止被终止, 以后再实现正常的 wait.

看完文档的例子:
			
| Address   | Name        | Data      | Type    |
|-----------|-------------|-----------|---------|
| 0xbfffffc | argv[3][...] | bar\0     | char[4] |
| 0xbfffff8 | argv[2][...] | foo\0     | char[4] |
| 0xbfffff5 | argv[1][...] | -l\0      | char[3] |
| 0xbffffed | argv[0][...] | /bin/ls\0 | char[8] |
| 0xbffffec | word-align   | 0         | uint8_t |
| 0xbffffe8 | argv[4]      | 0         | char *  |
| 0xbffffe4 | argv[3]      | 0xbfffffc | char *  |
| 0xbffffe0 | argv[2]      | 0xbfffff8 | char *  |
| 0xbffffdc | argv[1]      | 0xbfffff5 | char *  |
| 0xbffffd8 | argv[0]      | 0xbffffed | char *  |
| 0xbffffd4 | argv         | 0xbffffd8 | char ** |
| 0xbffffd0 | argc         | 4         | int     |
| 0xbffffcc | return address | 0       | void (*)() |

指针最开始在 `PHYS_BASE` 的位置, 首先堆入每个 `argv` 的串的内容, 堆的时候指针向地址较小的方向移动, 保证每个串在内存上顺序, 且包含代表字符串结束的 `\0`, 串之间顺序无所谓, 保存地址指针.

然后进行对齐.

然后把 `argv[i]` 的地址, 以及 `argv` 的地址压入栈, 最后压入 `argc` 和 `return address`.

此时传参结束, 汇编跳转后调用

```c
void
_start (int argc, char *argv[])
{
  exit (main (argc, argv));
}
```

开始运行 user program. 此时会调用 `syscall`, 进入 `syscall_handler` 环节.

## Task 3

实现 13 种 system call.

system call 属于内部中断, 和之前的 timer interrupt 等 CPU 外设备造成的中断不同.

首先查看 handler 如何处理 system call.

`syscall_init()` 保证了在程序使用 `int 0x30` 中断时调用 `syscall_handler()`, 查看 `src/lib/user/syscall.c` 宏, 使用汇编进行压栈, handler 需要从 `intr_frame` 获取信息.

### 确定调用种类

宏有 `syscall0`, `syscall1`, `syscall2`, `syscall3` 四种, 压栈 syscall 类型和参数, 则可以从 `esp` 指针获取 system call 类型, 然后再向高地址移动 `esp` 获取后面的参数.

`syscall_id = *(int *)f->esp;`

### halt

无参数, 直接关机.

### exit，exec，wait

问题: 进程在被 wait 之前调用 exit

需要适当保存一个进程所有子进程的 exit status. 如果将 exit status 保存在 `struct thread` 里面, 在 `thread_exit()` 时, 会释放这个结构, 无法保存 exit status.

可能有如下几种情况：

1. 子进程调用 `exit`，然后父进程调用 `wait`
2. 父进程在子进程完成初始化之前调用 `wait`，访问了未初始化的内存
3. 子进程创建过程中，加载可执行文件/分配内存失败，父进程调用 `wait` 需要得到合法的返回值
4. 子进程已经被 `wait`，父进程再次调用 `wait`，应该返回 -1
5. 父进程终止时子进程的返回情况不再被需要

考虑使用 `thread/malloc` 内实现的 `malloc()`, 这个函数在 kernel pool 内分配内存, 保证了父进程, 子进程都可以在内核态访问到, 且在进程终结时不会被立即释放, 可以等待父进程去释放.

在分配内容较少时, 避免使用 `palloc_get_page(0)`

1. 在 `thread_create()` 时, 为子进程分配内存, 返回 `tid`，初始化用于让父进程等待加载的一个信号量
2. 父进程 `sema_down()` 等待子进程加载完毕
3. 在子进程加载过程中，保存加载情况，保证中途加载失败然后 `sema_up()` 可以让父进程得知加载失败
4. 为了实现 `wait`，为每个进程创建子进程列表，列表内元素负责保存子进程的 `tid` 和 `exit status`，且包含一个信号量，用于父进程等待子进程结束，在 `wait` 时，父进程 `sema_up()`，在子进程结束时，若父进程存活则找到父进程的列表里面对应元素，更新 `exit status`，然后 `sema_up()` 通知父进程
5. 进程终止时，释放子进程列表

### 文件系统相关 syscall

包含 `create`, `remove`, `open`, `filesize`, `read`, `write`, `seek`, `tell`, `close`

实现一套为每个进程管理文件的文件描述符（file descriptor）体系。在打开文件时分配 `fd`，注意 `fd` 不能为 0, 1，这两个文件描述符用于标准输入输出。

在进程终止时，关闭所有文件，清空文件描述符表。

### 禁止访问非法地址

我采用了文档里面提到的：在实际访问之前，访问每个可能被用到的地址，如果有非法访问，触发 page fault，然后在 page fault handler 里面处理。

访问用到的函数在文档里面已经给出。在 syscall 需要任何读/写操作时，先全部访问一遍（字符串则需要访问每个字符），然后再进行操作。

错误的地址访问会触发 page fault，所有的 page fault 会被 `userprog/excepion.c` 中对应的 handler 处理，注意到原本的策略是使用 `thread_exit()`，现在实现了用户进程，改为使用 exit system call。根据 `page_fault()` 里面提供的 `not_present`，`write`，`user` 三个 flag 选择不同的处理方式即可。

## Task 4

拒绝用户写入可执行文件.

在 `load` 加载完成后使用 `filesys` 下相应代码拒绝写入。

首先此时任何涉及到文件系统的代码都应该互斥，于是整体加一个 `filesys_lock` 保护, 在 `filesys/filesys.h` 内 `extern struct lock filesys_lock;` 方便调用.
