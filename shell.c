/*
 * Mini Unix Shell — OS Project
 *
 * Features:
 *   - External command execution via fork/exec
 *   - Built-in commands: cd, exit, help, pwd
 *   - I/O redirection: cmd > file, cmd < file, cmd >> file
 *   - Pipes: cmd1 | cmd2
 *   - Background execution: cmd &
 *   - Signal handling (Ctrl-C doesn't kill the shell)
 */

#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define MAX_LINE 1024
#define MAX_ARGS 64

/* ── Signal handler ─────────────────────────────────────────────── */

static void sigint_handler(int sig) {
    (void)sig;
    write(STDOUT_FILENO, "\n", 1);
}

/* ── Prompt ─────────────────────────────────────────────────────── */

static void print_prompt(void) {
    char cwd[512];
    if (getcwd(cwd, sizeof(cwd)))
        printf("\033[1;36mmsh\033[0m:\033[1;34m%s\033[0m$ ", cwd);
    else
        printf("msh$ ");
    fflush(stdout);
}

/* ── Tokenise input into argv-style array ───────────────────────── */

static int parse(char *line, char **args) {
    int argc = 0;
    char *tok = strtok(line, " \t\n");
    while (tok && argc < MAX_ARGS - 1) {
        args[argc++] = tok;
        tok = strtok(NULL, " \t\n");
    }
    args[argc] = NULL;
    return argc;
}

/* ── Built-in commands ──────────────────────────────────────────── */

static int builtin_cd(char **args) {
    const char *dir = args[1] ? args[1] : getenv("HOME");
    if (!dir) { fprintf(stderr, "cd: HOME not set\n"); return 1; }
    if (chdir(dir) != 0) perror("cd");
    return 1;
}

static int builtin_help(void) {
    puts("Mini Shell (msh)");
    puts("  Built-ins:  cd [dir]   exit   pwd   help");
    puts("  Features:   cmd > file   cmd < file   cmd >> file");
    puts("              cmd1 | cmd2   cmd &");
    return 1;
}

static int try_builtin(char **args) {
    if (!args[0]) return 1;
    if (strcmp(args[0], "exit") == 0) return 0;
    if (strcmp(args[0], "cd")   == 0) return builtin_cd(args);
    if (strcmp(args[0], "pwd")  == 0) {
        char cwd[512];
        if (getcwd(cwd, sizeof(cwd))) puts(cwd);
        else perror("pwd");
        return 1;
    }
    if (strcmp(args[0], "help") == 0) return builtin_help();
    return -1;
}

/* ── Set up redirections (>, <, >>) in the child process ────────── */

static void setup_redirects(char **args, int *argc) {
    int i = 0;
    while (i < *argc) {
        int fd;
        if (strcmp(args[i], ">") == 0 && args[i + 1]) {
            fd = open(args[i + 1], O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fd < 0) { perror("open"); exit(1); }
            dup2(fd, STDOUT_FILENO);
            close(fd);
            /* remove > and filename from args */
            int j = i;
            while (args[j + 2]) { args[j] = args[j + 2]; j++; }
            args[j] = NULL;
            *argc -= 2;
        } else if (strcmp(args[i], ">>") == 0 && args[i + 1]) {
            fd = open(args[i + 1], O_WRONLY | O_CREAT | O_APPEND, 0644);
            if (fd < 0) { perror("open"); exit(1); }
            dup2(fd, STDOUT_FILENO);
            close(fd);
            int j = i;
            while (args[j + 2]) { args[j] = args[j + 2]; j++; }
            args[j] = NULL;
            *argc -= 2;
        } else if (strcmp(args[i], "<") == 0 && args[i + 1]) {
            fd = open(args[i + 1], O_RDONLY);
            if (fd < 0) { perror("open"); exit(1); }
            dup2(fd, STDIN_FILENO);
            close(fd);
            int j = i;
            while (args[j + 2]) { args[j] = args[j + 2]; j++; }
            args[j] = NULL;
            *argc -= 2;
        } else {
            i++;
        }
    }
}

/* ── Execute a single command (called in child after fork) ──────── */

static void exec_cmd(char **args, int argc) {
    setup_redirects(args, &argc);
    if (args[0] == NULL) exit(0);
    execvp(args[0], args);
    perror(args[0]);
    exit(127);
}

/* ── Find pipe position, or -1 ──────────────────────────────────── */

static int find_pipe(char **args, int argc) {
    for (int i = 0; i < argc; i++)
        if (strcmp(args[i], "|") == 0) return i;
    return -1;
}

/* ── Run a command line (handles pipe, background, redirects) ───── */

static void run(char **args, int argc) {
    /* check for background */
    int bg = 0;
    if (argc > 0 && strcmp(args[argc - 1], "&") == 0) {
        bg = 1;
        args[--argc] = NULL;
    }

    int ppos = find_pipe(args, argc);

    if (ppos > 0) {
        /* split into left | right */
        args[ppos] = NULL;
        char **left  = args;
        int    lc    = ppos;
        char **right = &args[ppos + 1];
        int    rc    = argc - ppos - 1;

        int pipefd[2];
        if (pipe(pipefd) < 0) { perror("pipe"); return; }

        pid_t p1 = fork();
        if (p1 == 0) {
            close(pipefd[0]);
            dup2(pipefd[1], STDOUT_FILENO);
            close(pipefd[1]);
            exec_cmd(left, lc);
        }

        pid_t p2 = fork();
        if (p2 == 0) {
            close(pipefd[1]);
            dup2(pipefd[0], STDIN_FILENO);
            close(pipefd[0]);
            exec_cmd(right, rc);
        }

        close(pipefd[0]);
        close(pipefd[1]);
        if (!bg) { waitpid(p1, NULL, 0); waitpid(p2, NULL, 0); }
    } else {
        pid_t pid = fork();
        if (pid < 0) { perror("fork"); return; }
        if (pid == 0) exec_cmd(args, argc);
        if (!bg) waitpid(pid, NULL, 0);
    }

    if (bg) printf("[bg] process launched\n");
}

/* ── Main loop ──────────────────────────────────────────────────── */

int main(void) {
    char line[MAX_LINE];
    char *args[MAX_ARGS];

    signal(SIGINT, sigint_handler);

    while (1) {
        print_prompt();

        if (!fgets(line, sizeof(line), stdin)) {
            printf("\n");
            break;
        }

        /* skip blank lines */
        if (line[0] == '\n') continue;

        int argc = parse(line, args);
        if (argc == 0) continue;

        int rc = try_builtin(args);
        if (rc == 0) break;    /* exit */
        if (rc == 1) continue; /* built-in handled */

        run(args, argc);
    }

    return 0;
}
