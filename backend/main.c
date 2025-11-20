#include <stdio.h>
#include <string.h>
#include <strings.h>
#include "scheduler.h"

static void print_results(Process p[], int n) {
    printf("\nPID\tTYPE\tAT\tBT\tCT\tTAT\tWT\n");
    for (int i = 0; i < n; i++) {
        printf("P%d\t%s\t%d\t%d\t%d\t%d\t%d\n",
               p[i].pid, p[i].type, p[i].at, p[i].bt,
               p[i].ct, p[i].tat, p[i].wt);
    }
}

int main(int argc, char *argv[]) {
    Process procs[512];

    int n = parse_transactions_json(procs, 512);
    if (n <= 0) {
        printf("No valid process data parsed.\n");
        return 1;
    }

    const char *algo = (argc >= 2) ? argv[1] : "MLQ";

    if (strcasecmp(algo, "FCFS") == 0) {
        printf("Running FCFS...\n");
        runFCFS(procs, n);
    }
    else if (strcasecmp(algo, "SJF") == 0) {
        printf("Running SJF (Non-Preemptive)...\n");
        runSJF(procs, n);
    }
    else if (strcasecmp(algo, "SJF-P") == 0 || strcasecmp(algo, "SJFP") == 0) {
        printf("Running SJF (Preemptive)...\n");
        runSJFPreemptive(procs, n);
    }
    else if (strcasecmp(algo, "PRIORITY") == 0) {
        printf("Running Priority (Non-Preemptive)...\n");
        runPriority(procs, n);
    }
    else if (strcasecmp(algo, "PRIORITY-P") == 0 || strcasecmp(algo, "PRIORITYP") == 0) {
        printf("Running Priority (Preemptive)...\n");
        runPriorityPreemptive(procs, n);
    }
    else if (strcasecmp(algo, "RR") == 0) {
        printf("Running Round Robin (q=3)...\n");
        runRR(procs, n, 3);
    }
    else if (strcasecmp(algo, "MLQ") == 0) {
        printf("Running Multi-Level Queue...\n");
        runMLQ(procs, n);
    }
    else if (strcasecmp(algo, "MLFQ") == 0) {
        printf("Running Multi-Level Feedback Queue...\n");
        runMLFQ(procs, n);
    }
    else {
        printf("Unknown algo '%s'. Defaulting to MLQ.\n", algo);
        runMLQ(procs, n);
    }

    print_results(procs, n);
    return 0;
}
