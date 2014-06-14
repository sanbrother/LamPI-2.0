/*
  Copyright (c) 2013,2014 Maarten Westenberg, mw12554@hotmail.com 
 
  This software is licensed under GNU license as detailed in the root directory
  of this distribution and on http://www.gnu.org/licenses/gpl.txt
 
  The above copyright notice and this permission notice shall be included in
  all copies or substantial portions of the Software.
 
  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
  THE SOFTWARE.
*/
#include <wiringPi.h>  
#include <stdio.h>  
#include <stdlib.h> 
#include <string.h>
#include <stdint.h> 
#include <time.h>
#include <errno.h>
#include <netdb.h>
#include <math.h>
#include <unistd.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <dirent.h>

#include "cJSON.h"
#include "sensor.h"


/* ----------------------------------------------------------------
 * The DS18B20 is a cheap yet powerful temperature sensor.
 * It works over the 1-wire Dallas bus
 * The Raspberry provides module support for the Dallas/Maxim bus
 * Make sure you load the w1-gpio and w1-therm modules.
 * > sudo modprobe w1-therm
 * > sodu modprobe w1-gpio
 * By reading the approrpiate device entry w1_slave the module will
 * return the sensor value
 *
 * ----------------------------------------------------------------	*/

// Declaration for Statistics
int statistics [I_MAX_ROWS][I_MAX_COLS];

// Declarations for Sockets
int sockfd;	
fd_set fds;
int socktcnt = 0;

// Options flags
static int dflg = 0;							// Daemon
static int cflg = 0;							// Check
int verbose = 0;
int debug = 0;
int checks = 0;									// Number of checks on messages
int sflg = 0;									// If set, gather statistics

/*
 *********************************************************************************
 * INIT STATISTICS
 *
 *********************************************************************************/
int init_statistics(int statistics[I_MAX_ROWS][I_MAX_COLS])
{
	// Brute force method. Just make everything 0
	int i;
	int j;
	for (i=0; i<I_MAX_ROWS; i++)
	{
		for (j=0; j<I_MAX_COLS; j++)
		{
			statistics[i][j]=0;
		}
	}
	return(0);
} 


/*
 *********************************************************************************
 * ds18b20_read
 *
 * Reading the sensor using the power method. Call the wait routine several times.
 * this method is more compute intensive than calling the interrupt routine.
 *
 *********************************************************************************
 */ 
int ds18b20_read(char *dir)  
{  
	char dev[128];
	char line[128];
	FILE *fp;
	int temperature;
	char * tpos = NULL;
	char * crcpos = NULL;
	//int cntr = 0;

	
	strcpy(dev,SPATH);
	strcat(dev,"/");
	strcat(dev,dir);
	strcat(dev,"/w1_slave");
	
	if (verbose) printf("dev: %s\n",dev);
	
	if (NULL == (fp = fopen(dev,"r") )) {
		perror("Error opening device");
		return(-1);
	}
	
	while (fgets(line, 128, fp) != NULL )
	{
		if (verbose) printf("read line: %s", line);
		
		// Before we read a temperature, first check the crc which is in a
		// line before the actual temperature line
		if (crcpos == NULL) {
			crcpos = strstr(line, "crc=");
			if ((crcpos != NULL) && (strstr(crcpos,"YES") == NULL)) {
				// CRC error
				crcpos = NULL;
				if (verbose) fprintf(stderr,"crc error for device %s\n" , dev);
			}
		}
		
		// If we have a valid crc check on a line, next line will contain valid temperature
		else {
			if (verbose) printf("crc read correctly\n");
			tpos = strstr(line, "t=");
			// Will only be true for 2nd line
			if (tpos != NULL) {
				tpos +=2;
				sscanf(tpos, "%d", &temperature);
			}
			if (verbose) printf("temp read: %d\n\n",temperature);
		}
		
	}
	
	fclose(fp);
	//
	
	return(temperature);
} 


/* ********************************************************************
 * MAIN PROGRAM
 *
 * Read the user option of the commandline and either print to stdout
 * or return the value over the socket. 
 *
 * ********************************************************************	*/  
int main(int argc, char **argv)  
{  
	int i,c;
	int errflg = 0;
	int repeats = 1;
	int temp = 0;
	int mode = SOCK_STREAM;					// Datagram is standard
	float temperature;						// 
	
	char *hostname = "255.255.255.255";		// Default setting for our host == this host
	char *port = UDPPORT;					// default port, 5000
	
    extern char *optarg;
    extern int optind, optopt;

	// ------------------------- COMMANDLINE OPTIONS SETTING ----------------------
	// Valid options are:
	// -h <hostname> ; hostname or IP address of the daemon
	// -p <port> ; Portnumber for daemon socket
	// -v ; Verbose, Give detailed messages
	//
    while ((c = getopt(argc, argv, ":bc:dh:p:r:stvx")) != -1) {
        switch(c) {
			case 'b':						// Broadcasting and UDP mode
				mode = SOCK_DGRAM;
			break;
			case 'c':
				cflg = 1;					// Checks
				checks = atoi(optarg);
			break;
			case 'd':						// Daemon mode, cannot be together with test?
				dflg = 1;
			break;
			case 'h':						// Socket communication
           		dflg++;						// Need daemon flag too, (implied)
				hostname = optarg;
			break;
			case 'p':						// Port number
            	port = optarg;
				dflg++;						// Need daemon flag too, (implied)
			break;
			case 'r':						// repeats
				repeats = atoi(optarg);
			break;
			case 's':						// Statistics
				sflg = 1;
			break;
			case 't':						// Test Mode, do debugging
				debug=1;
			break;
			case 'v':						// Verbose, output long timing/bit strings
				verbose = 1;
			break;
			case ':':       				// -f or -o without operand
				fprintf(stderr,"Option -%c requires an operand\n", optopt);
				errflg++;
			break;
			case '?':
				fprintf(stderr, "Unrecognized option: -%c\n", optopt);
				errflg++;
			break;
        }
    }
	
	// -------------------- PRINT ERROR ---------------------------------------
	// Print error message if parsing the commandline
	// was not successful
	
    if (errflg) {
        fprintf(stderr, "usage: argv[0] (options) \n\n");
		fprintf(stderr, "-b\t\t; Broadcast mode. Use UDP broadcast mode\n");
		fprintf(stderr, "-d\t\t; Daemon mode. Codes received will be sent to another host at port 5000\n");
		fprintf(stderr, "-s\t\t; Statistics, will gather statistics from remote\n");
		fprintf(stderr, "-t\t\t; Test mode, will output received code from remote\n");
		fprintf(stderr, "-v\t\t; Verbose, will output more information about the received codes\n");
        exit (2);
    }
	

	//	------------------ PRINTING Parameters ------------------------------
	//
	if (verbose == 1) {
		printf("The following options have been set:\n\n");
		printf("-v\t; Verbose option\n");
		if (mode == SOCK_DGRAM) printf("USP Broadcast mode\n");
		if (statistics>0)	printf("-s\t; Statistics option\n");
		if (dflg>0)			printf("-d\t; Daemon option\n");
		if (debug)			printf("-t\t; Test and Debug option");
		printf("\n");						 
	}//if verbose
	

	if (sflg) {
		fprintf(stderr,"init statistics\n");
		init_statistics(statistics);			// Make cells 0
	}
	
	chdir (SPATH);
	
	// ------------------------
	// MAIN LOOP
	// 

	if (verbose) printf("\nRepeats: %d::\n",repeats);
	for (i=0; i<repeats; i++)  
	{  
		// For every directory found in SPATH
		DIR *dir;
		struct dirent *ent;
		if ((dir = opendir (SPATH)) != NULL) {
			/* print all the files and directories within directory */
			while ((ent = readdir (dir)) != NULL) {
			
				if (verbose) printf ("%s\n", ent->d_name);
				// 28 is the prefix for ds18b20
				if (strncmp(ent->d_name,"28",2) == 0)
				{
					temp = ds18b20_read(ent->d_name);
					temperature = (float) temp/1000;		// must be float
					
					if (dflg) {
						// If we are in daemon mode, initialize sockets etc.
						if ((sockfd = socket_open(hostname, port, mode)) == -1) {
							fprintf(stderr,"socket_open failed\n");
							exit(1);
						}
						
						// Daemon, output to socket
						send_2_server(
							sockfd,	
							hostname,				// Actually HostIP
							port,			
							mode,					// Either SOCK_STREAM or SOCK_DGRAM
							ent->d_name,			// Address
							0, 						// Channel
							temperature,			// temperature
							-1,						// humidity (-1 == no value)
							-1,						// Windspeed (-1 == no value)
							-1,						// Winddirection (-1 == no value)
							-1						// Rainfall
						);
						
						printf("Sent to host: %s:%s, temp: %2.1f\n", hostname, port, temperature);
					}
					else {
					// Commandline
						if (temp > 0) {
							printf("Temperature for dev %s: %2.1f\n", ent->d_name, temperature);
						}
						else {
							temp = -temp;
							printf("Temperature for dev %s: -%d.%d\n",
											ent->d_name, temp/1000,temp%1000);
						}
					}
				}
			}
			closedir (dir);
		} 
		else {
  			/* could not open directory */
 			 perror ("No such directory ");
			return EXIT_FAILURE;
		}
	}
	delay(1000);
	// Close the socket to the daemon
	if (close(sockfd) == -1) {
		perror("Error closing socket to daemon");
	}
	// Should wait for confirmation of the daemon before closing
	
	exit(EXIT_SUCCESS); 
}  