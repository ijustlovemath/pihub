#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include <inttypes.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "dma.h"

#define TRANSMITTER (13) // GPIO13/Pin33 on the board, but linux actually abstracts that for us!

struct trace_entry {
	double seconds;
	int bit;
	struct trace_entry *next;
};

struct gpio_pin {
	int pin_number;
};

void die(const char *message)
{
	if(errno) {
		perror(message);
	} else {
		puts(message);
	}
	exit(errno);
}

int add_entry(struct trace_entry **head, struct trace_entry **tail, const struct trace_entry contents)
{
	struct trace_entry *new = malloc(sizeof(*new));
	if(new == NULL) {
		die("malloc failed in add_entry");
	}
	*new = contents;
	new->next = NULL;

	if(*head == NULL) {
	       	*head = new;
       	} else {
		(*tail)->next = new;
	}
	*tail = new;
 	return 0;		
}

void cleanup_entries(struct trace_entry **head)
{
	while(*head) {
		struct trace_entry *tofree = *head;
		*head = (*head)->next;
		free(tofree);
	}
}

void print_entries(const struct trace_entry *head)
{
	const struct trace_entry *iter = head;
	while(iter) {
		printf("%.6f\t%d\n", iter->seconds, iter->bit);
		iter = iter->next;
	}
}

int init_gpio_sysfs(const struct gpio_pin *pin)
{
	const char * export_path = "/sys/class/gpio/export";
	char pin_path[100];

	/* sprintf is bad but we have very clear known bounds here */
	sprintf(pin_path, "/sys/class/gpio/gpio%d", pin->pin_number);

	/* Let the kernel know which pin we want to access */
	FILE * export = fopen(export_path, "w");
	if(export == NULL) {
		die("your system does not support gpio in the expected way");
	}

	fprintf(export, "%d", pin->pin_number);
	fclose(export);

	/* wait for kernel to register the files */
	usleep(100000);

	/* Make sure it actually found said pin */
	int status = access(pin_path, F_OK);
	if(status < 0) {
		die("kernel unable to create gpio sysfs path");
	}

	/* set the direction of the pin to output */
	char direction_path[100];
	status = sprintf(direction_path, "%s/direction", pin_path);
	FILE * direction = fopen(direction_path, "w");
	if(direction == NULL) {
		die("gpio pin direction unavailable");
	}
	fprintf(direction, "out");
	fclose(direction);

	(void) status;
	
	return 0;
}

int write_gpio_sysfs(const struct gpio_pin *pin, const int bit)
{
	/* it would be nice to abstract this all out, 
	 * though doing it statically works well enough
	 */
	static char value_path[100];
	/* Initialize if it hasnt been, do this only once */
	if(value_path[0] == 0) {
		sprintf(value_path, "/sys/class/gpio/gpio%d/value", pin->pin_number);
	}

	FILE *value = fopen(value_path, "w");
	if(value == NULL) {
		die("unable to access value sysfs file");
	}
	int status = -1;
	if(bit) {
		status = fprintf(value, "1");
	} else {
		status = fprintf(value, "0");
	}
	fclose(value);
	return status != 1; /* 1 byte written means it worked */
}

// Return a pointer to a periphery subsystem register.
static void *mmap_bcm_register(off_t register_offset) {
  const off_t base = PERI_BASE;

  int mem_fd;
  if ((mem_fd = open("/dev/gpiomem", O_RDWR|O_SYNC) ) < 0) {
    perror("can't open /dev/gpiomem");
    fprintf(stderr, "You need to run this as root!\n");
    return NULL;
  }

  uint32_t *result =
    (uint32_t*) mmap(NULL,                  // Any adddress in our space will do
                     PAGE_SIZE,
                     PROT_READ|PROT_WRITE,  // Enable r/w on GPIO registers.
                     MAP_SHARED,
                     mem_fd,                // File to map
                     base + register_offset // Offset to bcm register
                     );
  close(mem_fd);

  if (result == MAP_FAILED) {
    fprintf(stderr, "mmap error %p\n", (void*)result);
    return NULL;
  }
  return result;
}

int write_gpio_dma(const struct gpio_pin *pin, const int bit)
{
	static volatile uint32_t *gpio_port = NULL;
	if(gpio_port == NULL) {
		gpio_port = mmap_bcm_register(GPIO_REGISTER_BASE);	
	}

	initialize_gpio_for_output(gpio_port, pin->pin_number);
	static volatile uint32_t *set_reg = NULL;
	if(set_reg == NULL) {
		set_reg = gpio_port + (GPIO_SET_OFFSET / sizeof(uint32_t));
	}

	static volatile uint32_t *clr_reg = NULL;
	if(clr_reg == NULL) {
		clr_reg = gpio_port + (GPIO_CLR_OFFSET / sizeof(uint32_t));
	}

	if(bit) {
		*set_reg = 1 << pin->pin_number;
	} else {
		*clr_reg = 1 << pin->pin_number;
	}
	return 0;
}

void replay(const struct trace_entry *trace, const struct gpio_pin *pin)
{
	while(trace) {
		double seconds = trace->seconds;
		if(seconds < 0.0) {
			puts("malformatted sleep, bailing replay");
			return;
		}
		/* We're dealing with ms scale sleeps,
		 * clamp anything outside that
		 * (aka the initial delay when recording)
		 */
		if(seconds > 1.0) {
			seconds = 0.0;
		}
		struct timespec wait = {
			.tv_sec = 0,
			.tv_nsec = (long) 1000000000 * seconds
		};
		if(nanosleep(&wait, NULL)) {
			puts("issue with nanosleep, interrupted");
			return; 
		}
		if(write_gpio_dma(pin, trace->bit)) {
			puts("problem writing gpio bit, bailing replay");
			return;
		}

		trace = trace->next;
	}
}

int main(int argc, char * argv[])
{
	if(argc < 2) {
		die("need a trace file path");
	}

	FILE *trace = fopen(argv[1], "r");
	if(!trace) {
		die("need to supply an existing trace file");
	}

	/* Fill up a linked list with the contents of the trace file */
	struct trace_entry *head = NULL, *tail = NULL;
	int items_found = 0;

	while(items_found != EOF) {
		struct trace_entry bucket;
		items_found = fscanf(trace, "%lf %d", &bucket.seconds, &bucket.bit);
		if(items_found != 2) {
			if(items_found > 0) {
				die("malformatted trace file");
			}
			break;
		}
		add_entry(&head, &tail, bucket);

	}
	/* Close the file we used to fill the linked list */
	fclose(trace);

	/* Prepare the GPIO pin we want to use */
	const struct gpio_pin transmitter = { .pin_number = TRANSMITTER };

	/* Replay the trace file read in, waiting as needed */
	replay(head, &transmitter);

	/* Cleanup the linked list of entries */
	cleanup_entries(&head);
	tail = NULL; // If we don't do this there's invlaid memory held in tail
	

}
