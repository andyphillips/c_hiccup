#define _GNU_SOURCE

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/time.h>
#include <string.h>
#include <poll.h>
#include <sys/timerfd.h>
#include <errno.h>
#include <limits.h>

#include <stdint.h>
#include <stdbool.h>
     
#include "hdr_histogram.h"

//notes
//
// 2) test against the spreadsheet

// measure the jitter of the itimerspec timer
#define VERSION 1.0
#define IDENTIFIER_SIZE 256

static inline unsigned long int nano_difftime(struct timespec t1, struct timespec t2)
{
     return (1000000000 * (t2.tv_sec - t1.tv_sec)) + t2.tv_nsec - t1.tv_nsec;
}

// globals. 
struct timeval start;
int verbose_flag=0;
FILE *log_file, *histogram_file;

/* c_hiccup.c */
int main(int argc, char *argv[]);
void print_header_line(FILE *out, struct timeval *start);
void print_histogram_line(FILE *out, struct hdr_histogram *interval, struct hdr_histogram *cumulative);
void print_full_histogram(FILE *out, struct hdr_histogram *cumulative);
void zero_timeout (struct itimerspec *timeout);
void usage (void);
void open_files(char *identifier);

int main ( int argc, char *argv[] )
{
     unsigned long int microdelta, interval_length, interval_diff, sample_desired_wait_time;
     int nfds,return_value, delay_microseconds, run_seconds, total_runtime_seconds;
     int fd, opt, quit=0;
     char buffer[128];
     char identifier[IDENTIFIER_SIZE];

     struct pollfd poll_list[256];
     struct itimerspec timeout;     
     struct timespec sample_start_time, sample_end_time, interval_start;
     struct timeval end;

     struct hdr_histogram *interval_histogram, *cumulative_histogram;
     
     // 10 seconds worth of microseconds 
     if (hdr_alloc(10000000, 3, &interval_histogram) == -1) {
	  printf("unable to allocate interval histogram\n");
	  exit (1);
     }
     if (hdr_alloc(10000000, 3, &cumulative_histogram) == -1) {
	  printf("unable to allocate cumulative histogram\n");
	  exit (1);
     }

     memset (buffer, 0, 128);
     memset (identifier, 0, IDENTIFIER_SIZE);
     
     // Timing and names
     // 
     // interval_length 		- this is the length of time between successive histogram line printouts
     // 
     // sample_desired_wait_time 	- this is the amount of time we set the timer to wait for. This should be 
     //               			  less than the jitter we're attempting to measure. Given the nature of this 
     //               			  program, this should always be less than 1 second. If it is not, you'll need to 
     //               			  modify the code below where we set the interval timer.  
     // sample_start_time, 
     // sample_end_time 		- The times associated with a single pass through the timing loop.
     // 
     // delay_microseconds	       	- An arbitrary amount of time we wait before starting anything. This is useful if
     // 				  you want to catch something, and there's a warm up period.
     // 				  For compatibility with jHiccup this is specified on the command line as millis. 
     // 
     // run_seconds			- The time we spend running the timing loops. 
     // 
     
     interval_length = 5;  			// 5 seconds per reporting interval 
     sample_desired_wait_time = 1000000; 	// 1 millisecond in nanoseconds.
     delay_microseconds = 0;
     run_seconds = 100000000;                   // forever, or near enough - 3 years. 
     
     while ( (opt = getopt(argc, argv, "d:l:r:vh"))  != -1) {
	  switch (opt) {
	  case 'd':
	       delay_microseconds = atoi(optarg) * 1000;
	       break;
	  case 'l':
	       strncpy(identifier, optarg, IDENTIFIER_SIZE-1);
	       break;
	  case 'r':
	       run_seconds = atoi(optarg);
	       break;
	  case 'v':
	       verbose_flag = 1;
	       break;
	  case 'h':
	  default:
	       usage();
	       exit(-1);
	  }
     }
     
     total_runtime_seconds = run_seconds + (delay_microseconds/1000000);
     
     open_files(identifier);
     
     if ((fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC)) < 0) {
	  perror("timerfd_create; failed to create timer");
	  return -1;
     }
     
     zero_timeout(&timeout);
     interval_length = interval_length * 1000000000;  // convert to nanoseconds

     // set up the poll list structure. 
     memset (&poll_list, 0, (256*sizeof(struct pollfd)));
 
     poll_list[0].fd=fd;
     poll_list[0].events=POLLIN|POLLPRI|POLLRDHUP;
     poll_list[0].revents=0;
     nfds = 1;

     // get wall clock start time and push out a header 
     gettimeofday(&start,NULL);   
     print_header_line(log_file, &start);
     if (verbose_flag) print_header_line(stdout, &start);
     
     // we wait a set number of seconds before starting the test. 
     usleep(delay_microseconds);
     // get start of our delta interval measurement
     clock_gettime(CLOCK_MONOTONIC, &interval_start);
     
     while (!quit) {
	  timeout.it_value.tv_sec = 0;
	  timeout.it_value.tv_nsec = sample_desired_wait_time;
	  
	  // We arm the timer first (timerfd_settime), then get the time as quickly as possible. 
	  // We don't want to measure the time that timerfd_settime takes, so its a choice between
	  // under or over reporting the time, as clock_gettime is expected to be fast. 
	  timerfd_settime(fd, 0, &timeout, NULL);
	  clock_gettime(CLOCK_MONOTONIC,&sample_start_time);
	  return_value = poll(poll_list,nfds,-1);
	  clock_gettime(CLOCK_MONOTONIC,&sample_end_time);

	  // the the error value is anything other than EINTR we have a real problem. 
	  if ((return_value < 0) && (errno != EINTR))  {
	       perror("poll problem");
	       exit(-1);
	  }
	  
	  // read the timer to clear the event.
	  read(fd,buffer,8);
	  
	  // record the delta for both the interval and cumulative histograms. 
	  microdelta = nano_difftime(sample_start_time, sample_end_time)/1000;
	  
	  hdr_record_value(interval_histogram, microdelta);
	  hdr_record_value(cumulative_histogram, microdelta);

	  // See if we're due to print out an interval update. 
	  interval_diff = nano_difftime(interval_start, sample_end_time);
	  
	  if (interval_diff >= interval_length) {
	       // print progress
	       print_histogram_line(log_file, interval_histogram, cumulative_histogram);
	       if (verbose_flag) print_histogram_line(stdout, interval_histogram, cumulative_histogram);
	       print_full_histogram(histogram_file,cumulative_histogram);
	       // are we done?
	       gettimeofday(&end, NULL);
	       if (end.tv_sec >= (start.tv_sec + total_runtime_seconds)) quit++;
	       // reset the interval histogram.
	       hdr_reset(interval_histogram);
	       // reset the interval start timer. 
	       clock_gettime(CLOCK_MONOTONIC, &interval_start);
	  }
     }
     
     close(fd);
     
     exit (0);
} 

void print_header_line(FILE *out, struct timeval *start)
{
     char buffer[128];
     
     memset(buffer, 0, 128);
     strftime(buffer, 127, "%a %b %d %T %Z %Y", localtime (&start->tv_sec));
     fprintf(out,"#[Logged with c_hiccup version %f] Times reported in microseconds\n", VERSION);
     fprintf(out,"#[Sampling start time: %s]\n", buffer);
     fprintf(out,"Time: IntervalPercentiles:count (50%% 90%%  Max) TotalPercentiles:count (50%% 90%% 99%% 99.9%% 99.99%% Max)\n"); 
     return;
}

// Time: seconds.millis 
// Interval: number
// IntervalPercentiles: ( 50% 90% Max )
// TotalPercentiles: ( 50% 90% 99% 99.9% 99.99% max ) 
void print_histogram_line (FILE *out, struct hdr_histogram *interval, struct hdr_histogram *cumulative)
{
     struct timeval now;
     unsigned int millis_delta, seconds_delta, millis_remainder;
     
     gettimeofday(&now,NULL);
     millis_delta = ((1000000 * (now.tv_sec - start.tv_sec)) + now.tv_usec - start.tv_usec)/1000;
     seconds_delta = millis_delta/1000;
     millis_remainder = millis_delta-(seconds_delta*1000);
     
     fprintf(out,"%d.%3.3d: I:%ld ",   seconds_delta,millis_remainder,interval->total_count);
     fprintf(out,"( %ld %ld %ld ) ",   hdr_value_at_percentile(interval,50.0),
	    hdr_value_at_percentile(interval,90.0),
	    hdr_max(interval));
     
     fprintf(out,"T:%ld ",cumulative->total_count);
     fprintf(out,"( %ld %ld %ld %ld %ld %ld)\n",  
	    hdr_value_at_percentile(cumulative,50.0),
	    hdr_value_at_percentile(cumulative,90.0),
	    hdr_value_at_percentile(cumulative,99.0),
	    hdr_value_at_percentile(cumulative,99.9),
	    hdr_value_at_percentile(cumulative,99.99),
	    hdr_max(cumulative));
     
     return;
}

// we rewrite the file each time. 
void print_full_histogram(FILE *out, struct hdr_histogram *cumulative)
{
     struct timeval now;

     gettimeofday(&now,NULL);
     fseek(out, 0, SEEK_SET);
     
     fprintf(out,"c_hiccup histogram report: %s\n------------------------\n",ctime(&now.tv_sec));
     hdr_percentiles_print(cumulative, out, 5, 1.0, CSV);
     fflush(out);
     return;
}

void zero_timeout (struct itimerspec *timeout)
{
     timeout->it_value.tv_sec = 0;
     timeout->it_value.tv_nsec = 0;
     timeout->it_interval.tv_sec = 0;
     timeout->it_interval.tv_nsec = 0;
     return;
}

void usage (void)
{
     printf("c_hiccup %f\n",VERSION);
     printf("\tc_hiccup [-d delay_time]  [-l identifier] [-r run_seconds] [-v] [-h]\n");
     printf("\n\t-d delay_time - time in milliseconds to wait before starting measurement\n");
     printf("\t-l identifier - output to identifier.hgrm and identifier.log instead of default hiccup.hgrm and hiccup.log\n");
     printf("\t-r run_seconds - run for a specific number of seconds. The default is to run forever (3 years/2 tech refreshes)\n");
     printf("\t-v  - verbose output. Display histogram summaries on stdout as well as to files.\n");
     printf("\t-h  - this text\n");
     printf("\tOutput should be compatible with the charting spreadsheet from jHiccup\n");
     return;
}

void open_files(char *identifier)
{
     char filename[IDENTIFIER_SIZE+50];
     
     memset(filename, 0, IDENTIFIER_SIZE+50);
     
     if (identifier[0] != 0) {
	  strncpy(filename, identifier, IDENTIFIER_SIZE-1);
	  strcat(filename,".log");
     } else {
	  strcpy(filename,"hiccup.log");
     }
     
     log_file = fopen(filename,"w+");
     
     if (log_file == NULL) {
	  perror(filename);
	  exit (-1);
     }
     
     memset(filename, 0, IDENTIFIER_SIZE+50);
     
     if (identifier[0] != 0) {
	  strncpy(filename, identifier, IDENTIFIER_SIZE-1);
	  strcat(filename,".hgrm");
     } else {
	  strcpy(filename,"hiccup.hgrm");
     }
     
     histogram_file = fopen(filename,"w+");
     
     if (histogram_file == NULL) {
	  perror(filename);
	  exit (-1);
     }
     
     return;
}