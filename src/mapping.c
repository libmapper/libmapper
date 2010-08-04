
#include "mapper_internal.h"
#include "types_internal.h"
#include <mapper/mapper.h>
#include "operations.h"
#include "expression.h"

void mapper_mapping_perform(mapper_mapping mapping,
                            mapper_signal_value_t *from_value,
                            mapper_signal_value_t *to_value)
{
    int p, changed=0;
    float v;
    error err=NO_ERR;


    p=mapping->history_pos;
    mapping->history_input[p] = from_value->f;
	v=mapping->history_input[p];

    if( mapping->type==BYPASS /*|| mapping->type==LINEAR*/)
    	{
			/*for (i=0; i < mapping->order_input; i++)
	  		v = mapping->history_input[(p+i)%5] * mapping->coef_input[i];

			for (i=0; i < mapping->order_output; i++)
	  		v = mapping->history_output[(p+i)%5] * mapping->coef_output[i];*/
			
			/*v=mapping->history_input[p];*/
			mapping->history_output[p] = v;

			--p;
			if (p < 0) p = MAX_HISTORY_ORDER;
      	}

    
	else if (mapping->type==EXPRESSION || mapping->type==LINEAR)
      	{
			v=EvalTree(mapping->expr_tree, mapping->history_input, mapping->history_output, p, &err);
			mapping->history_output[p] = v;

			--p;
			if (p < 0) p = MAX_HISTORY_ORDER-1;
      	}
	
	else if (mapping->type==CALIBRATE) {
		if(mapping->rewrite) {	/* If calibration mode has just taken effect, first data sample sets source min and max */
			mapping->range[0] = from_value->f;
			mapping->range[1] = from_value->f;
			mapping->rewrite = 0;
			changed = 1;
		} else {
			if(from_value->f < mapping->range[0]) {
				mapping->range[0] = from_value->f;
				changed = 1;
			}
			if(from_value->f > mapping->range[1]) {
				mapping->range[1] = from_value->f;
				changed = 1;
			}
		}
		
		if(changed) {
			// Need to arrange to send an admin bus message stating new ranges and expression
			// The expression has to be modified to fit the range
			if (mapping->range[0]==mapping->range[1])
			{
				free(mapping->expression);
				mapping->expression=malloc(100*sizeof(char));											
				snprintf(mapping->expression,100,"y=%f",mapping->range[0]);
			}
			else if (mapping->range[0]==mapping->range[2] && mapping->range[1]==mapping->range[3])
				snprintf(mapping->expression,100,"y=x");
			else {
				free(mapping->expression);		
				mapping->expression=malloc(256*sizeof(char));							
				snprintf(mapping->expression,256,"y=(x-%g)*%g+%g",
						 mapping->range[0],(mapping->range[3]-mapping->range[2])/(mapping->range[1]-mapping->range[0]),mapping->range[2]);
			}
			
			DeleteTree(mapping->expr_tree);
			Tree *T=NewTree();
			int success_tree=get_expr_Tree(T, mapping->expression);
			
			if (!success_tree)
				return;
			
			mapping->expr_tree=T;
		}
		
		v=EvalTree(mapping->expr_tree, mapping->history_input, mapping->history_output, p, &err);
		mapping->history_output[p] = v;
		
		--p;
		if (p < 0) p = MAX_HISTORY_ORDER-1;
	}

    
    mapping->history_pos = p;
	to_value->f = v;
}
