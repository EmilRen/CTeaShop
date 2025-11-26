#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdarg.h>
#include <time.h>
#include <stdbool.h>

// Number of supplier and enthusiast threads.
#define NUM_SUPPLIERS    6
#define NUM_ENTHUSIASTS 14

//enumerate indices into the arrays for each ingredient.
enum ingredient {
    GREEN_LEAF = 0,
    BLACK_LEAF,
    BERGAMOT,
    HIBISCUS,
    PUFFED_RICE,
    SPICE,
    NUM_INGREDIENTS
};

//readable names in this array that is static to avoid threading issues 
static const char *ingredient_names[NUM_INGREDIENTS] = {
    "Green Tea Leaf",
    "Black Tea Leaf",
    "Bergamot Oil",
    "Hibiscus Leaf",
    "Puffed Rice",
    "Spices"
};


//fixed number of pouches each supplier will add when restocked in order 
//of the array that each value will restocked to
static int supply_amounts[NUM_INGREDIENTS] = {10, 8, 10, 3, 1, 6};

//how many pouches are available
static int available[NUM_INGREDIENTS] = {0};

static pthread_mutex_t table_mutex = PTHREAD_MUTEX_INITIALIZER;

static pthread_cond_t cond_vars[NUM_INGREDIENTS];

static int remaining_enthusiasts = NUM_ENTHUSIASTS;

//if the last tea drinker is done
static bool program_done = false;

//helper function for colors
static void print_coloured(int colour_code, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    printf("\033[0;%dm", colour_code);
    vprintf(fmt, args);
    printf("\033[0m");
    va_end(args);
}

//supplier thread. Wait til out then restock
static void *supplier_thread(void *arg)
{
    int idx = *(int *)arg;
    free(arg);

    while (true) {
        pthread_mutex_lock(&table_mutex);
        while (!program_done && available[idx] > 0) {
            pthread_cond_wait(&cond_vars[idx], &table_mutex);
        }
        if (program_done) {
            pthread_mutex_unlock(&table_mutex);
            print_coloured(31, "%s supplier is done\n", ingredient_names[idx]);
            return NULL;
        }
        //refill
        available[idx] += supply_amounts[idx];
        int added = supply_amounts[idx];
        //wake up those waiting for restock
        pthread_cond_broadcast(&cond_vars[idx]);
        pthread_mutex_unlock(&table_mutex);
        print_coloured(31,
                       "%s supplier added %d pouches of %s to the table.\n",
                       ingredient_names[idx], added, ingredient_names[idx]);
    }
    //never reached
    return NULL;
}

//random recipe generation 
static void generate_recipe(int counts[NUM_INGREDIENTS])
{
    for (int i = 0; i < NUM_INGREDIENTS; i++) {
        counts[i] = 0;
    }
    counts[GREEN_LEAF] = rand() % 2;
    counts[BLACK_LEAF] = rand() % 2;
    if (counts[GREEN_LEAF] + counts[BLACK_LEAF] == 0) {
        //At least one leaf by picking one randomly
        if (rand() % 2 == 0) {
            counts[GREEN_LEAF] = 1;
        } else {
            counts[BLACK_LEAF] = 1;
        }
    }
    //random amount picked 1-4 for rest on ingredients
    for (int i = BERGAMOT; i < NUM_INGREDIENTS; i++) {
        counts[i] = (rand() % 4) + 1;
    }
}

//Tea enthusiast thread.An enthusiast tries between 15 and 25 recipes.
static void *enthusiast_thread(void *arg)
{
    //cast the arg pointer from void to int
    int tid = *(int *)arg;
    free(arg);
    unsigned int seed = (unsigned int)time(NULL) ^ (unsigned int)tid;

    //determine how many recipes to try
    int recipes = (rand_r(&seed) % 11) + 15;

    for (int r = 0; r < recipes; r++) {
        int need[NUM_INGREDIENTS];
        generate_recipe(need);
        print_coloured(92,
                       "Tea enthusiast %d is requesting tea with these many pouches:\n",
                       tid);
        print_coloured(92, " %d %s, %d %s, %d %s, %d %s, %d %s, %d %s\n",
                       need[GREEN_LEAF], ingredient_names[GREEN_LEAF],
                       need[BLACK_LEAF], ingredient_names[BLACK_LEAF],
                       need[BERGAMOT], ingredient_names[BERGAMOT],
                       need[HIBISCUS], ingredient_names[HIBISCUS],
                       need[PUFFED_RICE], ingredient_names[PUFFED_RICE],
                       need[SPICE], ingredient_names[SPICE]);
        pthread_mutex_lock(&table_mutex);
        for (int i = 0; i < NUM_INGREDIENTS; i++) {
            for (int k = 0; k < need[i]; k++) {
                //wait if more supply isnt available
                while (available[i] == 0) {
                    pthread_cond_signal(&cond_vars[i]);
                    pthread_cond_wait(&cond_vars[i], &table_mutex);
                    if (program_done) {
                        break;
                    }
                }
                if (program_done) {
                    break;
                }
                //consume
                available[i]--;
                //if empty, signal
                if (available[i] == 0) {
                    pthread_cond_signal(&cond_vars[i]);
                }
            }
            if (program_done) {
                break;
            }
        }
        pthread_mutex_unlock(&table_mutex);
        print_coloured(92, "Tea for enthusiast %d is prepared\n", tid);
        //sleep to simualte drinking time
        usleep(rand_r(&seed) % 5000);
    }

    pthread_mutex_lock(&table_mutex);
    remaining_enthusiasts--;
    bool last_one = false;
    if (remaining_enthusiasts == 0) {
        //end program flag
        program_done = true;
        last_one = true;
        for (int i = 0; i < NUM_INGREDIENTS; i++) {
            pthread_cond_broadcast(&cond_vars[i]);
        }
    }
    pthread_mutex_unlock(&table_mutex);
    print_coloured(92, "Tea enthusiast %d is done\n", tid);
    if (last_one) {
        print_coloured(92, "Tea Enthusiasts have finished running\n");
    }
    return NULL;
}

int main(void)
{
    for (int i = 0; i < NUM_INGREDIENTS; i++) {
        pthread_cond_init(&cond_vars[i], NULL);
    }

    printf("CS 370 Project 5 - Tea Enthusiast Problem\n");

    //create threads for suppliers
    pthread_t suppliers[NUM_SUPPLIERS];
    for (int i = 0; i < NUM_SUPPLIERS; i++) {
        int *idx = malloc(sizeof(int));
        if (!idx) {
            fprintf(stderr, "Failed to allocate memory for supplier index\n");
            exit(EXIT_FAILURE);
        }
        *idx = i;
        if (pthread_create(&suppliers[i], NULL, supplier_thread, idx) != 0) {
            fprintf(stderr, "Failed to create supplier thread %d\n", i);
            exit(EXIT_FAILURE);
        }
    }

    //create enthusiasts threads
    pthread_t enthusiasts[NUM_ENTHUSIASTS];
    for (int i = 0; i < NUM_ENTHUSIASTS; i++) {
        int *tid = malloc(sizeof(int));
        if (!tid) {
            fprintf(stderr, "Failed to allocate memory for enthusiast id\n");
            exit(EXIT_FAILURE);
        }
        *tid = i + 1;
        if (pthread_create(&enthusiasts[i], NULL, enthusiast_thread, tid) != 0) {
            fprintf(stderr, "Failed to create tea enthusiast thread %d\n", i + 1);
            exit(EXIT_FAILURE);
        }
    }

    //wait for enthusiasts to finish
    for (int i = 0; i < NUM_ENTHUSIASTS; i++) {
        pthread_join(enthusiasts[i], NULL);
    }

    //wait for supplier threads to finish
    for (int i = 0; i < NUM_SUPPLIERS; i++) {
        pthread_join(suppliers[i], NULL);
    }

    print_coloured(31, "Suppliers have finished running\n");
    printf("Program Complete\n");
    return 0;
}