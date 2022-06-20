
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#ifndef _NGX_ARRAY_H_INCLUDED_
#define _NGX_ARRAY_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>


// nginx数组
typedef struct {
    void        *elts; // 元素指针地址
    ngx_uint_t   nelts; // 当前元素数
    size_t       size; // 每个元素的大小
    ngx_uint_t   nalloc; // 已分配的元素空间个数
    ngx_pool_t  *pool;
} ngx_array_t;


// 创建一个数组结构，可动态扩容
ngx_array_t *ngx_array_create(ngx_pool_t *p, ngx_uint_t n, size_t size);
// 销毁数组并回收内存
void ngx_array_destroy(ngx_array_t *a);
// 在数组后分配一个元素的空间,并返回其指针地址
void *ngx_array_push(ngx_array_t *a);

// 在数组后分配n个元素的空间,并返回其首指针地址
void *ngx_array_push_n(ngx_array_t *a, ngx_uint_t n);


// 初始化n个size大小的元素空间
static ngx_inline ngx_int_t
ngx_array_init(ngx_array_t *array, ngx_pool_t *pool, ngx_uint_t n, size_t size)
{
    /*
     * set "array->nelts" before "array->elts", otherwise MSVC thinks
     * that "array->nelts" may be used without having been initialized
     */

    array->nelts = 0;
    array->size = size;
    array->nalloc = n;
    array->pool = pool;

    array->elts = ngx_palloc(pool, n * size);
    if (array->elts == NULL) {
        return NGX_ERROR;
    }

    return NGX_OK;
}


#endif /* _NGX_ARRAY_H_INCLUDED_ */
