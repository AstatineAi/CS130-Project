#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

typedef int mapid_t;

void syscall_init (void);
void syscall_exit (int);
void syscall_munmap (mapid_t);

#endif /* userprog/syscall.h */
