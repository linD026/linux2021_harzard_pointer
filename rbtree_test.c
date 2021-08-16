#include <stdio.h>
#include <stdlib.h>
#include "rbtree.h"

struct test_node {
    int value;
    struct rbtree node;
};

rb_cmp_insert_prototype(cmp_insert, __n1, __n2)
{
    struct test_node *n1 = container_of(__n1, struct test_node, node);
    struct test_node *n2 = container_of(__n2, struct test_node, node);

    if (n1->value < n2->value)
        return 1;
    else 
        return 0;
}

rb_cmp_search_prototype(cmp_search, __n, __d)
{
    struct test_node *n = container_of(__n, struct test_node, node);
    int *d = (int *)__d;

    if (n->value == *d)
        return RB_EQUAL;
    else if (n->value > *d)
        return RB_LEFT;
    else if (n->value < *d)
        return RB_RIGHT;
    else
        return RB_EQUAL_BREAK;
}

rb_delete_function_prototype(delete, __n)
{
    struct test_node *n = container_of(__n, struct test_node, node);
    free(n);
}

int main(void)
{
    struct task_tree_root root;
    struct test_node *tmp;

    RB_ROOT_INIT(root);
    for (int i = 0;i < 100000;i++) {
        tmp = malloc(sizeof(struct test_node));
        tmp->value = i;
        rbtree_insert(&root, &tmp->node, cmp_insert);
    }

    printf("inserted\n");

    for (int i = 330;i < 7042;i++) {
        int ret = rbtree_delete(&root, &i, cmp_search, delete);
        printf("delete %d %d\n", i, ret);
        assert(ret == 0);
    }

    printf("deleted\n");

    int i = 86503;
    struct rbtree *n = rbtree_search(&root, &i, cmp_search);
    if (n == &root.nil || n == NULL)
        printf("NO %d value\n", i);
    tmp = container_of(n, struct test_node, node);
    printf("search %d\n", tmp->value);
    printf("prev %d\n", container_of(rbtree_prev(&root, n), struct test_node, node)->value);
    printf("next %d\n", container_of(rbtree_next(&root, n), struct test_node, node)->value);
    
    rbtree_clean(&root, delete);

}