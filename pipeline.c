/**************************************************************/
/* CS/COE 1541				 			
   just compile with gcc -o pipeline pipeline.c			
   and execute using							
   ./pipeline  /afs/cs.pitt.edu/courses/1541/short_traces/sample.tr	0  
***************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <arpa/inet.h>
#include "trace_item.h" 

#define TRACE_BUFSIZE 1024*1024
#define PREDICTION_TABLE_SIZE 128

static FILE *trace_fd;
static int trace_buf_ptr;
static int trace_buf_end;
static struct trace_item *trace_buf;
struct trace_item *buffer[5]; //buffer array for instruction stages

int cycle_number = 0; //initializes the cycle number
int read_next = 1; //if this is one read next instruction from file
int branch_prediction_table[PREDICTION_TABLE_SIZE];
int predict_status = 0; //used for 1 bit predictor 0 = predict not taken, 1 = predict taken

int is_big_endian(void)
{
    union {
        uint32_t i;
        char c[4];
    } bint = {0x01020304};

    return bint.c[0] == 1; 
}

uint32_t my_ntohl(uint32_t x)
{
  u_char *s = (u_char *)&x;
  return (uint32_t)(s[3] << 24 | s[2] << 16 | s[1] << 8 | s[0]);
}

void trace_init()
{
  trace_buf = malloc(sizeof(struct trace_item) * TRACE_BUFSIZE);

  if (!trace_buf) {
    fprintf(stdout, "** trace_buf not allocated\n");
    exit(-1);
  }

  trace_buf_ptr = 0;
  trace_buf_end = 0;
}

void trace_uninit()
{
  free(trace_buf);
  fclose(trace_fd);
}

int trace_get_item(struct trace_item **item)
{
  int n_items;

  if (trace_buf_ptr == trace_buf_end) {	/* if no more unprocessed items in the trace buffer, get new data  */
    n_items = fread(trace_buf, sizeof(struct trace_item), TRACE_BUFSIZE, trace_fd);
    if (!n_items) return 0;				/* if no more items in the file, we are done */

    trace_buf_ptr = 0;
    trace_buf_end = n_items;			/* n_items were read and placed in trace buffer */
  }

  *item = &trace_buf[trace_buf_ptr];	/* read a new trace item for processing */
  trace_buf_ptr++;
  
  if (is_big_endian()) {
    (*item)->PC = my_ntohl((*item)->PC);
    (*item)->Addr = my_ntohl((*item)->Addr);
  }

  return 1;
}

//function to print contents of instruction, takes item and cycle number
int print_item (struct trace_item **item)
{
	switch((*item)->type) {
        case ti_NOP:
          printf("NOP\n");
          break;
        case ti_RTYPE:
          printf("RTYPE:");
          printf(" (PC: %x)(sReg_a: %d)(sReg_b: %d)(dReg: %d) \n", (*item)->PC, (*item)->sReg_a, (*item)->sReg_b, (*item)->dReg);
          break;
        case ti_ITYPE:
          printf("ITYPE:");
          printf(" (PC: %x)(sReg_a: %d)(dReg: %d)(addr: %x)\n", (*item)->PC, (*item)->sReg_a, (*item)->dReg, (*item)->Addr);
          break;
        case ti_LOAD:
          printf("LOAD:");      
          printf(" (PC: %x)(sReg_a: %d)(dReg: %d)(addr: %x)\n", (*item)->PC, (*item)->sReg_a, (*item)->dReg, (*item)->Addr);
          break;
        case ti_STORE:
          printf("STORE:");      
          printf(" (PC: %x)(sReg_a: %d)(sReg_b: %d)(addr: %x)\n", (*item)->PC, (*item)->sReg_a, (*item)->sReg_b, (*item)->Addr);
          break;
        case ti_BRANCH:
          printf("BRANCH:");
          printf(" (PC: %x)(sReg_a: %d)(sReg_b: %d)(addr: %x)\n", (*item)->PC, (*item)->sReg_a, (*item)->sReg_b, (*item)->Addr);
          break;
        case ti_JTYPE:
          printf("JTYPE:");
          printf(" (PC: %x)(addr: %x)\n", (*item)->PC, (*item)->Addr);
          break;
        case ti_SPECIAL:
          printf("SPECIAL:\n");      	
          break;
        case ti_JRTYPE:
          printf("JRTYPE:");
          printf(" (PC: %x) (sReg_a: %d)(addr: %x)\n", (*item)->PC, (*item)->dReg, (*item)->Addr);
          break;
    }
    return 1;
}

int print_buffers() 
{
	int i;
	printf("\n\t\t---Cycle Number: %d---\t\t\n", cycle_number);
	printf("IF STAGE:\t");
	print_item(&buffer[0]);
	printf("ID STAGE:\t");
	print_item(&buffer[1]);
	printf("EX STAGE:\t");
	print_item(&buffer[2]);
	printf("MEM STAGE:\t");
	print_item(&buffer[3]);
	printf("WB STAGE:\t");
	print_item(&buffer[4]);
	
}

//function to put new instruction in pipe and shift instructions to next stage
int shift_pipe(struct trace_item **incoming)
{
	buffer[4] = buffer[3];
	buffer[3] = buffer[2];
	buffer[2] = buffer[1];
	buffer[1] = buffer[0];
	buffer[0] = *incoming;	
}

//function to check for data hazard and insert stall if necessary
//returns 1 if there is a stall, 0 if no stall
int data_hazard(struct trace_item **incoming)
{
	
	//condition for lw stall with r-type instruction following it with dependency
	if(buffer[0]->type == 3 && (*incoming)->type == 1)
			if(buffer[0]->dReg == (*incoming)->sReg_a)
				return 1;
			else if(buffer[0]->dReg == (*incoming)->sReg_b)
				return 1;
	
	//condition for lw stall with i-type instruction following it with dependency
	if(buffer[0]->type == 3 && (*incoming)->type == 2)
			if(buffer[0]->dReg == (*incoming)->sReg_a)
				return 1;
				
	//condition for lw stall with branch instruction following it with dependency
	if(buffer[0]->type == 3 && (*incoming)->type == 5)
			if(buffer[0]->dReg == (*incoming)->sReg_a)
				return 1;
			else if(buffer[0]->dReg == (*incoming)->sReg_b)
				return 1;
				
	//condition for lw stall with sw instruction following it with dependency
	if(buffer[0]->type == 3 && (*incoming)->type == 4)
			if(buffer[0]->dReg == (*incoming)->sReg_a)
				return 1;
				
	return 0;
}

//returns 1 if there is a control hazard with no prediction method
int control_hazard_no_predict(struct trace_item **incoming)
{
	//there is a control hazard if next instruction incoming is resulted
	//from branch and the branch is taken (due to predicting not taken)
	if(buffer[0]->type == 5 && (*incoming)->PC != (buffer[0]->PC + 4))
		return 1;
	return 0;
}

//initializes the branch hash table to -1
int init_table()
{
	int i;
	for(i = 0; i < PREDICTION_TABLE_SIZE; i++)
		branch_prediction_table[i] = -1;
}

//returns 1 if there is a need to kill 2 instructions
//due to branch resolution in the ex stage
int control_hazard_predict(struct trace_item **incoming)
{
	int index;
	int check;
	//if instruction in IF stage is branch check prediction table
	if(buffer[0]->type == 5)
	{
		//bit masking of 10-4 bits of PC
		index = (buffer[0]->PC) >> 4;
		index = index & 0x0000007F; //gets bottom 7 bits

		
		//lookup index
		check = branch_prediction_table[index];
		//condition that branch prediction table index has predicted branch before
		if(check == 1)
		{
			//now check if this instruction will branch
			if((buffer[0]->PC + 4) != (*incoming)->PC) //condition for it will branch
			{
				return 0; //no need to stall/squash--predicted correctly
			}
			else //condition for branch predictor failed and branch not taken
			{
				branch_prediction_table[index] = 0; //update table
				return 1;
			}
		}
		
		//condition that branch prediction table index has predicted no branch before
		else if (check == 0)
		{
			//now check if the instruction will not branch
			if((buffer[0]->PC + 4) == (*incoming)->PC)//condition for no branch
			{
				return 0; // no need to stall/squash--predicted correctly
			}
			else
			{
				branch_prediction_table[index] = 1; //update table
				return 1; //branch taken when not predicted not taken
			}
		}
		
		//condition for table index not initialized --predict not taken
		else
		{
			if((buffer[0]->PC + 4) == (*incoming)->PC)//condition for no branch
			{
				branch_prediction_table[index] = 0; //update table
				return 0; //no need to stall because branch predicted not taken
			}
			else //condition for branch taken
			{
				branch_prediction_table[index] = 1; //update table
				return 1; //must stall because branch was predicted not taken
			}
		}
	}
	return 0;
}

//returns 1 if there is a jump hazard
int jump_hazard(struct trace_item **incoming)
{
	//there is a jump instruction, must stall 1 cycle to calculate
	//jump address
	if((buffer[0]->type == 6) || (buffer[0]->type == 8))
		return 1;
	return 0;
}

int main(int argc, char **argv)
{
	struct trace_item *tr_entry;
	size_t size;
	char *trace_file_name;
	int trace_view_on = 0;
	int branch_method = 0;
	int i; //use for iterations
	
	
	buffer[5] = (struct trace_item*)malloc(sizeof(struct trace_item*) * 5);
	
  
	if (argc == 1) 
	{
		fprintf(stdout, "\nUSAGE: tv <trace_file> <switch - any character>\n");
		fprintf(stdout, "\n(switch) to turn on or off individual item view.\n\n");
		exit(0);
	}
    
	trace_file_name = argv[1];
	
	//condition for only 3 command line args, branch method set to 0
	if (argc == 3) trace_view_on = atoi(argv[2]);
	
	//takes args [filename] [branch_method] [trace_view_on]
	if (argc == 4)
	{
		branch_method = atoi(argv[2]);
		trace_view_on = atoi(argv[3]);
	}
	fprintf(stdout, "\n ** opening file %s\n", trace_file_name);

	trace_fd = fopen(trace_file_name, "rb");

	if (!trace_fd) 
	{
		fprintf(stdout, "\ntrace file %s not opened.\n\n", trace_file_name);
		exit(0);
	}

	trace_init();
  
	//This block initializes the buffer pipeline to all noops
	struct trace_item* noOp = (struct trace_item*)malloc(sizeof(struct trace_item*));
	for (i = 0; i < 5; i++)
	{
		buffer[i] = noOp;
	}
	
	//initialize branch prediction table if branch predict mode is on
	if (branch_method == 1)
		init_table();
		
	//start of simulation
  
	read_next = 1; //if this is one read next instruction from file
	while(1) 
	{
		//will only read new instruction into tr_entry from the file
		//only if there was not a stall from previous cycle
		if(read_next)
			size = trace_get_item(&tr_entry);
			
		// no more instructions (trace_items) to simulate
		if (!size) 
		{       
			printf("\n+ Simulation terminates at cycle : %u\n", cycle_number);
			break;
		}
		// parse the next instruction to simulate and check hazards 
		else
		{
			//check for data_hazard
			if (data_hazard(&tr_entry))
			{
				if (trace_view_on)
					printf("\n\t\t---DATA HAZARD---\t\t\n");
				shift_pipe(&noOp); //shift pipe with NOOP (STALL)
				read_next = 0; //indicates the next instruction will not be read
				//will keep previous tr_entry for next loop to load into pipe
			}
			//condition for control hazard with prediction method turned off
			else if (control_hazard_no_predict(&tr_entry) && branch_method == 0)
			{
				if(trace_view_on)
					printf("\n\t\t---CONTROL HAZARD---\t\t\n");
				shift_pipe(&noOp); //inserts squashed
				cycle_number++; //counts for cycle inbetween
				if(trace_view_on)
					print_buffers();
				
				shift_pipe(&noOp);//inserts second squashed
				read_next = 0;
			}
			else if(control_hazard_predict(&tr_entry) && branch_method == 1)
			{
				if(trace_view_on)
					printf("\n\t\t---CONTROL HAZARD---\t\t\n");
				shift_pipe(&noOp); //inserts squashed
				cycle_number++; //counts for cycle inbetween
				if(trace_view_on)
					print_buffers();
				
				shift_pipe(&noOp);//inserts second squashed
				read_next = 0;
			}
			else if(jump_hazard(&tr_entry))
			{
				if (trace_view_on)
					printf("\n\t\t---JUMP HAZARD---\t\t\n");
				shift_pipe(&noOp); //shift pipe with NOOP (STALL)
				cycle_number++; //increment cycle number
				if (trace_view_on)
					print_buffers();
				shift_pipe(&noOp);//shifts pipe with second noop
				read_next = 0; //indicates the next instruction will not be read
				//will keep previous tr_entry for next loop to load into pipe
			}
			else //no hazard condition
			{
				read_next = 1; //enables the next instruction to be read from file
				shift_pipe(&tr_entry); //shift pipe with new instruction added
			}
				
		}  
		cycle_number++; //increments cycle number
		

		// print the executed instruction if trace_view_on=1 	
		if (trace_view_on) 
			print_buffers();
	}
	
	trace_uninit();

	exit(0);
}




