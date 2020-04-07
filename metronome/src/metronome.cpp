#include <iostream>
#include <errno.h>
#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>

/*
 * Define THREAD_POOL_PARAM_T such that we can avoid a compiler
 * warning when we use the dispatch_*() functions below
 */
#define THREAD_POOL_PARAM_T    dispatch_context_t

#include <sys/iofunc.h>
#include <sys/dispatch.h>

static resmgr_connect_funcs_t   connect_funcs;
static resmgr_io_funcs_t        io_funcs;
static iofunc_attr_t            ioattr;

#define ATTACH_POINT            "metronome"
#define METRONOME_PULSE_CODE    _PULSE_CODE_MINAVAIL
#define PAUSE_PULSE_CODE        (METRONOME_PULSE_CODE + 7)
#define QUIT_PULSE_CODE         (METRONOME_PULSE_CODE + 8)

typedef union
{
	struct _pulse   pulse;
	struct METRONOME{
		int             beats_per_minute;
		int             time_signature_top;
		int             time_signature_bottom;
	} METRONOME;

} my_message_t;

struct DataTableRow
{
	int time_signature_top;
	int time_signature_bottom;
	int num_intervals;
	std::string pattern;

} typedef DataTableRow;


/**
 * Time-signature-top | Time-signature bottom | Number of Intervals within each beat | Pattern for Intervals within Each Beat
 */
DataTableRow t[] =
{
		{2, 4, 4,   "|1&2&"},
		{3, 4, 6,   "|1&2&3&"},
		{4, 4, 8,   "|1&2&3&4&"},
		{5, 4, 10,  "|1&2&3&4-5-"},
		{3, 8, 6,   "|1-2-3-"},
		{6, 8, 6,   "|1&a2&a"},
		{9, 8, 9,   "|1&a2&a3&a"},
		{12, 8, 12, "|1&a2&a3&a4&a"}
};

int row;
name_attach_t *attach;
int rcvid;
char data[255];
int metronome_coid;
my_message_t msg;

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

	/* Create a local name to register the device */
	if ((attach = name_attach(NULL, ATTACH_POINT, 0)) == NULL)
	{
		printf("Failed to attach to device");
	}


	printf("Metronome initializing...\n");

	 // TODO: calculate the seconds-per-beat and nano seconds for the interval timer
	double pattern_interval = (60/msg.METRONOME.beats_per_minute)*msg.METRONOME.time_signature_top; /* Calculate the time for starting a new line */
	double output_internal = 0.0;

	printf("Matching key top %d - bottom %d\n", msg.METRONOME.time_signature_top, msg.METRONOME.time_signature_bottom);
	for (int i = 0; i < 8; i++)
	{
		if (t[i].time_signature_top == msg.METRONOME.time_signature_top && t[i].time_signature_bottom == msg.METRONOME.time_signature_bottom){
			output_internal = t[i].num_intervals;
			printf("MATCH - Time signature top %d Time signature bottom %d Output interval %d\n", t[i].time_signature_top, t[i].time_signature_bottom, t[i].num_intervals);
		} else {
			printf("No match - Time signature top %d Time signature bottom %d Output interval %d\n", t[i].time_signature_top, t[i].time_signature_bottom, t[i].num_intervals);
		}
	}

	printf("Ready to receive messages...\n");
	//double spacing_timer    = pattern_interval/
	 // TOOD: create an interval timer to "drive" the metronome
	 // TODO: configure the interval timer to send a pulse to channel at attach when it expires



		/* Phase II - receive pulses from interval timer OR io_write(pause, quit) */
	while(1)
	{
	   rcvid = MsgReceivePulse(attach->chid, &msg, sizeof(msg), NULL);

	   if (rcvid == -1) /* Error condition, exit */
	   {
		   printf("Failed to receive message");
		   break;
	   }


	   if (rcvid == 0) /* Pulse received */
	   {
		   switch (msg.pulse.code)
		   {
			   case METRONOME_PULSE_CODE:

			   	   /*
			   	    * TODO
			   	    * display the beat to stdout
			   	    * must handle 3 cases:
			   	    * start-of-measure: |1
			   	    * mid-measure: the symbol, as seen in the column "Pattern for Intervals within Each Beat"
			   	    * end-of-measure: \n
			   	    */
				   printf("Metronome pulse");
				   break;

		       case PAUSE_PULSE_CODE:

		    	   /*
		    	    * TODO:
		    	    * pause the running timer for pause <int> seconds
		    	    */
		    	   printf("Pausing running timer");

			   	   break;
		       case QUIT_PULSE_CODE:

		    	   /*
		    	    * TODO:
		    	    * implement Phase III:
		    	    * delete interval timer
		    	    * call name_detach()
		    	    * call name_close()
		    	    * exit with SUCCESS
		    	    */
		    	   printf("Exiting");
			   	   break;
			   default:
				   printf("Not the expected code");
				   break;
			   }
		   continue;
	   }

	}
	return NULL;
}

int io_read(resmgr_context_t *ctp, io_read_t *msg, RESMGR_OCB_T *ocb)
{
	int nb;

	if (data == NULL)
	{
		return 0;
	}

	//TODO: calculations for secs-per-beat, nanoSecs
	sprintf(data, "[metronome: %d beats/min, time signature %d/%d, secs-per-beat: %.2f, nanoSecs: %d]\n");
	nb = strlen(data);

	if (ocb->offset == nb) /* Test to see if we have already sent the whole message. */
	{
		return 0;
	}

	nb = std::min<int>(nb, msg->i.nbytes); /* return which ever is smaller the size of our data or the size of the buffer */

	_IO_SET_READ_NBYTES(ctp, nb); /* Set the number of bytes we will return */
	SETIOV(ctp->iov, data, nb);   /* Copy data into reply buffer. */
	ocb->offset += nb; /* update offset into our data used to determine start position for next read. */

	if (nb > 0) /* If we are going to send any bytes update the access time for this resource. */
	{
		ocb->attr->flags |= IOFUNC_ATTR_ATIME;
	}
	return(_RESMGR_NPARTS(1));
}

/**
 * This function sends a pulse to the main thread of the metronome to have the metronome
 * thread pause for the specified number of seconds.
 */
int io_write(resmgr_context_t *ctp, io_write_t *msg, RESMGR_OCB_T *ocb)
{
    int nb = 0;

    if( msg->i.nbytes == ctp->info.msglen - (ctp->offset + sizeof(*msg) ))
    {
		char *buf;
		char *pause_msg;
		int i;
		int pause;
		buf = (char *)(msg+1);

		std::cout << "Buffer: \n" << buf << std::endl;
		std::cout.flush();

		if(strstr(buf, "pause") != NULL)
		{
			for(i = 0; i < 2; i++){
				pause_msg = strsep(&buf, " ");
			}
			pause = atoi(pause_msg);
			if(pause >= 1 && pause <= 10)
			{
				printf("Sending pause message");
				MsgSendPulse(metronome_coid, SchedGet(0,0,NULL), PAUSE_PULSE_CODE, pause);
			}
			else
			{
				printf("Pause needs to be a number between 1 and 10. \n");
			}
		}
		else if (strstr(buf, "quit") != NULL)
		{
			MsgSendPulse(metronome_coid, SchedGet(0,0,NULL), QUIT_PULSE_CODE, 0);
		}
		else
		{
			strcpy(data, buf);
		}

		nb = msg->i.nbytes;
    }

    _IO_SET_WRITE_NBYTES (ctp, nb);

    if (msg->i.nbytes > 0)
        ocb->attr->flags |= IOFUNC_ATTR_MTIME | IOFUNC_ATTR_CTIME;

    return (_RESMGR_NPARTS (0));
}

int io_open(resmgr_context_t *ctp, io_open_t *msg, RESMGR_HANDLE_T *handle, void *extra)
{
    if ((metronome_coid = name_open( "metronome", 0 )) == -1) {
        perror("name_open failed.");
        return EXIT_FAILURE;
    }
    printf("IO Opened\n");
    return (iofunc_open_default (ctp, msg, handle, extra));
}

int main(int argc, char *argv[])
{
	/* Validate the number of command line arguments as per requirements */
	if (argc != 4)
	{
		display_usage();
		exit(EXIT_FAILURE);
	}

    memset( &msg, 0, sizeof(my_message_t));

    msg.METRONOME.beats_per_minute      = atoi(argv[1]);
    msg.METRONOME.time_signature_top    = atoi(argv[2]);
    msg.METRONOME.time_signature_bottom = atoi(argv[3]);

	printf("Handling sequence %d %d %d\n", msg.METRONOME.beats_per_minute, msg.METRONOME.time_signature_top, msg.METRONOME.time_signature_bottom);
	thread_pool_attr_t pool_attr;
	thread_pool_t      *tpp;
	dispatch_t         *dpp;
	resmgr_attr_t      resmgr_attr;
	int                id;

	dispatch_context_t   *ctp;


	if ((dpp = dispatch_create ()) == NULL)
	{
		fprintf (stderr,
				"%s:  Unable to allocate dispatch context.\n",
				argv [0]);

		return EXIT_FAILURE;
	}

	/* Initialize thread pool attributes */
	memset(&pool_attr, 0, sizeof(pool_attr));
	pool_attr.handle        = dpp;                     /* Handle that gets passed to the context_alloc function */
	pool_attr.context_alloc = dispatch_context_alloc;  /* The function that's called when the worker thread is ready to block, waiting for work. This function returns a pointer that's passed to handler_func. */
	pool_attr.block_func    = dispatch_block;          /* The function that's called to unblock threads */
	pool_attr.unblock_func  = dispatch_unblock;        /* The function that's called after block_func returns to do some work. The function is passed the pointer returned by block_func. */
	pool_attr.handler_func  = dispatch_handler;        /* The function that's called when a new thread is created by the thread pool. It is passed handle. */
	pool_attr.context_free  = dispatch_context_free;   /* The function that's called when the worker thread exits, to free the context allocated with context_alloc. */
	pool_attr.lo_water      = 2;                       /* The minimum number of threads that the pool should keep in the blocked state (i.e. threads that are ready to do work). */
	pool_attr.hi_water      = 4;                       /* The maximum number of threads to keep in a blocked state. */
	pool_attr.increment     = 1;                       /* The number of new threads created at one time. */
	pool_attr.maximum       = 50;                      /* The maximum number of threads that the pool can create. */

	/* allocate a thread pool handle */
	if ((tpp = thread_pool_create(&pool_attr,
	POOL_FLAG_EXIT_SELF)) == NULL)
	{
		fprintf(stderr,
				"%s: Unable to initialize thread pool.\n",
				argv[0]);

		return EXIT_FAILURE;
	}

	iofunc_func_init(_RESMGR_CONNECT_NFUNCS,
			          &connect_funcs,
					  _RESMGR_IO_NFUNCS,
					  &io_funcs);

	/* Overload the functions */
	connect_funcs.open = io_open;
	io_funcs.read      = io_read;
	io_funcs.write     = io_write;

	iofunc_attr_init(&ioattr,
			         S_IFNAM | 0666,
					 0,
					 0);

    memset( &resmgr_attr, 0, sizeof resmgr_attr );
    resmgr_attr.nparts_max   = 1;
    resmgr_attr.msg_max_size = 2048;


	/* Attach to device to open for clients to communicate */
	id = resmgr_attach (dpp,                     /* dispatch handle */
						&resmgr_attr,            /* resource manager attrs */
						"/dev/local/metronome",  /* device name */
						_FTYPE_ANY,              /* open type */
						0,                       /* flags */
						&connect_funcs,          /* connect routines */
						&io_funcs,               /* I/O routines */
						&ioattr);                /* handle */

	if (id == -1)
	{

		fprintf (stderr,
				"%s:  Unable to attach name.\n",
				argv [0]);

		return EXIT_FAILURE;
	}

	printf("Resource manager attached successfully\n");

    /* Start the thread which will handle interrupt events. */
    pthread_create ( NULL, NULL, metronome_thread, NULL );

    /* Never returns */
    thread_pool_start( tpp );

	ctp = dispatch_context_alloc(dpp);

	while(1)
	{
		ctp = dispatch_block(ctp);
		dispatch_handler(ctp);
	}
	return EXIT_SUCCESS;
}
