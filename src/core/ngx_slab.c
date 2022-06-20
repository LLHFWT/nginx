
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */

#include <ngx_config.h>
#include <ngx_core.h>


#define NGX_SLAB_PAGE_MASK   3
#define NGX_SLAB_PAGE        0
#define NGX_SLAB_BIG         1
#define NGX_SLAB_EXACT       2
#define NGX_SLAB_SMALL       3

#if (NGX_PTR_SIZE == 4)

#define NGX_SLAB_PAGE_FREE   0
#define NGX_SLAB_PAGE_BUSY   0xffffffff
#define NGX_SLAB_PAGE_START  0x80000000

#define NGX_SLAB_SHIFT_MASK  0x0000000f
#define NGX_SLAB_MAP_MASK    0xffff0000
#define NGX_SLAB_MAP_SHIFT   16

#define NGX_SLAB_BUSY        0xffffffff

#else /* (NGX_PTR_SIZE == 8) */

#define NGX_SLAB_PAGE_FREE   0
#define NGX_SLAB_PAGE_BUSY   0xffffffffffffffff
#define NGX_SLAB_PAGE_START  0x8000000000000000

#define NGX_SLAB_SHIFT_MASK  0x000000000000000f
#define NGX_SLAB_MAP_MASK    0xffffffff00000000
#define NGX_SLAB_MAP_SHIFT   32

#define NGX_SLAB_BUSY        0xffffffffffffffff

#endif

// slab slots开始地址
#define ngx_slab_slots(pool)                                                  \
    (ngx_slab_page_t *) ((u_char *) (pool) + sizeof(ngx_slab_pool_t))

#define ngx_slab_page_type(page)   ((page)->prev & NGX_SLAB_PAGE_MASK)

#define ngx_slab_page_prev(page)                                              \
    (ngx_slab_page_t *) ((page)->prev & ~NGX_SLAB_PAGE_MASK)

// ngx_pagesize_shift代表描述页大小的偏移量，比如4096大小的页对应的shift是12
// 该宏可以根据页结构体的地址获取实际的页对应的地址
#define ngx_slab_page_addr(pool, page)                                        \
    ((((page) - (pool)->pages) << ngx_pagesize_shift)                         \
     + (uintptr_t) (pool)->start)


#if (NGX_DEBUG_MALLOC)

#define ngx_slab_junk(p, size)     ngx_memset(p, 0xA5, size)

#elif (NGX_HAVE_DEBUG_MALLOC)

#define ngx_slab_junk(p, size)                                                \
    if (ngx_debug_malloc)          ngx_memset(p, 0xA5, size)

#else

#define ngx_slab_junk(p, size)

#endif

static ngx_slab_page_t *ngx_slab_alloc_pages(ngx_slab_pool_t *pool,
    ngx_uint_t pages);
static void ngx_slab_free_pages(ngx_slab_pool_t *pool, ngx_slab_page_t *page,
    ngx_uint_t pages);
static void ngx_slab_error(ngx_slab_pool_t *pool, ngx_uint_t level,
    char *text);


static ngx_uint_t  ngx_slab_max_size; // pagesize / 2
static ngx_uint_t  ngx_slab_exact_size;
static ngx_uint_t  ngx_slab_exact_shift;


void
ngx_slab_sizes_init(void)
{
    ngx_uint_t  n;

    ngx_slab_max_size = ngx_pagesize / 2;
    ngx_slab_exact_size = ngx_pagesize / (8 * sizeof(uintptr_t));
    for (n = ngx_slab_exact_size; n >>= 1; ngx_slab_exact_shift++) {
        /* void */
    }
}


void
ngx_slab_init(ngx_slab_pool_t *pool)
{
    u_char           *p;
    size_t            size;
    ngx_int_t         m;
    ngx_uint_t        i, n, pages;
    ngx_slab_page_t  *slots, *page;

    // 池中最小可分配内存大小计算
    pool->min_size = (size_t) 1 << pool->min_shift;

    // 跳过结构体ngx_slab_pool_t占用的内存
    slots = ngx_slab_slots(pool);

    // 计算剩余内存大小和起始位置
    p = (u_char *) slots;
    size = pool->end - p;

    ngx_slab_junk(p, size);

    // 计算一页内存可划分为多少种内存块，比如12 - 3 = 9，即8字节、16字节、32字节....
    n = ngx_pagesize_shift - pool->min_shift;

    // 初始化n个ngx_slab_page_t用于描述各种大小的内存块
    for (i = 0; i < n; i++) {
        /* only "next" is used in list head */
        slots[i].slab = 0;
        slots[i].next = &slots[i];
        slots[i].prev = 0;
    }

    // 将指针跳过这n个ngx_slab_page_t，继续往下初始化
    p += n * sizeof(ngx_slab_page_t);

    // 初始化n个ngx_slab_stat_t用于统计信息，继续往下
    pool->stats = (ngx_slab_stat_t *) p;
    ngx_memzero(pool->stats, n * sizeof(ngx_slab_stat_t));

    p += n * sizeof(ngx_slab_stat_t);

    // 更新剩余内存空间大小
    size -= n * (sizeof(ngx_slab_page_t) + sizeof(ngx_slab_stat_t));

    // 计算剩余内存空间可以放置多少页
    pages = (ngx_uint_t) (size / (ngx_pagesize + sizeof(ngx_slab_page_t)));

    // 初始化pages个ngx_slab_page_t用于描述每一页
    pool->pages = (ngx_slab_page_t *) p;
    ngx_memzero(pool->pages, pages * sizeof(ngx_slab_page_t));

    // page指向第一页的ngx_slab_page_t
    page = pool->pages;

    /* only "next" is used in list head */
    pool->free.slab = 0;
    pool->free.next = page; // free.next指向第一个page结构体
    pool->free.prev = 0;

    page->slab = pages; // slab代表可用的页数
    page->next = &pool->free;
    page->prev = (uintptr_t) &pool->free;

    // 对剩余内存按照页大小进行对齐，start指向的就是可用的第一页
    pool->start = ngx_align_ptr(p + pages * sizeof(ngx_slab_page_t),
                                ngx_pagesize);

    // 对齐后调整可用页数
    m = pages - (pool->end - pool->start) / ngx_pagesize;
    if (m > 0) {
        pages -= m;
        page->slab = pages;
    }

    // last指向可用的最后一页的ngx_slab_page_t
    pool->last = pool->pages + pages;
    // 可用页数
    pool->pfree = pages;

    pool->log_nomem = 1;
    pool->log_ctx = &pool->zero;
    pool->zero = '\0';
}


void *
ngx_slab_alloc(ngx_slab_pool_t *pool, size_t size)
{
    void  *p;

    ngx_shmtx_lock(&pool->mutex);

    p = ngx_slab_alloc_locked(pool, size);

    ngx_shmtx_unlock(&pool->mutex);

    return p;
}


void *
ngx_slab_alloc_locked(ngx_slab_pool_t *pool, size_t size)
{
    size_t            s;
    uintptr_t         p, m, mask, *bitmap;
    ngx_uint_t        i, n, slot, shift, map;
    ngx_slab_page_t  *page, *prev, *slots;

    if (size > ngx_slab_max_size) {

        ngx_log_debug1(NGX_LOG_DEBUG_ALLOC, ngx_cycle->log, 0,
                       "slab alloc: %uz", size);

        // page为分配的页结构体的起始地址
        page = ngx_slab_alloc_pages(pool, (size >> ngx_pagesize_shift)
                                          + ((size % ngx_pagesize) ? 1 : 0));
        if (page) {
            // 获取对应的页的起始地址
            p = ngx_slab_page_addr(pool, page);

        } else {
            p = 0;
        }

        goto done;
    }

    if (size > pool->min_size) { // 分配空间大于min_size(8字节)，计算它的shift，并据此计算它属于哪一级slot
        shift = 1;
        for (s = size - 1; s >>= 1; shift++) { /* void */ }
        slot = shift - pool->min_shift;

    } else { // 分配空间小于min_size，按照min_size分配，即8字节
        shift = pool->min_shift;
        slot = 0;
    }

    // 更新对应slot的统计信息
    pool->stats[slot].reqs++;

    ngx_log_debug2(NGX_LOG_DEBUG_ALLOC, ngx_cycle->log, 0,
                   "slab alloc: %uz slot: %ui", size, slot);

    slots = ngx_slab_slots(pool); // slots分级数组对应的起始地址
    page = slots[slot].next; // slots分级数组成员的初始值next指向自己，标记尚未分配过本级别内存块

    if (page->next != page) { // 如果该级别内存已经分配过

        if (shift < ngx_slab_exact_shift) { // 小块

            bitmap = (uintptr_t *) ngx_slab_page_addr(pool, page); // 页内数据空间起始位置是bitmap数组

            map = (ngx_pagesize >> shift) / (8 * sizeof(uintptr_t)); // 计算需要几个64bit的bitmap

            for (n = 0; n < map; n++) { // 遍历bitmap数组

                if (bitmap[n] != NGX_SLAB_BUSY) { // 当前bitmap对应的内存块还未分配完

                    for (m = 1, i = 0; m; m <<= 1, i++) { // 遍历每一个bit位
                        if (bitmap[n] & m) { // 如果当前块已经分配，继续下一个
                            continue;
                        }

                        bitmap[n] |= m; // 当前位未分配，置1，占上

                        i = (n * 8 * sizeof(uintptr_t) + i) << shift; // 计算要分配的内存块再页内的偏移量

                        p = (uintptr_t) bitmap + i; // 要分配的内存块的起始地址

                        pool->stats[slot].used++; // 更新统计数据

                        if (bitmap[n] == NGX_SLAB_BUSY) { // 如果当前内存块都被分配
                            for (n = n + 1; n < map; n++) { // 检查剩余bitmap对应的内存块
                                if (bitmap[n] != NGX_SLAB_BUSY) { // 如果当前页的内存块还没用完，返回
                                    goto done;
                                }
                            }

                            // 至此，该页的所有内存块都已用完，将它从slots[slot]链表中删除
                            prev = ngx_slab_page_prev(page);
                            prev->next = page->next;
                            page->next->prev = page->prev;

                            page->next = NULL;
                            page->prev = NGX_SLAB_SMALL;
                        }

                        goto done;
                    }
                }
            }

        } else if (shift == ngx_slab_exact_shift) {

            // page->slab记录bitmap
            for (m = 1, i = 0; m; m <<= 1, i++) {
                if (page->slab & m) {
                    continue;
                }

                page->slab |= m; // 占位

                if (page->slab == NGX_SLAB_BUSY) { // 如果本页已经分配完了，将其从slots[slot]链表上摘除
                    prev = ngx_slab_page_prev(page);
                    prev->next = page->next;
                    page->next->prev = page->prev;

                    page->next = NULL;
                    page->prev = NGX_SLAB_EXACT;
                }

                // 页起始地址加上内存块的页内偏移量即分配内存块的起始地址
                p = ngx_slab_page_addr(pool, page) + (i << shift);

                pool->stats[slot].used++; // 更新统计数据

                goto done;
            }

        } else { /* shift > ngx_slab_exact_shift */

             /* 在这个级别的页中 page->slab 的高32位的部分位作为 bitmap，
             * 具体占用多少位，据分配大小而定。如分配 256B, bitmap 占用 16位
             */
            mask = ((uintptr_t) 1 << (ngx_pagesize >> shift)) - 1;
            mask <<= NGX_SLAB_MAP_SHIFT;

            for (m = (uintptr_t) 1 << NGX_SLAB_MAP_SHIFT, i = 0;
                 m & mask;
                 m <<= 1, i++)
            {
                if (page->slab & m) {
                    continue;
                }

                page->slab |= m;

                if ((page->slab & NGX_SLAB_MAP_MASK) == mask) {
                    prev = ngx_slab_page_prev(page);
                    prev->next = page->next;
                    page->next->prev = page->prev;

                    page->next = NULL;
                    page->prev = NGX_SLAB_BIG;
                }

                p = ngx_slab_page_addr(pool, page) + (i << shift);

                pool->stats[slot].used++;

                goto done;
            }
        }

        ngx_slab_error(pool, NGX_LOG_ALERT, "ngx_slab_alloc(): page is busy");
        ngx_debug_point();
    }

    // 分配一个新的页
    page = ngx_slab_alloc_pages(pool, 1);

    if (page) {
        if (shift < ngx_slab_exact_shift) { // 如果是小块内存
            bitmap = (uintptr_t *) ngx_slab_page_addr(pool, page); // 将该页的前面几块内存用作bitmap

            n = (ngx_pagesize >> shift) / ((1 << shift) * 8); // 计算bitmap需要占用几个内存块

            if (n == 0) { // 至少占用一个内存块
                n = 1;
            }

            /* "n" elements for bitmap, plus one requested */

            for (i = 0; i < (n + 1) / (8 * sizeof(uintptr_t)); i++) {
                bitmap[i] = NGX_SLAB_BUSY;
            }

            // 把bitmap数组占用的内存块加上本次分配的一个快，对应的bitmap位置1
            m = ((uintptr_t) 1 << ((n + 1) % (8 * sizeof(uintptr_t)))) - 1;
            bitmap[i] = m;

            // 计算需要几个64bit的 bitmap
            map = (ngx_pagesize >> shift) / (8 * sizeof(uintptr_t));

            for (i = i + 1; i < map; i++) {
                bitmap[i] = 0; // 后面的bitmap置零，表明内存块空闲
            }

            page->slab = shift; // slab记录shift，用于free时得到得到内存块大小
            page->next = &slots[slot]; // 当前页挂载到slots[slot]双向链表上
            page->prev = (uintptr_t) &slots[slot] | NGX_SLAB_SMALL;

            slots[slot].next = page;

            pool->stats[slot].total += (ngx_pagesize >> shift) - n; // 当前页可划分的内存块数 - bitmap占用的内存块数

            p = ngx_slab_page_addr(pool, page) + (n << shift); // 所在的页的起始地址 + 分配的内存块的页内偏移地址

            pool->stats[slot].used++; // 该等级内存块的已分配数量

            goto done;

        } else if (shift == ngx_slab_exact_shift) { // page->slab是64位，正好可以做bitmap

            page->slab = 1; // bitmap第一个位 置为1，表示第一个内存块被分配
            // 当前页挂载到slots[slot]双向链表上，再分配同一等级的内存块可以从该页分配
            page->next = &slots[slot];
            page->prev = (uintptr_t) &slots[slot] | NGX_SLAB_EXACT; // 地址低位记录页类型，因为是对齐的，所以低位是0

            slots[slot].next = page;

            pool->stats[slot].total += 8 * sizeof(uintptr_t); // 更新该等级内存块总数

            p = ngx_slab_page_addr(pool, page);  // 直接返回页首地址即可

            pool->stats[slot].used++;

            goto done;

        } else { /* shift > ngx_slab_exact_shift 大块内存 */

            page->slab = ((uintptr_t) 1 << NGX_SLAB_MAP_SHIFT) | shift; // 低32位记录shift，高32位记录bitmap
            page->next = &slots[slot];
            page->prev = (uintptr_t) &slots[slot] | NGX_SLAB_BIG;

            slots[slot].next = page;

            pool->stats[slot].total += ngx_pagesize >> shift;

            p = ngx_slab_page_addr(pool, page);

            pool->stats[slot].used++;

            goto done;
        }
    }

    p = 0;

    // 分配失败更新统计结果
    pool->stats[slot].fails++;

done:

    ngx_log_debug1(NGX_LOG_DEBUG_ALLOC, ngx_cycle->log, 0,
                   "slab alloc: %p", (void *) p);

    return (void *) p;
}


void *
ngx_slab_calloc(ngx_slab_pool_t *pool, size_t size)
{
    void  *p;

    ngx_shmtx_lock(&pool->mutex);

    p = ngx_slab_calloc_locked(pool, size);

    ngx_shmtx_unlock(&pool->mutex);

    return p;
}


void *
ngx_slab_calloc_locked(ngx_slab_pool_t *pool, size_t size)
{
    void  *p;

    p = ngx_slab_alloc_locked(pool, size);
    if (p) {
        ngx_memzero(p, size);
    }

    return p;
}


void
ngx_slab_free(ngx_slab_pool_t *pool, void *p)
{
    ngx_shmtx_lock(&pool->mutex);

    ngx_slab_free_locked(pool, p);

    ngx_shmtx_unlock(&pool->mutex);
}


void
ngx_slab_free_locked(ngx_slab_pool_t *pool, void *p)
{
    size_t            size;
    uintptr_t         slab, m, *bitmap;
    ngx_uint_t        i, n, type, slot, shift, map;
    ngx_slab_page_t  *slots, *page;

    ngx_log_debug1(NGX_LOG_DEBUG_ALLOC, ngx_cycle->log, 0, "slab free: %p", p);

    if ((u_char *) p < pool->start || (u_char *) p > pool->end) { // 检查要释放的内存是否越界
        ngx_slab_error(pool, NGX_LOG_ALERT, "ngx_slab_free(): outside of pool");
        goto fail;
    }

    n = ((u_char *) p - pool->start) >> ngx_pagesize_shift; // 计算待释放的地址在第几页
    page = &pool->pages[n]; // 获取它的页管理结构
    slab = page->slab;
    type = ngx_slab_page_type(page); // 获取本页的类型

    switch (type) {

    case NGX_SLAB_SMALL: // 小块内存页

        shift = slab & NGX_SLAB_SHIFT_MASK; // slab记录了shift
        size = (size_t) 1 << shift; // 计算内存块大小

        if ((uintptr_t) p & (size - 1)) { // 地址必须是内存块大小的整数倍，否则非法
            goto wrong_chunk;
        }

        n = ((uintptr_t) p & (ngx_pagesize - 1)) >> shift; // 指针指向内存块的页内索引号
        m = (uintptr_t) 1 << (n % (8 * sizeof(uintptr_t))); // 内存块在bitmap中对应的位
        n /= 8 * sizeof(uintptr_t); // 内存块对应的bitmap在数组中的索引号
        bitmap = (uintptr_t *)
                             ((uintptr_t) p & ~((uintptr_t) ngx_pagesize - 1)); // 页起始地址

        if (bitmap[n] & m) { // 通过bitmap[n]的第m位是否被置1判断该内存是否已经释放
            slot = shift - pool->min_shift;

            if (page->next == NULL) { // 当前页不在分级链表中，表明已经占满了
                slots = ngx_slab_slots(pool);

                page->next = slots[slot].next; // 回收内存块后重新插入分级链表中
                slots[slot].next = page;

                page->prev = (uintptr_t) &slots[slot] | NGX_SLAB_SMALL;
                page->next->prev = (uintptr_t) page | NGX_SLAB_SMALL;
            }

            bitmap[n] &= ~m; // 对应位置0，标记已释放

            n = (ngx_pagesize >> shift) / ((1 << shift) * 8); // bitmap共占用几个内存块

            if (n == 0) {
                n = 1;
            }

            i = n / (8 * sizeof(uintptr_t));
            m = ((uintptr_t) 1 << (n % (8 * sizeof(uintptr_t)))) - 1;

            if (bitmap[i] & ~m) { // 除了bitmap只能用的内存块外还有其他块被占用，表明当前页不空
                goto done;
            }

            map = (ngx_pagesize >> shift) / (8 * sizeof(uintptr_t)); // 计算bitmap数组长度

            for (i = i + 1; i < map; i++) { // 遍历bitmap数组，只要有bitmap中的位不去安慰0，则表明当前页不为空
                if (bitmap[i]) {
                    goto done;
                }
            }

            // 走到这里说明除了bitmap数组占用的内存块，所有内存都已释放，回收当前页
            ngx_slab_free_pages(pool, page, 1);

            pool->stats[slot].total -= (ngx_pagesize >> shift) - n;

            goto done;
        }

        goto chunk_already_free;

    case NGX_SLAB_EXACT:

        m = (uintptr_t) 1 <<
                (((uintptr_t) p & (ngx_pagesize - 1)) >> ngx_slab_exact_shift); // 内存块对应的bitmap位
        size = ngx_slab_exact_size;

        if ((uintptr_t) p & (size - 1)) { // 地址大小必须是内存块大小的整数倍
            goto wrong_chunk;
        }

        if (slab & m) { // 该内存块还没回收
            slot = ngx_slab_exact_shift - pool->min_shift;

            if (slab == NGX_SLAB_BUSY) { // 当前页已被占满不在分级链表中
                slots = ngx_slab_slots(pool);

                page->next = slots[slot].next; // 重新插入分级链表，后续分配同级内存块时可从本页分配
                slots[slot].next = page;

                page->prev = (uintptr_t) &slots[slot] | NGX_SLAB_EXACT;
                page->next->prev = (uintptr_t) page | NGX_SLAB_EXACT;
            }

            page->slab &= ~m; // 位置0，表明被释放

            if (page->slab) { // bitmap部位0，表示还有内存块被使用
                goto done;
            }

            // 回收当前页
            ngx_slab_free_pages(pool, page, 1);

            pool->stats[slot].total -= 8 * sizeof(uintptr_t);

            goto done;
        }

        goto chunk_already_free;

    case NGX_SLAB_BIG:

        shift = slab & NGX_SLAB_SHIFT_MASK;
        size = (size_t) 1 << shift;

        if ((uintptr_t) p & (size - 1)) {
            goto wrong_chunk;
        }

        m = (uintptr_t) 1 << ((((uintptr_t) p & (ngx_pagesize - 1)) >> shift)
                              + NGX_SLAB_MAP_SHIFT);

        if (slab & m) {
            slot = shift - pool->min_shift;

            if (page->next == NULL) {
                slots = ngx_slab_slots(pool);

                page->next = slots[slot].next;
                slots[slot].next = page;

                page->prev = (uintptr_t) &slots[slot] | NGX_SLAB_BIG;
                page->next->prev = (uintptr_t) page | NGX_SLAB_BIG;
            }

            page->slab &= ~m;

            if (page->slab & NGX_SLAB_MAP_MASK) {
                goto done;
            }

            ngx_slab_free_pages(pool, page, 1);

            pool->stats[slot].total -= ngx_pagesize >> shift;

            goto done;
        }

        goto chunk_already_free;

    case NGX_SLAB_PAGE:

        if ((uintptr_t) p & (ngx_pagesize - 1)) {
            goto wrong_chunk;
        }

        if (!(slab & NGX_SLAB_PAGE_START)) {
            ngx_slab_error(pool, NGX_LOG_ALERT,
                           "ngx_slab_free(): page is already free");
            goto fail;
        }

        if (slab == NGX_SLAB_PAGE_BUSY) {
            ngx_slab_error(pool, NGX_LOG_ALERT,
                           "ngx_slab_free(): pointer to wrong page");
            goto fail;
        }

        size = slab & ~NGX_SLAB_PAGE_START;

        ngx_slab_free_pages(pool, page, size);

        ngx_slab_junk(p, size << ngx_pagesize_shift);

        return;
    }

    /* not reached */

    return;

done:

    pool->stats[slot].used--;

    ngx_slab_junk(p, size);

    return;

wrong_chunk:

    ngx_slab_error(pool, NGX_LOG_ALERT,
                   "ngx_slab_free(): pointer to wrong chunk");

    goto fail;

chunk_already_free:

    ngx_slab_error(pool, NGX_LOG_ALERT,
                   "ngx_slab_free(): chunk is already free");

fail:

    return;
}


// 分配pages个完整的页
static ngx_slab_page_t *
ngx_slab_alloc_pages(ngx_slab_pool_t *pool, ngx_uint_t pages)
{
    ngx_slab_page_t  *page, *p;

    for (page = pool->free.next; page != &pool->free; page = page->next) { // 遍历空闲页链表

        if (page->slab >= pages) { // 当前链表节点挂载的连续空闲页数足够分配

            if (page->slab > pages) { // 当前链表节点挂载的连续空闲页数大于待分配页数
                page[page->slab - 1].prev = (uintptr_t) &page[pages];

                // 以第pages页作为剩余空闲页的起始页，挂载到free链表上
                page[pages].slab = page->slab - pages; // 更新当前节点剩余空闲页数
                page[pages].next = page->next; // 挂载剩下的页
                page[pages].prev = page->prev; // 更新prev节点

                // 更新prev节点的链接
                p = (ngx_slab_page_t *) page->prev;
                p->next = &page[pages];
                page->next->prev = (uintptr_t) &page[pages];

            } else { // 当前节点连续的空闲页数正好够分配，直接把当前节点的前后节点相连
                p = (ngx_slab_page_t *) page->prev;
                p->next = page->next;
                page->next->prev = page->prev;
            }

            // 对该结构进行使用前初始化，然后返回
            page->slab = pages | NGX_SLAB_PAGE_START; // 管理的页数 ｜ 首页标识
            page->next = NULL; // 从free链表中删除
            page->prev = NGX_SLAB_PAGE; // 被分配的prev记载page类型

            pool->pfree -= pages; // 更新空闲页数

            if (--pages == 0) { // 如果只有一页直接返回
                return page;
            }

            // 申请了多个页
            for (p = page + 1; pages; pages--) {
                p->slab = NGX_SLAB_PAGE_BUSY; // 后面几页全部置为NGX_SLAB_PAGE_BUSY标记
                p->next = NULL;
                p->prev = NGX_SLAB_PAGE; // 记录page类型
                p++;
            }

            return page; // 返回首页
        }
    }

    if (pool->log_nomem) {
        ngx_slab_error(pool, NGX_LOG_CRIT,
                       "ngx_slab_alloc() failed: no memory");
    }

    return NULL;
}


static void
ngx_slab_free_pages(ngx_slab_pool_t *pool, ngx_slab_page_t *page,
    ngx_uint_t pages)
{
    ngx_slab_page_t  *prev, *join;

    pool->pfree += pages; // 更新总空闲页数

    page->slab = pages--; // 当前页管理器管理的页数

    if (pages) { // 如果是多页，除第一页外page结构体清空
        ngx_memzero(&page[1], pages * sizeof(ngx_slab_page_t));
    }

    if (page->next) { // 小块内存分配的页，挂载在分级链表中，把页从分级链表上摘除
        prev = ngx_slab_page_prev(page);
        prev->next = page->next;
        page->next->prev = page->prev;
    }

    join = page + page->slab; // 指向待释放的共享内存空间后面的一个页

    if (join < pool->last) { // 不是最后一个页

        if (ngx_slab_page_type(join) == NGX_SLAB_PAGE) {

            if (join->next != NULL) {
                pages += join->slab;
                page->slab += join->slab;

                prev = ngx_slab_page_prev(join);
                prev->next = join->next;
                join->next->prev = join->prev;

                join->slab = NGX_SLAB_PAGE_FREE;
                join->next = NULL;
                join->prev = NGX_SLAB_PAGE;
            }
        }
    }

    if (page > pool->pages) {
        join = page - 1;

        if (ngx_slab_page_type(join) == NGX_SLAB_PAGE) {

            if (join->slab == NGX_SLAB_PAGE_FREE) {
                join = ngx_slab_page_prev(join);
            }

            if (join->next != NULL) {
                pages += join->slab;
                join->slab += page->slab;

                prev = ngx_slab_page_prev(join);
                prev->next = join->next;
                join->next->prev = join->prev;

                page->slab = NGX_SLAB_PAGE_FREE;
                page->next = NULL;
                page->prev = NGX_SLAB_PAGE;

                page = join;
            }
        }
    }

    if (pages) {
        page[pages].prev = (uintptr_t) page;
    }

    page->prev = (uintptr_t) &pool->free;
    page->next = pool->free.next;

    page->next->prev = (uintptr_t) page;

    pool->free.next = page;
}


static void
ngx_slab_error(ngx_slab_pool_t *pool, ngx_uint_t level, char *text)
{
    ngx_log_error(level, ngx_cycle->log, 0, "%s%s", text, pool->log_ctx);
}
