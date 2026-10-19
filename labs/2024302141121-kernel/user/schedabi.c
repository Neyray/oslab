/* T4-3: ABI boundaries and recovery, including a sleeping kill target. */
#include "kernel/types.h"
#include "user/user.h"
static void check(int ok,const char *s) {
  if(!ok){printf("SCHEDABI FAIL %s\n",s);exit(1);}
}
int main(void) {
  int a=-1,b=-1,st=-1;
  check(schedstat(-1,&a,&b)==0 && a>=0 && b>=0,"self");
  check(schedstat(2147483647,&a,&b)==-1,"missing PID");
  check(schedstat(-2,&a,&b)==-1,"invalid PID");
  check(schedstat(-1,(int*)main,&b)==-1,"readonly pointer");
  check(schedstat(-1,&a,(int*)~0UL)==-1,"overflow pointer");
  check(schedstat(-1,0,&b)==-1,"null points into RX code");
  check(setickets(0)==-1 && setickets(-1)==-1,"invalid tickets");
  check(setickets(1)==0 && setickets(2147483647)==0,"positive tickets");
  check(pause(-1)==-1 && pause(0)==0,"pause boundary");
  unsigned long t=time_rdtime(); int start=uptime();
  check(pause(3)==0 && uptime()-start>=3 && time_rdtime()>t,"real sleep / CSR");
  char *base=sbrk(8192); check(base!=(char*)-1,"allocate boundary");
  int *cross=(int*)(((unsigned long)base+4095UL)&~4095UL);
  cross=(int*)((char*)cross+4094);
  check(schedstat(-1,cross,&b)==0 && *cross>=0,"cross-page copyout");
  check(sbrk(-8192)!=(char*)-1,"release boundary");
  int child=fork(); check(child>=0,"fork");
  if(!child){ pause(1000);exit(99); }
  pause(2);check(schedstat(child,&a,&b)==0,"sleeping child");
  check(wait((int*)~0UL)==-1,"bad wait pointer leaves child");
  check(kill(child)==0,"kill sleeper");
  pause(2);check(schedstat(child,&a,&b)==-1,"zombie rejected");
  check(wait(&st)==child && st==-1,"wait returns killed status");
  check(kill(child)==-1 && schedstat(child,&a,&b)==-1,"reaped PID rejected");
  check(wait(0)==-1,"no children");
  for(int i=0;i<32;i++){
    child=fork();check(child>=0,"reuse slot");
    if(!child)exit(i);
    check(wait(&st)==child && st==i,"reuse exit status");
  }
  int ids[64],n=0;
  for(;n<64;n++) {
    child=fork(); if(child<0)break;
    if(!child){pause(1000);exit(99);}
    ids[n]=child;
  }
  check(n==62,"NPROC capacity including shell and test");
  for(int i=0;i<n;i++)check(kill(ids[i])==0,"kill full table");
  for(int i=0;i<n;i++)check(wait(&st)>0 && st==-1,"reap full table");
  child=fork();check(child>=0,"fork recovers after capacity error");
  if(!child)exit(23);
  check(wait(&st)==child && st==23,"recovered status");
  printf("RESULT process_capacity = %d\n",n);
  printf("SCHEDABI PASS\n");exit(0);
}
