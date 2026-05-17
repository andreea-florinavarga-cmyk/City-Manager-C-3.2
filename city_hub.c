#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

void handle_start_monitor() {
    pid_t hub_mon_pid = fork();

    if (hub_mon_pid < 0) {
        perror("fork hub_mon");
        return;
    }

    if (hub_mon_pid == 0) {
        int pipefd[2];
        if (pipe(pipefd) < 0) {
            perror("pipe");
            exit(1);
        }

        pid_t mon_pid = fork();
        if (mon_pid < 0) {
            perror("fork monitor");
            exit(1);
        }

        if (mon_pid == 0) {
            close(pipefd[0]);

            dup2(pipefd[1], STDOUT_FILENO);
            close(pipefd[1]);

            execlp("./monitor_reports", "monitor_reports", (char *)NULL);
            perror("execlp monitor_reports");
            exit(1);
        }

        close(pipefd[1]);

        FILE *stream = fdopen(pipefd[0], "r");
        char line[512];

        while (fgets(line, sizeof(line), stream)) {
            char type[16], msg[256];
            int len;

            if (sscanf(line, "%15[^|]|%d|%255[^\n]", type, &len, msg) == 3) {
                if (strcmp(type, "ERR") == 0) {
                    printf("\n[MONITOR EROARE] %s\ncity_hub> ", msg);
                    fflush(stdout);
                    break;
                } else if (strcmp(type, "EXIT") == 0) {
                    printf("\n[MONITOR EXIT] %s\ncity_hub> ", msg);
                    fflush(stdout);
                    break;
                } else {
                    printf("\n[MONITOR %s] %s\ncity_hub> ", type, msg);
                    fflush(stdout);
                }
            }
        }

        fclose(stream);
        waitpid(mon_pid, NULL, 0);
        printf("\n[INFO] Procesul monitor (PID %d) a fost finalizat definitiv.\ncity_hub> ", mon_pid);
        fflush(stdout);
        exit(0);
    }

    printf("Procesul de background 'hub_mon' creat cu PID %d.\n", hub_mon_pid);
}

void handle_calculate_scores(char* args) {
    char* districts[50];
    int count = 0;

    char* token = strtok(args, " \n");
    while (token && count < 50) {
        districts[count++] = token;
        token = strtok(NULL, " \n");
    }

    if (count == 0) {
        printf("Eroare: Trebuie specificat cel putin un district.\n");
        return;
    }

    int pipes[50][2];
    pid_t pids[50];

    for (int i = 0; i < count; i++) {
        if (pipe(pipes[i]) < 0) {
            perror("pipe");
            return;
        }

        pids[i] = fork();
        if (pids[i] < 0) {
            perror("fork");
            return;
        }

        if (pids[i] == 0) {
            close(pipes[i][0]);

            dup2(pipes[i][1], STDOUT_FILENO);
            close(pipes[i][1]);

            execlp("./scorer", "scorer", districts[i], (char *)NULL);
            perror("execlp scorer");
            exit(1);
        }

        close(pipes[i][1]);
    }

    printf("\n=== RAPORT GLOBAL AL VOLUMULUI DE MUNCA ===\n");

    for (int i = 0; i < count; i++) {
        char buf[1024];
        ssize_t n;

        while ((n = read(pipes[i][0], buf, sizeof(buf) - 1)) > 0) {
            buf[n] = '\0';
            printf("%s", buf);
        }

        close(pipes[i][0]);
        waitpid(pids[i], NULL, 0);
    }
    printf("===========================================\n");
}

int main() {
    char input[256];

    printf("--- City Hub Interface ---\n");
    printf("Comenzi valabile:\n");
    printf("  start_monitor\n");
    printf("  calculate_scores <district1> <district2> ...\n");
    printf("  exit\n\n");

    while (1) {
        printf("city_hub> ");

        if (!fgets(input, sizeof(input), stdin)) break;

        input[strcspn(input, "\n")] = '\0';

        if (strlen(input) == 0) continue;

        if (strcmp(input, "exit") == 0 || strcmp(input, "quit") == 0) {
            break;
        } else if (strcmp(input, "start_monitor") == 0) {
            handle_start_monitor();
        } else if (strncmp(input, "calculate_scores", 16) == 0) {
            handle_calculate_scores(input + 16);
        } else {
            printf("Comanda necunoscuta.\n");
        }
    }

    return 0;
}