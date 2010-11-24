#ifndef _PWM_H_
#define _PWM_H_

int run_synth();
void stop_synth();

void set_rate(float rate);
void set_duty(float duty);
void set_freq(float freq);
void set_gain(float gain);

#endif
