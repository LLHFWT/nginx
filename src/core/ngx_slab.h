
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#ifndef _NGX_SLAB_H_INCLUDED_
#define _NGX_SLAB_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>


typedef struct ngx_slab_page_s  ngx_slab_page_t;

struct ngx_slab_page_s {
    uintptr_t         slab;
    ngx_slab_page_t  *next;
    uintptr_t         prev;
};


typedef struct {
    ngx_uint_t        total;
    ngx_uint_t        used;

    ngx_uint_t        reqs;
    ngx_uint_t        fails;
} ngx_slab_stat_t;


// 共享内存池，管理的是已经申请好的内存
typedef struct {
    ngx_shmtx_sh_t    lock;

    size_t            min_size; // 可以分配的最小内存
    size_t            min_shift; // 可以分配的最小内存的对应的偏移量，比如8的偏移量是3

    ngx_slab_page_t  *pages; // 指向描述第一页内存对应的ngx_slab_page_t结构
    ngx_slab_page_t  *last; // 指向与上面对应的最后一页
    ngx_slab_page_t   free; // 用于管理空闲页

    ngx_slab_stat_t  *stats; // 记录每种规格内存的统计信息
    ngx_uint_t        pfree; // 空闲页数

    u_char           *start; // 真正的可用页区起始位置
    u_char           *end; // 共享内存结束位置

    ngx_shmtx_t       mutex;

    u_char           *log_ctx;
    u_char            zero;

    unsigned          log_nomem:1;

    void             *data;
    void             *addr;
} ngx_slab_pool_t;

// 初始化共享内存池的各种size值
void ngx_slab_sizes_init(void);
// 内存池初始化
void ngx_slab_init(ngx_slab_pool_t *pool);
// 从内存池中分配内存，会自动加锁
void *ngx_slab_alloc(ngx_slab_pool_t *pool, size_t size);
// 在已加锁的情况下使用
void *ngx_slab_alloc_locked(ngx_slab_pool_t *pool, size_t size);
void *ngx_slab_calloc(ngx_slab_pool_t *pool, size_t size);
void *ngx_slab_calloc_locked(ngx_slab_pool_t *pool, size_t size);
void ngx_slab_free(ngx_slab_pool_t *pool, void *p);
void ngx_slab_free_locked(ngx_slab_pool_t *pool, void *p);


#endif /* _NGX_SLAB_H_INCLUDED_ */
