
#include "mapper_internal.h"
#include "types_internal.h"
#include <mapper/mapper.h>
#include "operations.h"
#include "expression.h"

void mapper_mapping_perform(mapper_mapping mapping,
                            mapper_signal_value_t *from_value,
                            mapper_signal_value_t *to_value)
{
    int i, p;
    float v;
    error err=NO_ERR;


    p=mapping->history_pos;
    mapping->history_input[p] = from_value->f;

    if( mapping->type==BYPASS /*|| mapping->type==LINEAR*/)
    	{
			/*for (i=0; i < mapping->order_input; i++)
	  		v = mapping->history_input[(p+i)%5] * mapping->coef_input[i];

			for (i=0; i < mapping->order_output; i++)
	  		v = mapping->history_output[(p+i)%5] * mapping->coef_output[i];*/
			
			v=mapping->history_input[p];
			mapping->history_output[p] = v;

			--p;
			if (p < 0) p = MAX_HISTORY_ORDER-1;/*-1 ??*/
      	}

    
	else if (mapping->type==EXPRESSION || mapping->type==LINEAR)
      	{
			v=EvalTree(mapping->expr_tree, mapping->history_input, mapping->history_output, p, &err);
			mapping->history_output[p] = v;

			--p;
			if (p < 0) p = MAX_HISTORY_ORDER-1;
      	}

    
    mapping->history_pos = p;
	to_value->f = v;
}
