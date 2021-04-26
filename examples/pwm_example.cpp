
#include <unistd.h>
#include <stdio.h>
#include <signal.h>

#include <mapper/mapper_cpp.h>

#include "pwm_synth/pwm.h"

using namespace mapper;

int done = 0;

void ctrlc(int)
{
    done = 1;
}

// use simple scalar handler
void handler_freq(Signal&& sig, float value, Time&& time)
{
    set_freq(value);
}

void handler_gain(Signal&& sig, int length, Type type, const void *value, Time&& time)
{
    if (value) {
        float *pgain = (float*)value;
        set_gain(*pgain);
    }
    else
        set_gain(0);
}

void handler_duty(Signal&& sig, int length, Type type, const void *value, Time&& time)
{
    if (value) {
        float *pduty = (float*)value;
        set_duty(*pduty);
    }
}

int main()
{
    signal(SIGINT, ctrlc);

    Device dev("pwm");

    float min0 = 0;
    float max1 = 1;
    float max1000 = 1000;

    dev.add_signal(Direction::INCOMING, "/freq", 1, Type::FLOAT, "Hz", &min0, &max1000, NULL)
       .set_callback(handler_freq, Signal::Event::UPDATE);
    dev.add_signal(Direction::INCOMING, "/gain", 1, Type::FLOAT, "Hz", &min0, &max1, NULL)
       .set_callback(handler_gain, Signal::Event::UPDATE);
    dev.add_signal(Direction::INCOMING, "/duty", 1, Type::FLOAT, "Hz", &min0, &max1, NULL)
       .set_callback(handler_duty, Signal::Event::UPDATE);

    run_synth();

    set_duty(0.1);
    set_freq(110.0);
    set_gain(0.1);

    printf("Press Ctrl-C to quit.\n");

    while (!done)
        dev.poll(10);

    set_freq(0);
    sleep(1);

    stop_synth();
}
