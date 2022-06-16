
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#ifndef _NGX_PALLOC_H_INCLUDED_
#define _NGX_PALLOC_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>


/*
 * NGX_MAX_ALLOC_FROM_POOL should be (ngx_pagesize - 1), i.e. 4095 on x86.
 * On Windows NT it decreases a number of locked pages in a kernel.
 */
#define NGX_MAX_ALLOC_FROM_POOL  (ngx_pagesize - 1)

#define NGX_DEFAULT_POOL_SIZE    (16 * 1024)

#define NGX_POOL_ALIGNMENT       16
#define NGX_MIN_POOL_SIZE                                                     \
    ngx_align((sizeof(ngx_pool_t) + 2 * sizeof(ngx_pool_large_t)),            \
              NGX_POOL_ALIGNMENT)


typedef void (*ngx_pool_cleanup_pt)(void *data);

typedef struct ngx_pool_cleanup_s  ngx_pool_cleanup_t;

// 删除内存池时的回调函数
struct ngx_pool_cleanup_s {
    ngx_pool_cleanup_pt   handler;
    void                 *data;
    ngx_pool_cleanup_t   *next;
};

// 申请的大块内存，形成链表
typedef struct ngx_pool_large_s  ngx_pool_large_t;

struct ngx_pool_large_s {
    ngx_pool_large_t     *next;
    void                 *alloc; // 内存地址
};

// 描述pool的结构体
typedef struct {
    u_char               *last; // 内存分配到的指针位置
    u_char               *end; // 分配的内存结束位置
    ngx_pool_t           *next; // 链接的下一个pool，当前pool中没有空余的时候，动态申请一个
    ngx_uint_t            failed; // 分配失败次数，超过4次后，更新current
} ngx_pool_data_t;


struct ngx_pool_s {
    ngx_pool_data_t       d;
    size_t                max; // 当前pool中可分配的最大的小块内存
    ngx_pool_t           *current; // 当前指向的pool
    ngx_chain_t          *chain;
    ngx_pool_large_t     *large; // 大块内存链
    ngx_pool_cleanup_t   *cleanup; // 删除时的回调函数链
    ngx_log_t            *log;
};


typedef struct {
    ngx_fd_t              fd;
    u_char               *name;
    ngx_log_t            *log;
} ngx_pool_cleanup_file_t;

// 创建内存池
ngx_pool_t *ngx_create_pool(size_t size, ngx_log_t *log);
// 销毁内存池
void ngx_destroy_pool(ngx_pool_t *pool);
// 重置内存池
void ngx_reset_pool(ngx_pool_t *pool);

// 从内存池中分配对齐的size大小的内存
void *ngx_palloc(ngx_pool_t *pool, size_t size);
// 从内存池中分配未对齐的size大小的内存
void *ngx_pnalloc(ngx_pool_t *pool, size_t size);
// 从内存池中分配对齐病初始化的size大小内存
void *ngx_pcalloc(ngx_pool_t *pool, size_t size);
// 分配一块对齐的大内存，挂到pool的large链表上
void *ngx_pmemalign(ngx_pool_t *pool, size_t size, size_t alignment);
// 释放从内存池中分配的内存，小块内存不处理，大块内存释放
ngx_int_t ngx_pfree(ngx_pool_t *pool, void *p);

// 添加一个清理时的回调函数
ngx_pool_cleanup_t *ngx_pool_cleanup_add(ngx_pool_t *p, size_t size);
// 执行cleanup链中的文件关闭函数
void ngx_pool_run_cleanup_file(ngx_pool_t *p, ngx_fd_t fd);
// 文件清理函数，close掉fd，data为ngx_pool_cleanup_file_t类型
void ngx_pool_cleanup_file(void *data);
// 删除并关闭文件，data为ngx_pool_cleanup_file_t类型
void ngx_pool_delete_file(void *data);


#endif /* _NGX_PALLOC_H_INCLUDED_ */
