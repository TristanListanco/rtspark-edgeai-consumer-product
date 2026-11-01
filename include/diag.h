/* Integrated self-test: one live check per onboard component (handout task 2). */
#ifndef DIAG_H
#define DIAG_H

typedef enum { DIAG_PASS, DIAG_WARN, DIAG_FAIL, DIAG_ABSENT, DIAG_UNTESTED } diag_status_t;

typedef struct {
    const char *name;
    diag_status_t status;
    char detail[56];
} diag_item_t;

#define DIAG_MAX_ITEMS 20

/* Runs every check (takes ~0.3 s: waits for fresh sensor samples). Not reentrant. */
unsigned diag_run(diag_item_t *items);
void diag_print(const diag_item_t *items, unsigned n);
const char *diag_status_str(diag_status_t s);

#endif /* DIAG_H */
