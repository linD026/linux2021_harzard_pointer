/*
 * Hazard pointers are a mechanism for protecting objects in memory from
 * being deleted by other threads while in use. This allows safe lock-free
 * data structures.
 */

#include <assert.h>
#include <inttypes.h>
#include <stdalign.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>

#include "rbtree.h"

#define HP_MAX_THREADS 128
#define HP_MAX_HPS 5 /* This is named 'K' in the HP paper */
#define CLPAD (128 / sizeof(uintptr_t)) /* 128 / 8 = 16 */
#define HP_THRESHOLD_R 0 /* This is named 'R' in the HP paper */

/* Maximum number of retired objects per thread */
#define HP_MAX_RETIRED (HP_MAX_THREADS * HP_MAX_HPS)

#define TID_UNKNOWN -1

typedef struct {
    uintptr_t ptr;
    struct rbtree rbnode;
} retirelist_t;

typedef void(list_hp_deletefunc_t)(struct rbtree *);

typedef struct list_hp {
    int max_hps;
    alignas(128) atomic_uintptr_t *hp[HP_MAX_THREADS];
    alignas(128) struct task_tree_root *rl[HP_MAX_THREADS * CLPAD];
    list_hp_deletefunc_t *deletefunc;
} list_hp_t;

static thread_local int tid_v = TID_UNKNOWN;
static atomic_int_fast32_t tid_v_base = ATOMIC_VAR_INIT(0);

static inline int tid(void)
{
    // atomic_fetch_add return previous value
    if (tid_v == TID_UNKNOWN) {
        tid_v = atomic_fetch_add(&tid_v_base, 1);
        assert(tid_v < HP_MAX_THREADS);
    }
    return tid_v;
}

/* Create a new hazard pointer array of size 'max_hps' (or a reasonable
 * default value if 'max_hps' is 0). The function 'deletefunc' will be
 * used to delete objects protected by hazard pointers when it becomes
 * safe to retire them.
 */
list_hp_t *list_hp_new(size_t max_hps, list_hp_deletefunc_t *deletefunc)
{
    list_hp_t *hp = aligned_alloc(128, sizeof(*hp));
    assert(hp);

    if (max_hps == 0)
        max_hps = HP_MAX_HPS;

    *hp = (list_hp_t){ .max_hps = max_hps, .deletefunc = deletefunc };

    for (int i = 0; i < HP_MAX_THREADS; i++) {
        hp->hp[i] = calloc(CLPAD * 2, sizeof(hp->hp[i][0]));
        hp->rl[i * CLPAD] = calloc(1, sizeof(*hp->rl[0]));
        for (int j = 0; j < hp->max_hps; j++)
            atomic_init(&hp->hp[i][j], 0);
        RB_ROOT_INIT(*(hp->rl[i * CLPAD]));
    }

    return hp;
}

/* Destroy a hazard pointer array and clean up all objects protected
 * by hazard pointers.
 */
void list_hp_destroy(list_hp_t *hp)
{
    for (int i = 0; i < HP_MAX_THREADS; i++) {
        free(hp->hp[i]);
        struct task_tree_root *rl = hp->rl[i * CLPAD];
        rbtree_clean(rl, hp->deletefunc);
        free(rl);
    }
    free(hp);
}

/* Clear all hazard pointers in the array for the current thread.
 * Progress condition: wait-free bounded (by max_hps)
 */
void list_hp_clear(list_hp_t *hp)
{
    for (int i = 0; i < hp->max_hps; i++)
        atomic_store_explicit(&hp->hp[tid()][i], 0, memory_order_release);
}

/* This returns the same value that is passed as ptr.
 * Progress condition: wait-free population oblivious.
 * ihp can be HP_CURR, HP_NEXT, HP_PREV
 */
uintptr_t list_hp_protect_ptr(list_hp_t *hp, int ihp, uintptr_t ptr)
{
    atomic_store(&hp->hp[tid()][ihp], ptr);
    return ptr;
}

/* Same as list_hp_protect_ptr(), but explicitly uses memory_order_release.
 * Progress condition: wait-free population oblivious.
 */
uintptr_t list_hp_protect_release(list_hp_t *hp, int ihp, uintptr_t ptr)
{
    atomic_store_explicit(&hp->hp[tid()][ihp], ptr, memory_order_release);
    return ptr;
}

rb_cmp_insert_prototype(cmp_insert, a, b)
{
    retirelist_t *node = container_of(a, retirelist_t, rbnode);
    retirelist_t *x = container_of(b, retirelist_t, rbnode);
    if (node->ptr < x->ptr)
        return 1;
    else
        return 0;
}

rb_cmp_search_prototype(cmp_search, node, data)
{
    retirelist_t *n = container_of(node, retirelist_t, rbnode);
    uintptr_t ptr = atomic_load((atomic_uintptr_t *)data);
    if (n->ptr == ptr)
        return RB_EQUAL;
    else if (n->ptr > ptr)
        return RB_LEFT;
    else
        return RB_RIGHT;
}

/* Retire an object that is no longer in use by any thread, calling
 * the delete function that was specified in list_hp_new().
 *
 * Progress condition: wait-free bounded (by the number of threads squared)
 */
void list_hp_retire(list_hp_t *hp, uintptr_t ptr)
{
    struct task_tree_root *rl = hp->rl[tid() * CLPAD];

    retirelist_t *node = malloc(sizeof(retirelist_t));
    node->ptr = ptr;
    rbtree_insert(rl, &node->rbnode, cmp_insert);
    assert(rl->cnt < HP_MAX_RETIRED);

    if (rl->cnt < HP_THRESHOLD_R)
        return;


    struct task_tree_root *new = malloc(sizeof(struct task_tree_root));
    RB_ROOT_INIT(*new);
    for (int itid = 0; itid < HP_MAX_THREADS; itid++) {
        if (itid == tid())
            continue;
        for (int ihp = hp->max_hps - 1; ihp >= 0; ihp--) {
            struct rbtree *tmp = rbtree_search(rl, &hp->hp[itid][ihp], cmp_search);
            if (tmp != NULL)
                rbtree_insert(new, tmp, cmp_insert);
        }
    }
    rbtree_clean(rl, hp->deletefunc);
    hp->rl[tid() * CLPAD] = new;
}

/*
 * The list structure and function
 * 
 */

#include <pthread.h>

#define N_ELEMENTS 128
#define N_THREADS (32 /*NNN*/)
#define MAX_THREADS 128

static atomic_uint_fast32_t deletes = 0, inserts = 0;

enum { HP_NEXT = 0, HP_CURR = 1, HP_PREV };

#define is_marked(p) (bool)((uintptr_t)(p)&0x01)
#define get_marked(p) ((uintptr_t)(p) | (0x01))
#define get_unmarked(p) ((uintptr_t)(p) & (~0x01))

#define get_marked_node(p) ((list_node_t *)get_marked(p))
#define get_unmarked_node(p) ((list_node_t *)get_unmarked(p))

typedef uintptr_t list_key_t;

typedef struct list_node {
    alignas(128) uint32_t magic;
    alignas(128) atomic_uintptr_t next;
    list_key_t key;
} list_node_t;

/* Per list variables */

typedef struct list {
    atomic_uintptr_t head, tail;
    list_hp_t *hp;
} list_t;

#define LIST_MAGIC (0xDEADBEAF)

list_node_t *list_node_new(list_key_t key)
{
    list_node_t *node = aligned_alloc(128, sizeof(*node));
    assert(node);
    *node = (list_node_t){ .magic = LIST_MAGIC, .key = key };
    (void)atomic_fetch_add(&inserts, 1);
    return node;
}

void list_node_destroy(list_node_t *node)
{
    if (!node)
        return;
    assert(node->magic == LIST_MAGIC);
    free(node);
    (void)atomic_fetch_add(&deletes, 1);
}

static void __list_node_delete(struct rbtree *node)
{
    retirelist_t *n = container_of(node, retirelist_t, rbnode);
    list_node_destroy((list_node_t *) n->ptr);
    free(n);
}

/*
 * When curr mark delete, curr->next marked. 
 */
static bool __list_find(list_t *list, list_key_t *key,
                        atomic_uintptr_t **par_prev, list_node_t **par_curr,
                        list_node_t **par_next)
{
    atomic_uintptr_t *prev = NULL;
    list_node_t *curr = NULL, *next = NULL;

try_again:
    prev = &list->head;
    curr = (list_node_t *)atomic_load(prev);
    (void)list_hp_protect_ptr(list->hp, HP_CURR, (uintptr_t)curr);
    if (atomic_load(prev) != get_unmarked(curr))
        goto try_again;

    while (true) {
        if (!get_unmarked_node(curr))
            return false;

        // next = &list->tail at first
        // how to let load the value into next
        // and the protect action does not being interrupt
        next = (list_node_t *)atomic_load(&get_unmarked_node(curr)->next);
        (void)list_hp_protect_ptr(list->hp, HP_NEXT, get_unmarked(next));

        //threadA currA->next  =>     nextA      =>
        //threadB       |prev  => currB(delete)  => nextB
        //        currA->next  =>                => nextB
        // currA->next != nextA
        // insert only use prev curr node, so it is fine.
        // but curr->next get mark is 0 persent
        if (atomic_load(&get_unmarked_node(curr)->next) != get_unmarked(next)) {
            break;
        }

        // at the end of list
        if (get_unmarked(next) == atomic_load((atomic_uintptr_t *)&list->tail))
            break;

        // one more time to check the prev is same as curr
        // if during the action the curr is marked, try again.
        if (atomic_load(prev) != get_unmarked(curr))
            goto try_again;

        // next is curr->next value, when curr marked delete
        // next is marking delete.
        if (get_unmarked_node(next) == next) {
            // curr is not marked delete
            if (!(get_unmarked_node(curr)->key < *key)) {
                *par_curr = curr;
                *par_prev = prev;
                *par_next = next;
                // return FFF;
                return get_unmarked_node(curr)->key == *key;
                // if same then delete function will delete it.
            }

            // curr->key is smaller than *key
            // in other word, *key is bigger than curr->key
            // prev go next
            // store curr->next pointer into prev
            prev = &get_unmarked_node(curr)->next;
            (void)list_hp_protect_release(list->hp, HP_PREV,
                                          get_unmarked(curr));
            // protect
            //if (atomic_load(&get_unmarked_node(curr)->next) !=
            //    atomic_load(prev))
            //    goto try_again;
        } else {
            // curr->next (next) is marked delete.
            // so curr node is deleted.
            // when prev is curr, put curr into retire list
            uintptr_t tmp = get_unmarked(curr);
            // put curr->next value into prev.
            if (!atomic_compare_exchange_strong(prev, &tmp, get_unmarked(next)))
                goto try_again;
            //  try to release curr (marked node)
            list_hp_retire(list->hp, get_unmarked(curr));
        }
        (void)list_hp_protect_release(list->hp, HP_CURR, get_unmarked(next));
        curr = next;
    }
    *par_curr = curr;
    *par_prev = prev;
    *par_next = next;

    return false;
}

bool list_insert(list_t *list, list_key_t key)
{
    list_node_t *curr = NULL, *next = NULL;
    atomic_uintptr_t *prev = NULL;

    list_node_t *node = list_node_new(key);

    while (true) {
        if (__list_find(list, &key, &prev, &curr, &next)) {
            list_node_destroy(node);
            list_hp_clear(list->hp);
            return false;
        }

        atomic_store_explicit(&node->next, (uintptr_t)curr,
                              memory_order_relaxed);
        uintptr_t tmp = get_unmarked(curr);
        if (atomic_compare_exchange_strong(prev, &tmp, (uintptr_t)node)) {
            list_hp_clear(list->hp);
            return true;
        }
    }
}

/*
 * delete curr node
 * node->next => curr => next
 *       prev
 */
bool list_delete(list_t *list, list_key_t key)
{
    list_node_t *curr, *next;
    atomic_uintptr_t *prev;
    while (true) {
        if (!__list_find(list, &key, &prev, &curr, &next)) {
            list_hp_clear(list->hp);
            return false;
        }

        // marke delete
        uintptr_t tmp = get_unmarked(next);
        if (!atomic_compare_exchange_strong(&curr->next, &tmp,
                                            get_marked(next)))
            continue;

        tmp = get_unmarked(curr);
        if (atomic_compare_exchange_strong(prev, &tmp, get_unmarked(next))) {
            list_hp_clear(list->hp);
            // DDD;
            list_hp_retire(list->hp, get_unmarked(curr));
        } else {
            list_hp_clear(list->hp);
        }
        return true;
    }
}

list_t *list_new(void)
{
    list_t *list = calloc(1, sizeof(*list));
    assert(list);
    list_node_t *head = list_node_new(0), *tail = list_node_new(UINTPTR_MAX);
    assert(head), assert(tail);
    list_hp_t *hp = list_hp_new(3, __list_node_delete);

    atomic_init(&head->next, (uintptr_t)tail);
    *list = (list_t){ .hp = hp };
    atomic_init(&list->head, (uintptr_t)head);
    atomic_init(&list->tail, (uintptr_t)tail);

    return list;
}

void list_destroy(list_t *list)
{
    assert(list);
    list_node_t *prev = (list_node_t *)atomic_load(&list->head);
    list_node_t *node = (list_node_t *)atomic_load(&prev->next);
    while (node) {
        list_node_destroy(prev);
        prev = node;
        node = (list_node_t *)atomic_load(&prev->next);
    }
    list_node_destroy(prev);
    list_hp_destroy(list->hp);
    free(list);
}

static uintptr_t elements[MAX_THREADS + 1][N_ELEMENTS];

static void *insert_thread(void *arg)
{
    list_t *list = (list_t *)arg;

    for (size_t i = 0; i < N_ELEMENTS; i++)
        (void)list_insert(list, (uintptr_t)&elements[tid()][i]);

    return NULL;
}

// odd
static void *delete_thread(void *arg)
{
    list_t *list = (list_t *)arg;

    for (size_t i = 0; i < N_ELEMENTS; i++)
        (void)list_delete(list, (uintptr_t)&elements[tid()][i]);

    return NULL;
}

#include <time.h>

#define time_diff(start, end)                                                  \
    (end.tv_nsec - start.tv_nsec < 0 ?                                         \
             (1000000000 + end.tv_nsec - start.tv_nsec) :                      \
             (end.tv_nsec - start.tv_nsec))
#define time_check(_FUNC_)                                                     \
    do {                                                                       \
        struct timespec time_start;                                            \
        struct timespec time_end;                                              \
        double during;                                                         \
        clock_gettime(CLOCK_MONOTONIC, &time_start);                           \
        _FUNC_;                                                                \
        clock_gettime(CLOCK_MONOTONIC, &time_end);                             \
        during = time_diff(time_start, time_end);                              \
        printf("%f\n", during);                                                \
    } while (0)

static inline int test(void)
{
    list_t *list = list_new();

    pthread_t thr[N_THREADS];

    for (size_t i = 0; i < N_THREADS; i++)
        pthread_create(&thr[i], NULL, (i & 1) ? delete_thread : insert_thread,
                       list);

    for (size_t i = 0; i < N_THREADS; i++)
        pthread_join(thr[i], NULL);

    for (size_t i = 0; i < N_ELEMENTS; i++) {
        for (size_t j = 0; j < tid_v_base; j++)
            list_delete(list, (uintptr_t)&elements[j][i]);
    }

    list_destroy(list);

    return 0;
}

int main(void)
{
    // time_check(test());
    test();
    return 0;
}