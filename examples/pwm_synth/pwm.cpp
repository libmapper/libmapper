// crtsine.cpp STK tutorial program

#include "RtAudio.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <math.h>

struct _synth {
    int count;     // number of samples since period
    int state;     // state of the duty cycle (0,1)
    float value;   // current value (0..1)
    float target;  // target value (0..1)
    float rate;    // how fast to converge on target (0..1))
    float duty;    // duty cycle per period (0..1)
    float t_duty;  // target duty cycle per period (0..1)
    float freq;    // frequency in Hz
    float t_freq;  // target frequency in Hz
    float srate;   // sample rate in Hz
    float gain;    // volume control (0..1)
    float t_gain;  // target gain (0..1)
} synth;

// This tick() function handles sample computation only.  It will be
// called automatically when the system needs a new buffer of audio
// samples.
int tick( void *outputBuffer, void *inputBuffer, unsigned int nBufferFrames,
         double streamTime, RtAudioStreamStatus status, void *dataPointer )
{
    float *samples = (float *) outputBuffer;
    for ( unsigned int i=0; i<nBufferFrames; i++ ) {

        // slowly approach the targets for control parameters
        // to avoid glitching
        synth.freq = synth.t_freq*0.0001 + synth.freq*0.9999;
        synth.gain = synth.t_gain*0.0001 + synth.gain*0.9999;
        synth.duty = synth.t_duty*0.0001 + synth.duty*0.9999;

        int limit_count = (1.0 / synth.freq * synth.srate);
        limit_count *= synth.state ? synth.duty : (1.0 - synth.duty);
        if (synth.count > limit_count) {
            synth.count = 0;
            synth.target = 1.0 - synth.target;
            synth.state = 1 - synth.state;
        }

        // this creates a bit of a strange waveform but it is
        // something like a pulse-- we're trying to round the corners
        // a bit
        if (fabs(synth.value-synth.target) > 0.5)
            synth.value = (synth.target*synth.rate
                           + synth.value*(1.0-synth.rate));
        else
            synth.value = (synth.target*(1.0-synth.rate)
                           + synth.value*(synth.rate));

        *samples++ = (synth.value - 0.5) * synth.gain;
        synth.count += 1;
    }

    return 0;
}

RtAudio *dac=0;

int run_synth()
{
    if (dac)
        return 0;

    dac = new RtAudio();
    synth.count = 0;
    synth.state = 0;
    synth.value = 0;
    synth.target = 0;
    synth.rate = 0.2;
    synth.duty = 0.1;
    synth.t_duty = 0.1;
    synth.freq = 0.0;
    synth.t_freq = 110;
    synth.srate = 0;
    synth.gain = 0; // REMEMBER TO SET THE GAIN AFTER STARTING SYNTH
    synth.t_gain = 0;

    // Figure out how many bytes in an float and setup the RtAudio stream.
    RtAudio::StreamParameters parameters;
    parameters.deviceId = dac->getDefaultOutputDevice();
    parameters.nChannels = 1;
    RtAudio::StreamOptions options;
    options.flags = RTAUDIO_MINIMIZE_LATENCY;
    options.numberOfBuffers = 4;
    options.streamName = "pwm";
    options.priority = RTAUDIO_SCHEDULE_REALTIME;
    RtAudioFormat format = RTAUDIO_FLOAT32;
    unsigned int bufferFrames = 2048;
    try {
        dac->openStream( &parameters, NULL, format, 48000,
                         &bufferFrames, &tick, &options );
        synth.srate = 48000.0;
    }
    catch ( RtAudioError &error ) {
        try {
            dac->openStream( &parameters, NULL, format, 44100,
                             &bufferFrames, &tick, &options );
            synth.srate = 44100.0;
        }
        catch ( RtAudioError &error ) {
            error.printMessage();
            goto cleanup;
        }
    }

    try {
        dac->startStream();
    }
    catch ( RtAudioError &error ) {
        error.printMessage();
        goto cleanup;
    }

    return 1;

cleanup:
    delete dac;
    dac = 0;
    return 0;
}

void stop_synth()
{
    if (!dac)
        return;

    // Shut down the output stream.
    try {
        dac->closeStream();
    }
    catch ( RtAudioError &error ) {
        error.printMessage();
    }
    delete dac;
    dac = 0;
}

void set_rate(float rate)
{
    if (rate >= 0 && rate <= 1.0)
        synth.rate = rate;
}

void set_duty(float duty)
{
    if (duty >= 0 && duty <= 1.0)
        synth.t_duty = duty;
}

void set_freq(float freq)
{
    if (freq >= 0 && freq <= 20000.0)
        synth.t_freq = freq;
}

void set_gain(float gain)
{
    if (gain >= 0 && gain <= 1.0)
        synth.t_gain = gain;
}
