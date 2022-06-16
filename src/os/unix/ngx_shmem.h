
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#ifndef _NGX_SHMEM_H_INCLUDED_
#define _NGX_SHMEM_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>

// 共享内存块结构描述
typedef struct {
    u_char      *addr; // 共享内存地址
    size_t       size; // 共享内存大小
    ngx_str_t    name; // 名字
    ngx_log_t   *log;
    ngx_uint_t   exists;   /* unsigned  exists:1;  */
} ngx_shm_t;

// 分配一块共享内存，根据系统选择mmap、shmget来实现
ngx_int_t ngx_shm_alloc(ngx_shm_t *shm);
// 释放共享内存
void ngx_shm_free(ngx_shm_t *shm);


#endif /* _NGX_SHMEM_H_INCLUDED_ */
