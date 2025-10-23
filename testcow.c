#include "types.h"
#include "stat.h"
#include "user.h"
#include "fs.h"

int var;

int
main()
{
	printf(1,"Parent start: no. of free page = %d\n",getNumFreePages());
	if(fork()==0)
	{
		printf(1,"Child1: no. of free pages after fork = %d\n",getNumFreePages());
		var = 15;
		printf(1,"Child1: no. of free page after modifying the global variable = %d\n",getNumFreePages());
		exit();
	}
	if(fork()==0)
	{
		printf(1,"Child2: no. of free page just after fork = %d\n",getNumFreePages());
		var = 25;
		printf(1,"Child2: no. of free page after modifying the global variable = %d\n",getNumFreePages());
		exit();
	}
	wait();
	wait();
	printf(1,"Parent end: no. of free page = %d\n",getNumFreePages());
	exit();
	return 0;
}