//test_file for scheduling
#include <stdio.h>		
#include <unistd.h>
#include <sys/syscall.h>
#include <stdio.h>
#include <stdlib.h>		
#include <string.h>
#include <sched.h>             
#include <sys/time.h>
#include <sys/types.h>
#include <sys/sysinfo.h>
#include <sys/time.h>
#include <ctype.h>

#define __NR_sched_setweight 380
#define __NR_sched_getweight 381

char *algorithm[] = {"SCHED_NORMAL", "SCHED_FIFO", "SCHED_RR", "SCHED_BATCH", "", "SCHED_IDLE", "SCHED_WRR"};

int main(int argc, char *argv[])
{
    
    int test_pid = getpid();
    int prioPolicy;
    int policy;
    int pid;
    int pre_policy = syscall(__NR_sched_getscheduler,test_pid);
    printf ("Current policy for this testfile is %s\n", algorithm[pre_policy]);
    printf ("PID for this testfile is %d\n", test_pid);
 
    struct sched_param param;
    
    struct timespec tp;
    printf("Please input the choice of scheduling algorithms:0-NORMAL,1-FIFO,2-RR,6-WRR: ");
    scanf("%d",&policy);

    prioPolicy = sched_get_priority_max(policy);

    printf("Please enter the id of testprocess: ");
    scanf("%d",&pid);

    pre_policy = syscall(__NR_sched_getscheduler,pid);
    printf ("Current scheduling algorithm for testprocess is %s\n", algorithm[pre_policy]);


    printf("Set testprocess's priority(1-99): ");
    scanf("%d",&prioPolicy);
    if(prioPolicy<1 || prioPolicy>99){
    	perror("Priority invalid!\n");
        return -1;
    }
    printf("Current scheduler priority for testprocess is %d\n",prioPolicy);
    param.sched_priority = prioPolicy;
    
    syscall(__NR_sched_setscheduler,pid, policy, (const struct sched_param *)&param);

    if(sched_rr_get_interval(pid, &tp) == -1){
        perror("sched_rr_get_interval() error!\n");
        return -1;
    }
    
    int cur_policy = syscall(__NR_sched_getscheduler,pid);
    printf ("pre scheduler: %s\ncur scheduler: %s\ntime slice is %.2lf", algorithm[pre_policy], algorithm[cur_policy],tp.tv_nsec/(1000000.0f));
    return 0;
}


