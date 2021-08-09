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

#ifdef ANALYSIS_OPS

/*
 * rtry - goto try_again
 * con  - wait free contain
 * trav - traversal all the node
 * fail - CAS failed
 * del  - list_delete failed
 * ins  - list_insert failed
 */
static atomic_uint_fast64_t rtry = 0, cons = 0, trav = 0, fail = 0;
static atomic_uint_fast64_t del = 0, ins = 0;
static atomic_uint_fast64_t deletes = 0, inserts = 0;

#define goto_try_again                                                         \
    do {                                                                       \
        atomic_fetch_add(&rtry, 1);                                            \
        goto try_again;                                                        \
    } while (0)
#define cons_inc                                                               \
    do {                                                                       \
        atomic_fetch_add(&cons, 1);                                            \
    } while (0)
#define trav_inc                                                               \
    do {                                                                       \
        atomic_fetch_add(&trav, 1);                                            \
    } while (0)
#define CAS(obj, expected, desired)                                            \
    ({                                                                         \
        atomic_fetch_add(&fail, 1);                                            \
        atomic_compare_exchange_strong(obj, expected, desired);                \
    })
#define del_inc                                                                \
    do {                                                                       \
        atomic_fetch_add(&del, 1);                                             \
    } while (0)
#define ins_inc                                                                \
    do {                                                                       \
        atomic_fetch_add(&ins, 1);                                             \
    } while (0)
#define deletes_inc                                                            \
    do {                                                                       \
        atomic_fetch_add(&deletes, 1);                                         \
    } while (0)
#define inserts_inc                                                            \
    do {                                                                       \
        atomic_fetch_add(&inserts, 1);                                         \
    } while (0)

void analysis_func(void)
{
    printf("%10s %10s %10s %10s %10s %10s %10s %10s\n", "rtry", "cons", "trav",
           "fail", "del", "ins", "deletes", "inserts");
    for (int i = 0; i < 87; i++)
        printf("-");
    printf("\n%10ld %10ld %10ld %10ld %10ld %10ld %10ld %10ld\n", rtry, cons,
           trav, fail, del, ins, deletes, inserts);
}

#else

#define goto_try_again                                                         \
    do {                                                                       \
        goto try_again;                                                        \
    } while (0)
#define cons_inc                                                               \
    do {                                                                       \
    } while (0)
#define trav_inc                                                               \
    do {                                                                       \
    } while (0)
#define CAS(obj, expected, desired)                                            \
    ({ atomic_compare_exchange_strong(obj, expected, desired); })
#define del_inc                                                                \
    do {                                                                       \
    } while (0)
#define ins_inc                                                                \
    do {                                                                       \
    } while (0)
#define deletes_inc                                                            \
    do {                                                                       \
    } while (0)
#define inserts_inc                                                            \
    do {                                                                       \
    } while (0)

#endif

#define HP_MAX_THREADS 128
#define HP_MAX_HPS 5 /* This is named 'K' in the HP paper */
#define CLPAD (128 / sizeof(uintptr_t)) /* 128 / 8 = 16 */
#define HP_THRESHOLD_R 0 /* This is named 'R' in the HP paper */

/* Maximum number of retired objects per thread */
#define HP_MAX_RETIRED (HP_MAX_THREADS * HP_MAX_HPS)

#define TID_UNKNOWN -1

typedef struct {
    int size;
    uintptr_t *list;
} retirelist_t;

typedef void(list_hp_deletefunc_t)(void *);

typedef struct list_hp {
    int max_hps;
    alignas(128) atomic_uintptr_t *hp[HP_MAX_THREADS];
    alignas(128) retirelist_t *rl[HP_MAX_THREADS * CLPAD];
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
        // sizeof(hp->hp[i][0]) == sizeof(uintptr_t)
        // nmemb * size = calloc(nmemb, size)
        // retirelist_t *rl[HP_MAX_THREADS * CLPAD]
        hp->hp[i] = calloc(CLPAD * 2, sizeof(hp->hp[i][0]));
        hp->rl[i * CLPAD] = calloc(1, sizeof(*hp->rl[0]));
        // why bound is max_hps not CLPAD * 2?
        for (int j = 0; j < hp->max_hps; j++)
            atomic_init(&hp->hp[i][j], 0);
        hp->rl[i * CLPAD]->list = calloc(HP_MAX_RETIRED, sizeof(uintptr_t));
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
        retirelist_t *rl = hp->rl[i * CLPAD];
        for (int j = 0; j < rl->size; j++) {
            void *data = (void *)rl->list[j];
            hp->deletefunc(data);
        }
        free(rl->list);
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

/* Retire an object that is no longer in use by any thread, calling
 * the delete function that was specified in list_hp_new().
 *
 * Progress condition: wait-free bounded (by the number of threads squared)
 */
void list_hp_retire(list_hp_t *hp, uintptr_t ptr)
{
    retirelist_t *rl = hp->rl[tid() * CLPAD];
    // append the ptr we want to delete
    rl->list[rl->size++] = ptr;
    assert(rl->size < HP_MAX_RETIRED);

    if (rl->size < HP_THRESHOLD_R)
        return;

    for (size_t iret = 0; iret < rl->size; iret++) {
        uintptr_t obj = rl->list[iret];
        bool can_delete = true;
        for (int itid = 0; itid < HP_MAX_THREADS && can_delete; itid++) {
            for (int ihp = hp->max_hps - 1; ihp >= 0; ihp--) {
                // if the thread's hp stored the ptr equal to obj
                // cannot delete.
                if (atomic_load(&hp->hp[itid][ihp]) == obj) {
                    // can_delete = false is breaking
                    // the second  for loop
                    can_delete = false;
                    break;
                }
            }
        }

        // when this obj is not in all the hp, delete it.
        if (can_delete) {
            size_t bytes = (rl->size - iret) * sizeof(rl->list[0]);
            memmove(&rl->list[iret], &rl->list[iret + 1], bytes);
            rl->size--;
            hp->deletefunc((void *)obj);
        }
    }
}

/*
 * The list structure and function
 * 
 */

#include <pthread.h>

#define N_ELEMENTS 128
#define N_THREADS (64 /*NNN*/)
#define MAX_THREADS 128

enum { HP_NEXT = 0, HP_CURR = 1, HP_PREV, HP_START };

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
    inserts_inc;
    return node;
}

void list_node_destroy(list_node_t *node)
{
    if (!node)
        return;
    assert(node->magic == LIST_MAGIC);
    free(node);
    deletes_inc;
}

static void __list_node_delete(void *arg)
{
    list_node_t *node = (list_node_t *)arg;
    list_node_destroy(node);
}

static bool __list_find_ordered(list_t *list, list_key_t *key,
                                atomic_uintptr_t *start,
                                atomic_uintptr_t **par_prev,
                                list_node_t **par_curr, list_node_t **par_next)
{
    atomic_uintptr_t *prev = NULL;
    list_node_t *curr = NULL, *next = NULL;

try_again:
    prev = start;
    curr = (list_node_t *)atomic_load(prev);
    (void)list_hp_protect_ptr(list->hp, HP_CURR, (uintptr_t)curr);

    while (true) {
        // get next
        next = (list_node_t *)atomic_load(&get_unmarked_node(curr)->next);
        (void)list_hp_protect_ptr(list->hp, HP_NEXT, get_unmarked(next));

        // find left_node(prev) and right_node (curr)
        do {
            trav_inc;
            if (!is_marked(next)) {
                (void)list_hp_protect_release(list->hp, HP_PREV,
                                              get_unmarked(curr));
                prev = &get_unmarked_node(curr)->next;
            }
#ifdef ANALYSIS_OPS
            else
                cons_inc;
#endif
            (void)list_hp_protect_release(list->hp, HP_CURR,
                                          get_unmarked(next));
            curr = get_unmarked_node(next);
            if (get_unmarked(curr) ==
                atomic_load((atomic_uintptr_t *)&list->tail))
                break;
            next = (list_node_t *)atomic_load(&get_unmarked_node(curr)->next);
            (void)list_hp_protect_ptr(list->hp, HP_NEXT, get_unmarked(next));
        } while (is_marked(next) || get_unmarked_node(curr)->key < *key);

        if (atomic_load(prev) == get_unmarked(curr)) {
            if (get_unmarked(curr) !=
                        atomic_load((atomic_uintptr_t *)&list->tail) &&
                is_marked(get_unmarked_node(curr)->next))
                goto_try_again;
            else {
                *par_curr = curr;
                *par_prev = prev;
                *par_next = next;
                return get_unmarked_node(curr)->key == *key;
            }
        }
        uintptr_t tmp = get_unmarked(curr);
        if (CAS(prev, &tmp, get_unmarked(curr))) {
            if (get_unmarked(curr) !=
                        atomic_load((atomic_uintptr_t *)&list->tail) &&
                is_marked(get_unmarked_node(curr)->next)) {
                goto_try_again;
            } else {
                *par_curr = curr;
                *par_prev = prev;
                *par_next = next;
                return get_unmarked_node(curr)->key == *key;
            }
        }

        curr = (list_node_t *)atomic_load(prev);
        (void)list_hp_protect_release(list->hp, HP_CURR, get_unmarked(curr));
        if (atomic_load(prev) != get_unmarked(curr))
            goto_try_again;

    } /* while (true) */
}

bool list_insert_conti(list_t *list, list_key_t key)
{
    list_node_t *curr = NULL, *next = NULL;
    atomic_uintptr_t *prev = NULL;

    list_node_t *node = list_node_new(key);

try_again:
    if (__list_find_ordered(list, &key, &list->head, &prev, &curr, &next)) {
        list_node_destroy(node);
        list_hp_clear(list->hp);
        return false;
    }

    while (true) {
        atomic_store_explicit(&node->next, (uintptr_t)curr,
                              memory_order_relaxed);
        uintptr_t tmp = get_unmarked(curr);
        if (CAS(prev, &tmp, (uintptr_t)node)) {
            list_hp_clear(list->hp);
            return true;
        }

        if (is_marked(atomic_load(prev))) {
            goto_try_again;
        }

        (void)list_hp_protect_release(list->hp, HP_START,
                                      get_unmarked(atomic_load(prev)));
        if (__list_find_ordered(list, &key, prev, &prev, &curr, &next)) {
            list_node_destroy(node);
            list_hp_clear(list->hp);
            return false;
        }
        ins_inc;
    } /* while(true) */
}

/*
 * delete curr node
 * node->next => curr => next
 *       prev
 */

bool list_delete_once(list_t *list, list_key_t key)
{
    list_node_t *curr, *next;
    atomic_uintptr_t *prev;
try_again:
    if (!__list_find_ordered(list, &key, &list->head, &prev, &curr, &next)) {
        list_hp_clear(list->hp);
        return false;
    }
    do {
        // already marked
        uintptr_t tmp = atomic_fetch_or(&curr->next, 0x01);
        if (is_marked(tmp))
            return true;

        tmp = get_unmarked(curr);
        if (CAS(prev, &tmp, get_unmarked(next))) {
            list_hp_clear(list->hp);
            list_hp_retire(list->hp, get_unmarked(curr));
            return true;
        }

        if (is_marked(atomic_load(prev))) {
            list_hp_clear(list->hp);
            goto_try_again;
        }
        (void)list_hp_protect_release(list->hp, HP_START,
                                      get_unmarked(atomic_load(prev)));
        if (!__list_find_ordered(list, &key, prev, &prev, &curr, &next)) {
            list_hp_clear(list->hp);
            return false;
        }
        del_inc;
    } while (true);
}

list_t *list_new(void)
{
    list_t *list = calloc(1, sizeof(*list));
    assert(list);
    list_node_t *head = list_node_new(0), *tail = list_node_new(UINTPTR_MAX);
    assert(head), assert(tail);
    list_hp_t *hp = list_hp_new(4, __list_node_delete);

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
        (void)list_insert_conti(list, (uintptr_t)&elements[tid()][i]);

    return NULL;
}

// odd
static void *delete_thread(void *arg)
{
    list_t *list = (list_t *)arg;

    for (size_t i = 0; i < N_ELEMENTS; i++)
        (void)list_delete_once(list, (uintptr_t)&elements[tid()][i]);

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
            list_delete_once(list, (uintptr_t)&elements[j][i]);
    }

    list_destroy(list);

    return 0;
}

int main(void)
{
    // time_check(test());
#ifdef ANALYSIS_OPS
    int times = 10;
    for (int i = 0;i < times;i++) {
        test();
        tid_v_base = 0;
    }
    analysis_func();
#else
    test();
#endif

    return 0;
}
