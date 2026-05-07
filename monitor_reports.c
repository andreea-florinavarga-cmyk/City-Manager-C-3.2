#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <string.h>

int keep_running = 1;
int report_added = 0;

void handle_sigint(int sig) {
    keep_running = 0;
}

void handle_sigusr1(int sig) {
    report_added = 1;
}

int main() {
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
    if (fd < 0) {
        perror("open");
        return 1;
    }

    char pid_str[32];
    int len = snprintf(pid_str, sizeof(pid_str), "%d\n", getpid());
    write(fd, pid_str, len);
    close(fd);

    printf("Monitor pornit cu PID-ul %d. Astept evenimente...\n", getpid());

    while (keep_running) {
        if (report_added) {
            printf("Monitor: A fost adaugat un raport nou!\n");
            fflush(stdout);
            report_added = 0;
        }
        pause();
    }

    printf("\nMonitor: Am primit SIGINT. Se inchide...\n");
    unlink(".monitor_pid");

    return 0;
}

