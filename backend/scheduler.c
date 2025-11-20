#include "scheduler.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h> // for strcasecmp
#include <ctype.h>

/********************************
 *   UTILITY / NORMALIZATION
 ********************************/

int convertPriority(const char *type) {
    if (strcasecmp(type, "emergency") == 0 || strcasecmp(type, "urgent") == 0)
        return 2; // highest
    else if (strcasecmp(type, "regular") == 0)
        return 1; // medium
    return 0;     // background / low / others
}

static void trim(char *s) {
    int i = 0, j = (int)strlen(s) - 1;
    while (i <= j && isspace((unsigned char)s[i])) i++;
    while (j >= i && isspace((unsigned char)s[j])) j--;
    memmove(s, s + i, j - i + 1);
    s[j - i + 1] = '\0';
}

static void lowercase(char *s) {
    for (int i = 0; s[i]; i++)
        s[i] = (char)tolower((unsigned char)s[i]);
}

// Map banking-ish types to internal types
static void normalizeType(char *s) {
    lowercase(s);

    // direct internal labels
    if (strcmp(s, "urgent") == 0 ||
        strcmp(s, "regular") == 0 ||
        strcmp(s, "background") == 0) {
        return;
    }

    // Banking mapping
    if (strcmp(s, "rtgs") == 0 || strcmp(s, "fraud") == 0) {
        strcpy(s, "urgent");
    } else if (strcmp(s, "upi") == 0 || strcmp(s, "neft") == 0 || strcmp(s, "atm") == 0) {
        strcpy(s, "regular");
    } else if (strcmp(s, "emi") == 0 || strcmp(s, "cheque") == 0) {
        strcpy(s, "background");
    } else {
        // default to regular
        strcpy(s, "regular");
    }
}

/********************************
 *   PARSER (transactions.json)
 *
 *   Reads the file and for each JSON object { ... }
 *   builds a small string chunk and parses:
 *     "id", "arrival", "burst", "type"
 ********************************/

int parse_transactions_json(Process p[], int maxn) {
    FILE *f = fopen("transactions.json", "r");
    if (!f) {
        printf("Error: transactions.json not found!\n");
        return 0;
    }

    int n = 0;
    int c;
    int in_obj = 0;
    int depth = 0;
    char obj[1024];
    int idx = 0;

    while ((c = fgetc(f)) != EOF && n < maxn) {
        if (!in_obj) {
            // wait until we see the start of an object
            if (c == '{') {
                in_obj = 1;
                depth = 1;
                idx = 0;
                obj[idx++] = (char)c;
            }
        } else {
            // we are inside an object { ... }
            obj[idx++] = (char)c;
            if (idx >= (int)sizeof(obj) - 1) {
                // safety: avoid overflow
                obj[idx] = '\0';
                fprintf(stderr, "Warning: JSON object too large, truncating.\n");
                break;
            }

            if (c == '{') depth++;
            else if (c == '}') {
                depth--;
                if (depth == 0) {
                    // end of this JSON object
                    obj[idx] = '\0';

                    int id = 0, at = 0, bt = 0;
                    char type[32] = "regular";

                    char *id_ptr = strstr(obj, "\"id\"");
                    if (id_ptr) sscanf(id_ptr, "\"id\"%*[^0-9]%d", &id);

                    char *type_ptr = strstr(obj, "\"type\"");
                    if (type_ptr) {
                        char temp[32] = "";
                        char *first = strchr(type_ptr + 6, '"');
                        if (first) {
                            char *second = strchr(first + 1, '"');
                            if (second) {
                                strncpy(temp, first + 1, second - first - 1);
                                temp[second - first - 1] = '\0';
                                trim(temp);
                                normalizeType(temp);
                                strcpy(type, temp);
                            }
                        }
                    }

                    char *arr_ptr = strstr(obj, "\"arrival\"");
                    if (arr_ptr) sscanf(arr_ptr, "\"arrival\"%*[^0-9]%d", &at);

                    char *burst_ptr = strstr(obj, "\"burst\"");
                    if (burst_ptr) sscanf(burst_ptr, "\"burst\"%*[^0-9]%d", &bt);

                    // fill struct
                    p[n].pid = id;
                    p[n].at  = at;
                    p[n].bt  = bt;
                    p[n].rt  = bt;
                    p[n].ct  = 0;
                    p[n].tat = 0;
                    p[n].wt  = 0;
                    p[n].done = 0;
                    strcpy(p[n].type, type);
                    p[n].pr = convertPriority(type);
                    n++;

                    // reset for next object
                    in_obj = 0;
                    idx = 0;
                }
            }
        }
    }

    fclose(f);
    printf("Parsed %d processes from JSON.\n", n);
    return n;
}

/********************************
 *   OUTPUT (output.json)
 ********************************/

void write_output_json(Process p[], int n) {
    FILE *out = fopen("output.json", "w");
    if (!out) {
        printf("Could not open output.json for writing\n");
        return;
    }

    fprintf(out, "[\n");
    for (int i = 0; i < n; i++) {
        fprintf(out,
            "  { \"id\": %d, \"type\": \"%s\", \"arrival\": %d, \"burst\": %d, "
            "\"CT\": %d, \"TAT\": %d, \"WT\": %d }%s\n",
            p[i].pid, p[i].type, p[i].at, p[i].bt,
            p[i].ct, p[i].tat, p[i].wt,
            (i == n - 1) ? "" : ",");
    }
    fprintf(out, "]\n");
    fclose(out);
    printf("output.json created successfully.\n");
}

/********************************
 *   RESET HELPER
 ********************************/

static void reset_processes(Process p[], int n) {
    for (int i = 0; i < n; i++) {
        p[i].rt   = p[i].bt;
        p[i].done = 0;
        p[i].ct   = 0;
        p[i].tat  = 0;
        p[i].wt   = 0;
    }
}

/********************************
 *   FCFS
 ********************************/

void runFCFS(Process p[], int n) {
    reset_processes(p, n);

    // sort by arrival time
    for (int i = 0; i < n; i++) {
        for (int j = i + 1; j < n; j++) {
            if (p[j].at < p[i].at) {
                Process tmp = p[i];
                p[i] = p[j];
                p[j] = tmp;
            }
        }
    }

    int time = 0;
    for (int i = 0; i < n; i++) {
        if (time < p[i].at) time = p[i].at;
        time += p[i].bt;
        p[i].ct  = time;
        p[i].tat = p[i].ct - p[i].at;
        p[i].wt  = p[i].tat - p[i].bt;
    }

    write_output_json(p, n);
}

/********************************
 *   SJF NON-PREEMPTIVE
 ********************************/

void runSJF(Process p[], int n) {
    reset_processes(p, n);

    int completed = 0, time = 0;

    while (completed < n) {
        int idx = -1;
        for (int i = 0; i < n; i++) {
            if (!p[i].done && p[i].at <= time) {
                if (idx == -1 || p[i].bt < p[idx].bt)
                    idx = i;
            }
        }

        if (idx == -1) {
            time++;
            continue;
        }

        time += p[idx].bt;
        p[idx].ct  = time;
        p[idx].tat = p[idx].ct - p[idx].at;
        p[idx].wt  = p[idx].tat - p[idx].bt;
        p[idx].done = 1;
        completed++;
    }

    write_output_json(p, n);
}

/********************************
 *   SJF PREEMPTIVE (SRTF)
 ********************************/

void runSJFPreemptive(Process p[], int n) {
    reset_processes(p, n);

    int completed = 0, time = 0;

    while (completed < n) {
        int idx = -1;
        for (int i = 0; i < n; i++) {
            if (!p[i].done && p[i].at <= time) {
                if (idx == -1 || p[i].rt < p[idx].rt)
                    idx = i;
            }
        }

        if (idx == -1) {
            time++;
            continue;
        }

        p[idx].rt--;
        time++;

        if (p[idx].rt == 0) {
            p[idx].done = 1;
            completed++;
            p[idx].ct  = time;
            p[idx].tat = p[idx].ct - p[idx].at;
            p[idx].wt  = p[idx].tat - p[idx].bt;
        }
    }

    write_output_json(p, n);
}

/********************************
 *   PRIORITY NON-PREEMPTIVE
 ********************************/

void runPriority(Process p[], int n) {
    reset_processes(p, n);

    int completed = 0, time = 0;

    while (completed < n) {
        int idx = -1;
        for (int i = 0; i < n; i++) {
            if (!p[i].done && p[i].at <= time) {
                if (idx == -1 || p[i].pr > p[idx].pr)
                    idx = i;
            }
        }

        if (idx == -1) {
            time++;
            continue;
        }

        time += p[idx].bt;
        p[idx].ct  = time;
        p[idx].tat = p[idx].ct - p[idx].at;
        p[idx].wt  = p[idx].tat - p[idx].bt;
        p[idx].done = 1;
        completed++;
    }

    write_output_json(p, n);
}

/********************************
 *   PRIORITY PREEMPTIVE
 ********************************/

void runPriorityPreemptive(Process p[], int n) {
    reset_processes(p, n);

    int completed = 0, time = 0;

    while (completed < n) {
        int idx = -1;
        for (int i = 0; i < n; i++) {
            if (!p[i].done && p[i].at <= time) {
                if (idx == -1 || p[i].pr > p[idx].pr)
                    idx = i;
            }
        }

        if (idx == -1) {
            time++;
            continue;
        }

        p[idx].rt--;
        time++;

        if (p[idx].rt == 0) {
            p[idx].done = 1;
            completed++;
            p[idx].ct  = time;
            p[idx].tat = p[idx].ct - p[idx].at;
            p[idx].wt  = p[idx].tat - p[idx].bt;
        }
    }

    write_output_json(p, n);
}

/********************************
 *   ROUND ROBIN
 ********************************/

void runRR(Process p[], int n, int quantum) {
    reset_processes(p, n);

    int time = 0, completed = 0;

    while (completed < n) {
        int executed = 0;

        for (int i = 0; i < n; i++) {
            if (!p[i].done && p[i].at <= time) {
                executed = 1;

                int slice = (p[i].rt > quantum) ? quantum : p[i].rt;
                p[i].rt -= slice;
                time += slice;

                if (p[i].rt == 0) {
                    p[i].done = 1;
                    completed++;
                    p[i].ct  = time;
                    p[i].tat = p[i].ct - p[i].at;
                    p[i].wt  = p[i].tat - p[i].bt;
                }
            }
        }

        if (!executed) {
            time++;
        }
    }

    write_output_json(p, n);
}

/********************************
 *   HELPERS FOR MLQ
 ********************************/

static int all_done(Process p[], int n) {
    for (int i = 0; i < n; i++)
        if (!p[i].done) return 0;
    return 1;
}

static int find_next_ready(Process p[], int n, const char *type, int time) {
    int idx = -1;
    int earliest = 1000000000;
    for (int i = 0; i < n; i++) {
        if (!p[i].done && strcmp(p[i].type, type) == 0 && p[i].at <= time) {
            if (p[i].at < earliest) {
                earliest = p[i].at;
                idx = i;
            }
        }
    }
    return idx;
}

/********************************
 *   MLQ
 ********************************/

void runMLQ(Process p[], int n) {
    reset_processes(p, n);

    int time = 0;
    const int quantum = 3;

    while (!all_done(p, n)) {
        int progressed = 0;

        // Queue 1: urgent (RR-like)
        for (int i = 0; i < n; i++) {
            if (!p[i].done && strcmp(p[i].type, "urgent") == 0 && p[i].at <= time) {
                int slice = (p[i].rt > quantum) ? quantum : p[i].rt;
                p[i].rt -= slice;
                time += slice;
                progressed = 1;
                if (p[i].rt == 0) {
                    p[i].done = 1;
                    p[i].ct  = time;
                    p[i].tat = p[i].ct - p[i].at;
                    p[i].wt  = p[i].tat - p[i].bt;
                }
            }
        }
        if (progressed) continue;

        // Queue 2: regular (FCFS)
        int idx = find_next_ready(p, n, "regular", time);
        if (idx != -1) {
            time += p[idx].rt;
            p[idx].rt = 0;
            p[idx].done = 1;
            p[idx].ct  = time;
            p[idx].tat = p[idx].ct - p[idx].at;
            p[idx].wt  = p[idx].tat - p[idx].bt;
            continue;
        }

        // Queue 3: background (FCFS)
        idx = find_next_ready(p, n, "background", time);
        if (idx != -1) {
            time += p[idx].rt;
            p[idx].rt = 0;
            p[idx].done = 1;
            p[idx].ct  = time;
            p[idx].tat = p[idx].ct - p[idx].at;
            p[idx].wt  = p[idx].tat - p[idx].bt;
            continue;
        }

        // idle
        time++;
    }

    write_output_json(p, n);
}

/********************************
 *   MLFQ
 ********************************/

typedef struct {
    int items[256];
    int front, rear;
} Queue;

static void initQueue(Queue *q) { q->front = q->rear = 0; }
static int isEmpty(Queue *q) { return q->front == q->rear; }
static void enqueue(Queue *q, int x) { q->items[q->rear++] = x; }
static int dequeue(Queue *q) { return q->items[q->front++]; }

static void addArrivals(Process p[], int n, Queue *q1, int inQ[], int time) {
    for (int i = 0; i < n; i++) {
        if (!p[i].done && p[i].at <= time && !inQ[i]) {
            enqueue(q1, i);
            inQ[i] = 1;
        }
    }
}

void runMLFQ(Process p[], int n) {
    reset_processes(p, n);

    Queue q1, q2, q3;
    initQueue(&q1);
    initQueue(&q2);
    initQueue(&q3);

    int inQ[256] = {0};
    int finished = 0, time = 0;
    const int q1q = 3, q2q = 6;

    addArrivals(p, n, &q1, inQ, time);

    while (finished < n) {
        if (!isEmpty(&q1)) {
            int i = dequeue(&q1);
            inQ[i] = 0;

            int exec = (p[i].rt > q1q) ? q1q : p[i].rt;
            p[i].rt -= exec;
            time += exec;
            addArrivals(p, n, &q1, inQ, time);

            if (p[i].rt == 0) {
                p[i].done = 1;
                finished++;
                p[i].ct  = time;
                p[i].tat = p[i].ct - p[i].at;
                p[i].wt  = p[i].tat - p[i].bt;
            } else {
                enqueue(&q2, i);
                inQ[i] = 1;
            }
        }
        else if (!isEmpty(&q2)) {
            int i = dequeue(&q2);
            inQ[i] = 0;

            int exec = (p[i].rt > q2q) ? q2q : p[i].rt;
            p[i].rt -= exec;
            time += exec;
            addArrivals(p, n, &q1, inQ, time);

            if (p[i].rt == 0) {
                p[i].done = 1;
                finished++;
                p[i].ct  = time;
                p[i].tat = p[i].ct - p[i].at;
                p[i].wt  = p[i].tat - p[i].bt;
            } else {
                enqueue(&q3, i);
                inQ[i] = 1;
            }
        }
        else if (!isEmpty(&q3)) {
            int i = dequeue(&q3);
            inQ[i] = 0;

            time += p[i].rt;
            p[i].rt = 0;
            p[i].done = 1;
            finished++;
            p[i].ct  = time;
            p[i].tat = p[i].ct - p[i].at;
            p[i].wt  = p[i].tat - p[i].bt;

            addArrivals(p, n, &q1, inQ, time);
        }
        else {
            time++;
            addArrivals(p, n, &q1, inQ, time);
        }
    }

    write_output_json(p, n);
}
