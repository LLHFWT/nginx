
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#ifndef _NGX_LIST_H_INCLUDED_
#define _NGX_LIST_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>

// nginx list实现，结构相当于链接起来的数组，每一个节点可看成一块大小相等的内存，可以存多个元素
// 链表元素只能增加
typedef struct ngx_list_part_s  ngx_list_part_t;

// list的节点，每个节点可以存多个元素，最大是下面的nalloc
struct ngx_list_part_s {
    void             *elts; // 元素存储地址
    ngx_uint_t        nelts; // 当前已存的元素数
    ngx_list_part_t  *next; // 下一个节点 
};


typedef struct {
    ngx_list_part_t  *last; // 最后一个链表节点
    ngx_list_part_t   part; // 第一个链表节点
    size_t            size; // 每个元素大小
    ngx_uint_t        nalloc; // 每个节点可存的元素数
    ngx_pool_t       *pool;
} ngx_list_t;


// 创建一个list，元素大小是size，每个节点可以存n个元素
ngx_list_t *ngx_list_create(ngx_pool_t *pool, ngx_uint_t n, size_t size);

// 初始化
static ngx_inline ngx_int_t
ngx_list_init(ngx_list_t *list, ngx_pool_t *pool, ngx_uint_t n, size_t size)
{
    list->part.elts = ngx_palloc(pool, n * size);
    if (list->part.elts == NULL) {
        return NGX_ERROR;
    }

    list->part.nelts = 0;
    list->part.next = NULL;
    list->last = &list->part;
    list->size = size;
    list->nalloc = n;
    list->pool = pool;

    return NGX_OK;
}


/*
 *
 *  the iteration through the list:
 *
 *  part = &list.part;
 *  data = part->elts;
 *
 *  for (i = 0 ;; i++) {
 *
 *      if (i >= part->nelts) {
 *          if (part->next == NULL) {
 *              break;
 *          }
 *
 *          part = part->next;
 *          data = part->elts;
 *          i = 0;
 *      }
 *
 *      ...  data[i] ...
 *
 *  }
 */

// 增加元素返回其内存地址
void *ngx_list_push(ngx_list_t *list);


#endif /* _NGX_LIST_H_INCLUDED_ */
