/*
 * SYSC4001 – Assignment 3 – Part 2(a)
 * Student: 101231344
 *
 * Concurrent TAs marking exams – race-condition version (no semaphores).
 *
 * Usage:
 *   ./part2a_101231344 <num_TAs> <rubric_file> <exam_list_file>
 *
 *   <num_TAs>        : n >= 2, one process per TA
 *   <rubric_file>    : text file with 5 lines, each "exercise, letter"
 *                      e.g. "1, A"
 *   <exam_list_file> : text file, one exam file path per line.
 *                      Each exam file contains one line with a 4-digit
 *                      student number (0001..9999). The file containing
 *                      9999 is used to terminate the simulation.
 *
 * Requirements satisfied:
 *   - n >= 2 processes running concurrently (one per TA).
 *   - Shared memory holds rubric + current exam info.
 *   - Each process prints what it is doing (reviewing rubric, marking).
 *   - NO critical-section protection (race conditions are possible).
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/wait.h>
#include <string.h>
#include <time.h>

#define NUM_QUESTIONS   5
#define MAX_EXAMS       256
#define MAX_PATH_LEN    256
#define RUBRIC_LINE_LEN 32

typedef struct {
    /* Rubric lines, e.g. "1, A" (no trailing newline). */
    char rubric[NUM_QUESTIONS][RUBRIC_LINE_LEN];

    /* Exam list (filenames) and student IDs. */
    int  total_exams;
    char exam_filenames[MAX_EXAMS][MAX_PATH_LEN];
    int  exam_student_ids[MAX_EXAMS];

    /* Current exam being processed. */
    int  current_exam_index;
    int  current_student_id;

    /* 0 = not marked yet, 1 = marked (racey in part 2a). */
    int  question_marked[NUM_QUESTIONS];

    /* 1 when execution should finish (after exam with student 9999). */
    int  finished;

} shared_data_t;

static int shm_id = -1;

/**/
/* Utility helpers */
/**/

static void random_sleep(double min_sec, double max_sec) {
    double r = (double)rand() / (double)RAND_MAX;
    double s = min_sec + r * (max_sec - min_sec);
    if (s < 0.0) s = 0.0;
    usleep((useconds_t)(s * 1e6));
}

static void trim_newline(char *s) {
    size_t len = strlen(s);
    if (len > 0 && (s[len - 1] == '\n' || s[len - 1] == '\r')) {
        s[len - 1] = '\0';
    }
}

/* Load rubric from file into shared memory (5 lines). */
static void load_rubric(const char *filename, shared_data_t *shared) {
    FILE *f = fopen(filename, "r");
    if (!f) {
        perror("fopen rubric");
        exit(EXIT_FAILURE);
    }

    for (int i = 0; i < NUM_QUESTIONS; ++i) {
        if (!fgets(shared->rubric[i], RUBRIC_LINE_LEN, f)) {
            fprintf(stderr, "Rubric file must contain %d lines\n", NUM_QUESTIONS);
            fclose(f);
            exit(EXIT_FAILURE);
        }
        trim_newline(shared->rubric[i]);
    }

    fclose(f);
}

/* Load exam filenames + student IDs into shared memory. */
static void load_exam_list(const char *list_file, shared_data_t *shared) {
    FILE *f = fopen(list_file, "r");
    if (!f) {
        perror("fopen exam list");
        exit(EXIT_FAILURE);
    }

    char line[MAX_PATH_LEN];
    int count = 0;

    while (fgets(line, sizeof(line), f)) {
        trim_newline(line);
        if (line[0] == '\0') {
            continue;   /* skip blank lines */
        }
        if (count >= MAX_EXAMS) {
            fprintf(stderr, "Too many exams (max %d)\n", MAX_EXAMS);
            fclose(f);
            exit(EXIT_FAILURE);
        }

        /* store filename */
        strncpy(shared->exam_filenames[count], line, MAX_PATH_LEN - 1);
        shared->exam_filenames[count][MAX_PATH_LEN - 1] = '\0';

        /* open exam file to read student ID */
        FILE *ef = fopen(line, "r");
        if (!ef) {
            perror("fopen exam file");
            fprintf(stderr, "File: %s\n", line);
            fclose(f);
            exit(EXIT_FAILURE);
        }
        char idbuf[16];
        if (!fgets(idbuf, sizeof(idbuf), ef)) {
            fprintf(stderr, "Exam file %s must contain a student number\n", line);
            fclose(ef);
            fclose(f);
            exit(EXIT_FAILURE);
        }
        fclose(ef);
        trim_newline(idbuf);
        shared->exam_student_ids[count] = atoi(idbuf);
        count++;
    }

    fclose(f);

    if (count == 0) {
        fprintf(stderr, "Exam list is empty\n");
        exit(EXIT_FAILURE);
    }

    shared->total_exams = count;
}

/* Load exam at index idx into shared memory (current_student_id + reset marks). */
static void load_exam(shared_data_t *shared, int idx) {
    if (idx < 0 || idx >= shared->total_exams) {
        shared->finished = 1;
        return;
    }

    shared->current_exam_index = idx;
    shared->current_student_id = shared->exam_student_ids[idx];

    for (int i = 0; i < NUM_QUESTIONS; ++i) {
        shared->question_marked[i] = 0;
    }

    printf("[PARENT/TA] Loaded exam %s (student %04d) into shared memory\n",
           shared->exam_filenames[idx],
           shared->current_student_id);
    fflush(stdout);
}

/**/
/* TA logic – PART 2(a), NO synchronisation */
/**/

static void review_rubric(shared_data_t *shared, int ta_id) {
    printf("[TA %d] Reviewing rubric for exam %04d\n",
           ta_id, shared->current_student_id);
    fflush(stdout);

    for (int q = 0; q < NUM_QUESTIONS; ++q) {
        /* Each decision takes between 0.5 and 1.0 seconds. */
        random_sleep(0.5, 1.0);

        int change = rand() % 2;   /* randomly decide if we correct this question’s rubric */
        if (change) {
            char *line = shared->rubric[q];
            char *comma = strchr(line, ',');
            if (comma) {
                char *p = comma + 1;
                while (*p == ' ') p++;       /* skip spaces after comma */

                if (*p >= 'A' && *p <= 'Z') {
                    char old = *p;
                    char newc = (old < 'Z') ? (old + 1) : old; /* next ASCII (C -> D etc.) */
                    *p = newc;

                    printf("[TA %d] Corrected rubric Q%d: %c -> %c\n",
                           ta_id, q + 1, old, newc);
                    fflush(stdout);
                }
            }
        }
    }
}

/* Mark all questions for the current exam (racey in this part). */
static void mark_questions(shared_data_t *shared, int ta_id) {
    int student = shared->current_student_id;

    for (int q = 0; q < NUM_QUESTIONS; ++q) {
        /* In part 2(a) we DO NOT protect question_marked[], so
         * different TAs can race and mark the same question.        */
        if (shared->question_marked[q] == 0) {
            shared->question_marked[q] = 1;

            /* Marking one question takes between 1.0 and 2.0 seconds. */
            random_sleep(1.0, 2.0);

            printf("[TA %d] Marked exam %04d, question %d\n",
                   ta_id, student, q + 1);
            fflush(stdout);
        }
    }
}

/* Check if all questions appear marked. */
static int all_questions_marked(const shared_data_t *shared) {
    for (int q = 0; q < NUM_QUESTIONS; ++q) {
        if (shared->question_marked[q] == 0) return 0;
    }
    return 1;
}

/* TA process entry point – loops over exams until finished flag is set. */
static void ta_main(shared_data_t *shared, int ta_id) {
    srand((unsigned int)(time(NULL) ^ (getpid() << 16)));

    while (!shared->finished) {
        int idx = shared->current_exam_index;
        int stu = shared->current_student_id;

        printf("[TA %d] Starting work on exam index %d (student %04d)\n",
               ta_id, idx, stu);
        fflush(stdout);

        review_rubric(shared, ta_id);
        mark_questions(shared, ta_id);

        /* Try to move to next exam.
         * NO protection here: multiple TAs may do this concurrently. */
        if (all_questions_marked(shared) &&
            shared->current_exam_index == idx &&
            !shared->finished) {

            int next = idx + 1;
            if (next >= shared->total_exams) {
                shared->finished = 1;
            } else {
                int next_student = shared->exam_student_ids[next];
                load_exam(shared, next);

                if (next_student == 9999) {
                    /* Last exam logically; finished will be set once it is done. */
                }
            }
        }

        /* If we are working on student 9999 and all questions are marked,
         * end the whole execution.                                       */
        if (shared->current_student_id == 9999 &&
            all_questions_marked(shared)) {
            shared->finished = 1;
        }
    }

    printf("[TA %d] Finishing execution\n", ta_id);
    fflush(stdout);

    _exit(0);
}

/**/
/* main */
/**/

int main(int argc, char *argv[]) {
    if (argc != 4) {
        fprintf(stderr,
                "Usage: %s <num_TAs>=n>=2 <rubric_file> <exam_list_file>\n",
                argv[0]);
        return EXIT_FAILURE;
    }

    int num_TAs = atoi(argv[1]);
    const char *rubric_file = argv[2];
    const char *exam_list_file = argv[3];

    if (num_TAs < 2) {
        fprintf(stderr, "Error: number of TAs (processes) must be >= 2\n");
        return EXIT_FAILURE;
    }

    /* Create shared memory segment. */
    shm_id = shmget(IPC_PRIVATE, sizeof(shared_data_t),
                    IPC_CREAT | 0666);
    if (shm_id < 0) {
        perror("shmget");
        return EXIT_FAILURE;
    }

    shared_data_t *shared =
        (shared_data_t *)shmat(shm_id, NULL, 0);
    if (shared == (void *)-1) {
        perror("shmat");
        return EXIT_FAILURE;
    }

    memset(shared, 0, sizeof(*shared));

    load_rubric(rubric_file, shared);
    load_exam_list(exam_list_file, shared);

    shared->finished = 0;
    shared->current_exam_index = 0;
    load_exam(shared, 0);    /* Load first exam into shared memory. */

    /* Fork TA processes. */
    for (int i = 0; i < num_TAs; ++i) {
        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            return EXIT_FAILURE;
        }
        if (pid == 0) {
            ta_main(shared, i + 1); /* child */
        }
    }

    /* Parent waits for all children. */
    for (int i = 0; i < num_TAs; ++i) {
        wait(NULL);
    }

    printf("[PARENT] All TAs finished. Cleaning up.\n");
    fflush(stdout);

    shmdt(shared);
    shmctl(shm_id, IPC_RMID, NULL);

    return EXIT_SUCCESS;
}
