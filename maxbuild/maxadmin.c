/**
	@file
	maxadmin - max object interface with libmapper
    http://www.idmil.org/software/mappingtools
	joseph malloch
*/

#include "ext.h"							// standard Max include, always required
#include "ext_obex.h"						// required for new style Max object
#include "./src/operations.h"
#include "./src/expression.h"
#include "./src/mapper_internal.h"
#include "./include/mapper/mapper.h"
#include <stdio.h>
#include <math.h>
#include "lo/lo.h"

#include <unistd.h>
#include <arpa/inet.h>

#define INTERVAL 1000

////////////////////////// object struct
typedef struct _maxadmin 
{
	t_object ob;			// the object itself (must be first)
    void *m_outlet;
    void *m_outlet2;
    void *m_outlet3;
    void *m_clock;          // pointer to clock object
    mapper_device device;
    mapper_signal sendsig;
    int ready;
} t_maxadmin;

int recvport = 9000;
int sendport = 9000;

///////////////////////// function prototypes
//// standard set
void *maxadmin_new(t_symbol *s, long argc, t_atom *argv);
void maxadmin_free(t_maxadmin *x);
void maxadmin_assist(t_maxadmin *x, void *b, long m, long a, char *s);
void maxadmin_anything(t_maxadmin *x, t_symbol *msg, long argc, t_atom *argv);
void maxadmin_add_signal(t_maxadmin *x, t_symbol *msg, long argc, t_atom *argv);
void maxadmin_remove_signal(t_maxadmin *x, t_symbol *msg, long argc, t_atom *argv);
void poll(t_maxadmin *x);

//////////////////////// global class pointer variable
void *maxadmin_class;


int main(void)
{	
	t_class *c;
	
	c = class_new("maxadmin", (method)maxadmin_new, (method)maxadmin_free, (long)sizeof(t_maxadmin), 
				  0L /* leave NULL!! */, A_GIMME, 0);
	
	/* you CAN'T call this from the patcher */
    class_addmethod(c, (method)maxadmin_assist,			"assist",   A_CANT,     0);
    class_addmethod(c, (method)maxadmin_add_signal,     "add",      A_GIMME,    0);
    class_addmethod(c, (method)maxadmin_remove_signal,  "remove",   A_GIMME,    0);
    class_addmethod(c, (method)maxadmin_anything,       "anything", A_GIMME,    0);
	
	class_register(CLASS_BOX, c); /* CLASS_NOBOX */
	maxadmin_class = c;

	post("I am the maxadmin object");
	return 0;
}

void maxadmin_assist(t_maxadmin *x, void *b, long m, long a, char *s)
{
	if (m == ASSIST_INLET) { // inlet
		sprintf(s, "I am inlet %ld", a);
	} 
	else {	// outlet
		sprintf(s, "I am outlet %ld", a); 			
	}
}

void maxadmin_free(t_maxadmin *x)
{
    clock_unset(x->m_clock);	// Remove clock routine from the scheduler
	clock_free(x->m_clock);		// Frees memeory used by clock
    
    if (x->device) {
        if (x->device->routers) {
            post("Removing router.. ");
            //fflush(stdout);
            mdev_remove_router(x->device, x->device->routers);
            post("ok\n");
        }
        post("Freeing device.. ");
        //fflush(stdout);
        mdev_free(x->device);
        post("ok\n");
    }
}

void maxadmin_add_signal(t_maxadmin *x, t_symbol *s, long argc, t_atom *argv)
{
	;
}

void maxadmin_remove_signal(t_maxadmin *x, t_symbol *s, long argc, t_atom *argv)
{
	;
}

void maxadmin_anything(t_maxadmin *x, t_symbol *msg, long argc, t_atom *argv)
{
	;
}

/*! Creation of a local sender. */
int setup_device(t_maxadmin *x)
{
    //use dummy name for now
    x->device = mdev_new("maxadmin", sendport);
    if (!x->device)
        return 1;
    else
        post("Device created.\n");
    
    // create a dummy signal for now
    x->sendsig = msig_float(1, "/outsig", 0, 0, 1, 0, 0, 0);
    
    mdev_register_output(x->device, x->sendsig);
    
    post("Output signal /outsig registered.\n");
    post("Number of outputs: %d\n", mdev_num_outputs(x->device));
    
    return 0;
}


void *maxadmin_new(t_symbol *s, long argc, t_atom *argv)
{
	t_maxadmin *x;
    long i;
    
    x = object_alloc(maxadmin_class);

    //intin(x,1);
    x->m_outlet = outlet_new((t_object *)x,0);
    x->m_outlet2 = outlet_new((t_object *)x,0);
    x->m_outlet3 = outlet_new((t_object *)x,0);
    object_post((t_object *)x, "a new %s object was instantiated: 0x%X", s->s_name, x);
    object_post((t_object *)x, "it has %ld arguments", argc);
        
    for (i = 0; i < argc; i++) {
        if ((argv + i)->a_type == A_LONG) {
            object_post((t_object *)x, "arg %ld: long (%ld)", i, atom_getlong(argv+i));
        } else if ((argv + i)->a_type == A_FLOAT) {
            object_post((t_object *)x, "arg %ld: float (%f)", i, atom_getfloat(argv+i));
        } else if ((argv + i)->a_type == A_SYM) {
            object_post((t_object *)x, "arg %ld: symbol (%s)", i, atom_getsym(argv+i)->s_name);
        } else {
            object_error((t_object *)x, "forbidden argument");
        }
    }
    
    if (setup_device(x)) {
        post("Error initializing device.\n");
    }
    
    x->ready = 0;
    
    x->m_clock = clock_new(x, (method)poll);	// Create the timing clock
    
    clock_delay(x->m_clock, INTERVAL);  // Set clock to go off after delay
    
	return (x);
}

void poll(t_maxadmin *x)
{
	clock_delay(x->m_clock, INTERVAL);  // Set clock to go off after delay
    
    mdev_poll(x->device, 0);
    
    if (!x->ready) {
        if (mdev_ready(x->device)) {
            mapper_db_dump();
            x->ready = 1;
        }
    }
}
