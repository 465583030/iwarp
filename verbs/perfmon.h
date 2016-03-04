/*Performance monitoring utilties

Copyright 2005 - Dennis Dalessandro (dennis@osc.edu)

This code has been tested successfully on the following architectures:
Intel Xeon 32 bit
AMD Opteron 32 and 64 bit
Intel Itanium 64 bit

This code has been tested successfully on the following platforms:
Linux


Suggested Use:
For Timing
	rdtsc(start);
	<WORK>
	rdtsc(stop);
	printf("elapsed time %f\n",elapsted_time(start,stop,SECONDS);



ToDo:
Do not let rdtsc, getmhz, getlcockrate, or elapsted time print anything or exit
all errors should just be returned to the calling process

long kernel_context_count() always seems to return 0, need to check this somehow
does not make sense that it is 0


*/

#include <stdint.h> /*for uint64_t*/
#include <sys/resource.h> /*rusage*/
#include <sys/times.h>
#include <sys/types.h>


typedef enum {
	SECONDS = 1,
	MICROSECONDS,
	MILLISECONDS,
	CLOCKTICKS
} unit_t;

double MHZ;  /*holds the value of the processor clock in MHz when VALID is set*/
int VALID = 0;

/*
- This gets the current count of the processors time stamp counter
For rdtsc macro it expects 64 bit integer so pass in a uint64_t and let the system
 types.h figure out what it really is*/
 #if defined(__ia64__)
#define rdtsc(v) do { \
	do { \
		asm volatile("mov %0 =ar.itc" : "=r" (v) :: "memory"); \
	} while (__builtin_expect((int32_t)(v) == -1, 0)); \
} while (0)
#elif defined(__x86_64__)
/* not sure why the =A forumlation fails, but it does with odd crashes */
#define rdtsc(v) do { \
	unsigned int __a, __d; \
	asm volatile("rdtsc" : "=a" (__a), "=d" (__d)); \
	(v) = ((unsigned long)__a) | (((unsigned long)__d)<<32); \
} while(0)
#elif defined(__i386__)
#define rdtsc(v) do { \
	asm volatile("rdtsc" : "=A" (v)); \
} while (0)
#else
#  error "Definition for rdtsc needed for your architecture"
#endif


#define perfmon_output 1  /*Set to 1 to print out information, 0 for silent*/
#define ERROR if(perfmon_output)printf

/*Function Prototypes*/

/*uint64_t rdtsc();             Macro defined above*/
double get_mhz(void);
double elapsed_wall_time(uint64_t start, uint64_t stop, unit_t unit);
long kernel_context_count(void);
clock_t get_cpu_time(void);
double elapsed_cpu_time(clock_t cpu_start, clock_t cpu_stop, unit_t unit);


/*Function Definitions*/
double get_mhz()
/*Will return the processor speed in mhz - This is not clock ticks per second!
Just call this function one time and save the variable it won't change throughout the run
of the program*/
{
    FILE *fp;
    char s[1024], *cp;
    int found = 0;
	double my_mhz;


    if (!(fp = fopen("/proc/cpuinfo", "r"))){
	printf("can not open /proc/cpuinfo\n");
	exit(1);
    }
    while (fgets(s, sizeof(s), fp)) {
	if (!strncmp(s, "cpu MHz", 7)) {
	    found = 1;
	    for (cp=s; *cp && *cp != ':'; cp++) ;
	    if (!*cp){
		printf("no colon found in string\n");
		exit(1);
	    }
	    ++cp;
	    if (sscanf(cp, "%lf", &my_mhz) != 1){
		printf("scanf got no value\n ");
		exit(1);
	    }
	}
    }
    if (!found){
	printf("no line \"cpu MHz\" found\n");
//	exit(1);
	my_mhz = 1000;
    }
    fclose(fp);



    return my_mhz;
}

static inline double get_clock_rate(void)
/*This will return the number of clock ticks per second
will only call the get_mhz helper function one time
subsuquent calls to this function are ok then*/
{
	if(VALID)
		return MHZ;
	else{
		MHZ = get_mhz();
		VALID = 1;
		return MHZ;
	}

}

double elapsed_wall_time(uint64_t start, uint64_t stop, unit_t unit)
/*Get the elapsed time in specified units*/
{
	double delta;

	switch (unit) {
		case SECONDS:
			delta = (double) (stop - start) / get_clock_rate() / 1000000;
		break;

		case MILLISECONDS:
			delta = (double) (stop - start) / get_clock_rate() / 1000;
		break;

		case MICROSECONDS:
			delta = (double) (stop - start) / get_clock_rate();
		break;

		default: /*clock ticks*/
			delta = (double) (stop - start);
		break;
	}

	return delta;

}

long kernel_context_count()
/*gets a count of the context switches the kernel has had to go through
so far in the current process - Includes child process context switches
as well
Input: void
Output: number context switches, if error return -1
*/
{
	struct rusage *usage;
	struct rusage *c_usage;

	usage = malloc(sizeof(struct rusage));
	c_usage = malloc(sizeof(struct rusage));

	int ret;

	ret = getrusage(RUSAGE_SELF, usage);
	if(ret != 0){
		ERROR("getrusage failed for self\n");
		return -1;
	}

	ret = getrusage(RUSAGE_SELF, c_usage);
	if(ret != 0){
		ERROR("getrusage failed for child proccesses\n");
		return -1;
	}

	return(usage->ru_nvcsw + usage->ru_nivcsw + c_usage->ru_nvcsw + c_usage->ru_nivcsw);

}

clock_t get_cpu_time()
/*Determine the time the calling process has spent on the CPU*/
{
	struct tms *time_struct;
	clock_t u_time, s_time, err;


	time_struct = malloc(sizeof(struct tms));

	err = times(time_struct);

	if(err < 0){
		ERROR("times() failed\n");
		return -1;
	}

	u_time = time_struct->tms_utime + time_struct->tms_cutime;
	s_time = time_struct->tms_stime + time_struct->tms_cstime;

	printf("%ld utime is \n",u_time);

	return (u_time + s_time);


}


double elapsed_cpu_time(clock_t cpu_start, clock_t cpu_stop, unit_t unit)
/*return the amount of time spent on the processor as a double*/
{
	double delta;

	printf("stop - start = %f, clock rate is %f\n", (double)(cpu_stop - cpu_start), get_clock_rate());


	switch (unit) {
		case SECONDS:
			delta = (double) (cpu_stop - cpu_start) / get_clock_rate() / 1000000;
		break;

		case MILLISECONDS:
			delta = (double) (cpu_stop - cpu_start) / get_clock_rate() / 1000;
		break;

		case MICROSECONDS:
			delta = (double) (cpu_stop - cpu_start) / get_clock_rate();
		break;

		default: /*clock ticks*/
			delta = (double) (cpu_stop - cpu_start);
		break;
	}

	return delta;


}





























