
#include <lo/lo.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <ifaddrs.h>
#include <arpa/inet.h>
#include <sys/time.h>

#include "mapper_internal.h"
#include "types_internal.h"
#include "config.h"
#include <mapper/mapper.h>

/*! Internal function to get the current time. */
static double get_current_time()
{
#ifdef HAVE_GETTIMEOFDAY
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double)tv.tv_sec + tv.tv_usec / 1000000.0;
#else
    #error No timing method known on this platform.
#endif
}

/* Internal message handler prototypes. */
static int handler_who(const char*, const char*, lo_arg **, int, lo_message, void*);
static int handler_registered(const char*, const char*, lo_arg **, int, lo_message, void*);
static int handler_id_n_namespace_input_get(const char*, const char*, lo_arg **, int, lo_message, void*);
static int handler_id_n_namespace_output_get(const char*, const char*, lo_arg **, int, lo_message, void*);
static int handler_id_n_namespace_get(const char*, const char*, lo_arg **, int, lo_message, void*);
static int handler_device_alloc_port(const char*, const char*, lo_arg **, int, lo_message, void*);
static int handler_device_alloc_name(const char*, const char*, lo_arg **, int, lo_message, void*);
static int handler_device_link(const char*, const char*, lo_arg **, int, lo_message, void*);
static int handler_device_unlink(const char*, const char*, lo_arg **, int, lo_message, void*);
static int handler_param_connect(const char*, const char*, lo_arg **, int, lo_message, void*);
static int handler_param_connect_to(const char*, const char*, lo_arg **, int, lo_message, void*);
static int handler_param_disconnect(const char*, const char*, lo_arg **, int, lo_message, void*);


/* Internal LibLo error handler */
static void handler_error(int num, const char *msg, const char *where)
{
    printf("liblo server error %d in path %s: %s\n", num, where, msg);
}

/* Functions for handling the resource allocation scheme.  If
 * check_collisions() returns 1, the resource in question should be
 * announced on the admin bus. */
static int check_collisions(mapper_admin admin,
                            mapper_admin_allocated_t *resource);
static void on_collision(mapper_admin_allocated_t *resource, mapper_admin admin, int type);

/*! Local function to get the IP address of a network interface. */
static struct in_addr get_interface_addr(const char *ifname)
{
    struct ifaddrs   *ifaphead;
    struct ifaddrs   *ifap;
    struct in_addr    error = {0};

    if (getifaddrs(&ifaphead)!=0)
        return error;

    ifap = ifaphead;
    while (ifap) {
        struct sockaddr_in *sa = (struct sockaddr_in*)ifap->ifa_addr;
        if (!sa) {
            trace("ifap->ifa_addr = 0, unknown condition.\n");
            ifap = ifap->ifa_next;
            continue;
        }
        if (sa->sin_family == AF_INET
            && strcmp(ifap->ifa_name, ifname)==0 )
        {
            return sa->sin_addr;
        }
        ifap = ifap->ifa_next;
    }

    return error;
}

/*! Allocate and initialize a new admin structure.
 *  \param identifier An identifier for this device which does not
 *  need to be unique.
 *  \param type The device type for this device. (Data direction,
 *  functionality.)
 *  \param initial_port The initial UDP port to use for this
 *  device. This will likely change within a few minutes after the
 *  device is allocated.
 *  \return A newly initialized mapper admin structure.
 */
mapper_admin mapper_admin_new(const char *identifier,
                              mapper_device device,
                              mapper_device_type_t type,
                              int initial_port)
{
    mapper_admin admin = malloc(sizeof(mapper_admin_t));
    if (!admin)
        return NULL;

    // Initialize interface information
    // We'll use defaults for now, perhaps this should be configurable in the future.
    {
        char *eths[] = {"eth0", "eth1", "eth2", "eth3", "eth4",
                        "en0", "en1", "en2", "en3", "en4", "lo"};
        int num = sizeof(eths)/sizeof(char*), i;
        for (i=0; i<num; i++) {
            admin->interface_ip = get_interface_addr(eths[i]);
            if (admin->interface_ip.s_addr != 0) {
                strcpy(admin->interface, eths[i]);
                break;
            }
        }
        if (i>=num) {
            trace("no interface found\n");
        }
    }

    /* Open address for multicast group 224.0.1.3, port 7570 */
    admin->admin_addr = lo_address_new("224.0.1.3", "7570");
    if (!admin->admin_addr) {
        free(admin->identifier);
        free(admin);
        return NULL;
    }

    /* Set TTL for packet to 1 -> local subnet */
    lo_address_set_ttl(admin->admin_addr, 1);

    /* Open server for multicast group 224.0.1.3, port 7570 */
    admin->admin_server = lo_server_new_multicast("224.0.1.3", "7570", handler_error);
    if (!admin->admin_server) {
        free(admin->identifier);
        lo_address_free(admin->admin_addr);
        free(admin);
        return NULL;
    }

    /* Initialize data structures */
    admin->identifier = strdup(identifier);
    admin->device_type = type;
    admin->ordinal.value = 1;
    admin->ordinal.locked = 0;
    admin->ordinal.collision_count = -1;
    admin->ordinal.count_time = get_current_time();
    admin->port.value = initial_port;
    admin->port.locked = 0;
    admin->port.collision_count = -1;
    admin->port.count_time = get_current_time();
    admin->registered = 0;
    admin->device = device;

    /* Add methods for admin bus.  Only add methods needed for
     * allocation here. Further methods are added when the device is
     * registered. */
    lo_server_add_method(admin->admin_server, "/port/probe", NULL, handler_device_alloc_port, admin);
    lo_server_add_method(admin->admin_server, "/name/probe", NULL, handler_device_alloc_name, admin);

    /* Announce port and name to admin bus. */
    mapper_admin_port_announce(admin);
    mapper_admin_name_announce(admin);

    return admin;
}

/*! Free the memory allocated by a mapper admin structure.
 *  \param admin An admin structure handle.
 */
void mapper_admin_free(mapper_admin admin)
{
    if (!admin)
        return;

    if (admin->identifier)
        free(admin->identifier);

    if (admin->admin_server)
        lo_server_free(admin->admin_server);

    if (admin->admin_addr)
        lo_address_free(admin->admin_addr);

    free(admin);
}

/*! This is the main function to be called once in a while from a
 *  program so that the admin bus can be automatically managed.
 */
void mapper_admin_poll(mapper_admin admin)
{

    
int count=0;

    while (count < 10 && lo_server_recv_noblock(admin->admin_server, 0))
        { 
			count++;
		}


    /* If the port is not yet locked, process collision timing.  Once
     * the port is locked it won't change. */
    if (!admin->port.locked)
        if (check_collisions(admin, &admin->port))
            /* If the port has changed, re-announce the new port. */
            mapper_admin_port_announce(admin);

    /* If the ordinal is not yet locked, process collision timing.
     * Once the ordinal is locked it won't change. */
    if (!admin->ordinal.locked)
        if (check_collisions(admin, &admin->ordinal))
            /* If the ordinal has changed, re-announce the new name. */
            mapper_admin_name_announce(admin);

    /* If we are ready to register the device, add the needed message
     * handlers. */
    if (!admin->registered && admin->port.locked && admin->ordinal.locked)
    {
        char namespaceget[256];
        lo_server_add_method(admin->admin_server, "/who", "", handler_who, admin);
		lo_server_add_method(admin->admin_server, "/registered", "ssssisssisisiiiiiiii", handler_registered, admin);
        
		snprintf(namespaceget, 256, "/%s.%d/namespace/get", admin->identifier, admin->ordinal.value);
        lo_server_add_method(admin->admin_server, namespaceget, "", handler_id_n_namespace_get, admin);
		
		snprintf(namespaceget, 256, "/%s.%d/namespace/input/get", admin->identifier, admin->ordinal.value);
        lo_server_add_method(admin->admin_server, namespaceget, "", handler_id_n_namespace_input_get, admin);

		snprintf(namespaceget, 256, "/%s.%d/namespace/output/get", admin->identifier, admin->ordinal.value);
        lo_server_add_method(admin->admin_server, namespaceget, "", handler_id_n_namespace_output_get, admin);
	
		snprintf(namespaceget, 256, "/%s.%d/info/get", admin->identifier, admin->ordinal.value);
        lo_server_add_method(admin->admin_server, namespaceget, "", handler_who, admin);

        lo_server_add_method(admin->admin_server, "/link", "ss", handler_device_link, admin);
		lo_server_add_method(admin->admin_server, "/unlink", "ss", handler_device_unlink, admin);

 		lo_server_add_method(admin->admin_server, "/connect", NULL, handler_param_connect, admin);
        lo_server_add_method(admin->admin_server, "/connect_to", NULL, handler_param_connect_to, admin);
		lo_server_add_method(admin->admin_server, "/disconnect", NULL, handler_param_disconnect, admin);

        /*admin->registered = 1; a mettre plutot dans le handler de registered et ensuite l'utiliser comme condition ?*/
		int send=lo_send(admin->admin_addr,"/who", "" );
		printf("ENVOI DE WHO %s\n",send==-1?"ECHEC":"REUSSI");

    }

}

/*! Announce the device's current port on the admin bus to enable the
 *  allocation protocol.
 */
void mapper_admin_port_announce(mapper_admin admin)
{
    trace("probing port\n");

    lo_send(admin->admin_addr, "/port/probe", "i", admin->port.value);

}

/*! Announce the device's current name on the admin bus to enable the
 *  allocation protocol.
 */
void mapper_admin_name_announce(mapper_admin admin)
{
    char name[256];
    trace("probing name\n");
    /*printf("identifier : %s ordinal : %d\n",admin->identifier, admin->ordinal.value);*/
    snprintf(name, 256, "/%s.%d", admin->identifier, admin->ordinal.value);
    
    lo_send(admin->admin_addr,"/name/probe", "s", name);

}

/*! Algorithm for checking collisions and allocating resources. */
static int check_collisions(mapper_admin admin,
                            mapper_admin_allocated_t *resource )
{
    double timediff;

    if (resource->locked)
        return 0;

    timediff = get_current_time() - resource->count_time;

    if (timediff >= 2.0) {
        resource->locked = 1;
        if (resource->on_lock)
            resource->on_lock(admin->device, resource);
    }

    else
        /* If port collisions were found within 500 milliseconds of the
         * last announcement, try a new random port. */
        if (timediff >= 0.5 && resource->collision_count > 0)
        {
            /* Otherwise, add a random number based on the number of
             * collisions. */
            resource->value += (int)(((double)rand())/RAND_MAX
                                     * (resource->collision_count+1));
            
            /* Prepare for causing new port collisions. */

            resource->collision_count = -1;
            resource->count_time = get_current_time();

            /* Indicate that we need to re-announce the new value. */
            return 1;
        }
    
    return 0;
}

static void on_collision(mapper_admin_allocated_t *resource, mapper_admin admin, int type)
	{
		char name[256];
    		snprintf(name, 256, "/%s.%d", admin->identifier, admin->ordinal.value);
    		
		if (resource->locked) 
			{
				if (type==0)/*resource=port*/
					{
         	 				lo_send(admin->admin_addr,"/port/registered", "i",admin->port.value );
					}
				else if (type==1)/*resource=ordinal*/
					{
         	  				lo_send(admin->admin_addr,"/name/registered", "s", name );
					}
				
				return;
			}
		

		/* Count port collisions. */
    		resource->collision_count ++;
    		trace("%d collision_count = %d\n", resource->value, resource->collision_count);
    		resource->count_time = get_current_time();
	}	

/**********************************/
/* Internal OSC message handlers. */
/**********************************/

/*! Respond to /who by announcing the current port. */
static int handler_who(const char *path, const char *types, lo_arg **argv,
                              int argc, lo_message msg, void *user_data)
{

    mapper_admin admin = (mapper_admin)user_data;
    char name[256];

    snprintf(name, 256, "/%s.%d", admin->identifier, admin->ordinal.value);

    lo_send(admin->admin_addr,
            "/registered", "ssssisssisisiiiiiiii",
            name,
            "@IP", inet_ntoa(admin->interface_ip),
            "@port", admin->port.value,
			"@canAlias", "no",/*******ALIAS*************/
            "@numInputs", mdev_num_inputs(admin->device),
            "@numOutputs", mdev_num_outputs(admin->device)/*)*/
			,"@hash",0,0,0,0,0,0,0,0);
    /*********************************************************************************************/
			
		if(!(*((mapper_admin) user_data)).registered)
			{

				strcpy((*((mapper_admin) user_data)).regist_info.full_name,name);
				strcpy((*((mapper_admin) user_data)).regist_info.host,inet_ntoa(admin->interface_ip));
    			(*((mapper_admin) user_data)).regist_info.port=admin->port.value;
				strcpy((*((mapper_admin) user_data)).regist_info.canAlias,"no");/***********ALIAS**************/
    			
				printf("LOCAL %s : Registered host : %s Registered port : %d\n", (*((mapper_admin) user_data)).regist_info.full_name ,(*((mapper_admin) user_data)).regist_info.host,(*((mapper_admin)user_data)).regist_info.port);
				/*LOCAL_DEVICES.admin[LOCAL_DEVICES.num]=(*((mapper_admin) user_data));
				LOCAL_DEVICES.num++;*/
				mdev_add_LOCAL_DEVICES((mapper_admin)user_data);
				printf("NEW LOCAL DEVICES : %s !\n",LOCAL_DEVICES->admin->identifier);
				(*((mapper_admin) user_data)).registered=1;
			
			}
    return 0;
}


/*! Register information about port and host for the device */
static int handler_registered(const char *path, const char *types, lo_arg **argv,
                                     int argc, lo_message msg, void *user_data)
{
	int i;
    int f=1;
    char registered_name[1024];
     
	/*printf("HANDLER REGISTERED\n");*/         	
            
    if (argc < 1)
        return 0;

    if (types[0]!='s' && types[0]!='S')
        return 0;

    strcpy(registered_name, &argv[0]->s);
    
	for(i=0; i<REGIST_DEVICES_INFO.num;i++)
		f*=strcmp(registered_name, REGIST_DEVICES_INFO.regist_info[i].full_name); 

	if(f!=0)
		{
			strcpy(REGIST_DEVICES_INFO.regist_info[REGIST_DEVICES_INFO.num].full_name,registered_name);
			strcpy(REGIST_DEVICES_INFO.regist_info[REGIST_DEVICES_INFO.num].host,&argv[2]->s);
			REGIST_DEVICES_INFO.regist_info[REGIST_DEVICES_INFO.num].port=argv[4]->i;
			strcpy(REGIST_DEVICES_INFO.regist_info[REGIST_DEVICES_INFO.num].canAlias,&argv[6]->s);
			printf("DEVICE %s no %d REGISTERED : name=%s, host=%s, port=%d, canAlias=%s\n",registered_name,REGIST_DEVICES_INFO.num,REGIST_DEVICES_INFO.regist_info[REGIST_DEVICES_INFO.num].full_name,REGIST_DEVICES_INFO.regist_info[REGIST_DEVICES_INFO.num].host,REGIST_DEVICES_INFO.regist_info[REGIST_DEVICES_INFO.num].port,REGIST_DEVICES_INFO.regist_info[REGIST_DEVICES_INFO.num].canAlias);
			REGIST_DEVICES_INFO.num++;
		}

    return 0;
}


/*! Respond to /namespace/input/get by enumerating all supported inputs*/
static int handler_id_n_namespace_input_get(const char *path, const char *types, lo_arg **argv,
                                      int argc, lo_message msg, void *user_data)
{
    mapper_admin admin = (mapper_admin)user_data;
    mapper_device md = admin->device;
    char name[1024], response[1024], method[1024], type[2]={0,0};
    int i;

    snprintf(name, 1024, "/%s.%d", admin->identifier, admin->ordinal.value);

    strcpy(response, name);
    strcat(response, "/namespace/input");
    for (i=0; i < md->n_inputs; i++)
    	{
        	mapper_signal sig = md->inputs[i];
        	lo_message m = lo_message_new();

        	/*strcpy(method, name);                          DO NOT SEND THE FULL NAME /device/signame !!!
        	strcat(method, sig->name);*/

			strcpy(method, sig->name);
        	lo_message_add_string(m, method);

        	lo_message_add_string(m, "@type");
        	type[0] = sig->type;
        	lo_message_add_string(m, type);

        	if (sig->minimum) {
            	lo_message_add_string(m, "@min");
            	mval_add_to_message(m, sig, sig->minimum);
        	}

        	if (sig->maximum) {
            	lo_message_add_string(m, "@max");
            	mval_add_to_message(m, sig, sig->maximum);
	        }

    	    lo_send_message(admin->admin_addr, response, m);
    	    lo_message_free(m);
    }

    return 0;
}

/*! Respond to /namespace/output/get by enumerating all supported outputs*/
static int handler_id_n_namespace_output_get(const char *path, const char *types, lo_arg **argv,
                                      int argc, lo_message msg, void *user_data)
{
    mapper_admin admin = (mapper_admin)user_data;
    mapper_device md = admin->device;
    char name[1024], response[1024], method[1024], type[2]={0,0};
    int i;

    snprintf(name, 1024, "/%s.%d", admin->identifier, admin->ordinal.value);

    strcpy(response, name);
    strcat(response, "/namespace/output");
    for (i=0; i < md->n_outputs; i++)
    {
        mapper_signal sig = md->outputs[i];
        lo_message m = lo_message_new();

        /*strcpy(method, name);
        strcat(method, sig->name);*/                /*DO NOT SEND THE FULL NAME /device/signame !!!*/

		strcpy(method, sig->name);
        lo_message_add_string(m, method);

        lo_message_add_string(m, "@type");
        type[0] = sig->type;
        lo_message_add_string(m, type);

        if (sig->minimum) {
            lo_message_add_string(m, "@min");
            mval_add_to_message(m, sig, sig->minimum);
        }

        if (sig->maximum) {
            lo_message_add_string(m, "@max");
            mval_add_to_message(m, sig, sig->maximum);
        }

        lo_send_message(admin->admin_addr, response, m);
        lo_message_free(m);
    }

    return 0;
}

/*! Respond to /namespace/get by enumerating all supported inputs and outputs*/
static int handler_id_n_namespace_get(const char *path, const char *types, lo_arg **argv,
                                      int argc, lo_message msg, void *user_data)
{

handler_id_n_namespace_input_get(path, types, argv, argc,msg, user_data);
handler_id_n_namespace_output_get(path, types, argv, argc,msg, user_data);

return 0;

}

static int handler_device_alloc_port(const char *path, const char *types, lo_arg **argv,
                                     int argc, lo_message msg, void *user_data)
{
    mapper_admin admin = (mapper_admin) user_data;
    

    unsigned int  announced_port = 0;

    if (argc < 1)
        return 0;

    if (types[0]=='i')
        announced_port = argv[0]->i;
    else if (types[0]=='f')
        announced_port = (unsigned int)argv[0]->f;
    else
        return 0;

    trace("got /port/probe %d \n", announced_port);

    /* Process port collisions. */
    if (announced_port == admin->port.value)
        on_collision(&admin->port, admin, 0);

    return 0;
}

static int handler_device_alloc_name(const char *path, const char *types, lo_arg **argv,
                                     int argc, lo_message msg, void *user_data)
{
    mapper_admin admin = (mapper_admin) user_data;
    	

    char         *announced_name = 0, *s;
    unsigned int  announced_ordinal = 0;

    if (argc < 1)
        return 0;

    if (types[0]!='s' && types[0]!='S')
        return 0;

    announced_name = &argv[0]->s;

    /* Parse the ordinal from the complete name which is in the format: /<name>.<n> */
    s = announced_name;
    if (*s++ != '/') return 0;
    while (*s != '.' && *s++) {}
    announced_ordinal = atoi(++s);

    trace("got /name/probe %s\n", announced_name);

    /* Process ordinal collisions. */
    if (announced_ordinal == admin->ordinal.value)
        on_collision(&admin->ordinal, admin, 1);

    return 0;
}


static int handler_device_link(const char *path, const char *types, lo_arg **argv,
                                     int argc, lo_message msg, void *user_data)
{

	/*printf("\n\nHANDLER LINK\n");*/
    /*mapper_admin admin = (mapper_admin) user_data;            (*((mapper_admin) user_data)).   */
    /*mapper_device md = admin->device;*/
    
	int i;
    char device_name[1024], sender_name[1024], target_name[1024], tmp_target_name[1024], host_adress[1024];
	int recvport;    
	mapper_router router = 0;

    /*int recvport = (*((mapper_admin) user_data)).regist_info.port;
	strcpy(host_adress, (*((mapper_admin) user_data)).regist_info.host);*/                          

    if (argc < 1)
        return 0;

    if (types[0]!='s' && types[0]!='S'&& types[1]!='s' && types[1]!='S')
        return 0;

 
    snprintf(device_name, 256, "/%s.%d", (*((mapper_admin) user_data)).identifier, (*((mapper_admin) user_data)).ordinal.value);
    strcpy(sender_name,&argv[0]->s);
    strcpy(target_name,&argv[1]->s);


    trace("got /link %s %s\n", sender_name, target_name);
	printf("%s GOT /link %s %s\n", device_name, sender_name, target_name);
    
    if ( strcmp(device_name,sender_name)==0 )
		{
			/*******************************************************************************************************************/
			for (i=0; i<REGIST_DEVICES_INFO.num; i++)
    		{
				
				strcpy(tmp_target_name,REGIST_DEVICES_INFO.regist_info[i].full_name);
				printf("PASSAGE %d DANS REGIST : REGARDE %s\n", i,tmp_target_name); 
				if (strcmp (target_name, tmp_target_name)==0)
					{
						recvport=REGIST_DEVICES_INFO.regist_info[i].port;
						strcpy(host_adress, REGIST_DEVICES_INFO.regist_info[i].host);
						printf("OK ! host=%s port=%d\n", host_adress, recvport);
					}
			}
			/*******************************************************************************************************************/

	   		router = mapper_router_new(host_adress, recvport, target_name);
   		    mdev_add_router((*((mapper_admin) user_data)).device, router);
			(*((mapper_admin) user_data)).device->num_routers++;
			printf("Router to %s : %d added.\n", host_adress,recvport);
			lo_send((*((mapper_admin) user_data)).admin_addr,"/linked", "ss", device_name, ((*((mapper_admin) user_data)).device->routers->target_name) );
			/*printf("/linked %s %s\n", device_name, ((*((mapper_admin) user_data)).device->routers->target_name) );*/		
	
		}

    else if ( strcmp(device_name,target_name)==0 )
		{	/****************************************************************************/
			lo_send((*((mapper_admin) user_data)).admin_addr,"/linked", "ss", sender_name, target_name );
    		/*************************************************************************/
		}

  

    return 0;
}

static int handler_device_unlink(const char *path, const char *types, lo_arg **argv,
                                     int argc, lo_message msg, void *user_data)
{
    
	int f=0;
    char device_name[1024], sender_name[1024], target_name[1024];    
	mapper_admin admin = (mapper_admin) user_data;
    mapper_device md = admin->device;
	mapper_router router=md->routers;                    

    if (argc < 1)
        return 0;

    if (types[0]!='s' && types[0]!='S'&& types[1]!='s' && types[1]!='S')
        return 0;

    snprintf(device_name, 256, "/%s.%d", (*((mapper_admin) user_data)).identifier, (*((mapper_admin) user_data)).ordinal.value);
    strcpy(sender_name,&argv[0]->s);
    strcpy(target_name,&argv[1]->s);


    trace("got /unlink %s %s\n", sender_name, target_name);
	printf("%s GOT /unlink %s %s\n", device_name, sender_name, target_name);

    
    if ( strcmp(device_name,sender_name)==0 )
		{
			printf("MOI %s CHERCHE PARMI MES ROUTEURS... %s\n",device_name, target_name);
			while ( router!=NULL && f==0 ) 
				{
					if ( strcmp (router->target_name , target_name ) == 0 )
						{								
							printf("TROUVE LE ROUTEUR %s !!! JE VAIS LE SUPPRIMER...\n",target_name);
							/*(*((mapper_admin) user_data)).device->routers=*/mdev_remove_router( /*(*((mapper_admin) user_data)).device*/md, router);
							(*((mapper_admin) user_data)).device->num_routers--;							
							printf("JE L'AI SUPPRIME !!!!!\n");
							/*mapper_router_free(router);*/
							f=1;
							printf("F FLAGUE A 1\n");
						}
					else router = router->next;
				}
	
			if (f==1)
				{
					printf("J'AI VU QUE F=1\n");
					lo_send((*((mapper_admin) user_data)).admin_addr,"/unlinked", "ss", device_name, target_name );	
					printf("%s SENT : /unlinked %s %s\n", device_name, sender_name, target_name );
				}
			else printf("ERROR UNLINK PAS TROUVE ROUTER...\n");
		}

    else if ( strcmp(device_name,target_name)==0 )
		{
			/*lo_send((*((mapper_admin) user_data)).admin_addr,"/unlinked", "ss", sender_name, target_name );*/
		}

  

    return 0;
}



static int handler_param_connect_to(const char *path, const char *types, lo_arg **argv,
                                     int argc, lo_message msg, void *user_data)
{

    mapper_admin admin = (mapper_admin) user_data;
    mapper_device md = admin->device;
	mapper_router router=md->routers;

    int md_num_outputs=(*((mapper_admin) user_data)).device->n_outputs;
    mapper_signal *md_outputs=(*((mapper_admin) user_data)).device->outputs;

    int i=0;
	int c=1;
	int j=2;
	int f1=0,f2=0;

    char sig_name[1024], src_param_name[1024], target_param_name[1024],target_device_name[1024];

	char src_type,dest_type;
	float dest_range_min,dest_range_max,src_range_min,src_range_max;
	char scaling[1024],clipMin[1024],clipMax[1024];	
	char *expression;

	expression=strdup("y =2 *x");

    if (argc < 2)
        return 0;
	/*printf("il y a %d arguments a connect_to\n", argc);
	for (int k=0; k<argc;k++)
		{
			printf("type argument %d : %c \n", k, types[k]);
		}*/

    if ( (types[0]!='s' && types[0]!='S') || (types[1]!='s' && types[1]!='S') || (strcmp(&argv[2]->s,"@type")!=0) || (types[3]!='c' && types[3]!='s' && types[3]!='S') )
        return 0;

    strcpy(src_param_name,&argv[0]->s);
    strcpy(target_param_name, &argv[1]->s);
	while (target_param_name[c]!='/')
		c++;
	strncpy(target_device_name, target_param_name,c);
	target_device_name[c]='\0';
		
	/*******************************************???????**************************************************/
	
	if( argc>4 )
		{
				
			trace("got /connect_to %s %s @type %c+ OPTIONS\n", src_param_name, target_param_name, dest_type);
			printf("GOT /connect_to %s %s @type %c+ OPTIONS\n", src_param_name, target_param_name, dest_type);

			while(j<argc)
				{
					if (types[j]!='s' && types[j]!='S')
						{
							printf("syntaxe message incorrecte\n");
							return 0;
						}

					else if(strcmp(&argv[j]->s,"@scaling")==0)
						{
							strcpy(scaling,&argv[j+1]->s);
							j+=2;
						}

					else if(strcmp(&argv[j]->s,"@range")==0)
						{
							dest_range_min=argv[j+1]->f;
							dest_range_max=argv[j+2]->f;
							j+=3;
						}

					else if(strcmp(&argv[j]->s,"@expression")==0)
						{
							strcpy(expression,&argv[j+1]->s);
							j+=2;
						}

					else if(strcmp(&argv[j]->s,"@clipMin")==0)
						{
							strcpy(clipMin,&argv[j+1]->s);
							j+=2;
						}			

					else if(strcmp(&argv[j]->s,"@clipMax")==0)
						{
							strcpy(clipMax,&argv[j+1]->s);
							j+=2;
						}			
				
				}
								
		}

	else
		{
			trace("got /connect_to %s %s @type %c\n", src_param_name, target_param_name, dest_type);
			printf("GOT /connect_to %s %s @type %c\n", src_param_name, target_param_name,dest_type);
			dest_range_min=0;
			dest_range_max=1;
			strcpy(clipMin,"none");
			strcpy(clipMax,"none");
		}
 
/************************************************************************************************************************************************/

    while (i<md_num_outputs && f1==0)
    	{

			msig_full_name(md_outputs[i],sig_name,256); 
			printf("signal etudie : %s ...\n",sig_name);
			
    		
    		if ( strcmp(sig_name,src_param_name)==0 )
				{		

 		   			printf("Mapping signal %s -> %s...\n",sig_name, target_param_name);
					src_type=md_outputs[i]->type;
					src_range_min=md_outputs[i]->minimum->f;
					src_range_max=md_outputs[i]->maximum->f;

					if (argc==2)
						strcpy(scaling,"bypass");
					if (argc >2)
						{
							if (strcmp(&argv[2]->s,"@type")==0)							
								dest_type=argv[3]->c;

							if ( ( src_type=='i'|| src_type=='f') && ( dest_type=='i'|| dest_type=='f') )
								strcpy(scaling,"linear");
							else strcpy(scaling,"bypass");
						}

					
    				while ( router!=NULL && f2==0 ) 
						{
							printf("COMPARE LE ROUTEUR %s...\n",router->target_name);	
							if ( strcmp ( router->target_name , target_device_name ) == 0 )
								f2=1;
							else router=router->next;
							
						}

					if (f2==1)
						{
							printf("ROUTER %s CORRESPONDANT !!\n",router->target_name);

							if (strcmp(scaling,"bypass")==0)
								{	
									printf("SCALING=BYPASS\n");
									expression=strdup("y=x");
									mapper_router_add_direct_mapping(router, (*((mapper_admin) user_data)).device->outputs[i],target_param_name);
								}
							else if (strcmp(scaling,"linear")==0)
								{	
									printf("SCALING=LINEAR\n");
									if (src_range_min==src_range_max)
										{
											/*free(expression);*/
											sprintf(expression,"y=%f",src_range_min);
										}
									else
										{	
											printf("CAS GENERAL CAS LINEAR\n");
											free(expression);		
											printf("%f %f %f %f\n",src_range_min,src_range_max,dest_range_min,dest_range_max);
											expression=malloc(100*sizeof(char));									
											snprintf(expression,100,"y=(x-%f)*%f+%f",
												src_range_min,(dest_range_max-dest_range_min)/(src_range_max-src_range_min),dest_range_min);
											printf("OK EXPRESSION MODIFIEE : %s \n",expression);	
										}							
									mapper_router_add_expression_mapping(router, (*((mapper_admin) user_data)).device->outputs[i],target_param_name/**/,expression/**/) ;	
								}

						    else if (strcmp(scaling,"expression")==0)
								mapper_router_add_expression_mapping(router, (*((mapper_admin) user_data)).device->outputs[i],target_param_name/**/,expression/**/) ;	
						 	

							(*((mapper_admin) user_data)).device->num_mappings_out++;
							lo_send((*((mapper_admin) user_data)).admin_addr,"/connected", "sssssffffssssss", 
								sig_name, target_param_name, 
								"@scaling",scaling,
								"@range",src_range_min,src_range_max,dest_range_min,dest_range_max,
								"@expression",expression,
								"@clipMin",clipMin,
								"@clipMax",clipMax);		
						}
					else printf("AUCUN ROUTER N'EXISTE !!!!!\n");
					f1=1;
				}

    		else i++;
		}


    return 0;
}

static int handler_param_disconnect(const char *path, const char *types, lo_arg **argv,
                                     int argc, lo_message msg, void *user_data)
{

	
    int md_num_outputs=(*((mapper_admin) user_data)).device->n_outputs;
    mapper_signal *md_outputs=(*((mapper_admin) user_data)).device->outputs;
    int i=0;
	int c=1;
	int f1=0,f2=0;

    char sig_name[1024], src_param_name[1024], target_param_name[1024],target_device_name[1024];

    if (argc < 1)
        return 0;

    if (types[0]!='s' && types[0]!='S' && types[1]!='s' && types[1]!='S')
        return 0;

    strcpy(src_param_name,&argv[0]->s);
    strcpy(target_param_name, &argv[1]->s);
	while (target_param_name[c]!='/')
		c++;
	strncpy(target_device_name, target_param_name,c);
	target_device_name[c]='\0';

    trace("got /disconnect %s %s\n", src_param_name, target_param_name);
	printf("GOT /disconnect %s %s\n", src_param_name, target_param_name);


    while (i<md_num_outputs && f1==0)
    	{

			msig_full_name(md_outputs[i],sig_name,256); 
			printf("signal etudie : %s ...\n",sig_name);
			
    
    		if ( strcmp(sig_name,src_param_name)==0 )
				{		
 		   			printf("disconnecting signal %s -> %s...\n",sig_name, target_param_name);


    				while ( (*((mapper_admin) user_data)).device->routers!=NULL && f2==0 )
						{
							if ( strcmp ( (*((mapper_admin) user_data)).device->routers->target_name , target_device_name ) == 0 )
								f2=1;
							else (*((mapper_admin) user_data)).device->routers=(*((mapper_admin) user_data)).device->routers->next;
							
						}

					if (f2==1)
						{
 		   				
							mapper_router_remove_mapping(((*((mapper_admin) user_data)).device->routers), (*((mapper_admin) user_data)).device->outputs[i],
								target_param_name) ;
							(*((mapper_admin) user_data)).device->num_mappings_out--;
							lo_send((*((mapper_admin) user_data)).admin_addr,"/disconnected", "ss", sig_name, target_param_name );	
							
						}
					else printf("AUCUN ROUTER N'EXISTE !!!!!\n");
					f1=1;
				}

    		else i++;
		}


    return 0;
}

static int handler_param_connect(const char *path, const char *types, lo_arg **argv,
                                     int argc, lo_message msg, void *user_data)
{

	/*printf("\n\nHANDLER CONNECT\n");*/
    /*mapper_admin admin = (mapper_admin) user_data;*/
    /*mapper_device md = admin->device;*/
    int md_num_inputs=(*((mapper_admin) user_data)).device->n_inputs;
    mapper_signal *md_inputs=(*((mapper_admin) user_data)).device->inputs;
    int i=0;
	int j=2;
	int f=0;

    char sig_name[1024], src_param_name[1024], target_param_name[1024];

    if (argc < 2)
        return 0;

    if (types[0]!='s' && types[0]!='S' && types[1]!='s' && types[1]!='S')
        return 0;

    strcpy(src_param_name,&argv[0]->s);
    strcpy(target_param_name, &argv[1]->s);

    trace("got /connect %s %s\n", src_param_name, target_param_name);
	printf("GOT /connect %s %s\n",src_param_name, target_param_name);


	while (i<md_num_inputs && f==0)
    	{

			msig_full_name(md_inputs[i],sig_name,256); 
			printf("signal etudie : %s ...\n",sig_name);
    
    		if ( strcmp(sig_name,target_param_name)==0 )
				{		
						lo_message m=lo_message_new();
						lo_message_add(m,"sssc",src_param_name,target_param_name,"@type",(md_inputs[i]->type));
						/*If options added to the connect message*/
						if(argc>2)
							{
								
								while(j<argc)
									{
										switch (types[j])
											{
												case ('s'): case ('S'): 
												lo_message_add(m, types[j], (char *)&argv[j]->s);
												break;												

												case ('i'): case ('h'): 
												lo_message_add(m, types[j], (int)argv[j]->i);
												break;

												case ('f'): 
												lo_message_add(m, types[j], (float)argv[j]->f);
												break;
												
												default:
												printf("Unknown message type %c\n",types[j]);
											}
									
										j++;
									}
								
								printf("SEND /connect_to %s %s @type %c + OPTIONS\n", src_param_name, sig_name,(md_inputs[i]->type));
							}						
						/*No options added to the connect message*/
						else
							{
								printf("SEND /connect_to %s %s @type %c\n", src_param_name, sig_name,(md_inputs[i]->type) );
							}
						lo_send_message((*((mapper_admin) user_data)).admin_addr,"/connect_to",m);
						lo_message_free(m);
						f=1;
				}

    		else i++;
		}
  

    return 0;
}
