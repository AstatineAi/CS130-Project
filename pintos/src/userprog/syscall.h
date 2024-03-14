#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include "threads/interrupt.h"
void syscall_init (void);
int syscall_parse_type (struct intr_frame *);

void syscall_halt (void);
void syscall_exit (const void *);
void syscall_write (const void *);

#endif /* userprog/syscall.h */
