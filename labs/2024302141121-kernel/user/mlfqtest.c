/* T4-1/2: user-visible measurements, without reading kernel metadata. */
#include "kernel/types.h"
#include "user/user.h"
static volatile unsigned long sink;
static void check(int ok, const char *s) {
  if (!ok) { printf("MLFQTEST FAIL %s\n", s); exit(1); }
}
static void burn(void) {
  for (;;) for (int i=0; i<200000; i++) sink += i;
}
int main(void) {
  int ids[3], prev[3]={0,0,0}, base[3]={0,0,0}, mask=0, cpu, slice, status;
  for (int i=0;i<3;i++) {
    ids[i]=fork(); check(ids[i]>=0,"fork workers");
    if (!ids[i]) burn();
  }
  for (int k=-16;k<48;k++) {
    check(pause(1)==0,"sample sleep");
    for (int i=0;i<3;i++) {
      check(schedstat(ids[i],&cpu,&slice)==0,"sample live worker");
      check(cpu>=prev[i] && slice>=0 && slice<=4,"monotonic CPU / max slice");
      prev[i]=cpu;
      if(k==-1)base[i]=cpu;
      if (slice==1) mask|=1;
      if (slice==2) mask|=2;
      if (slice==4) mask|=4;
    }
  }
  for(int i=0;i<3;i++)prev[i]-=base[i];
  printf("RESULT mlfq_slices = %d workers = %d:%d:%d\n",mask,prev[0],prev[1],prev[2]);
  check(mask==7 && prev[0]>4 && prev[1]>4 && prev[2]>4,"three tiers / round robin");
  int min=prev[0],max=prev[0];
  for(int i=1;i<3;i++){if(prev[i]<min)min=prev[i];if(prev[i]>max)max=prev[i];}
  check(max-min<=8,"same-level fairness within two quanta");
  for (int i=0;i<3;i++) check(kill(ids[i])==0,"kill worker");
  for (int i=0;i<3;i++) check(wait(&status)>0 && status==-1,"reap worker");
  int bg=fork(); check(bg>=0,"fork background"); if (!bg) burn();
  pause(12); /* background has exhausted 1+2 ticks and is now in tier 2 */
  check(schedstat(bg,&cpu,&slice)==0,"background baseline"); int before=cpu;
  int interactive=fork(); check(interactive>=0,"fork interactive");
  if (!interactive) {
    int maxlag=0;
    for (int i=0;i<12;i++) {
      for (int j=0;j<1000;j++) sink+=j;
      check(schedstat(-1,&cpu,&slice)==0 && slice<=1,"interactive short slice");
      int begin=uptime(); check(pause(1)==0,"interactive sleep");
      int lag=uptime()-begin; if (lag>maxlag) maxlag=lag;
      check(lag>=1 && lag<=5,"interactive wake latency");
    }
    printf("RESULT mlfq_interactive_maxlag = %d\n",maxlag); exit(17);
  }
  printf("MLFQ interactive-ready pid=%d\n",interactive);
  check(wait(&status)==interactive && status==17,"interactive exit");
  check(schedstat(bg,&cpu,&slice)==0 && cpu>before,"background still advances");
  check(kill(bg)==0 && wait(&status)==bg && status==-1,"cleanup background");
  printf("MLFQTEST PASS\n"); exit(0);
}
