# Project 1: Threads

!!! note "Overview"
    - Task 1 : solve busy waiting
    - Task 2 :
      - priority schedule
      - priority donate
    - Task3 : advanced schedule

## Task 1

`src/device/timer.c` 解决 `timer_sleep()` 的 busy waiting 问题.

```c
/* Sleeps for approximately TICKS timer ticks.  Interrupts must
   be turned on. */
void
timer_sleep (int64_t ticks) 
{
  int64_t start = timer_ticks ();

  ASSERT (intr_get_level () == INTR_ON);
  while (timer_elapsed (start) < ticks) 
    thread_yield ();
}
```

框架代码实现: 不断循环检查是否等待了足够的 ticks, 若等待没有结束则挂起线程.

`int64_t start = timer_ticks ();`

`timer_ticks()` 首先禁止 interrupt, 然后获取运行总 ticks, 最后恢复 interrupt 状态, 保证原子性.

然后在等待足够 ticks 之前一直 `thread_yield ()`

```c
/* Yields the CPU.  The current thread is not put to sleep and
   may be scheduled again immediately at the scheduler's whim. */
void
thread_yield (void) 
{
  struct thread *cur = thread_current ();
  enum intr_level old_level;
  
  ASSERT (!intr_context ());

  old_level = intr_disable ();
  if (cur != idle_thread) 
    list_push_back (&ready_list, &cur->elem);
  cur->status = THREAD_READY;
  schedule ();
  intr_set_level (old_level);
}
```

当前 thread 虽然让出 CPU 但是又在 `ready_list` 里面加上自己, 导致 busy_waiting.




    