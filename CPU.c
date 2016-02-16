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

static FILE *trace_fd;
static int trace_buf_ptr;
static int trace_buf_end;
static struct trace_item *trace_buf;
struct trace_item *buffer[5]; //buffer array for instruction stages

int cycle_number = 0; //initializes the cycle number

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
int print_item (struct trace_item **item, int cycle_number)
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
          printf("SPECIAL:");      	
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
	print_item(&buffer[0], cycle_number);
	printf("ID STAGE:\t");
	print_item(&buffer[1], cycle_number);
	printf("EX STAGE:\t");
	print_item(&buffer[2], cycle_number);
	printf("MEM STAGE:\t");
	print_item(&buffer[3], cycle_number);
	printf("WB STAGE:\t");
	print_item(&buffer[4], cycle_number);
	
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

int main(int argc, char **argv)
{
	struct trace_item *tr_entry;
	size_t size;
	char *trace_file_name;
	int trace_view_on = 0;
	int i; //use for iterations
	buffer[5] = (struct trace_item*)malloc(sizeof(struct trace_item*) * 5);
  
	unsigned char t_type = 0;
	unsigned char t_sReg_a= 0;
	unsigned char t_sReg_b= 0;
	unsigned char t_dReg= 0;
	unsigned int t_PC = 0;
	unsigned int t_Addr = 0;

	
  
	if (argc == 1) 
	{
		fprintf(stdout, "\nUSAGE: tv <trace_file> <switch - any character>\n");
		fprintf(stdout, "\n(switch) to turn on or off individual item view.\n\n");
		exit(0);
	}
    
	trace_file_name = argv[1];
	if (argc == 3) trace_view_on = atoi(argv[2]) ;

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
  
  
	//start of simulation
  

	while(1) 
	{
		
		size = trace_get_item(&tr_entry);
		// no more instructions (trace_items) to simulate
		if (!size) 
		{       
			printf("+ Simulation terminates at cycle : %u\n", cycle_number);
			break;
		}
		// parse the next instruction to simulate 
		else
		{              
		  t_type = tr_entry->type;
		  t_sReg_a = tr_entry->sReg_a;
		  t_sReg_b = tr_entry->sReg_b;
		  t_dReg = tr_entry->dReg;
		  t_PC = tr_entry->PC;
		  t_Addr = tr_entry->Addr;
		}  
		cycle_number++; //increments cycle number
		shift_pipe(&tr_entry);

		// print the executed instruction if trace_view_on=1 	
		if (trace_view_on) 
			print_buffers();
	}
  
  trace_uninit();

  exit(0);
}




