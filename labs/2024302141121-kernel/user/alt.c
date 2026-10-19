#include "kernel/types.h"
#include "user/user.h"
static volatile unsigned long sink;
static void run(char c){
  for(int i=0;i<16;i++){
    for(int j=0;j<1000000;j++) sink+=j;
    write(1,&c,1);
  }
  exit(0);
}
int main(void){
  int a=fork();if(a==0)run('A');if(a<0)exit(1);
  int b=fork();if(b==0)run('B');if(b<0)exit(1);
  int st;for(int i=0;i<2;i++)if(wait(&st)<0 || st!=0)exit(1);
  printf("\nalt done\n");exit(0);
}
