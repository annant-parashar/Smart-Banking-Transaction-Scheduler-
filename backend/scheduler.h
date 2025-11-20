#ifndef SCHEDULER_H
#define SCHEDULER_H

typedef struct {
    int pid;
    int at;      // arrival time
    int bt;      // burst time
    int rt;      // remaining time
    char type[20];  // urgent / regular / background (or mapped from banking types)
    int pr;      // priority value
    int ct;      // completion time
    int tat;     // turnaround time
    int wt;      // waiting time
    int done;    // flag
} Process;

// Parsing and output
int convertPriority(const char *type);
int parse_transactions_json(Process p[], int maxn);
void write_output_json(Process p[], int n);

// Scheduling algorithms
void runFCFS(Process p[], int n);
void runSJF(Process p[], int n);
void runSJFPreemptive(Process p[], int n);
void runPriority(Process p[], int n);
void runPriorityPreemptive(Process p[], int n);
void runRR(Process p[], int n, int quantum);
void runMLQ(Process p[], int n);
void runMLFQ(Process p[], int n);

#endif
