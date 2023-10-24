#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include <pthread.h>

#define IN 0
#define OUT 1
#define LOW 0
#define HIGH 1
#define PIN1 24 //flame sensor
#define PIN2 17 //gas sesor
#define VALUE_MAX 40


void error_handling(char *message){
    fputs(message,stderr);
    fputc('\n',stderr);
    exit(1);
}

static int GPIOExport(int pin){
        #define BUFFER_MAX 3
		char buffer[BUFFER_MAX];
		ssize_t bytes_written;
		int fd;
		
		fd = open("/sys/class/gpio/export", O_WRONLY);
		if (-1 == fd){
			fprintf(stderr, "Failed to open export for writing!\n");
			return(-1);
		}
		
		bytes_written = snprintf(buffer, BUFFER_MAX,"%d", pin);
		write(fd, buffer, bytes_written);
		close(fd);
		return(0);
			
}
	
static int GPIODirection(int pin, int dir){
	static const char s_direction_str[] = "in\0out";
    int fd;
    #define DIRECTION_MAX 35

	char path[DIRECTION_MAX]="/sys/class/gpio/gpio%d/direction";
	snprintf(path, DIRECTION_MAX, "/sys/class/gpio/gpio%d/direction", pin);
	
	fd = open(path, O_WRONLY);
	if (-1==fd){
		fprintf(stderr,"Failed to open gpio direction for writing\n");
		return(-1);
		
	}
	
	if (-1 == write(fd, &s_direction_str[IN == dir ? 0 : 3 ], IN == dir ? 2 : 3)){
		fprintf(stderr,"Failed to set direction\n");
		close(fd);
		return(-1);
		
	}
	
	close(fd);
	return(0);

}
	
static int GPIOWrite(int pin, int value) {
	static const char s_values_str[]="01";
	
	char path[VALUE_MAX];
	int fd;
	
	snprintf(path, VALUE_MAX, "/sys/class/gpio/gpio%d/value", pin);
	fd = open(path, O_WRONLY);
	if (-1 == fd){
		fprintf(stderr, "Failed to open gpio value for writing\n");
		return(-1);
	}
	
	if (1!=write(fd,&s_values_str[LOW == value ? 0 :1], 1)) {
		fprintf(stderr, "failed to write value\n");
		close(fd);
		return(-1);
	}
	
	close(fd);
	return(0);
	
	
}

static int GPIORead(int pin){
	
	char path[VALUE_MAX];
	char value_str[3];
	int fd;
	
	snprintf(path, VALUE_MAX, "/sys/class/gpio/gpio%d/value",pin);
	fd = open (path, O_RDONLY);
	if (-1 == fd){
		fprintf(stderr, "Failed to open gpio value for reading %d!\n",pin);
		return(-1);
	}
	
	if (-1 == read(fd, value_str, 3)){
		fprintf(stderr, "Failed to read value\n");
		close(fd);
		return(-1);
		
	}
	close(fd);
	
	return(atoi(value_str));
	
}
			
	
static int GPIOUnexport(int pin){
	char buffer[BUFFER_MAX];
	ssize_t byte_written;
	int fd;
	
	fd=open("/sys/class/gpio/unexport",O_WRONLY);
	if (-1 == fd){
		fprintf(stderr,"Failed to open unexport for writing\n");
		return(-1);
	}
	
	byte_written = snprintf(buffer, BUFFER_MAX, "%d", pin);
	write(fd, buffer, byte_written);
	close(fd);
	return(0);
	
}

void *t_function(int *data)   //gas detect
{
	if(-1 == GPIOExport(PIN2))
		return(1);
	if(-1 == GPIODirection(PIN2,IN))
		return(2);
	
	while(1){
		
		if(GPIORead(PIN2)==0){
			*data=1;
		}
		else{
			*data=0;
		}
		
		sleep(1);
	}
}
	
int main (int argc, char *argv[]){
	

	if (-1 == GPIOExport(PIN1))
		return(1);
		
	if (-1 == GPIODirection(PIN1,IN))
		return(2);
			
	pthread_t p_thread;
	int thr_id;
	int status;
	int p1=0; //p1 is gas detector

	thr_id = pthread_create(&p_thread,NULL,t_function,&p1);
	if (thr_id<0){
		perror("thread create error : ");
		exit(0);
	}

	int sock;
	struct sockaddr_in serv_addr;
	char on[2] = "1";
	int str_len;
	int light = 0;

	
	if (argc != 3) {
		printf("Usage : %s <IP> <port>\n", argv[0]);
		exit(1);
	}

    sock = socket(PF_INET, SOCK_STREAM, 0);
    if (sock == -1)
        error_handling("socket() error");

    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = inet_addr(argv[1]);
    serv_addr.sin_port = htons(atoi(argv[2]));

    if (connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) == -1)
        error_handling("connect() error");
	
	
	while(1) {
		//PIN1 : flame Detect , p1: gas Detect
		printf("GPIO READ %d from pin %d and p1 : %d\n",GPIORead(PIN1),PIN1,p1);
		
		if (GPIORead(PIN1) == 0 && p1 == 1) { 
			write(sock, on, sizeof(on));
		}
		
		sleep(1);
	}
	
	if ((-1 ==GPIOUnexport(PIN1)) && (-1 ==GPIOUnexport(PIN2)))
		return(4);
	
	return(0);
	
}
	
