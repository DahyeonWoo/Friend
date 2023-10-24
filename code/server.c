#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <stdio.h>
#include <wiringPi.h>
#include <softTone.h>
#include <pthread.h>

#define BUF_SIZE 30
#define IN 0
#define OUT 1
#define PWM 2
#define PIN 17
#define PIN2 23
#define LOW 0
#define HIGH 1
#define VALUE_MAX 256
#define ENABLE "1"
#define UNENABLE "0"
#define BTN_L 0
#define BTN_R 1
#define BuzzPin 7

const int Melody[8] = {523, 587, 659, 689, 784, 880, 988, 1047};
const int NomotionMelody[8] = {523, 587, 659, 689, 784, 880, 988, 1047};

static int
GPIOExport(int pin)
{
#define BUFFER_MAX 3
    char buffer[BUFFER_MAX];
    ssize_t bytes_written;
    int fd;

    fd = open("/sys/class/gpio/export", O_WRONLY);
    if (-1 == fd)
    {
        fprintf(stderr, "Failed to open export for writing!\n");
        return (-1);
    }

    bytes_written = snprintf(buffer, BUFFER_MAX, "%d", pin);
    write(fd, buffer, bytes_written);
    close(fd);
    return (0);
}

static int
GPIOUnexport(int pin)
{
    char buffer[BUFFER_MAX];
    ssize_t bytes_written;
    int fd;

    fd = open("/sys/class/gpio/unexport", O_WRONLY);
    if (-1 == fd)
    {
        fprintf(stderr, "Failed to open unexport for writing!\n");
        return (-1);
    }

    bytes_written = snprintf(buffer, BUFFER_MAX, "%d", pin);
    write(fd, buffer, bytes_written);
    close(fd);
    return (0);
}

static int
GPIODirection(int pin, int dir)
{
    static const char s_directions_str[] = "in\0out";

#define DIRECTION_MAX 35
    char path[DIRECTION_MAX];
    int fd;

    snprintf(path, DIRECTION_MAX, "/sys/class/gpio/gpio%d/direction", pin);
    fd = open(path, O_WRONLY);
    if (-1 == fd)
    {
        fprintf(stderr, "Failed to open gpio direction for writing!\n");
        return (-1);
    }

    if (-1 == write(fd, &s_directions_str[IN == dir ? 0 : 3], IN == dir ? 2 : 3))
    {
        fprintf(stderr, "Failed to set direction!\n");
        return (-1);
    }

    close(fd);
    return (0);
}

static int
GPIORead(int pin)
{
    char path[VALUE_MAX];
    char value_str[3];
    int fd;

    snprintf(path, VALUE_MAX, "/sys/class/gpio/gpio%d/value", pin);

    fd = open(path, O_RDONLY);
    if (-1 == fd)
    {
        fprintf(stderr, "Failed to open gpio value for reading!\n");
        return (-1);
    }

    if (-1 == read(fd, value_str, 3))
    {
        fprintf(stderr, "Failed to read value!\n");
        close(fd);
        return (-1);
    }

    close(fd);

    return (atoi(value_str));
}

static int
GPIOWrite(int pin, int value)
{
    static const char s_values_str[] = "01";

    char path[VALUE_MAX];
    int fd;

    snprintf(path, VALUE_MAX, "/sys/class/gpio/gpio%d/value", pin);

    fd = open(path, O_WRONLY);
    if (-1 == fd)
    {
        fprintf(stderr, "Failed to open gpio value for writing!\n");
        return (-1);
    }

    if (1 != write(fd, &s_values_str[LOW == value ? 0 : 1], 1))
    {
        fprintf(stderr, "Failed to write value!\n");
        close(fd);
        return (-1);
    }

    close(fd);
    return (0);
}

static int
PWMExport(int pwmnum)
{
#define BUFFER_MAX 3

    char buffer[BUFFER_MAX];
    int bytes_written;
    int fd;

    fd = open("/sys/class/pwm/pwmchip0/unexport", O_WRONLY);
    if (-1 == fd)
    {
        fprintf(stderr, "Failed to open in unexport!\n");
        return -1;
    }

    bytes_written = snprintf(buffer, BUFFER_MAX, "%d", pwmnum);
    write(fd, buffer, bytes_written);
    close(fd);

    fd = open("/sys/class/pwm/pwmchip0/export", O_WRONLY);

    if (-1 == fd)
    {
        fprintf(stderr, "Failed to open in export!\n");
        return -1;
    }

    bytes_written = snprintf(buffer, BUFFER_MAX, "%d", pwmnum);
    write(fd, buffer, bytes_written);
    close(fd);

    sleep(1);

    return 0;
}

static int
PWMEnable(int pwmnum, int value)
{
#define DIRECTION_MAX 45

    static const char s_unenable_str[] = "0";
    static const char s_enable_str[] = "1";

    char path[DIRECTION_MAX];
    int fd;

    snprintf(path, DIRECTION_MAX, "/sys/class/pwm/pwmchip0/pwm%d/enable", pwmnum);
    fd = open(path, O_WRONLY);

    if (-1 == fd)
    {
        fprintf(stderr, "Failed to open in enable!\n");
        return -1;
    }

    if (value == 0)
    {
        write(fd, s_unenable_str, strlen(s_unenable_str));
        close(fd);
    }
    else
    {
        write(fd, s_enable_str, strlen(s_enable_str));
        close(fd);
    }

    fd = open(path, O_WRONLY);
    if (-1 == fd)
    {
        fprintf(stderr, "Failed to open in enable!\n");
        return (-1);
    }

    write(fd, s_enable_str, strlen(s_enable_str));
    close(fd);

    return 0;
}

static int
PWMUnexport(int pwmnum)
{
#define BUFFER_MAX 3

    char buffer[BUFFER_MAX];
    int bytes_written;
    int fd;

    fd = open("/sys/class/pwm/pwmchip0/unexport", O_WRONLY);
    if (-1 == fd)
    {
        fprintf(stderr, "Failed to open in unexport!\n");
        return -1;
    }

    bytes_written = snprintf(buffer, BUFFER_MAX, "%d", pwmnum);
    write(fd, buffer, bytes_written);
    close(fd);
}

static int
PWMWritePeriod(int pwmnum, int value)
{
    char s_values_str[VALUE_MAX];
    char path[VALUE_MAX];
    int fd, byte;

    snprintf(path, VALUE_MAX, "/sys/class/pwm/pwmchip0/pwm%d/period", pwmnum);
    fd = open(path, O_WRONLY);
    if (-1 == fd)
    {
        fprintf(stderr, "Failed to open in period!\n");
        return -1;
    }

    byte = snprintf(s_values_str, 10, "%d", value);

    if (-1 == write(fd, s_values_str, byte))
    {
        close(fd);
        return -1;
    }

    close(fd);
    return 0;
}

static int
PWMWriteDutyCycle(int pwmnum, int value)
{
    char path[VALUE_MAX];
    int s_values_str[VALUE_MAX];
    int fd, byte;

    snprintf(path, VALUE_MAX, "/sys/class/pwm/pwmchip0/pwm%d/duty_cycle", pwmnum);
    fd = open(path, O_WRONLY);
    if (-1 == fd)
    {
        fprintf(stderr, "Failed to open in duty_cycle!\n");
        return -1;
    }

    byte = snprintf(s_values_str, 10, "%d", value);

    if (-1 == write(fd, s_values_str, byte))
    {
        fprintf(stderr, "Failed to write value! in duty_cycle\n");
        close(fd);
        return -1;
    }

    close(fd);
    return 0;
}

void error_handling(char *message)
{
    fputs(message, stderr);
    fputc('\n', stderr);
    exit(1);
}

void read_childproc(int sig)
{
    pid_t pid;
    int status;

    pid = waitpid(-1, &status, WNOHANG);
    printf("removed proc id: %d \n", pid);
}

int send_msg(int warning_type)
{

    return 0;
}

void *pwm_thd(void *data)
{
    printf("light on\n");
    PWMWritePeriod(0, 500000000);
    PWMWriteDutyCycle(0, 10000000);
    PWMEnable(0, 0);
}

int main(int argc, char *argv[])
{
    int serv_sock, clnt_sock;
    struct sockaddr_in serv_addr, clnt_addr;

    PWMExport(0);

    pid_t pid;
    struct sigaction act;
    socklen_t addr_sz;
    int str_len, state;
    char buf[BUF_SIZE];
    char fire[BUF_SIZE] = "1";
    char fallen[BUF_SIZE] = "2";
    char nomotion[BUF_SIZE] = "3";
    pthread_t p_thread;
    int thr_id;
    int status;
    char p1[] = "thread_1";
    char p2[] = "thread_2";

    if (argc != 2)
    {
        printf("Usage : %s <port>\n", argv[0]);
        exit(1);
    }

    act.sa_handler = read_childproc;
    sigemptyset(&act.sa_mask);
    act.sa_flags = 0;
    state = sigaction(SIGCHLD, &act, 0);

    serv_sock = socket(PF_INET, SOCK_STREAM, 0);
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(atoi(argv[1]));

    if (bind(serv_sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) == -1)
        error_handling("bind() error");

    if (listen(serv_sock, 5) == -1)
        error_handling("listen() error");

    if (wiringPiSetup() == -1)
    {
        printf("wiringPi initialized failed\n");
        return 1;
    }

    pinMode(BuzzPin, OUTPUT);

    while (1)
    {

        addr_sz = sizeof(clnt_addr);
        clnt_sock = accept(serv_sock, (struct sockaddr *)&clnt_addr, &addr_sz);

        if (clnt_sock == -1)
            continue;
        else
            puts("new client connected...");

        pid = fork();

        if (pid == -1)
        {
            close(clnt_sock);
            continue;
        }

        if (pid == 0)
        {
            close(serv_sock);
            while ((str_len = read(clnt_sock, buf, BUF_SIZE)) != 0)
            {
                printf("from client: %s\n", buf);

                if (strcmp(buf, fire) == 0)
                {
                    thr_id = pthread_create(&p_thread, NULL, pwm_thd, (void *)p1);
                    if (thr_id < 0)
                    {
                        perror("thread create error : ");
                        exit(0);
                    }

                    while (1)
                    {

                        digitalWrite(BuzzPin, HIGH);
                        printf("buzzer on-fire\n");
                        delay(100);

                        softToneCreate(BuzzPin);

                        while (1)
                        {
                            int i;
                            for (i = 0; i < 8; i++)
                                if (Melody[i] % 2 == 0)
                                {
                                    softToneWrite(BuzzPin, Melody[i]);
                                    delay(1000);
                                }

                            softToneWrite(BuzzPin, 0);
                            delay(1000);
                            break;
                        }

                        digitalWrite(BuzzPin, LOW);
                        printf("buzzer off-fire\n");
                        break;
                    }
                    printf("light off\n");
                    PWMExport(0);
                }

                if (strcmp(buf, nomotion) == 0)
                {
                    thr_id = pthread_create(&p_thread, NULL, pwm_thd, (void *)p1);
                    if (thr_id < 0)
                    {
                        perror("thread create error : ");
                        exit(0);
                    }

                    while (1)
                    {

                        digitalWrite(BuzzPin, HIGH);
                        printf("buzzer on-nomotion\n");
                        delay(100);

                        softToneCreate(BuzzPin);

                        while (1)
                        {
                            int i;
                            for (i = 0; i < 8; i++)
                                if (Melody[i] % 2 == 1)
                                {
                                    softToneWrite(BuzzPin, Melody[i]);
                                    delay(1000);
                                }

                            softToneWrite(BuzzPin, 0);
                            delay(1000);
                            break;
                        }

                        digitalWrite(BuzzPin, LOW);
                        printf("buzzer off-nomotion num\n ");
                        break;
                    }
                    printf("light off\n");
                    PWMExport(0);
                }

                if (strcmp(buf, fallen) == 0)
                {
                    thr_id = pthread_create(&p_thread, NULL, pwm_thd, (void *)p1);
                    if (thr_id < 0)
                    {
                        perror("thread create error : ");
                        exit(0);
                    }

                    while (1)
                    {

                        digitalWrite(BuzzPin, HIGH);
                        printf("buzzer on-fall melody\n");
                        delay(100);

                        softToneCreate(BuzzPin);

                        while (1)
                        {
                            int i;
                            for (i = 0; i < 8; i++)
                                if (Melody[i] % 2 == 1)
                                {
                                    softToneWrite(BuzzPin, Melody[i]);
                                    delay(1000);
                                }

                            softToneWrite(BuzzPin, 0);
                            delay(1000);
                            break;
                        }

                        digitalWrite(BuzzPin, LOW);
                        printf("buzzer off-fall melody\n ");
                        break;
                    }
                    printf("light off\n");
                    PWMExport(0);
                }
            }

            close(clnt_sock);
            puts("client disconnected...");

            return 0;
        }

        else
            close(clnt_sock);
    }

    close(serv_sock);

    return 0;
}
