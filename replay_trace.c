#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>

#define TRANSMITTER (33)

struct trace_entry {
	double seconds;
	int bit;
	struct trace_entry *next;
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
		printf("initializing head to %p\n", new);
       	} else {
		(*tail)->next = new;
	}
	*tail = new;
	printf("head = %p\n", *head);
 	return 0;		
}

void cleanup_entries(struct trace_entry **head)
{
	struct trace_entry *tofree;
	while(*head) {
		tofree = *head;
		*head = (*head)->next;
		free(tofree);
		if(*head == NULL) {
			break;
		}
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

	struct trace_entry *head = NULL, *tail = NULL;
	int items_found = 0;

	while(items_found != EOF) {
		struct trace_entry bucket;
		items_found = fscanf(trace, "%lf %d", &bucket.seconds, &bucket.bit);
		printf("scanned %d items from %s\n", items_found, argv[1]);
		if(items_found != 2) {
			break;
			die("malformatted trace file");
		}
		add_entry(&head, &tail, bucket);

	}
	struct trace_entry *iter = head;
	while(iter) {
		printf("%.6f\t%d\n", iter->seconds, iter->bit);
		iter = iter->next;
	}

	cleanup_entries(&head);
	tail = NULL;

	fclose(trace);

}
