
#include <mapper/mapper.h>

int main()
{
    mapper_device md = md_new("tester");
    if (!md) {
        printf("Error allocating tester device.\n");
        return 1;
    }

    md_free(md);
    printf("Tester device freed.\n");
    return 0;
}
