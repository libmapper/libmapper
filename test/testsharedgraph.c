#include <sys/time.h>
#include <stdio.h>
#include <mapper/mapper.h>


int main()
{
    mpr_graph graph = mpr_graph_new(MPR_OBJ);
    mpr_dev foo = mpr_dev_new("/foo", graph);
    mpr_dev bar = mpr_dev_new("/bar", graph);
    const int max_time = 10000;
    int elapsed = 0;

    while (!(mpr_dev_get_is_ready(foo) && mpr_dev_get_is_ready(bar)))
    {
        mpr_dev_poll(foo, 10);
        mpr_dev_poll(bar, 10);
        elapsed += 20;
        if (elapsed > max_time) return 1;
    }

    return 0;
}
