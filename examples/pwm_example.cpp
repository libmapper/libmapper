
#include <unistd.h>
#include <stdio.h>
#include <signal.h>

#include <mpr/mpr_cpp.h>

#include "pwm_synth/pwm.h"

int done = 0;

void ctrlc(int)
{
    done = 1;
}

void handler_freq(mpr_sig sig, mpr_sig_evt event, mpr_id instance, int length,
                  mpr_type type, const void *value, mpr_time time)
{
    if (value) {
        float *pfreq = (float*)value;
        set_freq(*pfreq);
    }
}

void handler_gain(mpr_sig sig, mpr_sig_evt event, mpr_id instance, int length,
                  mpr_type type, const void *value, mpr_time time)
{
    if (value) {
        float *pgain = (float*)value;
        set_gain(*pgain);
    }
    else
        set_gain(0);
}

void handler_duty(mpr_sig sig, mpr_sig_evt event, mpr_id instance, int length,
                  mpr_type type, const void *value, mpr_time time)
{
    if (value) {
        float *pduty = (float*)value;
        set_duty(*pduty);
    }
}

int main()
{
    signal(SIGINT, ctrlc);

    mpr::Device dev("pwm");

    float min0 = 0;
    float max1 = 1;
    float max1000 = 1000;

    dev.add_sig(MPR_DIR_IN, "/freq", 1, MPR_FLT, "Hz", &min0, &max1000, NULL,
                handler_freq, MPR_SIG_UPDATE);
    dev.add_sig(MPR_DIR_IN, "/gain", 1, MPR_FLT, "Hz", &min0, &max1, NULL,
                handler_gain, MPR_SIG_UPDATE);
    dev.add_sig(MPR_DIR_IN, "/duty", 1, MPR_FLT, "Hz", &min0, &max1, NULL,
                handler_duty, MPR_SIG_UPDATE);

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
