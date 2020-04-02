#include <iostream>
using namespace std;

int metronome_coid;

/**
 * This function "drives" the metronome. It receives pulse from interval timer;
 * each time the timer expires it receives pulses from io_write (quit and pause <int>)
 */
void metronome_thread()
{

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

	// TODO: Create the metronome thread

	return 0;
}
