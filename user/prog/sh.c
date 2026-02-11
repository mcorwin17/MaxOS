/* The shell, as a program.
 *
 * Reads a line, splits it into pipeline stages, forks each stage with its
 * stdin/stdout rewired through pipes or a > file, execs binaries off the
 * disk, waits for all of it. Bare names resolve to /BIN/NAME.BIN.
 *
 * SIGINT is ignored here and handed to the foreground child via tcsetfg -
 * Ctrl-C kills the pipeline's last stage, and SIGPIPE folds up the rest. */

#include "ulib.h"

#define LINE_MAX  200
#define TOK_MAX   24
#define STAGE_MAX 4

static char upcase(char c) {
    return (c >= 'a' && c <= 'z') ? (char)(c - 32) : c;
}

static void str_copy(char* dst, const char* src, u32 max) {
    u32 i = 0;
    while (src[i] && i < max - 1) { dst[i] = src[i]; ++i; }
    dst[i] = '\0';
}

static int str_eq(const char* a, const char* b) {
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}

/* /X stays /X; anything else becomes /BIN/NAME.BIN. */
static void resolve(const char* name, char* out) {
    if (name[0] == '/') {
        str_copy(out, name, 64);
        return;
    }

    u32 n = 0;
    const char* pre = "/BIN/";
    for (u32 i = 0; pre[i]; ++i) out[n++] = pre[i];
    for (u32 i = 0; name[i] && n < 55; ++i) out[n++] = upcase(name[i]);
    out[n++] = '.'; out[n++] = 'B'; out[n++] = 'I'; out[n++] = 'N';
    out[n] = '\0';
}

static i32 read_line(char* line, u32 max) {
    u32 len = 0;

    for (;;) {
        char c;
        i32 got = read(0, &c, 1);
        if (got <= 0) return -1;            /* signal or EOF */

        if (c == '\n') { line[len] = '\0'; return (i32)len; }

        if (c == 0x08 || c == 0x7F) {       /* backspace */
            if (len > 0) --len;
            continue;
        }

        if (c >= 32 && c < 127 && len < max - 1) line[len++] = c;
    }
}

int main(void) {
    signal(2, SIG_IGN, 0);                  /* Ctrl-C is for the children */

    print("sh: ready\n");

    char line[LINE_MAX];

    for (;;) {
        print("$ ");

        if (read_line(line, sizeof(line)) < 0) continue;

        /* Tokenize in place. */
        char* toks[TOK_MAX];
        u32 ntok = 0;
        char* s = line;
        while (*s && ntok < TOK_MAX) {
            while (*s == ' ') *s++ = '\0';
            if (!*s) break;
            toks[ntok++] = s;
            while (*s && *s != ' ') ++s;
        }
        if (ntok == 0) continue;

        if (str_eq(toks[0], "exit")) return 0;

        /* Split into pipeline stages at |, and peel a trailing > file. */
        u32 stage_start[STAGE_MAX], stage_len[STAGE_MAX];
        u32 nstage = 0;
        char* redirect = 0;

        stage_start[0] = 0;
        for (u32 i = 0; i < ntok && nstage < STAGE_MAX - 1; ++i) {
            if (str_eq(toks[i], "|")) {
                stage_len[nstage] = i - stage_start[nstage];
                nstage++;
                stage_start[nstage] = i + 1;
            } else if (str_eq(toks[i], ">") && i + 1 < ntok) {
                redirect = toks[i + 1];
                ntok = i;                   /* stages end here */
                break;
            }
        }
        stage_len[nstage] = ntok - stage_start[nstage];
        nstage++;

        if (stage_len[nstage - 1] == 0) { print("sh: bad pipeline\n"); continue; }

        /* Launch each stage. */
        i32 pids[STAGE_MAX];
        i32 in_fd = 0;

        for (u32 st = 0; st < nstage; ++st) {
            i32 pfd[2] = { -1, -1 };
            int has_next = (st + 1 < nstage);

            if (has_next && pipe(pfd) != 0) { print("sh: pipe failed\n"); break; }

            i32 pid = fork();
            if (pid == 0) {
                if (in_fd != 0) { dup2(in_fd, 0); close(in_fd); }

                if (has_next) {
                    dup2(pfd[1], 1);
                    close(pfd[0]);
                    close(pfd[1]);
                } else if (redirect) {
                    i32 f = open_create(redirect);
                    if (f < 0) { print("sh: can't create file\n"); exit(1); }
                    dup2(f, 1);
                    close(f);
                }

                char prog[64];
                resolve(toks[stage_start[st]], prog);

                /* Re-join this stage's arguments for exec. */
                char args[LINE_MAX];
                u32 n = 0;
                for (u32 t = 1; t < stage_len[st]; ++t) {
                    const char* a = toks[stage_start[st] + t];
                    if (n) args[n++] = ' ';
                    for (u32 i = 0; a[i] && n < LINE_MAX - 1; ++i) args[n++] = a[i];
                }
                args[n] = '\0';

                exec(prog, n ? args : 0);

                print("sh: not found: ");
                print(prog);
                print("\n");
                exit(127);
            }

            pids[st] = pid;

            if (in_fd != 0) close(in_fd);
            if (has_next) {
                close(pfd[1]);
                in_fd = pfd[0];
            }
        }

        /* Terminal to the last stage; SIGPIPE folds the rest if it dies. */
        tcsetfg(pids[nstage - 1]);

        for (u32 st = 0; st < nstage; ++st) wait();

        tcsetfg(0);
    }
}
