#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE   700
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <sys/wait.h>
#include <dirent.h>
#include <signal.h>

typedef struct {
    int    id;
    char   inspector[50];
    float  lat;
    float  lon;
    char   category[30];
    int    severity;
    time_t timestamp;
    char   description[256];
} Report;

void get_perms_string(mode_t mode, char *str) {
    str[0] = (mode & S_IRUSR) ? 'r' : '-';
    str[1] = (mode & S_IWUSR) ? 'w' : '-';
    str[2] = (mode & S_IXUSR) ? 'x' : '-';
    str[3] = (mode & S_IRGRP) ? 'r' : '-';
    str[4] = (mode & S_IWGRP) ? 'w' : '-';
    str[5] = (mode & S_IXGRP) ? 'x' : '-';
    str[6] = (mode & S_IROTH) ? 'r' : '-';
    str[7] = (mode & S_IWOTH) ? 'w' : '-';
    str[8] = (mode & S_IXOTH) ? 'x' : '-';
    str[9] = '\0';
}

void log_action(const char *district, const char *role, const char *user, const char *action) {
    char log_path[512];
    snprintf(log_path, sizeof(log_path), "%s/logged_district", district);

    struct stat st;
    if (stat(log_path, &st) == 0) {
        if (strcmp(role, "inspector") == 0) {
            return;
        }
    }

    int fd = open(log_path, O_WRONLY | O_APPEND | O_CREAT, 0644);
    if (fd < 0) {
        return;
    }

    time_t now = time(NULL);
    char entry[768];
    snprintf(entry, sizeof(entry), "[%ld] role=%s user=%s action=%s\n",
             (long)now, role, user, action);
    write(fd, entry, strlen(entry));
    close(fd);
}

int check_permission(const char *path, const char *role, int need_write) {
    struct stat st;
    if (stat(path, &st) != 0) {
        return 1;
    }
    mode_t m = st.st_mode;
    if (strcmp(role, "manager") == 0) {
        if (need_write) {
            return (m & S_IWUSR) ? 1 : 0;
        } else {
            return (m & S_IRUSR) ? 1 : 0;
        }
    } else {
        if (need_write) {
            return (m & S_IWGRP) ? 1 : 0;
        } else {
            return (m & S_IRGRP) ? 1 : 0;
        }
    }
}

void ensure_district(const char *district) {
    struct stat st;

    if (stat(district, &st) != 0) {
        if (mkdir(district, 0750) != 0 && errno != EEXIST) {
            perror("mkdir");
            return;
        }
    }
    chmod(district, 0750);

    char cfg_path[512];
    snprintf(cfg_path, sizeof(cfg_path), "%s/district.cfg", district);
    if (stat(cfg_path, &st) != 0) {
        int fd = open(cfg_path, O_WRONLY | O_CREAT | O_TRUNC, 0640);
        if (fd >= 0) {
            write(fd, "severity_threshold=2\n", 21);
            close(fd);
        }
    }
    chmod(cfg_path, 0640);

    char log_path[512];
    snprintf(log_path, sizeof(log_path), "%s/logged_district", district);
    if (stat(log_path, &st) != 0) {
        int fd = open(log_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd >= 0) {
            close(fd);
        }
    }
    chmod(log_path, 0644);
}

void update_symlink(const char *district) {
    char sym_name[256];
    char target[512];
    snprintf(sym_name, sizeof(sym_name), "active_reports-%s", district);
    snprintf(target,   sizeof(target),   "%s/reports.dat",    district);

    struct stat lst;
    if (lstat(sym_name, &lst) == 0) {
        unlink(sym_name);
    }
    if (symlink(target, sym_name) != 0) {
        perror("symlink");
    }
}

int parse_condition(const char *input, char *field, char *op, char *value) {
    int r = sscanf(input, "%49[^:]:%9[^:]:%99s", field, op, value);
    return r == 3;
}

int match_condition(const Report *r, const char *field, const char *op, const char *value) {
    if (strcmp(field, "severity") == 0) {
        int v = atoi(value);
        if (strcmp(op, "==") == 0) return r->severity == v;
        if (strcmp(op, "!=") == 0) return r->severity != v;
        if (strcmp(op, "<")  == 0) return r->severity <  v;
        if (strcmp(op, "<=") == 0) return r->severity <= v;
        if (strcmp(op, ">")  == 0) return r->severity >  v;
        if (strcmp(op, ">=") == 0) return r->severity >= v;
    }
    if (strcmp(field, "category") == 0) {
        int cmp = strcmp(r->category, value);
        if (strcmp(op, "==") == 0) return cmp == 0;
        if (strcmp(op, "!=") == 0) return cmp != 0;
    }
    if (strcmp(field, "inspector") == 0) {
        int cmp = strcmp(r->inspector, value);
        if (strcmp(op, "==") == 0) return cmp == 0;
        if (strcmp(op, "!=") == 0) return cmp != 0;
    }
    if (strcmp(field, "timestamp") == 0) {
        time_t v = (time_t)atol(value);
        if (strcmp(op, "==") == 0) return r->timestamp == v;
        if (strcmp(op, "!=") == 0) return r->timestamp != v;
        if (strcmp(op, "<")  == 0) return r->timestamp <  v;
        if (strcmp(op, "<=") == 0) return r->timestamp <= v;
        if (strcmp(op, ">")  == 0) return r->timestamp >  v;
        if (strcmp(op, ">=") == 0) return r->timestamp >= v;
    }
    fprintf(stderr, "Unknown field or operator: %s %s\n", field, op);
    return 0;
}

/* ----------------------------------------------------------------
 * Phase 2: Notify monitor_reports via SIGUSR1.
 * Reads PID from .monitor_pid, sends SIGUSR1.
 * Returns 1 on success, 0 on failure.
 * The log message is written by the caller (cmd_add).
 * ---------------------------------------------------------------- */
static int notify_monitor(void) {
    int fd = open(".monitor_pid", O_RDONLY);
    if (fd < 0) {
        return 0;   /* file does not exist – monitor not running */
    }

    char buf[32];
    memset(buf, 0, sizeof(buf));
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);

    if (n <= 0) {
        return 0;
    }

    pid_t pid = (pid_t)atol(buf);
    if (pid <= 0) {
        return 0;
    }

    if (kill(pid, SIGUSR1) != 0) {
        return 0;
    }

    return 1;
}

void cmd_add(const char *district, const char *role, const char *user) {
    ensure_district(district);

    char dat_path[512];
    snprintf(dat_path, sizeof(dat_path), "%s/reports.dat", district);

    if (!check_permission(dat_path, role, 1)) {
        fprintf(stderr, "ERROR: Role '%s' does not have write permission on %s\n", role, dat_path);
        return;
    }

    int next_id = 1;
    struct stat st;
    if (stat(dat_path, &st) == 0 && st.st_size > 0) {
        next_id = (int)(st.st_size / sizeof(Report)) + 1;
    }

    Report r;
    memset(&r, 0, sizeof(Report));
    r.id        = next_id;
    r.timestamp = time(NULL);
    r.lat       = 0.0f;
    r.lon       = 0.0f;
    strncpy(r.inspector, user, sizeof(r.inspector) - 1);

    printf("=== New Report (ID: %d) ===\n", r.id);

    printf("Category (road/lighting/flooding/other): ");
    if (fgets(r.category, sizeof(r.category), stdin)) {
        r.category[strcspn(r.category, "\n")] = '\0';
    }

    printf("Severity (1=minor, 2=moderate, 3=critical): ");
    scanf("%d", &r.severity);
    getchar();
    if (r.severity < 1 || r.severity > 3) {
        fprintf(stderr, "ERROR: Severity must be 1, 2 or 3.\n");
        return;
    }

    printf("GPS Latitude: ");
    scanf("%f", &r.lat);
    printf("GPS Longitude: ");
    scanf("%f", &r.lon);
    getchar();

    printf("Description: ");
    if (fgets(r.description, sizeof(r.description), stdin)) {
        r.description[strcspn(r.description, "\n")] = '\0';
    }

    int fd = open(dat_path, O_WRONLY | O_APPEND | O_CREAT, 0664);
    if (fd < 0) {
        perror("open reports.dat");
        return;
    }

    ssize_t written = write(fd, &r, sizeof(Report));
    close(fd);

    if (written != sizeof(Report)) {
        fprintf(stderr, "ERROR: Failed to write report.\n");
        return;
    }

    chmod(dat_path, 0664);
    update_symlink(district);

    /* Phase 2: notify monitor and log the outcome */
    int notified = notify_monitor();
    char action[128];
    if (notified) {
        snprintf(action, sizeof(action),
                 "add report ID=%d; monitor notified via SIGUSR1", r.id);
    } else {
        snprintf(action, sizeof(action),
                 "add report ID=%d; monitor could not be informed (not running or error)", r.id);
    }
    log_action(district, role, user, action);

    printf("Report %d added to district '%s'.\n", r.id, district);
    if (notified) {
        printf("Monitor process notified (SIGUSR1 sent).\n");
    } else {
        printf("WARNING: Monitor process could not be notified.\n");
    }
}

void cmd_list(const char *district, const char *role, const char *user) {
    char dat_path[512];
    char sym_name[256];
    snprintf(dat_path, sizeof(dat_path), "%s/reports.dat", district);
    snprintf(sym_name, sizeof(sym_name), "active_reports-%s", district);

    struct stat lst, st;
    if (lstat(sym_name, &lst) == 0) {
        if (S_ISLNK(lst.st_mode)) {
            if (stat(sym_name, &st) != 0) {
                printf("WARNING: Dangling symlink '%s' (target does not exist)\n", sym_name);
            }
        }
    }

    if (!check_permission(dat_path, role, 0)) {
        fprintf(stderr, "ERROR: Role '%s' does not have read permission on %s\n", role, dat_path);
        return;
    }

    if (stat(dat_path, &st) == 0) {
        char perms[11];
        get_perms_string(st.st_mode, perms);
        printf("File      : %s\n", dat_path);
        printf("Perms     : %s\n", perms);
        printf("Size      : %ld bytes\n", (long)st.st_size);
        printf("Modified  : %s", ctime(&st.st_mtime));
        printf("Records   : %ld\n", (long)(st.st_size / sizeof(Report)));
        printf("---------------------------------------------------\n");
    } else {
        printf("No reports file found for district '%s'.\n", district);
        return;
    }

    int fd = open(dat_path, O_RDONLY);
    if (fd < 0) {
        perror("open");
        return;
    }

    Report r;
    int count = 0;
    while (read(fd, &r, sizeof(Report)) == sizeof(Report)) {
        char ts_buf[32];
        struct tm *tm_info = localtime(&r.timestamp);
        strftime(ts_buf, sizeof(ts_buf), "%Y-%m-%d %H:%M:%S", tm_info);
        printf("[%d] Category: %-12s | Sev: %d | Inspector: %-20s | %s\n",
               r.id, r.category, r.severity, r.inspector, ts_buf);
        count++;
    }
    close(fd);

    if (count == 0) {
        printf("(no reports)\n");
    }

    log_action(district, role, user, "list");
}

void cmd_view(const char *district, const char *role, const char *user, int report_id) {
    char dat_path[512];
    snprintf(dat_path, sizeof(dat_path), "%s/reports.dat", district);

    if (!check_permission(dat_path, role, 0)) {
        fprintf(stderr, "ERROR: Role '%s' does not have read permission on %s\n", role, dat_path);
        return;
    }

    int fd = open(dat_path, O_RDONLY);
    if (fd < 0) {
        perror("open");
        return;
    }

    Report r;
    int found = 0;
    while (read(fd, &r, sizeof(Report)) == sizeof(Report)) {
        if (r.id == report_id) {
            found = 1;
            char ts_buf[64];
            struct tm *tm_info = localtime(&r.timestamp);
            strftime(ts_buf, sizeof(ts_buf), "%Y-%m-%d %H:%M:%S", tm_info);
            printf("=== Report %d ===\n", r.id);
            printf("Inspector  : %s\n", r.inspector);
            printf("Category   : %s\n", r.category);
            printf("Severity   : %d\n", r.severity);
            printf("GPS        : %.6f, %.6f\n", r.lat, r.lon);
            printf("Timestamp  : %s\n", ts_buf);
            printf("Description: %s\n", r.description);
            break;
        }
    }
    close(fd);

    if (!found) {
        fprintf(stderr, "ERROR: Report %d not found in district '%s'.\n", report_id, district);
    }

    char action[64];
    snprintf(action, sizeof(action), "view report ID=%d", report_id);
    log_action(district, role, user, action);
}

void cmd_remove_report(const char *district, const char *role, const char *user, int report_id) {
    if (strcmp(role, "manager") != 0) {
        fprintf(stderr, "ERROR: Only managers can remove reports.\n");
        return;
    }

    char dat_path[512];
    snprintf(dat_path, sizeof(dat_path), "%s/reports.dat", district);

    if (!check_permission(dat_path, role, 1)) {
        fprintf(stderr, "ERROR: Permission denied on %s\n", dat_path);
        return;
    }

    int fd = open(dat_path, O_RDWR);
    if (fd < 0) {
        perror("open");
        return;
    }

    Report r;
    off_t target_offset = -1;
    off_t cur = 0;

    while (read(fd, &r, sizeof(Report)) == sizeof(Report)) {
        if (r.id == report_id) {
            target_offset = cur;
            break;
        }
        cur += sizeof(Report);
    }

    if (target_offset == -1) {
        fprintf(stderr, "ERROR: Report %d not found.\n", report_id);
        close(fd);
        return;
    }

    off_t read_pos  = target_offset + sizeof(Report);
    off_t write_pos = target_offset;

    while (1) {
        lseek(fd, read_pos, SEEK_SET);
        ssize_t n = read(fd, &r, sizeof(Report));
        if (n != sizeof(Report)) {
            break;
        }
        lseek(fd, write_pos, SEEK_SET);
        write(fd, &r, sizeof(Report));
        read_pos  += sizeof(Report);
        write_pos += sizeof(Report);
    }

    ftruncate(fd, write_pos);
    close(fd);

    char action[64];
    snprintf(action, sizeof(action), "remove_report ID=%d", report_id);
    log_action(district, role, user, action);

    printf("Report %d removed from district '%s'.\n", report_id, district);
}

void cmd_update_threshold(const char *district, const char *role, const char *user, int threshold) {
    if (strcmp(role, "manager") != 0) {
        fprintf(stderr, "ERROR: Only managers can update the severity threshold.\n");
        return;
    }

    char cfg_path[512];
    snprintf(cfg_path, sizeof(cfg_path), "%s/district.cfg", district);

    struct stat st;
    if (stat(cfg_path, &st) != 0) {
        perror("stat district.cfg");
        return;
    }

    mode_t expected = S_IRUSR | S_IWUSR | S_IRGRP;
    mode_t actual   = st.st_mode & 0777;
    if (actual != expected) {
        char perms[11];
        get_perms_string(st.st_mode, perms);
        fprintf(stderr,
                "ERROR: district.cfg has unexpected permissions %s (expected rw-r-----). "
                "Refusing to write.\n", perms);
        return;
    }

    int fd = open(cfg_path, O_WRONLY | O_TRUNC);
    if (fd < 0) {
        perror("open district.cfg");
        return;
    }

    char buf[64];
    int len = snprintf(buf, sizeof(buf), "severity_threshold=%d\n", threshold);
    write(fd, buf, len);
    close(fd);

    char action[64];
    snprintf(action, sizeof(action), "update_threshold=%d", threshold);
    log_action(district, role, user, action);

    printf("Severity threshold for district '%s' updated to %d.\n", district, threshold);
}

void cmd_filter(const char *district, const char *role, const char *user,
                int cond_count, char **conditions) {
    char dat_path[512];
    snprintf(dat_path, sizeof(dat_path), "%s/reports.dat", district);

    if (!check_permission(dat_path, role, 0)) {
        fprintf(stderr, "ERROR: Role '%s' does not have read permission on %s\n", role, dat_path);
        return;
    }

    int fd = open(dat_path, O_RDONLY);
    if (fd < 0) {
        perror("open");
        return;
    }

    char fields[16][50], ops[16][10], vals[16][100];
    int valid_conds = 0;
    for (int i = 0; i < cond_count && i < 16; i++) {
        if (parse_condition(conditions[i], fields[i], ops[i], vals[i])) {
            valid_conds++;
        } else {
            fprintf(stderr, "WARNING: Ignoring malformed condition: %s\n", conditions[i]);
        }
    }

    Report r;
    int found = 0;
    printf("=== Filter results for district '%s' ===\n", district);

    while (read(fd, &r, sizeof(Report)) == sizeof(Report)) {
        int match = 1;
        for (int i = 0; i < valid_conds; i++) {
            if (!match_condition(&r, fields[i], ops[i], vals[i])) {
                match = 0;
                break;
            }
        }
        if (match) {
            char ts_buf[32];
            struct tm *tm_info = localtime(&r.timestamp);
            strftime(ts_buf, sizeof(ts_buf), "%Y-%m-%d %H:%M:%S", tm_info);
            printf("[%d] Category: %-12s | Sev: %d | Inspector: %-20s | %s\n",
                   r.id, r.category, r.severity, r.inspector, ts_buf);
            found++;
        }
    }
    close(fd);

    if (!found) {
        printf("(no matching reports)\n");
    } else {
        printf("(%d result(s))\n", found);
    }

    log_action(district, role, user, "filter");
}

/* ----------------------------------------------------------------
 * Phase 2: remove_district
 * Manager only. Forks a child that runs rm -rf on the district
 * directory, then removes the active_reports-* symlink.
 * ---------------------------------------------------------------- */
void cmd_remove_district(const char *district, const char *role, const char *user) {
    if (strcmp(role, "manager") != 0) {
        fprintf(stderr, "ERROR: Only managers can remove districts.\n");
        return;
    }

    /* Safety check: district name must not be empty or contain dangerous chars */
    if (district == NULL || district[0] == '\0' ||
        strchr(district, '/') != NULL || strchr(district, '.') != NULL) {
        fprintf(stderr, "ERROR: Invalid district name '%s'.\n",
                district ? district : "(null)");
        return;
    }

    /* Check that the district directory exists */
    struct stat st;
    if (stat(district, &st) != 0 || !S_ISDIR(st.st_mode)) {
        fprintf(stderr, "ERROR: District '%s' does not exist.\n", district);
        return;
    }

    printf("Removing district '%s'...\n", district);

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return;
    }

    if (pid == 0) {
        /* Child: execute rm -rf <district> */
        execlp("rm", "rm", "-rf", district, (char *)NULL);
        /* If exec fails, exit child with error */
        perror("execlp rm");
        _exit(1);
    }

    /* Parent: wait for child */
    int status;
    if (waitpid(pid, &status, 0) < 0) {
        perror("waitpid");
        return;
    }

    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        fprintf(stderr, "ERROR: rm -rf failed for district '%s'.\n", district);
        return;
    }

    /* Remove the active_reports symlink */
    char sym_name[256];
    snprintf(sym_name, sizeof(sym_name), "active_reports-%s", district);
    struct stat lst;
    if (lstat(sym_name, &lst) == 0) {
        if (unlink(sym_name) != 0) {
            perror("unlink symlink");
        } else {
            printf("Symlink '%s' removed.\n", sym_name);
        }
    }

    printf("District '%s' successfully removed.\n", district);

    /* We cannot log into the district log since it's been deleted.
       Log to stdout only (already done above). */
}

void print_usage(const char *prog) {
    fprintf(stderr,
        "Usage:\n"
        "  %s --role <manager|inspector> --user <name> add <district>\n"
        "  %s --role <manager|inspector> --user <name> list <district>\n"
        "  %s --role <manager|inspector> --user <name> view <district> <report_id>\n"
        "  %s --role manager             --user <name> remove_report <district> <report_id>\n"
        "  %s --role manager             --user <name> remove_district <district>\n"
        "  %s --role manager             --user <name> update_threshold <district> <value>\n"
        "  %s --role <manager|inspector> --user <name> filter <district> <field:op:value> ...\n",
        prog, prog, prog, prog, prog, prog, prog);
}

int main(int argc, char *argv[]) {
    char *role     = NULL;
    char *user     = NULL;
    char *cmd      = NULL;
    char *district = NULL;

    int i;
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--role") == 0 && i + 1 < argc) {
            role = argv[++i];
        } else if (strcmp(argv[i], "--user") == 0 && i + 1 < argc) {
            user = argv[++i];
        } else if (argv[i][0] != '-') {
            cmd = argv[i];
            break;
        }
    }

    if (!role || !user || !cmd) {
        fprintf(stderr, "ERROR: --role, --user, and a command are required.\n");
        print_usage(argv[0]);
        return 1;
    }

    if (strcmp(role, "manager") != 0 && strcmp(role, "inspector") != 0) {
        fprintf(stderr, "ERROR: Role must be 'manager' or 'inspector'.\n");
        return 1;
    }

    int cmd_idx = i;

    if (strcmp(cmd, "add") == 0) {
        if (cmd_idx + 1 >= argc) {
            fprintf(stderr, "ERROR: add requires <district>\n");
            return 1;
        }
        district = argv[cmd_idx + 1];
        cmd_add(district, role, user);

    } else if (strcmp(cmd, "list") == 0) {
        if (cmd_idx + 1 >= argc) {
            fprintf(stderr, "ERROR: list requires <district>\n");
            return 1;
        }
        district = argv[cmd_idx + 1];
        cmd_list(district, role, user);

    } else if (strcmp(cmd, "view") == 0) {
        if (cmd_idx + 2 >= argc) {
            fprintf(stderr, "ERROR: view requires <district> <report_id>\n");
            return 1;
        }
        district  = argv[cmd_idx + 1];
        int rid   = atoi(argv[cmd_idx + 2]);
        cmd_view(district, role, user, rid);

    } else if (strcmp(cmd, "remove_report") == 0) {
        if (cmd_idx + 2 >= argc) {
            fprintf(stderr, "ERROR: remove_report requires <district> <report_id>\n");
            return 1;
        }
        district = argv[cmd_idx + 1];
        int rid  = atoi(argv[cmd_idx + 2]);
        cmd_remove_report(district, role, user, rid);

    } else if (strcmp(cmd, "remove_district") == 0) {
        if (cmd_idx + 1 >= argc) {
            fprintf(stderr, "ERROR: remove_district requires <district>\n");
            return 1;
        }
        district = argv[cmd_idx + 1];
        cmd_remove_district(district, role, user);

    } else if (strcmp(cmd, "update_threshold") == 0) {
        if (cmd_idx + 2 >= argc) {
            fprintf(stderr, "ERROR: update_threshold requires <district> <value>\n");
            return 1;
        }
        district  = argv[cmd_idx + 1];
        int val   = atoi(argv[cmd_idx + 2]);
        cmd_update_threshold(district, role, user, val);

    } else if (strcmp(cmd, "filter") == 0) {
        if (cmd_idx + 1 >= argc) {
            fprintf(stderr, "ERROR: filter requires <district> [conditions...]\n");
            return 1;
        }
        district       = argv[cmd_idx + 1];
        int cond_start = cmd_idx + 2;
        int cond_count = argc - cond_start;
        cmd_filter(district, role, user, cond_count, &argv[cond_start]);

    } else {
        fprintf(stderr, "ERROR: Unknown command '%s'\n", cmd);
        print_usage(argv[0]);
        return 1;
    }

    return 0;
}