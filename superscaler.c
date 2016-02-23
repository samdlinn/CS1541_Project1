//Joel McCoy jvm13@pitt.edu
//Shmuel Dlinn syd6@pitt.edu

/**************************************************************/
/* CS/COE 1541				 			
   just compile with gcc -o superscaler superscaler.c			
   and execute using							
   ./superscaler  /afs/cs.pitt.edu/courses/1541/short_traces/sample.tr	0  
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
struct trace_item *IF_ID[2]; //buffer array for instruction stages
struct trace_item *REG[2]; //buffer array for instruction stages
struct trace_item *ALU[3]; //buffer array for ALU/Branch instruction stages
struct trace_item *MEM[3]; //buffer array for MEM instructions


int cycle_number = 0; //initializes the cycle number
int read_next1 = 1; //if this is one read next instruction from file
int read_next2 = 1; //if this is one read next instruction from file
int branch_prediction_table[PREDICTION_TABLE_SIZE];
int predict_status = 0; //used for 1 bit predictor 0 = predict not taken, 1 = predict taken
int mem_after_branch_flag; //used for branch hazards
int trace_view_on = 0; //used for turning on prints

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
					if((*item)->Addr == 1)
						printf("SQUASHED\n");
					else
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

//method to print the contents of the superscaler pipeline
int print_buffers() 
{
	int i;
	printf("\n\t\t---Cycle Number: %d---\t\t\n", cycle_number);
	printf("IF+ID STAGE:\t");
	print_item(&IF_ID[0]);
	printf("\t\t");
	print_item(&IF_ID[1]);
	printf("REG STAGE:\t");
	print_item(&REG[0]);
	printf("\t\t");
	print_item(&REG[1]);
	printf("ALU EX STAGE:\t");
	print_item(&ALU[0]);
	printf("MEM EX STAGE:\t");
	print_item(&MEM[0]);
	printf("ALU MEM STAGE:\t");
	print_item(&ALU[1]);
	printf("MEM MEM STAGE:\t");
	print_item(&MEM[1]);
	printf("ALU WB STAGE:\t");
	print_item(&ALU[2]);
	printf("MEM WB STAGE:\t");
	print_item(&MEM[2]);
	
}


//function to put new instruction in pipe and shift instructions to next stage
int shift_ALU(struct trace_item **incoming)
{
	ALU[2] = ALU[1];
	ALU[1] = ALU[0];
	ALU[0] = *incoming;	
}

//function to put new instruction in pipe and shift instructions to next stage
int shift_MEM(struct trace_item **incoming)
{
	MEM[2] = MEM[1];
	MEM[1] = MEM[0];
	MEM[0] = *incoming;		
}

//special shift for a branch condition hazard
int shift_MEM_special(struct trace_item **incoming)
{
	MEM[2] = MEM[1];
	MEM[1] = *incoming;
	//replace a noop with a squashed instruction
	if(MEM[0]->type == 0)
		MEM[0]= (*incoming);
				
}

//returns 1 when there is a dependency between a lw and
//another instruction that writes back to register file
int is_dependent(struct trace_item **lw, struct trace_item **read)
{
	//if the instruction type has both a sReg_a and sRegb
	if((*read)->type == 1 || (*read)->type == 5 || (*read)->type == 4)
	{
			if((*lw)->dReg == (*read)->sReg_a || (*lw)->dReg == (*read)->sReg_b)
				return 1;
			else
				return 0;
	}
	//if the instruction just has a sReg_a
	else if((*read)->type == 2 || (*read)->type == 3)
	{
			if((*lw)->dReg == (*read)->sReg_a)
				return 1;
			else
				return 0;
	}
	return 0;
}


//returns 0 if alu instruction
//returns 1 if mem instruction
int get_type(struct trace_item **incoming)
{
		//case for load/store
		if ((*incoming)->type == 3 || (*incoming)->type == 4)
			return 1;
		else
			return 0;
}

//returns 1 if there is a data hazard
int check_data_hazard()
{
		//if the reg[0] contains a lw
		if(REG[0]->type == 3)
		{
			//check dependency
			if(is_dependent(&REG[0], &REG[1]))
			{
					return 1;
			}
		}
		return 0;
}

//return 1 if branch is taken
//takes previous instruction and current instruction
int branch_taken(struct trace_item **prev, struct trace_item **cur)
{
	if(((*prev)->PC - 4) == (*cur)->PC)
		return 0;
	else
		return 1;
}

//handles control hazard detection without prediction
int check_branch_no_predict()
{
		//
		if(ALU[0]->type == 5)
		{
					//case if the next instruction is in the IF_ID stage	
					if(mem_after_branch_flag)
					{
							//checks if branch is taken
							if(branch_taken(&REG[0], &ALU[0]))
							{
								return 1;
							}
							
					}
					//case if the next instruction is in the mem stage
					else
					{
							//checks if the branch is taken
							if(branch_taken(&MEM[0], &ALU[0]))
							{
								return 2;
							}
					}
		}
		return 0;
}


//uses branch prediction to check data hazard
int check_branch_predict()
{
		int index;//used to index table
		int check;
		
		//if instruction in ALU(0) stage is branch check prediction table
		if(ALU[0]->type == 5)
		{
			//bit masking of 10-4 bits of PC
			index = (ALU[0]->PC) >> 4;
			index = index & 0x0000007F; //gets bottom 7 bits
			
			//lookup index
			check = branch_prediction_table[index];
			//condition that branch prediction table index has predicted branch before
			if(check == 1)
			{
				//now check if this instruction will branch
				if((check_branch_no_predict() == 1) || (check_branch_no_predict() == 2)) //condition for it will branch
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
				if(check_branch_no_predict() == 0)//condition for no branch
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
				if(check_branch_no_predict() == 0)//condition for no branch
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

//method to inititialize branch prediction table with -1
int init_table()
{
	int i;
	for(i = 0; i < PREDICTION_TABLE_SIZE; i++)
		branch_prediction_table[i] = -1;
}

//returns 1 if there is a jump hazard being a jump or jrtype instruction
int jump_hazard()
{
	if(ALU[0]->type == 6 || ALU[0]->type == 8)
		return 1;
		
	return 0;
}


int main(int argc, char **argv)
{
	struct trace_item *tr_entry1, *tr_entry2;
	size_t size;
	char *trace_file_name;
	int branch_method = 0;
	int i; //use for iterations
	
	
	IF_ID[2] = (struct trace_item*)malloc(sizeof(struct trace_item*) * 2);
	REG[2] = (struct trace_item*)malloc(sizeof(struct trace_item*) * 2);
	ALU[3] = (struct trace_item*)malloc(sizeof(struct trace_item*) * 3);
	MEM[3] = (struct trace_item*)malloc(sizeof(struct trace_item*) * 3);
  
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
	for (i = 0; i < 2; i++)
	{
		IF_ID[i] = noOp;
		REG[i] = noOp;
	}
	for (i = 0; i < 3; i++)
	{
		ALU[i] = noOp;
		MEM[i] = noOp;
	}
	
	struct trace_item* squashed = (struct trace_item*)malloc(sizeof(struct trace_item*));
	squashed->Addr = 1;
	
	//initialize branch prediction table if branch predict mode is on
	if (branch_method == 1)
		init_table();
		
	//start of simulation
  
	read_next1 = 1; //if this is one read next instruction from file
	read_next2 = 1;
	while(1) 
	{
		//flag used for determining how to handle branch and jump hazards
		mem_after_branch_flag = 0;
		//will only read new instruction into tr_entry from the file
		//only if there was not a stall from previous cycle
		if(read_next1)
		{
			size = trace_get_item(&tr_entry1);
		}
		
		if (!size) 
		{       
			printf("\n+ Simulation terminates at cycle : %u\n", cycle_number);
			break;
		}
		
		if(read_next2)
		{
			size = trace_get_item(&tr_entry2);
		}
		
		// no more instructions (trace_items) to simulate
		if (!size) 
		{       
			printf("\n+ Simulation terminates at cycle : %u\n", cycle_number);
			break;
		}
		
		
		
		// parse the next instruction to simulate and check hazards 
		else
		{
			//insert hazard conditions here
			
			//check for jump hazard
			if(jump_hazard())
			{
				//condition for no instruction in mem stage yet
				if(!mem_after_branch_flag) 
				{
					if(trace_view_on)
						printf("\n\t\t---JUMP HAZARD---\t\t\n");
					shift_ALU(&squashed);
					shift_MEM(&squashed);
					cycle_number++;
					if(trace_view_on)
						print_buffers();
					shift_ALU(&squashed);
					shift_MEM(&squashed);
				}
				//condition for instruction in mem stage already
				//must squash mem stage with special method
				else
				{
					if(trace_view_on)
					printf("\n\t\t---JUMP HAZARD---\t\t\n");
					shift_ALU(&squashed);
					shift_MEM_special(&squashed);
					cycle_number++;
					if(trace_view_on)
						print_buffers();
					shift_ALU(&squashed);
					shift_MEM_special(&squashed);
				}
				
			}
			//case for a branch condition and no current instruction in mem stage
			//squash two instructions
			//does not use prediction
			else if(check_branch_no_predict() == 1 && !branch_method)
			{
				if(trace_view_on)
						printf("\n\t\t---CONTROL HAZARD---\t\t\n");
				shift_ALU(&squashed);
				shift_MEM(&squashed);
				cycle_number++;
				if(trace_view_on)
					print_buffers();
				shift_ALU(&squashed);
				shift_MEM(&squashed);
			}
			//condition control branch hazard and special squash method
			//for the hazard
			//does not use prediction
			else if(check_branch_no_predict() == 2 && !branch_method)
			{
				if(trace_view_on)
					printf("\n\t\t---CONTROL HAZARD---\t\t\n");
				shift_ALU(&squashed);
				shift_MEM_special(&squashed);
				cycle_number++;
				if(trace_view_on)
					print_buffers();
				shift_ALU(&squashed);
				shift_MEM_special(&squashed);
			}
			//handles branch conditions with prediction
			else if(check_branch_predict() && branch_method)
			{
					//condition for hazard and no instruction in mem stage
					if(check_branch_no_predict() == 1)
					{
						if(trace_view_on)
							printf("\n\t\t---CONTROL HAZARD---\t\t\n");
						shift_ALU(&squashed);
						shift_MEM(&squashed);
						cycle_number++;
						if(trace_view_on)
							print_buffers();
						shift_ALU(&squashed);
						shift_MEM(&squashed);
					}
					//condition for hazard and instruction in mem stage
					//must use shift special
					else if(check_branch_no_predict() == 2)
					{
						if(trace_view_on)
							printf("\n\t\t---CONTROL HAZARD---\t\t\n");
						shift_ALU(&squashed);
						shift_MEM(&squashed);
						cycle_number++;
						if(trace_view_on)
							print_buffers();
						shift_ALU(&squashed);
						shift_MEM_special(&squashed);
					}
			}
			//checks for data hazards with lw
			else if(check_data_hazard())
			{
					//case for first pipe has a lw
					shift_MEM(&REG[0]);
					shift_ALU(&noOp);
					read_next1 = 0;
					read_next2 = 1;
					
					REG[0] = REG[1];
					REG[1] = IF_ID[0];	
					IF_ID[0] = IF_ID[1];
					IF_ID[1] = tr_entry2;

					
					if(trace_view_on)
						printf("\n\t\t---DATA HAZARD---\t\t\n");
			}
			//data hazard for when ahead
			else if((MEM[0]->type == 3) && is_dependent(&MEM[0], &REG[0]))
			{
				shift_ALU(&noOp);
				shift_MEM(&noOp);
				read_next1 = 0;
				read_next2 = 0;
				if (trace_view_on)
					printf("\n\t\t---DATA HAZARD---\t\t\n");
			}
			
			
			else //no hazard condition 
			{
				//case when both can be pushed into pipe
				if(get_type(&REG[0]) != get_type(&REG[1]))
				{
						if(get_type(&REG[0]))
						{
							shift_MEM(&REG[0]);
							shift_ALU(&REG[1]);
						}
						else
						{
							shift_MEM(&REG[1]);
							shift_ALU(&REG[0]);
							mem_after_branch_flag = 1;
						}
						read_next1 = 1;
						read_next2 = 1;
						
						REG[0] = IF_ID[0];
						REG[1] = IF_ID[1];
						
						IF_ID[0] = tr_entry1;
						IF_ID[1] = tr_entry2;
				}
				else if(REG[0]->type == REG[1]->type && REG[1]->type == 0)
				{
						shift_MEM(&REG[0]);
						shift_ALU(&REG[1]);

						read_next1 = 1;
						read_next2 = 1;
						
						REG[0] = IF_ID[0];
						REG[1] = IF_ID[1];
						
						IF_ID[0] = tr_entry1;
						IF_ID[1] = tr_entry2;
				}
				//condition for same instruction types in ID stage
				else
				{
						if(get_type(&REG[0]))
						{
								shift_MEM(&REG[0]);
								shift_ALU(&noOp);
								read_next1 = 0;
								read_next2 = 1;	
						}
						else
						{
								shift_ALU(&REG[0]);
								shift_MEM(&noOp);
								
								read_next1 = 0;
								read_next2 = 1;
						}
						
						REG[0] = REG[1];
						REG[1] = IF_ID[0];
						
						IF_ID[0] = IF_ID[1];
						IF_ID[1] = tr_entry2;
						
				}
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




