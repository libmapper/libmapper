
#include <mapper.h>
#include <stdlib.h>
#include <string.h>

int mapper_method_add(mapper_method *head, const char *path,
                      const char *types)
{
    /* Allocate new structure */
    mapper_method method = malloc(sizeof(mapper_method_t));
    if (!method)
        return 0;

    method->path = strdup(path);
    if (!method->path) {
        free(method);
        return 0;
    }

    method->types = strdup(types);
    if (!method->types) {
        free(method->path);
        free(method);
        return 0;
    }

    method->next = NULL;

    if (*head) {
        /* Find end of list.  Check if same method has been previously
         * added while we're at it */
        mapper_method node = *head;
        while (node->next) {
            if (strcmp(node->path, path) == 0
                && strcmp(node->types, types) == 0) {
                free(method->path);
                free(method->types);
                free(method);
                return 0;
            }
            node = node->next;
        }

        if (strcmp(node->path, path) == 0
            && strcmp(node->types, types) == 0) {
            free(method->path);
            free(method->types);
            free(method);
            return 0;
        }
        node->next = method;
    } else {
        *head = method;
    }

    return 1;
}

int mapper_method_remove(mapper_method *head, const char *path,
                         const char *types)
{
    if (*head) {
        /* Find specified method */
        mapper_method node = *head;
        mapper_method *prev = head;
        while (node) {
            if (strcmp(node->path, path) == 0
                && strcmp(node->types, types) == 0)
                break;
            prev = &node->next;
            node = node->next;
        }
        if (node) {
            /* Remove it from the list */
            *prev = node->next;
            free(node->path);
            free(node->types);
            free(node);
        } else
            return 0;
    } else {
        return 0;
    }
}

int mapper_method_list_free(mapper_method *head)
{
    mapper_method node, tmp;
    node = *head;
    *head = 0;
    while (node) {
        tmp = node->next;
        free(node->path);
        free(node->types);
        free(node);
        node = tmp;
    }
}

int mapper_method_list_count(mapper_method head)
{
    mapper_method node = head;
    int count = 0;

    while (node) {
        count++;
        node = node->next;
    }

    return count;
}
