/*
 * SYSC4001 – Assignment 3 – Part 2(b)
 * Student: 101231344
 *
 * Concurrent TAs marking exams – semaphore + shared memory version.
 *
 * Usage:
 *   ./part2b_101231344 <num_TAs> <rubric_file> <exam_list_file>
 *
 * This program converts Part 2(a) into a semaphore-based solution with
 * shared memory. The critical sections are protected by semaphores so
 * that only one TA:
 *   - modifies the rubric at a time,
 *   - chooses and marks a given question,
 *   - loads the next exam into shared memory.
 *
 * 
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
#include <semaphore.h>

#define NUM_QUESTIONS   5
#define MAX_EXAMS       256
#define MAX_PATH_LEN    256
#define RUBRIC_LINE_LEN 32

typedef struct {
    char rubric[NUM_QUESTIONS][RUBRIC_LINE_LEN];

    int  total_exams;
    char exam_filenames[MAX_EXAMS][MAX_PATH_LEN];
    int  exam_student_ids[MAX_EXAMS];

    int  current_exam_index;
    int  current_student_id;

    int  question_marked[NUM_QUESTIONS];

    int  finished;

    /* Semaphores in shared memory. */
    sem_t mutex_rubric;   /* protects rubric modifications */
    sem_t mutex_exam;     /* protects questions + exam loading */
    sem_t mutex_print;    /* serialises printing    */

} shared_data_t;

static int shm_id = -1;

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
        if (line[0] == '\0') continue;

        if (count >= MAX_EXAMS) {
            fprintf(stderr, "Too many exams (max %d)\n", MAX_EXAMS);
            fclose(f);
            exit(EXIT_FAILURE);
        }

        strncpy(shared->exam_filenames[count], line, MAX_PATH_LEN - 1);
        shared->exam_filenames[count][MAX_PATH_LEN - 1] = '\0';

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

/* Must be called with mutex_exam HELD. */

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

    sem_wait(&shared->mutex_print);
    printf("[PARENT/TA] Loaded exam %s (student %04d) into shared memory\n",
           shared->exam_filenames[idx],
           shared->current_student_id);
    fflush(stdout);
    sem_post(&shared->mutex_print);
}

static void review_rubric(shared_data_t *shared, int ta_id) {
    sem_wait(&shared->mutex_print);
    printf("[TA %d] Reviewing rubric for exam %04d\n",
           ta_id, shared->current_student_id);
    fflush(stdout);
    sem_post(&shared->mutex_print);

    for (int q = 0; q < NUM_QUESTIONS; ++q) {
        random_sleep(0.5, 1.0);

        int change = rand() % 2;
        if (change) {
            sem_wait(&shared->mutex_rubric);

            char *line = shared->rubric[q];
            char *comma = strchr(line, ',');
            if (comma) {
                char *p = comma + 1;
                while (*p == ' ') p++;
                if (*p >= 'A' && *p <= 'Z') {
                    char old = *p;
                    char newc = (old < 'Z') ? (old + 1) : old;
                    *p = newc;

                    sem_wait(&shared->mutex_print);
                    printf("[TA %d] Corrected rubric Q%d: %c -> %c\n",
                           ta_id, q + 1, old, newc);
                    fflush(stdout);
                    sem_post(&shared->mutex_print);
                }
            }

            sem_post(&shared->mutex_rubric);
        }
    }
}

/* Choose ONE question to mark and mark it.
 * Returns 1 if a question was marked, 0 otherwise. */

static int mark_one_question(shared_data_t *shared, int ta_id) {
    int q_to_mark = -1;
    int student;

    sem_wait(&shared->mutex_exam);

    if (shared->finished) {
        sem_post(&shared->mutex_exam);
        return 0;
    }

    student = shared->current_student_id;

    for (int q = 0; q < NUM_QUESTIONS; ++q) {
        if (shared->question_marked[q] == 0) {
            q_to_mark = q;
            shared->question_marked[q] = 1;  /* reserve this question */
            break;
        }
    }

    sem_post(&shared->mutex_exam);

    if (q_to_mark == -1) {
        return 0;   /* nothing left to mark for this exam */
    }

    random_sleep(1.0, 2.0);

    sem_wait(&shared->mutex_print);
    printf("[TA %d] Marked exam %04d, question %d\n",
           ta_id, student, q_to_mark + 1);
    fflush(stdout);
    sem_post(&shared->mutex_print);

    return 1;
}

/* Helper: assumes mutex_exam is already HELD. */

static int all_questions_marked_nolock(const shared_data_t *shared) {
    for (int q = 0; q < NUM_QUESTIONS; ++q) {
        if (shared->question_marked[q] == 0) return 0;
    }
    return 1;
}

static void ta_main(shared_data_t *shared, int ta_id) {
    srand((unsigned int)(time(NULL) ^ (getpid() << 16)));

    while (1) {
        /* First check if we are finished. */
        sem_wait(&shared->mutex_exam);
        if (shared->finished) {
            sem_post(&shared->mutex_exam);
            break;
        }
        int idx = shared->current_exam_index;
        int stu = shared->current_student_id;
        sem_post(&shared->mutex_exam);

        sem_wait(&shared->mutex_print);
        printf("[TA %d] Starting work on exam index %d (student %04d)\n",
               ta_id, idx, stu);
        fflush(stdout);
        sem_post(&shared->mutex_print);

        review_rubric(shared, ta_id);

        /* Mark questions until none available. */
        while (mark_one_question(shared, ta_id)) {
            /* keep marking */
        }

        /* Attempt to load the next exam – at most one TA succeeds. */
        sem_wait(&shared->mutex_exam);

        if (!shared->finished && all_questions_marked_nolock(shared)) {
            int next = shared->current_exam_index + 1;
            if (next >= shared->total_exams) {
                shared->finished = 1;
            } else {
                int next_student = shared->exam_student_ids[next];
                load_exam(shared, next);

                if (next_student == 9999) {
                    /* last exam logically; finished will be set
                     * once it has been fully marked.              */
                }
            }
        }

        /* After exam with student 9999 is fully marked, stop. */
        if (!shared->finished &&
            shared->current_student_id == 9999 &&
            all_questions_marked_nolock(shared)) {
            shared->finished = 1;
        }

        int done = shared->finished;
        sem_post(&shared->mutex_exam);

        if (done) break;
    }

    sem_wait(&shared->mutex_print);
    printf("[TA %d] Finishing execution\n", ta_id);
    fflush(stdout);
    sem_post(&shared->mutex_print);

    _exit(0);
}

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

    /* Initialise semaphores (pshared = 1 so they are shared between processes). */
  
    if (sem_init(&shared->mutex_rubric, 1, 1) == -1 ||
        sem_init(&shared->mutex_exam,   1, 1) == -1 ||
        sem_init(&shared->mutex_print,  1, 1) == -1) {
        perror("sem_init");
        return EXIT_FAILURE;
    }

    sem_wait(&shared->mutex_exam);
    shared->finished = 0;
    shared->current_exam_index = 0;
    load_exam(shared, 0);
    sem_post(&shared->mutex_exam);

    /* Fork TA processes. */
    for (int i = 0; i < num_TAs; ++i) {
        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            return EXIT_FAILURE;
        }
        if (pid == 0) {
            ta_main(shared, i + 1);
        }
    }

    /* Parent waits for children. */
    for (int i = 0; i < num_TAs; ++i) {
        wait(NULL);
    }

    /* Clean up. */
    sem_destroy(&shared->mutex_rubric);
    sem_destroy(&shared->mutex_exam);
    sem_destroy(&shared->mutex_print);

    shmdt(shared);
    shmctl(shm_id, IPC_RMID, NULL);

    return EXIT_SUCCESS;
}
