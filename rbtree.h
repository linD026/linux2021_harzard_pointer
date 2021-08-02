#ifndef __RBTREE_H__
#define __RBTREE_H__

#include <stddef.h>

#define container_of(ptr, type, member)                                        \
    ({                                                                         \
        const typeof(((type *)0)->member) *__mptr = (ptr);                     \
        (type *)((char *)__mptr - offsetof(type, member));                     \
    })

#define RED 0
#define BLACK 1

#define RB_EQUAL 0
#define RB_EQUAL_BREAK -1
#define RB_LEFT 1
#define RB_RIGHT 2

struct rbtree {
    unsigned long parent_color;
    struct rbtree *leftC;
    struct rbtree *rightC;
} __attribute__((aligned(sizeof(long))));

struct task_tree_root {
    struct rbtree *head;
    struct rbtree nil;
    size_t cnt;
};

#include <stdlib.h>

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

/**
 *          |                                    |
 *          y             left_rotate            x
 *         / \          <--------------         / \
 *        x   r         --------------->       a   y
 *       / \              right_rotate            / \
 *      a   b                                    b   r
 * 
 */
static inline void task_tree_left_rotate(struct task_tree_root *root,
                                                   struct rbtree *x)
{
    struct rbtree *y = x->rightC;
    x->rightC = y->leftC;
    if (y->leftC != &root->nil)
        rb_set_parent(y->leftC, x);
    rb_set_parent(y, rb_parent(x));
    if (rb_parent(x) == &root->nil)
        root->head = y;
    else if (x == rb_parent(x)->leftC)
        rb_parent(x)->leftC = y;
    else
        rb_parent(x)->rightC = y;
    y->leftC = x;
    rb_set_parent(x, y);
}

static inline void task_tree_right_rotate(struct task_tree_root *root,
                                                    struct rbtree *y)
{
    struct rbtree *x = y->leftC;
    y->leftC = x->rightC;
    if (x->rightC != &root->nil)
        rb_set_parent(x->rightC, y);
    rb_set_parent(x, rb_parent(y));
    if (rb_parent(y) == &root->nil)
        root->head = x;
    else if (y == rb_parent(y)->rightC)
        rb_parent(y)->rightC = x;
    else
        rb_parent(y)->leftC = x;
    x->rightC = y;
    rb_set_parent(y, x);
}

void task_insert_fixup(struct task_tree_root *root, struct rbtree *node)
{
    while (rb_color(rb_parent(node)) == RED) {
        if (rb_parent(node) == rb_parent(rb_parent(node))->leftC) {
            struct rbtree *y = rb_parent(rb_parent(node))->rightC;
            if (rb_color(y) == RED) {
                rb_set_black(rb_parent(node));
                rb_set_black(y);
                rb_set_red(rb_parent(rb_parent(node)));
                node = rb_parent(rb_parent(node));
            } else {
                if (node == rb_parent(node)->rightC) {
                    node = rb_parent(node);
                    task_tree_left_rotate(root, node);
                }
                rb_set_black(rb_parent(node));
                rb_set_red(rb_parent(rb_parent(node)));
                task_tree_right_rotate(root, rb_parent(rb_parent(node)));
            }
        } else {
            struct rbtree *y = rb_parent(rb_parent(node))->leftC;
            if (rb_color(y) == RED) {
                rb_set_black(rb_parent(node));
                rb_set_black(y);
                rb_set_red(rb_parent(rb_parent(node)));
                node = rb_parent(rb_parent(node));
            } else {
                if (node == rb_parent(node)->leftC) {
                    node = rb_parent(node);
                    task_tree_right_rotate(root, node);
                }
                rb_set_black(rb_parent(node));
                rb_set_red(rb_parent(rb_parent(node)));
                task_tree_left_rotate(root, rb_parent(rb_parent(node)));
            }
        }
    }
    rb_set_black(root->head);
}

/*
 * if node1 < node 2 return true, otherwise return 0.
 */
#define rb_cmp_insert_prototype(name, rbnode1, rbnode2)                            \
    int name(struct rbtree *rbnode1, struct rbtree *rbnode2)

static inline void rbtree_insert(struct task_tree_root *root,
                                 struct rbtree *node,
                                 int (*cmp)(struct rbtree *, struct rbtree *))
{
    struct rbtree *y = &root->nil;
    struct rbtree *x = root->head;

    while (x != &root->nil) {
        y = x;
        // node < x
        if (cmp(node, x))
            x = x->leftC;
        else
            x = x->rightC;
    }
    rb_set_parent(node, y);
    if (y == &root->nil)
        root->head = node;
    // node < y
    else if (cmp(node, y))
        y->leftC = node;
    else
        y->rightC = node;
    
    node->rightC = &root->nil;
    node->leftC = &root->nil;
    rb_set_red(node);
    task_insert_fixup(root, node);

    root->cnt++;
}

#define rb_free_function_prototype(name, rbnode) void name(struct rbtree *node)

static inline void _rbtree_clean(struct task_tree_root *root,
                                 struct rbtree *node,
                                 void (*freefunc)(struct rbtree *))
{
    if (node != &root->nil) {
        struct rbtree *left = node->leftC;
        struct rbtree *right = node->rightC;
        freefunc(node);
        _rbtree_clean(root, left, freefunc);
        _rbtree_clean(root, right, freefunc);
    }
}

static inline void rbtree_clean(struct task_tree_root *root,
                                void (*freefunc)(struct rbtree *))
{
    _rbtree_clean(root, root->head, freefunc);
}

/*
 * The return value must in RB_EQUAL, RB_LEFT, RB_RIGHT, RB_EQUAL_BREAK.
 */
#define rb_cmp_search_prototype(name, rbnode, data)                            \
    int name(struct rbtree *rbnode, void *data)

static inline struct rbtree *rbtree_search(struct task_tree_root *root,
                                           void *data,
                                           int (*cmp)(struct rbtree *, void *))
{
    struct rbtree *temp = root->head;
    int ret = 0;

    while (temp != &root->nil) {
        ret = cmp(temp, data);
        switch (ret) {
            case RB_EQUAL:
                goto done;
            case RB_LEFT:
                temp = temp->leftC;
                break;
            case RB_RIGHT:
                temp = temp->rightC;
                break;
            case RB_EQUAL_BREAK:
                goto out;
        }
    }
    return NULL;
out:
    return &root->nil;
done:
    return temp;
}

void task_delete_fixup(struct task_tree_root *root, struct rbtree *node)
{
    struct rbtree *w;

    while (node != root->head && rb_is_black(node)) {
        if (node == rb_parent(node)->leftC) {
            w = rb_parent(node)->rightC;
            if (rb_is_red(w)) {
                rb_set_black(w);
                rb_set_red(rb_parent(node));
                task_tree_left_rotate(root, rb_parent(node));
                w = rb_parent(node)->rightC;
            }
            if (rb_is_black(w->leftC) && rb_is_black(w->rightC)) {
                rb_set_red(w);
                node = rb_parent(node);
            } else {
                if (rb_is_black(w->rightC)) {
                    rb_set_black(w->leftC);
                    rb_set_red(w);
                    task_tree_right_rotate(root, w);
                    w = rb_parent(node)->rightC;
                }
                rb_set_color(w, rb_parent(node));
                rb_set_black(rb_parent(node));
                rb_set_black(w->rightC);
                task_tree_left_rotate(root, rb_parent(node));
                node = root->head;
            }
        } else {
            w = rb_parent(node)->leftC;
            if (rb_is_red(w)) {
                rb_set_black(w);
                rb_set_red(rb_parent(node));
                task_tree_right_rotate(root, rb_parent(node));
                w = rb_parent(node)->leftC;
            }
            if (rb_is_black(w->rightC) && rb_is_black(w->leftC)) {
                rb_set_red(w);
                node = rb_parent(node);
            } else {
                if (rb_is_black(w->leftC)) {
                    rb_set_black(w->rightC);
                    rb_set_red(w);
                    task_tree_left_rotate(root, w);
                    w = rb_parent(node)->leftC;
                }
                rb_set_color(w, rb_parent(node));
                rb_set_black(rb_parent(node));
                rb_set_black(w->leftC);
                task_tree_right_rotate(root, rb_parent(node));
                node = root->head;
            }
        }
    }
    rb_set_black(node);
}

static inline struct rbtree *task_tree_min(struct task_tree_root *root,
                                           struct rbtree *x)
{
    while (x->leftC != &root->nil)
        x = x->leftC;
    return x;
}

static inline void task_tree_transplant(struct task_tree_root *root,
                                        struct rbtree *u, struct rbtree *v)
{
    if (rb_parent(u) == &root->nil)
        root->head = v;
    else if (u == rb_parent(u)->leftC)
        rb_parent(u)->leftC = v;
    else
        rb_parent(u)->rightC = v;
    rb_set_parent(v, rb_parent(u));
}

#include <stdbool.h>

void _rbtree_delete(struct task_tree_root *root, struct rbtree *node)
{
    struct rbtree *y = node;
    bool yoc = rb_color(y);
    struct rbtree *x;

    if (node->leftC == &root->nil) {
        x = node->rightC;
        task_tree_transplant(root, node, node->rightC);
    } else if (node->rightC == &root->nil) {
        x = node->leftC;
        task_tree_transplant(root, node, node->leftC);
    } else {
        y = task_tree_min(root, node->rightC);
        yoc = rb_color(y);
        x = y->rightC;
        if (rb_parent(y) == node)
            rb_set_parent(x, y);
        else {
            task_tree_transplant(root, y, y->rightC);
            y->rightC = node->rightC;
            rb_set_parent(y->rightC, y);
        }
        task_tree_transplant(root, node, y);
        y->leftC = node->leftC;
        rb_set_parent(y->leftC, y);
        rb_set_color(y, node);
    }
    if (yoc == BLACK)
        task_delete_fixup(root, x);
}

#define rb_delete_function_prototype(name, rbnode)                             \
    void name(struct rbtree *rbnode)

static inline int rbtree_delete(struct task_tree_root *root, void *data,
                                 int (*cmp)(struct rbtree *, void *),
                                 void (*deletefunc)(struct rbtree *))
{
    struct rbtree *z = rbtree_search(root, data, cmp);
    if (z == NULL)
        return 1;
    _rbtree_delete(root, z);
    deletefunc(z);
    root->cnt--;
    return 0;
}

#endif /* __RBTREE_H__ */