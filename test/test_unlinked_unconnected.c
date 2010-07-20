#include "../src/operations.h"
#include "../src/expression.h"
#include "../src/mapper_internal.h"
#include <mapper/mapper.h>
#include <stdio.h>
#include <math.h>

#include <unistd.h>
#include <arpa/inet.h>

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
        msig_float(1, "/outsig", 0, INFINITY, INFINITY, 0, 0, 0);

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
        msig_float(1, "/insig", 0, INFINITY, INFINITY, 0, insig_handler, 0);

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

int setup_router()
{

	int i,j;
	int  count= 0;    
	printf("\n\n\n\n!!!!!!!!!!!!!!!!!!!! -> 15 to give the LINK order !!!!!!!!!!!!!!!!!!\n\n\n\n");
    while (count++ < 15)                                                           
    	{   
			
			for(i=0;i<LOCAL_DEVICES.num;i++)
				{
					mdev_poll(LOCAL_DEVICES.admin[i].device,0);
					/*printf("POLLING no %d : %s\n",i,LOCAL_DEVICES.admin[i].regist_info.full_name);*/
				}			

			/*mdev_poll(sender, 0);      
			mdev_poll(receiver, 0);*/  
			
			usleep(500*1000);

			printf("%d\n",count);
    	}



   char signame_in[1024];
    if (!msig_full_name(recvsig, signame_in, 1024)) {
        printf("Could not get receiver signal name.\n");
        return 1;
    }

    char signame_out[1024];
    if (!msig_full_name(sendsig, signame_out, 1024)) {
        printf("Could not get sender signal name.\n");
        return 1;
    }    
	
	/*printf("********************\nROUTER INFO : Router name : %s\nHost : %s\nPort : %d\n********************\n", sender->routers->target_name,sender->admin->regist_info.host,sender->admin->regist_info.port);	*/
	
	
	printf("\n\n\n\n!!!!!!!!!!!!!!!!!!!!  -> 45 to give the CONNECTION order !!!!!!!!!!!!!!!!!!\n\n\n\n");
    while (count++ < 45)                                                           
    	{   
			/*mdev_poll(sender, 0);       
			mdev_poll(receiver, 0);*/

			for(j=0;j<LOCAL_DEVICES.num;j++)
				{
					mdev_poll(LOCAL_DEVICES.admin[j].device,0);
					/*printf("POLLING no %d : %s\n", j, LOCAL_DEVICES.admin[j].regist_info.full_name);*/
				}

			usleep(500*1000);
			printf("%d\n",count);
    	}         
	/*printf("********************\nMAPPING INFO : Mapping Name = %s Mapping type = %d\n********************\n", sender->routers->mappings->mapping->name,sender->routers->mappings->mapping->type);*/                                          
     
    return 0;
}


void wait_ready()
{

    int count = 0;
    /*while (count++ < 15  || !(   mdev_ready(sender) && mdev_ready(receiver)) && ! (sender->admin->registered==1 && receiver->admin->registered==1) ) */
    
 while (count++ < 20  || (!(   mdev_ready(sender) && mdev_ready(receiver)) /*&& ! (sender->admin->registered==1 && receiver->admin->registered==1)*/) )
		{   
			mdev_poll(sender, 0);       
			mdev_poll(receiver, 0);       

			usleep(500*1000);
    	}


}

void loop()
{
    printf("Polling device..\n");
    int i,j;
    for (i=0; i<20; i++) 
		{
	        /*mdev_poll(sender, 0);*/
			for(j=0;j<LOCAL_DEVICES.num;j++)
				{
					mdev_poll(LOCAL_DEVICES.admin[j].device,0);
					/*printf("POLLING no %d : %s\n", j, LOCAL_DEVICES.admin[j].regist_info.full_name);*/
				}

			if (sender->routers)
				{	printf("LOCAL SENDER\n");
	        		printf("Updating signal %s to %f\n", sendsig->name, (i*1.0f));
	        		msig_update_scalar(sendsig, (mval)(i*1.0f));
	        		sent ++;
				}
			
			else printf("ONLY LOCAL RECEIVER\n");

	        usleep(1000*1000);
	        /*mdev_poll(receiver, 0);*/
			for(j=0;j<LOCAL_DEVICES.num;j++)
				{
					mdev_poll(LOCAL_DEVICES.admin[j].device,0);
					/*printf("POLLING no %d : %s\n",j,LOCAL_DEVICES.admin[j].regist_info.full_name);*/
				}
    	}
}

int main()
{
	
	/********************************************************************************/
	 LOCAL_DEVICES.admin=malloc(100*sizeof(mapper_admin_t));
	 REGIST_DEVICES_INFO.regist_info=malloc(200*sizeof(mapper_admin_registered_info));
	/********************************************************************************/   
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

    wait_ready();

    if (setup_router()) {
        printf("Error initializing router.\n");
        result = 1;
        goto done;
    }

    loop();

    if (sent != received) {
        printf("Not all sent messages were received.\n");
        printf("Updated value %d time%s, but received %d of them.\n",
               sent, sent==1?"":"s", received);
        result = 1;
    }


  done:
    cleanup_receiver();
    cleanup_sender();
	free(LOCAL_DEVICES.admin);
    printf("Test %s.\n", result?"FAILED":"PASSED");
    return result;
}
