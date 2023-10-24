#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <linux/spi/spidev.h>

#define BUFFER_MAX 3
#define DIRECTION_MAX 45

#define IN  0
#define OUT 1
#define PWM  2

#define LOW 0
#define HIGH 1
#define VALUE_MAX 256

#define VIN 18
#define PIN 20

#define ARRAY_SIZE(array) sizeof(array) / sizeof(array[0])
static const char *DEVICE = "/dev/spidev0.0";
static uint8_t MODE = SPI_MODE_0;
static uint8_t BITS = 8;
static uint32_t CLOCK = 1000000;
static uint16_t DELAY = 5;


double distance = 0;
int fall_state = 0; // normal:0, fall:2
int death_state = 0; // normal:0, death:3

clock_t start, end;


void error_handling(char *message){
    fputs(message,stderr);
    fputc('\n',stderr);
    exit(1);
}


static int GPIOExport(int pin) 
{
    char buffer[VALUE_MAX];
    ssize_t bytes_written;
    int fd;

    fd = open("/sys/class/gpio/export",O_WRONLY);
    if (-1 == fd) {
        fprintf(stderr, "Failed to open export for writing!\n");
        return(-1);
    }

    bytes_written = snprintf(buffer, BUFFER_MAX, "%d", pin);
    write(fd, buffer, bytes_written);
    close(fd);
    return(0);
}

static int GPIOUnexport(int pin) {
    char buffer[BUFFER_MAX];
    ssize_t bytes_written;
    int fd;

    fd = open("/sys/class/gpio/unexport", O_WRONLY);
    if(-1 == fd) {
        fprintf(stderr, "Failed to open unexport for writing!\n");
        return(-1);
    }
    bytes_written = snprintf(buffer, BUFFER_MAX, "%d", pin);
    write(fd, buffer, bytes_written);
    close(fd);
    return(0);
}

static int GPIODirection(int pin, int dir) {
    static const char s_directions_str[] = "in\0out";

    char path[DIRECTION_MAX]="/sys/class/gpio/gpio%d/direction";
    int fd;

    snprintf(path, DIRECTION_MAX, "/sys/class/gpio/gpio%d/direction", pin);

    fd = open(path, O_WRONLY);
    if (-1 == fd) {
        fprintf(stderr, "Failed to open gpio direction for writing!\n");
        return(-1);
    }

    if (-1 == write(fd, &s_directions_str[IN == dir ? 0:3], IN == dir ? 2 : 3)){
        fprintf(stderr, "Failed to set direction!\n");
        return(-1);
    }

    close(fd);
    return(0);
}

static int GPIORead(int pin) {
    char path[VALUE_MAX];
    char value_str[3];
    int fd;

    snprintf(path, VALUE_MAX, "/sys/class/gpio/gpio%d/value", pin);
    fd = open(path, O_RDONLY);
    if (-1 == fd) {
        fprintf(stderr, "Failed to open gpio value for reading!\n");
        return (-1);
    }

    if (-1 == read(fd, value_str, 3)) {
        fprintf(stderr, "Failed to read value!\n");
        return(-1);
    }

    close(fd);

    return(atoi(value_str));
}


static int GPIOWrite(int pin, int value) {
    static const char s_values_str[] = "01";
    char path[VALUE_MAX];
    int fd;

    snprintf(path, VALUE_MAX, "/sys/class/gpio/gpio%d/value", pin);
    fd = open(path, O_WRONLY);
    if (-1 == fd) {
        fprintf(stderr, "Failed to open gpio value for writing!\n");
        return(-1);
    }

    if(1 != write(fd, &s_values_str[LOW == value ? 0 : 1], 1)) {
        fprintf(stderr, "Failed to write value!\n");
        return(-1);
    }

    close(fd);
    return(0);
}

// ADC
static int prepare(int fd)
{

    if (ioctl(fd, SPI_IOC_WR_MODE, &MODE) == -1)
    {
        perror("Can't set MODE");
        return -1;
    }

    if (ioctl(fd, SPI_IOC_WR_BITS_PER_WORD, &BITS) == -1)
    {
        perror("Can't set number of BITS");
        return -1;
    }

    if (ioctl(fd, SPI_IOC_WR_MAX_SPEED_HZ, &CLOCK) == -1)
    {
        perror("Can't set write CLOCK");
        return -1;
    }

    if (ioctl(fd, SPI_IOC_RD_MAX_SPEED_HZ, &CLOCK) == -1)
    {
        perror("Can't set read CLOCK");
        return -1;
    }

    return 0;
}

uint8_t control_bits_differential(uint8_t channel)
{
    return (channel & 7) << 4;
}

uint8_t control_bits(uint8_t channel)
{
    return 0x8 | control_bits_differential(channel);
}

int readadc(int fd, uint8_t channel)
{
    uint8_t tx[] = {1, control_bits(channel), 0};
    uint8_t rx[3];

    struct spi_ioc_transfer tr = {
        .tx_buf = (unsigned long)tx,
        .rx_buf = (unsigned long)rx,
        .len = ARRAY_SIZE(tx),
        .delay_usecs = DELAY,
        .speed_hz = CLOCK,
        .bits_per_word = BITS,
    };

    if (ioctl(fd, SPI_IOC_MESSAGE(1), &tr) == 1)
    {
        perror("IO Error");
        abort();
    }

    return ((rx[1] << 8) & 0x300) | (rx[2] & 0xFF);
}


void *vibration_thread_function(){

    //Enable GPIO pins
    if (-1 == GPIOExport(VIN)) {
        printf("gpio export err\n");
        exit(0);
    }
    // wait for writing to export file
    usleep(100000);

    //Set GPIO directions
    if (-1 == GPIODirection(VIN, IN)) {
        printf("gpio direction err\n");
        exit(0);
    }

    // start
    while(1) {

        fall_state = 0;

        if(GPIORead(VIN) == 1){
            fall_state = 2;
        }

        sleep(1);
    }

}

void *pir_thread_function()
{
    int pir;

    int fd = open(DEVICE, O_RDWR);
    if (fd <= 0)
    {
        printf("Device %s not found\n", DEVICE);
        exit(0);
    }

    if (prepare(fd) == -1)
    {
        exit(0);
    }

    end = clock();
    while (1)
    {

        pir = readadc(fd, 0);

        printf("%d\n", pir);

        if (pir < 10){
            start = clock();
            death_state = 0;
        }
        else{
            printf("reset\n");
            end = clock();
        }

        //printf("%lf\n", (double)start-end);
        if((double)start-end > 300000) {// millisecond, 5 minute
            death_state = 3;
        }
        
        sleep(1);

    }

    close(fd);
}


int main(int argc, char *argv[]) {

    start = clock();

    pthread_t vibration_thread;
    int vibration_tid;
    int vibration_status;

    // start sensor thread
    vibration_tid = pthread_create(&vibration_thread, NULL, vibration_thread_function, &vibration_status);
    if(vibration_tid < 0){
        perror("vibration thread create error : ");
        exit(0);
    }
    printf("vibration thread started\n");


    pthread_t pir_thread;
    int pir_tid;
    int pir_status;

    // start sensor thread
    pir_tid = pthread_create(&pir_thread, NULL, pir_thread_function, &pir_status);
    if(pir_tid < 0){
        perror("pir thread create error : ");
        exit(0);
    }
    printf("pir thread started\n");

    int sock;
    struct sockaddr_in serv_addr;
    char msg[3];
    
    if(argc!=3){
        printf("Usage : %s <IP> <port>\n",argv[0]);
        exit(1);
    }

    sock = socket(PF_INET, SOCK_STREAM, 0);
    if(sock == -1)
        error_handling("socket() error");
    
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = inet_addr(argv[1]);
    serv_addr.sin_port = htons(atoi(argv[2]));  
    
    if(connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr))==-1)
        error_handling("connect() error");


    while(1){
        
        printf("Fall: %d\n", fall_state);

        printf("Motion: %d\n", death_state);
        

        if (fall_state == 2) {
            write(sock, "2", 2);
        }

        if (death_state == 3) {
            write(sock, "3", 2);
        }
        
        sleep(1);
    }

    //Disable GPIO pins
    if (-1 == GPIOUnexport(VIN))
        return(-1);

    return 0;
}
