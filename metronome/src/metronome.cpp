#include <iostream>
#include <string>
#include <errno.h>
#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <signal.h>
#include <sys/siginfo.h>
#include <sys/neutrino.h>
#include <sys/netmgr.h>
#include <math.h>

/*
 * Define THREAD_POOL_PARAM_T such that we can avoid a compiler
 * warning when we use the dispatch_*() functions below
 */
#define THREAD_POOL_PARAM_T    dispatch_context_t

#include <sys/iofunc.h>
#include <sys/dispatch.h>

static resmgr_connect_funcs_t connect_funcs; /* POSIX calls that use device name  e.g. open, unlink */
static resmgr_io_funcs_t io_funcs;           /* POSIX calls that use file descriptors e.g. read, write */
static iofunc_attr_t ioattr;                 /* Describes the attributes of the device that's associated with a resource manager. Handles device identifiers */

#define PATHNAME                "/dev/local/metronome"
#define ATTACH_POINT            "metronome"
#define METRONOME_PULSE_CODE    9
#define PAUSE_PULSE_CODE        7
#define QUIT_PULSE_CODE         8
#define EXPECTED_NUM_ATTR       4
#define NSECS_PER_SEC           1000000000
#define UPPER_INPUT_LIMIT       9
#define LOWER_INPUT_LIMIT       1
#define NUM_PATTERNS            8
typedef union
{
	struct _pulse pulse;
	struct METRONOME
	{
		int pulse_code;
		int beats_per_minute;
		int time_signature_top;
		int time_signature_bottom;

	} METRONOME;

} my_message_t;

struct DataTableRow
{
	int         time_signature_top;
	int         time_signature_bottom;
	int         num_intervals;
	std::string pattern;

}typedef DataTableRow;

/**
 * Time-signature-top | Time-signature bottom | Number of Intervals within each beat | Pattern for Intervals within Each Beat
 */
DataTableRow t[] = {
		{ 2, 4, 4,   "|1&2&" },
		{ 3, 4, 6,   "|1&2&3&" },
		{ 4, 4, 8,   "|1&2&3&4&" },
		{ 5, 4, 10,  "|1&2&3&4-5-" },
		{ 3, 8, 6,   "|1-2-3-" },
		{ 6, 8, 6,   "|1&a2&a" },
		{ 9, 8, 9,   "|1&a2&a3&a" },
		{ 12, 8, 12, "|1&a2&a3&a4&a" }
};

    /* Resource Manager */
dispatch_t         *dpp;           /* Dispatch structure used to schedule the calling of user functions */
resmgr_attr_t      resmgr_attr;
dispatch_context_t *ctp;

    /* Messaging */
my_message_t metronome_msg;
my_message_t *recv_msg;
char         data[255];

    /* Time Driver */
struct itimerspec  itime;
timer_t            timer_id;
struct sigevent    event;
struct sched_param scheduling_params;

    /* Flags */
bool output_complete = false;
bool case_1          = true;

    /* Application variables */
int          row;
int          metronome_coid;
double       output_internal;
double       pattern_interval;
int          current_pattern;
int          id;
double       spacing_timer;
std::string  str = "0";
unsigned int char_index = 0;
int          current_output;

/**
 * This function displays the usage information to the user 
 */
void display_usage()
{
	printf("Usage:\n ./metronome <beats-per-minute> <time-signature-top> <time-signature-bottom>\n");
}

/**
 * This function "drives" the metronome. It receives pulse from interval timer;
 * each time the timer expires it receives pulses from io_write (quit and pause <int>)
 */
void* metronome_thread(void* argv)
{
	        /* Phase I - create a named channel to receive pulses */
	
	    /* Create a local name to register the device and create a channel */
	name_attach_t *attach;
	if ((attach = name_attach(NULL, ATTACH_POINT, 0)) == NULL) 
	{
		printf("Failed to attach to device\n");
		return NULL;
	}

	pattern_interval = (60.0 / metronome_msg.METRONOME.beats_per_minute) * metronome_msg.METRONOME.time_signature_top; /* Calculate the time for starting a new line */
	output_internal  = 0.0;

	for (int i = 0; i < NUM_PATTERNS; i++)
	{
		if (t[i].time_signature_top == metronome_msg.METRONOME.time_signature_top && 
		    t[i].time_signature_bottom == metronome_msg.METRONOME.time_signature_bottom)
		{
			output_internal = t[i].num_intervals;
			row = 0;
			std::string test(t[row].pattern);
			str = test;
		}
	}

	/* Create interval timer to drive metronome */
	spacing_timer = pattern_interval / output_internal; /* Actual output interval */
	spacing_timer *= 1000000000;                        /* Covert to nano seconds */

	    /* Get our priority. */
	int prio;
	if (SchedGet(0, 0, &scheduling_params) != -1)
	{
		prio = scheduling_params.sched_priority;
	}
	else
	{
		prio = 10;
	}

	event.sigev_notify   = SIGEV_PULSE;
	event.sigev_coid     = ConnectAttach(ND_LOCAL_NODE, 0, attach->chid, _NTO_SIDE_CHANNEL, 0);
	event.sigev_priority = prio;
	event.sigev_code     = METRONOME_PULSE_CODE;
	timer_create(CLOCK_MONOTONIC, &event, &timer_id);

	int sec        = 0;
	double nanos   = 0.0;
	double integer = 0.0;
	double decimal = 0.0;
	double temp    = pattern_interval;

	while (temp != 0)
	{
		decimal = modf(pattern_interval, &integer);
		if (decimal != 0)
		{
			nanos = decimal * 1000000000; /* Convert to nano seconds */
		}
		if (integer != 0) 
		{
			sec = pattern_interval;
		}
		temp -= (integer + decimal);
	}

	printf("Running timer every %d seconds and %.2f nano seconds\n", sec, nanos);

	    /* Timer Interval */
	itime.it_value.tv_sec     = 0;
	itime.it_value.tv_nsec    = spacing_timer;
	    /* Initial Expiration */
	itime.it_interval.tv_sec  = 0;
	itime.it_interval.tv_nsec = spacing_timer;
	timer_settime(timer_id, 0, &itime, NULL);

	        /* Phase II - receive pulses from interval timer OR io_write(pause, quit) */
	for (;;)
	{
		int rcvid = MsgReceivePulse(attach->chid, (void*) &data, sizeof(data), NULL);
		if (rcvid == -1) /* Error condition, exit */
		{
			printf("Failed to receive message\n");
			break;
		}
		recv_msg = (my_message_t*) data;
		if (rcvid == 0) /* Pulse received */
		{
			switch (recv_msg->pulse.code)
			{
			case METRONOME_PULSE_CODE:

				if (output_complete){
					/* No need to process more pulses - Measures are done */
					// TODO: Destroy timer as there is no need to send more pulses
					break;
				}else {

					if (current_output == output_internal){
						output_complete = true;
						/* Done break the loop */
						break;
					}

					/* Case 1 - Start sequence */
					if (case_1) {
						std::cout << str[char_index] << str[char_index + 1];
						std::cout.flush();
						case_1 = false;
						char_index = 2;
					} else {
						if (char_index == str.length()) {
							/* End of measure*/
							std::cout << "\n";
							std::cout.flush();
							char_index = 0;
							case_1 = true;
							current_output++;
						} else {
							/* Mid measure */
							std::cout << str[char_index];
							std::cout.flush();
							char_index++;
						}
					}
				}
				break;

			case PAUSE_PULSE_CODE:
				    /* Pause the running timer */
				itime.it_value.tv_sec  = recv_msg->pulse.value.sival_int;
				itime.it_value.tv_nsec = 0;
				timer_settime(timer_id, 0, &itime, 0);
				break;
			case QUIT_PULSE_CODE:
				    /* Phase III - Cleanup */
				timer_delete(timer_id);
				name_detach(attach, 0);
				name_close(metronome_coid);
				exit(EXIT_SUCCESS);
			default:
				printf("Not the expected Code %d Value %d\n",
						metronome_msg.METRONOME.pulse_code,
						metronome_msg.pulse.value.sival_int);
				break;
			}
			continue;
		}
	}
	return NULL;
}

/**
 * Overloaded function responsible for returning bytes to the client after receiving an _IO_READ message
 *
 * @param ctp - Pointer to the structure containing details about who and what to return to the client
 * @param msg - Pointer to the message sent by the client
 * @param ocb - Pointer to a structure containing the information about the client/server interaction
 */
int io_read(resmgr_context_t *ctp, io_read_t *msg, RESMGR_OCB_T *ocb) 
{
	int nb; /* Number of bytes to send back */

	if (data == NULL)
	{
		return 0;
	}

	    /* Calculate the seconds-per-beat and nano seconds for the interval timer */
	double seconds_per_beat = (60.0 / metronome_msg.METRONOME.beats_per_minute)*metronome_msg.METRONOME.time_signature_top;
	seconds_per_beat /= output_internal;
	double nanos_per_beat = seconds_per_beat * 1000000000;

	sprintf(data,
			"[metronome: %d beats/min, time signature %d/%d, secs-per-beat: %.2f, nanoSecs: %d]\n",
			metronome_msg.METRONOME.beats_per_minute,
			metronome_msg.METRONOME.time_signature_top,
			metronome_msg.METRONOME.time_signature_bottom,
			seconds_per_beat,
			nanos_per_beat);

	nb = strlen(data);

	if (ocb->offset == nb) /* Test to see if we have already sent the whole message. (EOF) */
	{
		return 0;
	}

	    /* Determine how many bytes the client is requesting */
	nb = std::min<int>(nb, msg->i.nbytes); /* Return which ever is smaller the size of our data or the size of the buffer */

	_IO_SET_READ_NBYTES(ctp, nb); /* Read nbytes of data and set the number of bytes we will return */
	SETIOV(ctp->iov, data, nb);   /* Fill the return buffer with the data and size (Copy data into reply buffer) */
	ocb->offset += nb;            /* update offset into our data used to determine start position for next read. */

	if (nb > 0) /* If we are going to send any bytes update the access time for this resource. */
	{
		ocb->attr->flags |= IOFUNC_ATTR_ATIME;
	}
	     /* Return to the resource manager library so it can do a MsgReply */
	return (_RESMGR_NPARTS(1)); /* The one is the number of parts we are sending */
}

/**
 * Overloaded function that sends a pulse to the main thread of the metronome to have the metronome
 * thread pause for the specified number of seconds.
 *
 * @param ctp - Pointer to the structure containing details about who and what to return to the client
 * @param msg - Pointer to the message sent by the client
 * @param ocb - Pointer to a structure containing the information about the client/server interaction
 */
int io_write(resmgr_context_t *ctp, io_write_t *msg, RESMGR_OCB_T *ocb) {
	int nb = 0;

	    /* Check if the number of bytes to be written match the message length - All the data is in the current buffer */
	if (msg->i.nbytes == ctp->info.msglen - (ctp->offset + sizeof(*msg))) {
		char *buf;
		char *pause_msg;
		int i;
		int pause;

		buf = (char *) (msg + 1);

		if (strstr(buf, "pause") != NULL) 
		{
			    /* Process pause value */
			for (i = 0; i < 2; i++) 
			{
				pause_msg = strsep(&buf, " ");
			}
			pause = atoi(pause_msg);
			if (pause >= LOWER_INPUT_LIMIT && pause <= UPPER_INPUT_LIMIT) 
			{
				MsgSendPulse(metronome_coid, SchedGet(0, 0, NULL),
				        PAUSE_PULSE_CODE, pause);
			} 
			else 
			{
				printf("Pause needs to be a number between 1 and 10. \n");
			}
		} 
		else if (strstr(buf, "quit") != NULL) 
		{
			    /* Process quit value */
			MsgSendPulse(metronome_coid, SchedGet(0, 0, NULL), 
			            QUIT_PULSE_CODE,0);
		} 
		else 
		{
			strcpy(data, buf);
		}

		nb = msg->i.nbytes;
	} 
	else 
	{
		    /* There is more data - requires resmgr_msfread to request more data */
		printf("There's more data\n");
	}

	_IO_SET_WRITE_NBYTES(ctp, nb); /* Set the number of bytes that were written */

	if (msg->i.nbytes > 0) 
	{
		ocb->attr->flags |= IOFUNC_ATTR_MTIME | IOFUNC_ATTR_CTIME; /* Update the access times of the resource */
	}
	return (_RESMGR_NPARTS(0));
}

/**
 * Overloaded function to handle the setup and identification of a given client
 */
int io_open(resmgr_context_t *ctp, io_open_t *msg, RESMGR_HANDLE_T *handle, void *extra)
{
	if ((metronome_coid = name_open("metronome", 0)) == -1) 
	{
		perror("name_open failed.\n");
		return EXIT_FAILURE;
	}
	return (iofunc_open_default(ctp, msg, handle, extra));
}

int main(int argc, char *argv[]) 
{
	    /* Validate the number of command line arguments as per requirements */
	if (argc != EXPECTED_NUM_ATTR)
	{
		display_usage();
		exit(EXIT_FAILURE);
	}

	    /* Process command line input */
	memset(&metronome_msg, 0, sizeof(metronome_msg));
	metronome_msg.METRONOME.beats_per_minute      = atoi(argv[1]);
	metronome_msg.METRONOME.time_signature_top    = atoi(argv[2]);
	metronome_msg.METRONOME.time_signature_bottom = atoi(argv[3]);

	    /* Create dispatch structure */
	if ((dpp = dispatch_create()) == NULL)
	{
		printf("Unable to allocate dispatch context.\n");
		return EXIT_FAILURE;
	}

	    /* Fill out tables with the default resource manager IO functions  */
	iofunc_func_init(_RESMGR_CONNECT_NFUNCS, &connect_funcs,
	_RESMGR_IO_NFUNCS, &io_funcs);

	    /* Overload the functions */
	connect_funcs.open = io_open;
	io_funcs.read      = io_read;
	io_funcs.write     = io_write;

	    /* Fill out device attributes structure to define the device the resource manager is going to manage */
	iofunc_attr_init(&ioattr,          /* A pointer to the iofunc_attr_t structure that needs to be initialized */
			         S_IFCHR | 0666,   /* Set permission for Character special file - See README for other permissions */
	                 NULL,             /* Pointer to a iofunc_attr_t structure to initialize the structure pointed to by attr. */
	                 NULL);            /* Pointer to a _client_info structure that contains the information about a client connection */

	    /* Initialize resource manager attributes */
	memset(&resmgr_attr, 0, sizeof resmgr_attr);
	resmgr_attr.nparts_max   = 1;      /* Number of IOV structures available for server replies */
	resmgr_attr.msg_max_size = 2048;   /* Minimum receive buffer size */

	/* Attach resource manager to device to open for clients to communicate */
	id = resmgr_attach(dpp,            /* dispatch handle */
	                   &resmgr_attr,   /* resource manager attrs */
	                   PATHNAME,       /* Device's path */
	                   _FTYPE_ANY,     /* Message types supported */
	                   0,              /* Control flags */
	                   &connect_funcs, /* connect routines */
	                   &io_funcs,      /* I/O routines */
	                   &ioattr);       /* Pointer to device attributes */

	if (id == -1) 
	{
		printf("Unable to attach name.\n");
		return EXIT_FAILURE;
	}

	pthread_attr_t attr;
	pthread_attr_init(&attr); /* Initialize attr with all default thread attributes */
	if (pthread_create(NULL, &attr, metronome_thread, NULL) != 0) {
		printf("Error creating metronome thread\n");
		exit(EXIT_FAILURE);
	}
	pthread_attr_destroy(&attr);

	    /* Allocate dispatch context - Contains all relevant data for the message receive loop */
	ctp = dispatch_context_alloc(dpp);

	while (1)
	{
		ctp = dispatch_block(ctp); /* Perform the msgReceive and store relevant msginfo */
		dispatch_handler(ctp);     /* Lookup on function tables created and call the appropriate functions for each message received */
	}
	return EXIT_SUCCESS;
}
