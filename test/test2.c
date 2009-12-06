
#include <mapper/mapper.h>

int main()
{
    mapper_device md = mdev_new("tester", 0);
    if (!md) {
        printf("Error allocating tester device.\n");
        return 1;
    }

    mdev_free(md);
    printf("Tester device freed.\n");
    return 0;
}
