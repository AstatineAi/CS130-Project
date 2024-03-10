# Project 2: User Program

!!! warning "No plagiarism"
    If you are enrolled in CS130, you may not copy code from this repository.

!!! note "Overview"
    - Task1 : print termination messages
    - Task2 : argument passing
    - Task3 : system calls
    - Task4 : deny writes to executables

## 分析

在 `src/tests/userprog/Make.tests` 内有此 project 的自测数据点.

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