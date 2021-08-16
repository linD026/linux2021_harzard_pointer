#ifndef __RBTREE_H__
#define __RBTREE_H__

#include <assert.h>
#include <stddef.h>

#ifdef __GNUC__

struct rbtree {
    unsigned long parent_color;
    struct rbtree *leftC;
    struct rbtree *rightC;
} __attribute__((aligned(sizeof(long))));

#elif __STDC_VERSION__ >= 201112L

#include <stdalign.h>

struct rbtree {
    alignas(sizeof(long)) unsigned long parent_color;
    struct rbtree *leftC;
    struct rbtree *rightC;
};

#else

static_assert(0, "Oops! Version is not support.");

#endif

struct task_tree_root {
    struct rbtree *head;
    struct rbtree nil;
    size_t cnt;
};

#define container_of(ptr, type, member)                                        \
    __extension__({                                                            \
        const __typeof__(((type *)0)->member) *__mptr = (ptr);                 \
        (type *)((char *)__mptr - offsetof(type, member));                     \
    })

#define RED 0
#define BLACK 1

#define RB_EQUAL 0
#define RB_EQUAL_BREAK -1
#define RB_LEFT 1
#define RB_RIGHT 2

#define rb_parent(r) ((struct rbtree *)((r)->parent_color & ~3))
#define rb_color(r) ((r)->parent_color & 1)

#define rb_is_red(r) (!rb_color(r))
#define rb_is_black(r) (rb_color(r))

#define rb_set_parent(r, p)                                                    \
    do {                                                                       \
        (r)->parent_color = rb_color(r) | (unsigned long)(p);                  \
    } while (0)

#define rb_set_red(r)                                                          \
    do {                                                                       \
        (r)->parent_color &= ~1;                                               \
    } while (0)

#define rb_set_black(r)                                                        \
    do {                                                                       \
        (r)->parent_color |= 1;                                                \
    } while (0)

#define rb_set_color(d, s)                                                     \
    do {                                                                       \
        struct rbtree __tmp = *(s);                                            \
        __tmp.parent_color |= (unsigned long)~1;                               \
        (d)->parent_color &= __tmp.parent_color;                               \
    } while (0)

#define RB_ROOT_INIT(root)                                                     \
    do {                                                                       \
        (root).head = &((root).nil);                                           \
        (root).nil.parent_color = (unsigned long)BLACK;                        \
        (root).nil.leftC = NULL;                                               \
        (root).nil.rightC = NULL;                                              \
        (root).cnt = 0;                                                        \
    } while (0)

struct rbtree *task_tree_min(struct task_tree_root *root, struct rbtree *x);
struct rbtree *task_tree_max(struct task_tree_root *root, struct rbtree *x);

#define rbtree_next(r, n)                                                      \
    ({                                                                         \
        struct rbtree *__tmp, *__n;                                            \
        if (n->rightC != NULL)                                                 \
            __tmp = task_tree_min(r, n->rightC);                               \
        else {                                                                 \
            __tmp = rb_parent(n);                                              \
            while (__tmp != r->nil && n == __tmp->rightC) {                    \
                __n = __tmp;                                                   \
                __tmp = rb_parent(__tmp);                                      \
            }                                                                  \
        }                                                                      \
        __tmp;                                                                 \
    })

#define rbtree_prev(r, n)                                                      \
    ({                                                                         \
        struct rbtree *__tmp, __n;                                             \
        if (n->leftC != NULL)                                                  \
            __tmp = task_tree_max(r, n->leftC);                                \
        else {                                                                 \
            __tmp = rb_parent(n);                                              \
            while (__tmp != r->nil && n == __tmp->leftC) {                     \
                __n = __tmp;                                                   \
                __tmp = rb_parent(__tmp);                                      \
            }                                                                  \
        }                                                                      \
        __tmp;                                                                 \
    })

/*
 * if node1 < node 2 return true, otherwise return 0.
 */
#define rb_cmp_insert_prototype(name, rbnode1, rbnode2)                        \
    int name(struct rbtree *rbnode1, struct rbtree *rbnode2)

void rbtree_insert(struct task_tree_root *root, struct rbtree *node,
                   int (*cmp)(struct rbtree *, struct rbtree *));

void rbtree_clean(struct task_tree_root *root,
                  void (*freefunc)(struct rbtree *));

/*
 * The return value must in RB_EQUAL, RB_LEFT, RB_RIGHT, RB_EQUAL_BREAK.
 */
#define rb_cmp_search_prototype(name, rbnode, data)                            \
    int name(struct rbtree *rbnode, void *data)

struct rbtree *rbtree_search(struct task_tree_root *root, void *data,
                             int (*cmp)(struct rbtree *, void *));

#define rb_delete_function_prototype(name, rbnode)                             \
    void name(struct rbtree *rbnode)

void _rbtree_delete(struct task_tree_root *root, struct rbtree *node);

int rbtree_delete(struct task_tree_root *root, void *data,
                  int (*cmp)(struct rbtree *, void *),
                  void (*deletefunc)(struct rbtree *));

#endif /* __RBTREE_H__ */
