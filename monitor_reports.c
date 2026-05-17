#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <string.h>
#include <time.h>
#include <errno.h>

static int flag_sigint  = 0;
static int flag_sigusr1 = 0;

static void handle_sigint(int sig) {
    (void)sig;
    flag_sigint = 1;
}

static void handle_sigusr1(int sig) {
    (void)sig;
    flag_sigusr1 = 1;
}
/*
void send_message(const char *type,const char *msg) {

}
*/

int main(void) {
    struct sigaction sa_int, sa_usr1;

    sa_int.sa_handler = handle_sigint;
    sigemptyset(&sa_int.sa_mask);
    sa_int.sa_flags = 0;
    sigaction(SIGINT, &sa_int, NULL);

    sa_usr1.sa_handler = handle_sigusr1;
    sigemptyset(&sa_usr1.sa_mask);
    sa_usr1.sa_flags = 0;
    sigaction(SIGUSR1, &sa_usr1, NULL);

    int fd = open(".monitor_pid", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) { perror("open .monitor_pid"); return 1; }
    char buf[32];
    int len = snprintf(buf, sizeof(buf), "%d\n", (int)getpid());
    write(fd, buf, len);
    close(fd);

    printf("Monitor pornit cu PID-ul %d. Astept evenimente...\n", (int)getpid());
    fflush(stdout);

    while (!flag_sigint) {
        pause();
        if (flag_sigusr1) {
            flag_sigusr1 = 0;
            printf("Monitor: A fost adaugat un raport nou!\n");
            fflush(stdout);
        }
    }

    printf("\nMonitor: Am primit SIGINT. Se inchide...\n");
    fflush(stdout);
    unlink(".monitor_pid");
    return 0;
}