# Project 1: Threads

!!! note "Overview"
    - Task 1 : solve busy waiting
    - Task 2 :
        - priority scheduling (Round Robin)
        - priority donation
    - Task3 : advanced scheduler

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

需要额外记录进入顺序.

### priority-preempt

高优先级线程就绪 (`thread_create()` / `thread_unblock()`) 时, 抢占.

### priority-sema

在信号量就绪的时候优先唤醒等待此信号量的线程中优先级高的.

### priority-condvar

condition 也优先唤醒优先级高的.

##  Task 2 实现

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

思考失败, 重新思考.

### 线程的行为

线程可以尝试获取多把锁, 但是在第一次 `lock_acquire()` 失败的时候就会等待第一把锁, 暂时不会把优先级捐赠给下一把锁.

为什么等待一把锁不是 busy waiting? 因为线程本身被 block, 需要等待 lock->sema 来 unblock 线程, 此时也要优先 unblock 高优先级线程.

线程可以同时获取到多把锁, 得到其中最高的优先级的捐赠.

拿着锁 = 可能被 donate

等待锁 = donate 给别的 thread

thread -> lock:
- donate 发生的时间: acquire 失败, 被 block 之前 donate 给锁
- donate 结束的时间: 由 semaphore unblock, 获得锁

lock -> thread:
- donate 发生的时间: acquire 失败, 接受别的 donate 之后 donate 给 holder
- donate 结束的时间: release, 原 holder 可以接受其他锁的 donation

#### 如何更新 `priority`?

无死锁的 donation 的关系构成一个内向树: 所有的线程最多等待一个锁, 对那个锁 donate, 一个线程可以同时持有多个锁, 从多个锁获得 donation.

`lock_release()` 只发生在根节点, 此时会得到一个新的根节点, 那个节点不需要更新, 更新老的根节点.

`lock_acquire()` 是加边, 需要向父亲贡献, 直到达到根节点.

`thread_set_priority()` 只发生在根节点, 无影响.

两个更新的方向:

1. thread->lock_waiting (--捐赠给->) lock->holder (--捐赠给->) thread->lock_waiting (--捐赠给->) ...
2. thread->locks_holding_list (--获取捐赠->) lock->semaphor->waiters (--获取捐赠->) thread->locks_holding_list (--获取捐赠->)...

一个重要的行为是: 捐赠优先级 (即等待锁) 的 thread, 在得到锁之前, 这个 thread 的 priority 可能改变.

- 不会 `thread_set_priority`
- 可能从别处得到新的 donation, 如 TA 拿着锁 A 等待锁 B, TB 拿着锁 B, 此时 TC 请求锁 A, TB 的优先级因此受到 TC 捐赠.

#### `thread_set_priority()`

更新非捐赠所得的优先级 `priority_original`


```
- 如果当前线程在等锁 (一把)
  - 捐赠给锁.
  - 锁需要考虑之前捐赠出的优先级是不是不存在了 (例如优先级 50 捐赠出去, 但是 50 优先级的线程被 set 到了低优先级).
  - 如果高了或者没现在的优先级不是捐赠得到的, 更新 `priority`
```

上面的分析哪里错了?

答案在于, 在等待锁的线程会处于 block 状态, 不能改变优先级.

所以说, 只有持有锁的线程或者和锁无关的线程会进入 `thread_set_priority()`

- 如果当前线程持有锁 (可能多把)
    - 高于捐赠得到的优先级: 更新 `priority`
    - 不高于捐赠得到的优先级: 并没有发生什么事情.

- 如果当前线程不持有锁
    - 更新 `priority`

#### `lock_acquire()`

- 直接拿到
    - 这锁也没人要啊, 拿了.
    - 可能中间被打断过, 所以说让锁再去更新一下, 看看有没有好心人的 donation

- 没拿到
    - donate 给锁, 让锁更新 holder
    - 被 block, 等 semaphore
    - 终于放出来了, 把锁拿走, 更新一下锁收到的 priority, 更新一下自己的 priority


问题: 死锁?

```
两个线程 TA, TB, 两个锁 A, B.

TA 拿锁 A, interrupt 切换到 TB

TB 拿锁 B, interrupt 切换到 TA

TA 拿锁 B, TA 捐赠给 B, B 捐赠给 TB, interrupt

TB 拿锁 A, TB 捐赠给 A, A 捐赠给 TA, TA 捐赠给 B...
```

解决?方式: 对于嵌套优先级捐赠关系的环 (From document "If necessary, you may impose a reasonable limit on depth of nested priority donation, such as 8 levels."), 加一个传递层数?.

外界死锁实在没法解决, 但是属于 UB, 不解决也行. 可以考虑加一个检查是否所有的线程都被 block, 如果如此就立一个 flag 清除所有的线程, 然后 PANIC.

#### `lock_release()`

- 不被这个 lock donate 了, 更新一下自己的 priority

`sema_up()`

问题: 被更新 priority 的 thread 还在堆 / ready_list 里面, 但是插入已经发生了, 该 heap_up / heap_down?

解决?方式: 在完成从叶子节点到根节点的 donation 之后, 禁用中断然后 $O(n)$ 建堆.

## Task 3

在全局 `bool` 变量 `thread_mlfqs` 为 `true` 的时候, 启用 Multilevel Feedback Queue Scheduling, 而不是 Task 2 的基于优先级的 Round Robin 调度.

### 实现定点数

在 pintos 内核中没有浮点数运算, 需要手动实现定点数来完成 `load_avg`, `nice` 等 MLFQS 相关值的计算.

使用 32 位 `int` 保存一个定点数, 令 `x` 表示实数 `x / (2^14)`, 即使用最低 14 位存储分数.


### 更改 nice

nice 只来自外界传入 / 从 parent thread 继承, 修改后更新优先级即可.

### 计算 priority

$$
priority = PRI_MAX - \dfrac{1}{4} recent_cpu - 2 \cdot nice
$$

需要控制范围不超过 `PRI_MIN` 到 `PRI_MAX`.

### 计算 recent_cpu

$$
recent_cpu = \dfrac{2 \cdot load_avg}{2 \cdot load_avg + 1} \cdor recent_cpu + nice
$$

### 计算 load_avg

`load_avg` 是全局的, 不是某个线程的属性.

$$
load_avg = \dfrac{59}{60} load_avg + \dfrac{1}{60} ready_thread
$$

$ready_thread$ 代表正在运行/就绪的线程数量.