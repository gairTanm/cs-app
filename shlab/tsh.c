/*
 * tsh - A tiny shell program with job control
 *
 * <Put your name and login ID here>
 */
#include <ctype.h>
#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

/* Misc manifest constants */
#define MAXLINE 1024   /* max line size */
#define MAXARGS 128    /* max args on a command line */
#define MAXJOBS 16     /* max jobs at any point in time */
#define MAXJID 1 << 16 /* max job ID */

/* Job states */
#define UNDEF 0 /* undefined */
#define FG 1    /* running in foreground */
#define BG 2    /* running in background */
#define ST 3    /* stopped */

int debug = 0;

void printdebug(const char *fmt, ...) {
    if (!debug) return;
    printf("Debug: ");
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
}

/*
 * Jobs states: FG (foreground), BG (background), ST (stopped)
 * Job state transitions and enabling actions:
 *     FG -> ST  : ctrl-z
 *     ST -> FG  : fg command
 *     ST -> BG  : bg command
 *     BG -> FG  : fg command
 * At most 1 job can be in the FG state.
 */

/* Global variables */
extern char **environ;   /* defined in libc */
char prompt[] = "tsh> "; /* command line prompt (DO NOT CHANGE) */
int verbose = 0;         /* if true, print additional output */
int nextjid = 1;         /* next job ID to allocate */
char sbuf[MAXLINE];      /* for composing sprintf messages */
int job_count = 0;

struct job_t {             /* The job struct */
    pid_t pid;             /* job PID */
    int jid;               /* job ID [1, 2, ...] */
    int state;             /* UNDEF, BG, FG, or ST */
    char cmdline[MAXLINE]; /* command line */
};
struct job_t jobs[MAXJOBS]; /* The job list */
/* End global variables */

/* Function prototypes */

/* Here are the functions that you will implement */
void eval(char *cmdline);
int builtin_cmd(char **argv);
void do_bgfg(char **argv);
void waitfg(pid_t pid, int status, int direct_fg);

void sigchld_handler(int sig);
void sigtstp_handler(int sig);
void sigint_handler(int sig);

int stopjob(struct job_t *jobs, pid_t pid);
/* Here are helper routines that we've provided for you */
int parseline(const char *cmdline, char **argv);
void sigquit_handler(int sig);

void clearjob(struct job_t *job);
void initjobs(struct job_t *jobs);
int maxjid(struct job_t *jobs);
int addjob(struct job_t *jobs, pid_t pid, int state, char *cmdline);
int deletejob(struct job_t *jobs, pid_t pid);
pid_t fgpid(struct job_t *jobs);
struct job_t *getjobpid(struct job_t *jobs, pid_t pid);
struct job_t *getjobjid(struct job_t *jobs, int jid);
int pid2jid(pid_t pid);
void listjobs(struct job_t *jobs);

void usage(void);
void unix_error(char *msg);
void app_error(char *msg);
typedef void handler_t(int);
handler_t *Signal(int signum, handler_t *handler);

/*
 * main - The shell's main routine
 */
int main(int argc, char **argv) {
    char c;
    char cmdline[MAXLINE];
    int emit_prompt = 1; /* emit prompt (default) */

    /* Redirect stderr to stdout (so that driver will get all output
     * on the pipe connected to stdout) */
    dup2(1, 2);

    /* Parse the command line */
    while ((c = getopt(argc, argv, "hvp")) != EOF) {
        switch (c) {
            case 'h': /* print help message */
                usage();
                break;
            case 'v': /* emit additional diagnostic info */
                debug = 1;
                break;
            case 'p':            /* don't print a prompt */
                emit_prompt = 0; /* handy for automatic testing */
                break;
            default:
                usage();
        }
    }

    /* Install the signal handlers */

    /* These are the ones you will need to implement */
    Signal(SIGINT, sigint_handler);   /* ctrl-c */
    Signal(SIGTSTP, sigtstp_handler); /* ctrl-z */
    Signal(SIGCHLD, sigchld_handler); /* Terminated or stopped child */

    /* This one provides a clean way to kill the shell */
    Signal(SIGQUIT, sigquit_handler);

    /* Initialize the job list */
    initjobs(jobs);

    fflush(stdout);
    /* Execute the shell's read/eval loop */
    while (1) {
        /* Read command line */
        if (emit_prompt) {
            printf("%s", prompt);
            fflush(stdout);
        }
        if (feof(stdin)) { /* End of file (ctrl-d) */
            fflush(stdout);
            exit(0);
        }
        if ((fgets(cmdline, MAXLINE, stdin) == NULL) && ferror(stdin))
            app_error("fgets error");

        /* Evaluate the command line */
        printdebug("evaling: %s", cmdline);
        eval(cmdline);
        fflush(stdout);
        fflush(stdout);
    }

    exit(0); /* control never reaches here */
}

/*
 * eval - Evaluate the command line that the user has just typed in
 *
 * If the user has requested a built-in command (quit, jobs, bg or fg)
 * then execute it immediately. Otherwise, fork a child process and
 * run the job in the context of the child. If the job is running in
 * the foreground, wait for it to terminate and then return.  Note:
 * each child process must have a unique process group ID so that our
 * background children don't receive SIGINT (SIGTSTP) from the kernel
 * when we type ctrl-c (ctrl-z) at the keyboard.
 */
void eval(char *cmdline) {
    char *env[] = {NULL};
    char *argv[MAXARGS];
    int is_bg = parseline(cmdline, argv);
    printdebug("execing: %s\n", argv[0]);

    if (argv[0] == NULL) return;
    sigset_t mask_all, prev_all;

    sigfillset(&mask_all);

    if (!builtin_cmd(&argv[0])) {
        // if it isn't a builtin command, fork-exec it

        /* Job Overflow
         * Check for job overflows
         */
        if (job_count >= MAXJOBS) {
            printf(
                "Shell is handling too many jobs at the moment, only builtin "
                "commands are available\n");
            return;
        }

        sigprocmask(SIG_BLOCK, &mask_all, &prev_all);
        pid_t pid = fork();

        job_count += addjob(jobs, pid, is_bg ? BG : FG, cmdline);
        printdebug("job count: %d\n", job_count);
        if (pid == 0) {
            sigprocmask(SIG_SETMASK, &prev_all, NULL);
            if (setpgid(0, 0) < 0) {
                printf("setpgid error\n");
                exit(1);
            };

            if (execve(argv[0], argv, env) < 0) {
                printf("%s: Command not found.\n", argv[0]);
                exit(0);
            }
        };
        sigprocmask(SIG_SETMASK, &prev_all, NULL);

        if (!is_bg) {
            waitfg(pid, 0, 1);
        } else {
            printf("%d %s", pid, cmdline);
        }
    }
    return;
}

/*
 * parseline - Parse the command line and build the argv array.
 *
 * Characters enclosed in single quotes are treated as a single
 * argument.  Return true if the user has requested a BG job, false if
 * the user has requested a FG job.
 */
int parseline(const char *cmdline, char **argv) {
    static char array[MAXLINE]; /* holds local copy of command line */
    char *buf = array;          /* ptr that traverses command line */
    char *delim;                /* points to first space delimiter */
    int argc;                   /* number of args */
    int bg;                     /* background job? */

    strcpy(buf, cmdline);
    buf[strlen(buf) - 1] = ' ';   /* replace trailing '\n' with space */
    while (*buf && (*buf == ' ')) /* ignore leading spaces */
        buf++;

    /* Build the argv list */
    argc = 0;
    if (*buf == '\'') {
        buf++;
        delim = strchr(buf, '\'');
    } else {
        delim = strchr(buf, ' ');
    }

    while (delim) {
        argv[argc++] = buf;
        *delim = '\0';
        buf = delim + 1;
        while (*buf && (*buf == ' ')) /* ignore spaces */
            buf++;

        if (*buf == '\'') {
            buf++;
            delim = strchr(buf, '\'');
        } else {
            delim = strchr(buf, ' ');
        }
    }
    argv[argc] = NULL;

    if (argc == 0) /* ignore blank line */
        return 1;

    /* should the job run in the background? */
    if ((bg = (*argv[argc - 1] == '&')) != 0) {
        argv[--argc] = NULL;
    }
    return bg;
}

/*
 * builtin_cmd - If the user has typed a built-in command then execute
 *    it immediately.
 */
int builtin_cmd(char **argv) {
    if (strcmp(*argv, "quit") == 0) {
        exit(0);
    } else if (strncmp(*argv, "bg", 2) == 0 || strncmp(*argv, "fg", 2) == 0) {
        do_bgfg(argv);
    } else if (strcmp(*argv, "jobs") == 0) {
        listjobs(jobs);
    } else {
        printdebug("Not a builtin command\n", *argv);
        return 0; /* not a builtin command */
    }
    return 1;
}

struct job_t *get_job(unsigned int jid_or_pid, int is_pid) {
    struct job_t *target_job = NULL;
    if (!is_pid) {
        if ((target_job = getjobjid(jobs, jid_or_pid)) == NULL) {
            printf("%%%d: no such job\n", jid_or_pid);
        }
    } else {
        if ((target_job = getjobpid(jobs, jid_or_pid)) == NULL) {
            printf("(%d): no such process\n", jid_or_pid);
        }
    }
    return target_job;
}

/*
 * do_bgfg - Execute the builtin bg and fg commands
 */
void do_bgfg(char **argv) {
    unsigned int jid_or_pid;
    int is_pid = 0;
    if (argv[1] == NULL) {
        printf("Please provide an arg for bg/fg\n");
        return;
    }
    if (argv[1][0] == '%')
        jid_or_pid = atoi(argv[1] + 1);
    else {
        is_pid = 1;
        jid_or_pid = atoi(argv[1]);
    }
    struct job_t *target_job;
    if ((target_job = get_job(jid_or_pid, is_pid)) == NULL) {
        return;
    }

    kill(-target_job->pid, SIGCONT);
    if (strncmp(*argv, "bg", 2) == 0) {
        target_job->state = BG;
        printf("[%d] (%d) %d %s\n", target_job->jid, target_job->pid,
               target_job->state, target_job->cmdline);
    } else {
        target_job->state = FG;
        waitfg(target_job->pid, 0, 1);
    }
}

typedef int (*manip_jobs_t)(struct job_t *, int);
void modify_jobs_safely(manip_jobs_t manip_jobs, int pid) {
    sigset_t mask, prev_mask;

    sigfillset(&mask);

    sigprocmask(SIG_SETMASK, &mask, &prev_mask);
    (*manip_jobs)(jobs, pid);
    sigprocmask(SIG_SETMASK, &prev_mask, NULL);
}

/*
 * waitfg - Block until process pid is no longer the foreground process
 */
void waitfg(pid_t pid, int status, int direct_fg) {
    sigset_t mask_all, prev_all;
    if (direct_fg == 1) {
        printdebug("Waiting for pid: %d\n", pid);
        pid_t child_pid = waitpid(pid, &status, WUNTRACED);
    }
    if (WSTOPSIG(status)) {
        printf("Sending SIGTSTP child with PID: %d\n", pid);
        modify_jobs_safely(&stopjob, pid);
    } else if (WTERMSIG(status)) {
        printf("Stopping (SIGINT) child with PID: %d\n", pid);
        modify_jobs_safely(&deletejob, pid);
    } else if (WIFEXITED(status)) {
        modify_jobs_safely(&deletejob, pid);
        printdebug("Child process finished\n");
    }
}

/*****************
 * Signal handlers
 *****************/

/*
 * sigchld_handler - The kernel sends a SIGCHLD to the shell whenever
 *     a child job terminates (becomes a zombie), or stops because it
 *     received a SIGSTOP or SIGTSTP signal. The handler reaps all
 *     available zombie children, but doesn't wait for any other
 *     currently running children to terminate.
 */
void sigchld_handler(int sig) {
    int status;
    pid_t pid;
    while ((pid = waitpid(-1, &status, WNOHANG | WUNTRACED)) > 0) {
        printdebug("Got a SIGCHLD for ");
        printdebug("pid: (%d)\n", pid);
        if (WSTOPSIG(status)) {
            // printdebug("sigtstp\n");
            // kill(getpid(), SIGTSTP);
        } else if (WTERMSIG(status)) {
            // printdebug("sigint\n");
            // sigint_handler(sig);
        } else {
            waitfg(pid, status, 0);
        }
        job_count -= 1;
    }
}

/*
 * sigint_handler - The kernel sends a SIGINT to the shell whenver the
 *    user types ctrl-c at the keyboard.  Catch it and send it along
 *    to the foreground job.
 */
void sigint_handler(int sig) {
    sigset_t mask_all, prev_all;
    int status;
    pid_t pid = fgpid(jobs);

    kill(-pid, SIGTERM);
}

/*
 * sigtstp_handler - The kernel sends a SIGTSTP to the shell whenever
 *     the user types ctrl-z at the keyboard. Catch it and suspend the
 *     foreground job by sending it a SIGTSTP.
 */
void sigtstp_handler(int sig) {
    sigset_t mask_all, prev_all;
    int status;
    pid_t pid = fgpid(jobs);
    if (pid == 0) return;

    kill(-pid, SIGTSTP);
}

int stopjob(struct job_t *jobs, pid_t pid) {
    for (int i = 0; i < MAXJOBS; i++) {
        if (jobs[i].pid == pid) {
            jobs[i].state = ST;
            printdebug("Stopped job: %d\n", pid);
            return 1;
        }
    }
    return 0;
}

/*********************
 * End signal handlers
 *********************/

/***********************************************
 * Helper routines that manipulate the job list
 **********************************************/

/* clearjob - Clear the entries in a job struct */
void clearjob(struct job_t *job) {
    job->pid = 0;
    job->jid = 0;
    job->state = UNDEF;
    job->cmdline[0] = '\0';
}

/* initjobs - Initialize the job list */
void initjobs(struct job_t *jobs) {
    int i;

    for (i = 0; i < MAXJOBS; i++) clearjob(&jobs[i]);
}

/* maxjid - Returns largest allocated job ID */
int maxjid(struct job_t *jobs) {
    int i, max = 0;

    for (i = 0; i < MAXJOBS; i++)
        if (jobs[i].jid > max) max = jobs[i].jid;
    return max;
}

/* addjob - Add a job to the job list */
int addjob(struct job_t *jobs, pid_t pid, int state, char *cmdline) {
    int i;
    if (pid < 1) return 0;

    printdebug("adding job: %d\n", pid);
    for (i = 0; i < MAXJOBS; i++) {
        if (jobs[i].pid == 0) {
            jobs[i].pid = pid;
            jobs[i].state = state;
            jobs[i].jid = nextjid++;
            if (nextjid > MAXJOBS) nextjid = 1;
            strcpy(jobs[i].cmdline, cmdline);
            printdebug("added job %d\n", jobs[i].pid);
            if (verbose) {
                printf("Added job [%d] %d %s\n", jobs[i].jid, jobs[i].pid,
                       jobs[i].cmdline);
            }
            return 1;
        }
    }
    printf("Tried to create too many jobs\n");
    return 0;
}

/* deletejob - Delete a job whose PID=pid from the job list */
int deletejob(struct job_t *jobs, pid_t pid) {
    int i;

    if (pid < 1) return 0;
    printdebug("deleting job: %d", pid);
    for (i = 0; i < MAXJOBS; i++) {
        if (jobs[i].pid == pid) {
            clearjob(&jobs[i]);
            nextjid = maxjid(jobs) + 1;
            return 1;
        }
    }
    return 0;
}

/* fgpid - Return PID of current foreground job, 0 if no such job */
pid_t fgpid(struct job_t *jobs) {
    int i;

    for (i = 0; i < MAXJOBS; i++)
        if (jobs[i].state == FG) return jobs[i].pid;
    return 0;
}

/* getjobpid  - Find a job (by PID) on the job list */
struct job_t *getjobpid(struct job_t *jobs, pid_t pid) {
    int i;

    if (pid < 1) return NULL;
    for (i = 0; i < MAXJOBS; i++)
        if (jobs[i].pid == pid) return &jobs[i];
    return NULL;
}

/* getjobjid  - Find a job (by JID) on the job list */
struct job_t *getjobjid(struct job_t *jobs, int jid) {
    int i;

    if (jid < 1) return NULL;
    for (i = 0; i < MAXJOBS; i++)
        if (jobs[i].jid == jid) return &jobs[i];
    return NULL;
}

/* pid2jid - Map process ID to job ID */
int pid2jid(pid_t pid) {
    int i;

    if (pid < 1) return 0;
    for (i = 0; i < MAXJOBS; i++)
        if (jobs[i].pid == pid) {
            return jobs[i].jid;
        }
    return 0;
}

/* listjobs - Print the job list */
void listjobs(struct job_t *jobs) {
    int i;
    for (i = 0; i < MAXJOBS; i++) {
        if (jobs[i].pid != 0) {
            printf("[%d] (%d) ", jobs[i].jid, jobs[i].pid);
            switch (jobs[i].state) {
                case BG:
                    printf("Running ");
                    break;
                case FG:
                    printf("Foreground ");
                    break;
                case ST:
                    printf("Stopped ");
                    break;
                default:
                    printf("listjobs: Internal error: job[%d].state=%d ", i,
                           jobs[i].state);
            }
            printf("%s", jobs[i].cmdline);
        }
    }
}
/******************************
 * end job list helper routines
 ******************************/

/***********************
 * Other helper routines
 ***********************/

/*
 * usage - print a help message
 */
void usage(void) {
    printf("Usage: shell [-hvp]\n");
    printf("   -h   print this message\n");
    printf("   -v   print additional diagnostic information\n");
    printf("   -p   do not emit a command prompt\n");
    exit(1);
}

/*
 * unix_error - unix-style error routine
 */
void unix_error(char *msg) {
    fprintf(stdout, "%s: %s\n", msg, strerror(errno));
    exit(1);
}

/*
 * app_error - application-style error routine
 */
void app_error(char *msg) {
    fprintf(stdout, "%s\n", msg);
    exit(1);
}

/*
 * Signal - wrapper for the sigaction function
 */
handler_t *Signal(int signum, handler_t *handler) {
    struct sigaction action, old_action;

    action.sa_handler = handler;
    sigemptyset(&action.sa_mask); /* block sigs of type being handled */
    action.sa_flags = SA_RESTART; /* restart syscalls if possible */

    if (sigaction(signum, &action, &old_action) < 0) unix_error("Signal error");
    return (old_action.sa_handler);
}

/*
 * sigquit_handler - The driver program can gracefully terminate the
 *    child shell by sending it a SIGQUIT signal.
 */
void sigquit_handler(int sig) {
    printf("Terminating after receipt of SIGQUIT signal\n");
    exit(1);
}

