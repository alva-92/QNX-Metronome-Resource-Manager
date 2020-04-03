#include <iostream>
#include <stdlib.h>
#include <stdio.h>
#include <sys/iofunc.h>
#include <sys/dispatch.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <errno.h>

#define ATTACH_POINT    "metronome"
#define METRONOME_PULSE_CODE _PULSE_CODE_MINAVAIL
#define PAUSE_PULSE_CODE 7
#define QUIT_PULSE_CODE  8

typedef union
{
	struct _pulse   pulse;
	char msg[255];

} my_message_t;

name_attach_t *attach;
int rcvid;
char data[255];
int metronome_coid;
my_message_t msg;

/**
 * This function "drives" the metronome. It receives pulse from interval timer;
 * each time the timer expires it receives pulses from io_write (quit and pause <int>)
 */
void metronome_thread()
{
		/* Phase I - create a named channel to receive pulses */

	/* Create a local name to register the device */
	if ((attach = name_attach(NULL, ATTACH_POINT, 0)) == NULL)
	{
		printf("Failed to attach to device");
	}

	/*
	 *  TODO
	 *  calculate the seconds-per-beat and nano seconds for the interval timer
	 *  create an interval timer to "drive" the metronome
	 *  configure the interval timer to send a pulse to channel at attach when it expires
	 */

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

				   break;

		       case PAUSE_PULSE_CODE:

		    	   /*
		    	    * TODO:
		    	    * pause the running timer for pause <int> seconds
		    	    */

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

			   	   break;
			   default:
				   printf("Not the expected code");
				   break;
			   }
		   continue;
	   }

	}
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
    return (iofunc_open_default (ctp, msg, handle, extra));
}

int main(int argc, char *argv[])
{
	/* Validate the number of command line arguments as per requirements */
	if (argc != 4)
	{
		perror("Error - Incorrect number of parameters");
		exit(EXIT_FAILURE);
	}

	/*
	 *   TODO: Implement
	 *   process the command-line arguments:
	 *   beats-per-minute
	 *   time-signature (top)
	 *   time-signature (bottom)
	 */

	dispatch_t* dpp;
	resmgr_io_funcs_t io_funcs;
	resmgr_connect_funcs_t connect_funcs;
	iofunc_attr_t ioattr;
	dispatch_context_t   *ctp;
	int id;

	if ((dpp = dispatch_create ()) == NULL)
	{
		fprintf (stderr,
				"%s:  Unable to allocate dispatch context.\n",
				argv [0]);

		return EXIT_FAILURE;
	}

	iofunc_func_init(_RESMGR_CONNECT_NFUNCS, &connect_funcs, _RESMGR_IO_NFUNCS, &io_funcs);
	connect_funcs.open = io_open;
	io_funcs.read = io_read;
	io_funcs.write = io_write;

	iofunc_attr_init(&ioattr, S_IFCHR | 0666, NULL, NULL);

	if ((id = resmgr_attach (dpp, NULL, "/dev/local/metronome",
					_FTYPE_ANY, 0, &connect_funcs, &io_funcs,
	                &ioattr)) == -1)
	{

		fprintf (stderr,
				"%s:  Unable to attach name.\n",
				argv [0]);

		return EXIT_FAILURE;
	}

	// TODO: Create the metronome thread

	ctp = dispatch_context_alloc(dpp);

	while(1)
	{
		ctp = dispatch_block(ctp);
		dispatch_handler(ctp);
	}
	return EXIT_SUCCESS;
}
