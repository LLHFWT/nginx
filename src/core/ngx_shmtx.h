
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#ifndef _NGX_SHMTX_H_INCLUDED_
#define _NGX_SHMTX_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>

// ngx_atomic_t是个volatile整数
typedef struct {
    ngx_atomic_t   lock;
    // 如果系统支持信号量则使用信号量唤醒等待锁的进程
#if (NGX_HAVE_POSIX_SEM)
    ngx_atomic_t   wait;
#endif
} ngx_shmtx_sh_t;


typedef struct {
    // 如果系统支持原子操作则使用原子操作实现锁
#if (NGX_HAVE_ATOMIC_OPS)
    ngx_atomic_t  *lock;
#if (NGX_HAVE_POSIX_SEM)
    ngx_atomic_t  *wait;
    ngx_uint_t     semaphore;
    sem_t          sem;
#endif
    // 如果系统不支持原子操作则使用文件锁
#else
    ngx_fd_t       fd;
    u_char        *name;
#endif
    ngx_uint_t     spin; // 控制自旋次数
} ngx_shmtx_t;


// 创建共享内存锁，mtx是进程创建的、用于存储锁的变量，addr是共享内存块中用于标识锁的变量
ngx_int_t ngx_shmtx_create(ngx_shmtx_t *mtx, ngx_shmtx_sh_t *addr,
    u_char *name);
// 销毁共享内存锁
void ngx_shmtx_destroy(ngx_shmtx_t *mtx);
// 尝试加锁，失败直接返回
ngx_uint_t ngx_shmtx_trylock(ngx_shmtx_t *mtx);
// 一直等待加锁直到成功
void ngx_shmtx_lock(ngx_shmtx_t *mtx);
// 释放锁
void ngx_shmtx_unlock(ngx_shmtx_t *mtx);
// 强制释放pid进程持有的锁
ngx_uint_t ngx_shmtx_force_unlock(ngx_shmtx_t *mtx, ngx_pid_t pid);


#endif /* _NGX_SHMTX_H_INCLUDED_ */
