#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>

#define TRANSMITTER (13) // Pin (33) on the board, but linux actually abstracts that for us!

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
		if(*head == NULL) {
			break;
		}
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

int init_gpio(const struct gpio_pin *pin)
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

int write_gpio_bit(const struct gpio_pin *pin, const int bit)
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

void replay(const struct trace_entry *head, const struct gpio_pin *pin)
{
	while(head) {
		double seconds = head->seconds;
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
		if(write_gpio_bit(pin, head->bit)) {
			puts("problem writing gpio bit, bailing replay");
			return;
		}

		head = head->next;
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
	init_gpio(&transmitter);

	/* Replay the trace file read in, waiting as needed */
	replay(head, &transmitter);

	/* Cleanup the linked list of entries */
	cleanup_entries(&head);
	tail = NULL; // If we don't do this there's invlaid memory held in tail
	

}
