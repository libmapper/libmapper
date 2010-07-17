#include <stk/RtWvOut.h>
#include <cstdlib>
#include <cmath>
#include <stk/BeeThree.h>
#include <stdio.h>
#include <math.h>

#include <unistd.h>
#include <arpa/inet.h>

extern "C" {
#include <mapper/mapper.h>
}

using namespace stk;

int recvport = 9000;
mapper_device synth_chords = 0;


BeeThree *fond;
mapper_signal note_fond = 0;

BeeThree *third;
mapper_signal note_third = 0;

BeeThree *fifth;
mapper_signal note_fifth = 0;

BeeThree *seventh;
mapper_signal note_seventh = 0;

mapper_signal feedback_gain = 0;
mapper_signal gain = 0;
mapper_signal LFO_speed = 0;
mapper_signal LFO_depth = 0;


void note_fond_handler(mapper_device mdev, mapper_signal_value_t *v)
{

    float received=(StkFloat)(*v).f;

	if (received==(float)0)
		fond->noteOff(1);
	else
		{
    		float freq=pow(2,(received/12))*110;
			fond->noteOn(freq,1);
			printf("note_fond_handler, %f\n",freq);
		}
		
}

void note_third_handler(mapper_device mdev, mapper_signal_value_t *v)
{

    float received=(StkFloat)(*v).f;

	if (received==(float)0)
		third->noteOff(1);
	else
		{
    		float freq;
			switch(((int)received-1)%12)
				{
					/*m3*/
					case 0: case 5: case 7: case 12:
					freq=pow(2,((received+4)/12))*110;
					break;

					/*M3*/
					case 2: case 4: case 9: case 11:
					freq=pow(2,((received+3)/12))*110;
					break;

					default:
					third->noteOff(1);
					break;
				}

			
			third->noteOn(freq,1);
			printf("note_third_handler, %f\n",freq);
		}
		
}

void note_fifth_handler(mapper_device mdev, mapper_signal_value_t *v)
{

    float received=(StkFloat)(*v).f;

	if (received==(float)0)
		fifth->noteOff(1);
	else
		{
    		float freq;
			switch(((int)received-1)%12)
				{
					/*5*/
					case 0: case 2: case 4: case 5: case 7: case 9: case 12:
					freq=pow(2,((received+7)/12))*110;
					break;

					/*b5*/
					case 11:
					freq=pow(2,((received+6)/12))*110;
					break;

					default:
					fifth->noteOff(1);
					break;
				}

			
			fifth->noteOn(freq,1);
			printf("note_fifth_handler, %f\n",freq);
		}
		
}


void note_seventh_handler(mapper_device mdev, mapper_signal_value_t *v)
{

    float received=(StkFloat)(*v).f;

	if (received==(float)0)
		seventh->noteOff(1);
	else
		{
    		float freq;
			switch(((int)received-1)%12)
				{
					/*M7*/
					case 0: case 5: case 12:
					freq=pow(2,((received+11)/12))*110;
					break;

					/*m7*/
					case 2: case 4: case 7: case 9: case 11:      
					freq=pow(2,((received+10)/12))*110;
					break;

					default:
					seventh->noteOff(1);
					break;
				}

			
			seventh->noteOn(freq,1);
			printf("note_fifth_handler, %f\n",freq);
		}
		
}


void feedback_gain_handler(mapper_device mdev, mapper_signal_value_t *v)
{
	fond->controlChange(2, (StkFloat)(*v).f);
	third->controlChange(2, (StkFloat)(*v).f);
	fifth->controlChange(2, (StkFloat)(*v).f);
	seventh->controlChange(2, (StkFloat)(*v).f);

	printf("feedback_gain_handler, %f\n",(*v).f);
}

void gain_handler(mapper_device mdev, mapper_signal_value_t *v)
{
	fond->controlChange(4, (StkFloat)(*v).f);
	third->controlChange(4, (StkFloat)(*v).f);
	fifth->controlChange(4, (StkFloat)(*v).f);	
	seventh->controlChange(4, (StkFloat)(*v).f);	

	printf("gain_handler, %f\n",(*v).f);
}

void LFO_speed_handler(mapper_device mdev, mapper_signal_value_t *v)
{
	fond->controlChange(11, (StkFloat)(*v).f);
	third->controlChange(11, (StkFloat)(*v).f);
	fifth->controlChange(11, (StkFloat)(*v).f);
	seventh->controlChange(11, (StkFloat)(*v).f);

	printf("LFO_speed_handler, %f\n",(*v).f);
}

void LFO_depth_handler(mapper_device mdev, mapper_signal_value_t *v)
{
	fond->controlChange (1, (StkFloat)(*v).f);
	third->controlChange (1, (StkFloat)(*v).f);
	fifth->controlChange (1, (StkFloat)(*v).f);	
	seventh->controlChange (1, (StkFloat)(*v).f);	

	printf("LFO_depth_handler, %f\n",(*v).f);
}


/*! Creation of the synth_chords device*/
int setup_synth_chords()
{
    synth_chords = mdev_new("synth_chords", recvport);
    if (!synth_chords) goto error;
    printf("synth_chords created.\n");

    note_fond = msig_float(1, "/note_fond", 0, 55, 220, 0, note_fond_handler, 0);
	mdev_register_input(synth_chords, note_fond);
    printf("Input signal /note_fond registered.\n");

	note_third = msig_float(1, "/note_third", 0, 55, 220, 0, note_third_handler, 0);
	mdev_register_input(synth_chords, note_third);
    printf("Input signal /note_third registered.\n");
	
	note_fifth = msig_float(1, "/note_fifth", 0, 55, 220, 0, note_fifth_handler, 0);
	mdev_register_input(synth_chords, note_fifth);
    printf("Input signal /note_fifth registered.\n");

	note_seventh = msig_float(1, "/note_seventh", 0, 55, 220, 0, note_seventh_handler, 0);
	mdev_register_input(synth_chords, note_seventh);
    printf("Input signal /note_seventh registered.\n");

	feedback_gain = msig_float(1, "/feedback_gain", 0, 0, 128, 0, feedback_gain_handler, 0);
	mdev_register_input(synth_chords, feedback_gain);
    printf("Input signal /feedback_gain registered.\n");

	gain = msig_float(1, "/gain", 0, 0, 128, 0, gain_handler, 0);
	mdev_register_input(synth_chords, gain);
    printf("Input signal /gain registered.\n");

	LFO_speed = msig_float(1, "/LFO_speed", 0, 0, 128, 0, LFO_speed_handler, 0);
	mdev_register_input(synth_chords, LFO_speed);
    printf("Input signal /LFO_speed registered.\n");

	LFO_depth = msig_float(1, "/LFO_depth", 0, 0, 128, 0, LFO_depth_handler, 0);
	mdev_register_input(synth_chords, LFO_depth);
    printf("Input signal /LFO_depth registered.\n");

    printf("Number of inputs: %d\n", mdev_num_inputs(synth_chords));
    return 0;

  error:
    return 1;
}

void cleanup_synth_chords()
{
    if (synth_chords) 
		{
	        printf("Freeing synth_chords->. ");
	        fflush(stdout);
	        mdev_free(synth_chords);
	        printf("ok\n");
	    }
}

void wait_local_devices()
{ 
	int count = 0;
	while (count < 20 && !(mdev_ready(synth_chords)) )
		{   
			mdev_poll(synth_chords, 0);   
			usleep(500*1000);
			count++;
    	}

	list_regist_info tmp_regist_dev_info=REGIST_DEVICES_INFO2;
	printf("REGISTERED DEVICES :\n");
	while(tmp_regist_dev_info != NULL)
		{   
			printf("%s, %s, %i, %s\n", tmp_regist_dev_info->regist_info->full_name, tmp_regist_dev_info->regist_info->host, 																					tmp_regist_dev_info->regist_info->port, tmp_regist_dev_info->regist_info->canAlias); 
			tmp_regist_dev_info=tmp_regist_dev_info->next;
		}
	printf("LOCAL DEVICES OK\n");

}


int main()
{
	int result=0;
	int j=0;
    float maxval = -100000;
	
	// Set the global sample rate before creating class instances.
	Stk::setSampleRate( 44100.0 );
	Stk::showWarnings( true );
	Stk::setRawwavePath( RAWWAVE_PATH );

	int nFrames = (int)(2.5/1000*44100.0); // 5 ms
	RtWvOut *dac = 0;
	StkFrames frames( nFrames, 2 );

    if (setup_synth_chords()) 
		{
        	printf("Error initializing synth_chords\n");
        	result = 1;
        	goto cleanup;
    	}

  	try {
		    // Define and open the default realtime output device for one-channel playback
		    dac = new RtWvOut( 2 );
            fond = new BeeThree();
			third = new BeeThree();
			fifth = new BeeThree();
			seventh = new BeeThree();
	
		 }
 	catch ( StkError & ) 
		{
    		goto cleanup;
  		}
	
	wait_local_devices();

	printf("-------------------- GO ! --------------------\n");
	while (j>=0) 
		{

            for (int k=0; k<11; k++)
                mdev_poll(synth_chords,0);

			try 
				{
      				for (int i=0; i<nFrames; i++) {
						float f = fond->tick(0)+third->tick(0)+fifth->tick(0)+seventh->tick(0);
						f = f/2.5;
						if (f >  1) f=1;
						if (f < -1) f=-1;
	          			frames(i, 1) = frames(i, 0) = f;
					}
      				dac->tick( frames );
  				}
			catch ( StkError & ) 
				{
 		   			goto cleanup;
  				}

			j++;
    	}

	cleanup:
    cleanup_synth_chords();
  	delete dac;
  	return result;
}
