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

当前 thread 虽然让出 CPU 但是又在 `ready_list` 里面加上自己, 导致 busy_waiting, 需要一个统一的唤醒方式, 避免重复 `thread_yield()`.

考虑维护一个按照唤醒时间为关键字的优先队列, 使用二叉堆实现.

最开始使用了

```c
struct sleep_thream_elem
{
  struct thread *t;
  int64_t wake_time;
};
```

作为堆内存储对象, 但是这样就需要 `malloc()` 和 `free()`, 在 `timer_interrupt()` 过程中无法实现, 于是直接在 `struct thread` 内加上 `wake_time`, 在 `timer_sleep()` 时将线程阻塞, 然后 push 到堆内.

在 `timer_interrupt()` 的时候检查堆顶元素是否就绪, 就绪则 `thread_unblock()`.

此时解决了:

```
alarm-wait
alarm-simultaneous
alarm-zero
alarm-negative
```

## Task 2

### alarm-priority

要求高优先级的准备线程优先运行, 使用堆, 比较 `priority` 即可.

### priority-change

要求在更新一个线程优先级之后, 若其不是优先级最高的线程则 yield.

在 `thread_set_priority()` 时直接 `thread_yield()`, 若仍是最高优先级则没有影响.

### priority-donate-one

这是一个来自互斥的问题. 现在我们有一把锁, 然后三个线程 `A, B, C` 想要获得锁, 进入临界区, 其优先级顺序是 `A > B > C`, 此时 `C` 在临界区, `A` 需要获得锁, 但是 `B` 的优先级高于 `C`, 不断抢占 `C`, 导致 `A` 虽然具有最高优先级但是一直无法运行, 这就是优先级反转/优先级倒置.

需要实现优先级捐赠, 即对于锁, 目前有 `A, C` 需要它, 那么我们可以使用高优先级的 `A` 的优先级去代表这个锁的某种 "优先级".

也就是在 `lock_acquire()` 但是并没有获取到的时候需要考虑将自己的优先级转交给 `lock`, 让占用 `lock` 的获取到这个高优先级即可.

### priority-donate-multiple

被捐赠了优先级不代表线程可以一直拿着那个优先级, 不需要避免优先级反转的时候, 要恢复到没捐赠的优先级.

### priority-donate-nest

`A` 等待 `B`, `B` 等待 `C`, 则捐赠可以嵌套, 即 `A -> B -> C`, 让 `C` 可以得到 `A` 的高优先级.

### priority-donate-sema



### priority-donate-lower

在被捐赠的时候修改线程优先级, 若优先级低于捐赠的优先级, 这个修改在捐赠结束后可以生效.

### priority-fifo

相同优先级, 则先进先出.

### priority-preempt

高优先级线程 `thread_create()` 时可以抢占.

### priority-sema

在信号量就绪的时候优先唤醒等待此信号量的线程中优先级高的.

### priority-condvar

condition 也优先唤醒优先级高的.

### 实现

分析 `lock_acquire()` 的行为.

```c
void
lock_acquire (struct lock *lock)
{
  ASSERT (lock != NULL);
  ASSERT (!intr_context ());
  ASSERT (!lock_held_by_current_thread (lock));

  sema_down (&lock->semaphore);     /* 得不到就等着 */
  lock->holder = thread_current (); /* 等到了就拿走 */
}
```

如果 acquire 成功了, 尝试更新自己的 `priority` (被捐赠).

如果 acquire 失败了, 捐赠自己的 `priority`.

坏了, 那我缺的 lock 捐赠给 thread 这块谁给我补啊?
