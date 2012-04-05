
#include <unistd.h>
#include <stdio.h>
#include <signal.h>

#include <mapper/mapper.h>

#include "pwm_synth/pwm.h"

int done = 0;

void ctrlc(int)
{
    done = 1;
}

void handler_freq(mapper_signal msig,
                  int instance_id,
                  mapper_db_signal props,
                  mapper_timetag_t *timetag,
                  void *value)
{
    float *pfreq = (float*)value;
    set_freq(*pfreq);
}

void handler_gain(mapper_signal msig,
                  int instance_id,
                  mapper_db_signal props,
                  mapper_timetag_t *timetag,
                  void *value)
{
    float *pgain = (float*)value;
    set_gain(*pgain);
}

void handler_duty(mapper_signal msig,
                  int instance_id,
                  mapper_db_signal props,
                  mapper_timetag_t *timetag,
                  void *value)
{
    float *pduty = (float*)pduty;
    set_duty(*pduty);
}

int main()
{
    signal(SIGINT, ctrlc);

    mapper_device dev = mdev_new("pwm", 9000, 0);

    float min0 = 0;
    float max1 = 1;
    float max1000 = 1000;

    mdev_add_input(dev, "/freq", 1, 'f', "Hz", &min0, &max1000,
                   handler_freq, 0);
    mdev_add_input(dev, "/gain", 1, 'f', "Hz", &min0, &max1,
                   handler_gain, 0);
    mdev_add_input(dev, "/duty", 1, 'f', "Hz", &min0, &max1,
                   handler_duty, 0);

    run_synth();

    set_duty(0.1);
    set_freq(110.0);
    set_gain(0.1);

    printf("Press Ctrl-C to quit.\n");

    while (!done)
        mdev_poll(dev, 10);

    mdev_free(dev);

    set_freq(0);
    sleep(1);

    stop_synth();
}
