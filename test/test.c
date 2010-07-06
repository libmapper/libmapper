#include "../src/operations.h"
#include "../src/expression.h"
#include "../src/mapper_internal.h"
#include <mapper/mapper.h>
#include <stdio.h>
#include <math.h>

#include <unistd.h>
#include <arpa/inet.h>

void copy_list_admins(list_admins src, list_admins dest);

mapper_device sender = 0;
mapper_device receiver = 0;
mapper_signal sendsig = 0;
mapper_signal recvsig = 0;

int recvport = 9000;
int sendport = 9001;

int sent = 0;
int received = 0;


int setup_sender()
{
    sender = mdev_new("testsend", sendport);
    if (!sender) goto error;
    printf("Sender created.\n");

    sendsig =
        msig_float(1, "/outsig", 0, /**/0/**/, /**/1/**/, 0, 0, 0);

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

    printf("--------------------------------HANDLER UPDATE-------------------------------------\n");
    printf("handler: Got %f\n", (*v).f);
	printf("-----------------------------------------------------------------------------------\n");    
	received++;
}

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
 
 printf("Waiting for local devices... ");
 int count = 0;
 /*while (count++ < 15  && !(   mdev_ready(sender) && mdev_ready(receiver)) && ! (sender->admin->registered==1 && receiver->admin->registered==1) ) */
    
 while (count++ < 20 && !(mdev_ready(sender) && mdev_ready(receiver) && sender->admin->registered==1 && receiver->admin->registered==1) )
		{   
			mdev_poll(sender, 0);       
			mdev_poll(receiver, 0);       

			usleep(500*1000);
    	}


}

void loop()
{
	printf("-------------------- GO ! --------------------\n");
    int i=0,j=0;
	int f;
	/*int j;*/
	mapper_device tmp_device=0;
	list_admins tmp_local_devices =0;
    
	while (i>=0) 
		{
			/*for(j=0;j<LOCAL_DEVICES.num;j++)
				{
					mdev_poll(LOCAL_DEVICES.admin[j].device,0);
			}*/

			tmp_local_devices=LOCAL_DEVICES;
			while(tmp_local_devices != NULL)
				{   f=0;
					tmp_device=tmp_local_devices->admin->device;
					printf("\n\n--POLLING 1-- %s\n",tmp_local_devices->admin->identifier);
					mdev_poll(tmp_device,0);

					if (tmp_device->num_routers>0)
						{
									f=1;	
									printf("LE NOM DU ROUTER DE TETE EST %s\n", tmp_device->routers->target_name);
									if (tmp_device->num_mappings_out>0)
										{		
											printf("MAPPED\n");
	        								msig_update_scalar(tmp_device->outputs[0], (mval)(i*1.0f));
										}
									else printf("NO MAPPING FOR THIS ROUTER SENDER\n");
						}
					if (f==0) 
						printf("THIS DEVICE IS A RECEIVER OR AN UNLINKED SENDER\n");
					tmp_local_devices=tmp_local_devices->next;
				}

	        usleep(500*1000);

			tmp_local_devices=LOCAL_DEVICES;
			while(tmp_local_devices!=NULL)
				{
					tmp_device=tmp_local_devices->admin->device;
					printf("\n\n--POLLING 2-- %s\n",tmp_local_devices->admin->identifier);
					mdev_poll(tmp_device,0);
					tmp_local_devices=tmp_local_devices->next;
				}
			
			/*for(j=0;j<LOCAL_DEVICES.num;j++)
				{
					mdev_poll(LOCAL_DEVICES.admin[j].device,0);
				}*/
			
			i++;
    	}
}

int main()
{
	
	 /*LOCAL_DEVICES.admin=malloc(100*sizeof(mapper_admin_t));*/
	 REGIST_DEVICES_INFO.regist_info=malloc(200*sizeof(mapper_admin_registered_info));
	
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

    /*if (sent != received) {
        printf("Not all sent messages were received.\n");
        printf("Updated value %d time%s, but received %d of them.\n",
               sent, sent==1?"":"s", received);
        result = 1;
    }*/


  done:
    cleanup_receiver();
    cleanup_sender();
	/*free(LOCAL_DEVICES.admin);*/
    /*printf("Test %s.\n", result?"FAILED":"PASSED");*/
    return result;
}
