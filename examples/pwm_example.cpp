
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

void handler_freq(mapper_signal sig, mapper_id instance, const void *value,
                  int count, mapper_timetag_t *timetag)
{
    if (value) {
        float *pfreq = (float*)value;
        set_freq(*pfreq);
    }
}

void handler_gain(mapper_signal sig, mapper_id instance, const void *value,
                  int count, mapper_timetag_t *timetag)
{
    if (value) {
        float *pgain = (float*)value;
        set_gain(*pgain);
    }
    else
        set_gain(0);
}

void handler_duty(mapper_signal sig, mapper_id instance, const void *value,
                  int count, mapper_timetag_t *timetag)
{
    if (value) {
        float *pduty = (float*)value;
        set_duty(*pduty);
    }
}

int main()
{
    signal(SIGINT, ctrlc);

    mapper_device dev = mapper_device_new("pwm", 9000, 0);

    float min0 = 0;
    float max1 = 1;
    float max1000 = 1000;

    mapper_device_add_input(dev, "/freq", 1, 'f', "Hz", &min0, &max1000,
                            handler_freq, 0);
    mapper_device_add_input(dev, "/gain", 1, 'f', "Hz", &min0, &max1,
                            handler_gain, 0);
    mapper_device_add_input(dev, "/duty", 1, 'f', "Hz", &min0, &max1,
                            handler_duty, 0);

    run_synth();

    set_duty(0.1);
    set_freq(110.0);
    set_gain(0.1);

    printf("Press Ctrl-C to quit.\n");

    while (!done)
        mapper_device_poll(dev, 10);

    mapper_device_free(dev);

    set_freq(0);
    sleep(1);

    stop_synth();
}
