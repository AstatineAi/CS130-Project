# Project 2: User Program

!!! warning "No plagiarism"
    If you are enrolled in CS130, you may not copy code from this repository.

!!! note "Overview"
    - Task1 : print termination messages
    - Task2 : argument passing
    - Task3 : system calls
    - Task4 : deny writes to executables

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
    msg ("argv[%d] = '%s'", i, argv[i]);
else
    msg ("argv[%d] = null", i);
msg ("end");
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

按照注释里的内容, 这个函数作用是等待编号 `child_tid` 线程的运行结果, 如果被系统中止/传入的 `tid` 不存在/则返回 `-1`, 否则返回