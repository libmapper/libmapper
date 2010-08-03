#include "../src/operations.h"
#include "../src/expression.h"
#include "../src/mapper_internal.h"
#include <mapper/mapper.h>
#include <stdio.h>
#include <math.h>
#include <lo/lo.h>

#include <unistd.h>
#include <arpa/inet.h>

int automate = 1;

mapper_device sender = 0;
mapper_device receiver = 0;
mapper_signal sendsig = 0;
mapper_signal recvsig = 0;

int recvport = 9000;
int sendport = 9000;

int sent = 0;
int received = 0;

/*! Creation of a local sender*/
int setup_sender()
{
    sender = mdev_new("testsend", sendport);
    if (!sender) goto error;
    printf("Sender created.\n");

    sendsig =
        msig_float(1, "/outsig", 0, 0, 1, 0, 0, 0);

    mdev_register_output(sender, sendsig);

    printf("Output signal /outsig registered.\n");
    printf("Number of outputs: %d\n", mdev_num_outputs(sender));
    return 0;

  error:
    return 1;
}

void cleanup_sender()
{
    if (sender) {
        if (sender->routers) {
            printf("Removing router.. ");
            fflush(stdout);
            mdev_remove_router(sender, sender->routers);
            printf("ok\n");
        }
        printf("Freeing sender.. ");
        fflush(stdout);
        mdev_free(sender);
        printf("ok\n");
    }
}

void insig_handler(mapper_device mdev, mapper_signal_value_t *v)
{
    printf("--> Receiver got %f\n\n", (*v).f); 
	received++;
}

/*! Creation of a local receiver*/
int setup_receiver()
{
    receiver = mdev_new("testrecv", recvport);
    if (!receiver) goto error;
    printf("Receiver created.\n");

    recvsig =
        msig_float(1, "/insig", 0, 0, 1, 0, insig_handler, 0);

    mdev_register_input(receiver, recvsig);

    printf("Input signal /insig registered.\n");
    printf("Number of inputs: %d\n", mdev_num_inputs(receiver));
    return 0;

  error:
    return 1;
}

void cleanup_receiver()
{
    if (receiver) {
        printf("Freeing receiver.. ");
        fflush(stdout);
        mdev_free(receiver);
        printf("ok\n");
    }
}



void wait_local_devices()
{
 
 int count = 0;
    
 while (count++ < 20 && !(mdev_ready(sender) && mdev_ready(receiver)) )
		{   
			mdev_poll(sender, 0);       
			mdev_poll(receiver, 0);       

			usleep(500*1000);
    	}

list_regist_info tmp_regist_dev_info=REGIST_DEVICES_INFO2;
printf("INITIAL REGISTERED DEVICES :\n");
while(tmp_regist_dev_info != NULL)
	{   
		printf("%s, %s, %i, %s\n", tmp_regist_dev_info->regist_info->full_name, tmp_regist_dev_info->regist_info->host, 																					tmp_regist_dev_info->regist_info->port, tmp_regist_dev_info->regist_info->canAlias); 
		tmp_regist_dev_info=tmp_regist_dev_info->next;
	}

}

void loop()
{
	printf("-------------------- GO ! --------------------\n");
    int i=0;
	/*mapper_device tmp_device=0;
	list_admins tmp_local_devices =0;*/
	
	if(automate) {
        char sender_name[1024], receiver_name[1024];

		printf("%s\n", mdev_name(sender));
		printf("%s\n", mdev_name(receiver));
		
		lo_address a = lo_address_new_from_url("osc.udp://224.0.1.3:7570");
		lo_address_set_ttl(a, 1);

		lo_send(a, "/link", "ss", mdev_name(sender), mdev_name(receiver));

        msig_full_name(sendsig, sender_name, 1024);
        msig_full_name(recvsig, receiver_name, 1024);

        lo_send(a, "/connect", "ss", sender_name, receiver_name);

		lo_address_free(a);
	}
    
	while (i>=0) 
		{
			
			/*tmp_local_devices=LOCAL_DEVICES;
			while(tmp_local_devices != NULL)
				{
					tmp_device=tmp_local_devices->admin->device;
					mdev_poll(tmp_device,0);

					if (tmp_device->num_routers>0)
						{	
									if (tmp_device->num_mappings_out>0)
										{		
	        								msig_update_scalar(tmp_device->outputs[0], (mval)((i%10)*1.0f));
											printf("%s value updated to %d -->\n",tmp_device->admin->identifier,i%10);
										}
						}
					tmp_local_devices=tmp_local_devices->next;
				}
	        usleep(500*1000);

		
			tmp_local_devices=LOCAL_DEVICES;
			while(tmp_local_devices!=NULL)
				{
					tmp_device=tmp_local_devices->admin->device;
					mdev_poll(tmp_device,0);
					tmp_local_devices=tmp_local_devices->next;
				}*/

			mdev_poll(sender, 0);
			if (sender->num_mappings_out>0)
				{		
	        		msig_update_scalar(sender->outputs[0], (mval)((i%10)*1.0f));
					printf("sender value updated to %d -->\n",i%10);
				}

        	usleep(500*1000);
        	mdev_poll(receiver, 0);				
			
			i++;
    	}		
}

int main()
{	
	int result=0;

    if (setup_receiver()) {
        printf("Error initializing receiver.\n");
        result = 1;
        goto done;
    }

    if (setup_sender()) {
        printf("Done initializing sender.\n");
        result = 1;
        goto done;
    }

    wait_local_devices();

    loop();

  done:
    cleanup_receiver();
    cleanup_sender();
    return result;
}
