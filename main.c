#include <stdio.h>
#include <stdlib.h>
#include <pthread.h> // for threads and mutex
#include <sys/mman.h> // for shared memory
#include <fcntl.h> // for shared memory
#include <unistd.h>
#include <sys/syscall.h>
#include <string.h>
#include <errno.h>

// Parameters
#define MAX_ITEMS 10
#define MAX_CUSTOMERS 10
#define MAX_WAITERS 3
#define MAX_NAME_LEN 15
#define MIN_SLEEP_TIME_FOR_CUSTOMER 3
#define MAX_SLEEP_TIME_FOR_CUSTOMER 6
#define MIN_SLEEP_TIME_FOR_WAITER 1
#define MAX_SLEEP_TIME_FOR_WAITER 2

// Catalogs for shared memory
#define SHM_NAME_MENU "/my_shm_menu"
#define SHM_NAME_BOARD "/my_shm_orders_board"
#define SHM_NAME_CONFIG "/my_shm_config"
#define MENU_SIZE sizeof(Menu)
#define ORDERS_BOARD_SIZE sizeof(OrdersBoard)
#define CONFIG_SIZE sizeof(Config)

typedef struct {
    int id;
    char name[MAX_NAME_LEN];
    float price;
    int totalOrdered;
} Menu;

typedef struct {
    int customerId;
    int itemId;
    int amount;
    int done; // true if no active order
} OrdersBoard;

typedef struct {
    int simulationTime;
    struct timespec startTime;
    int numItems;
    int numCustomers;
    int numWaiters;
} Config;

pthread_mutex_t mutexOutput;
pthread_mutex_t mutexMenu;
pthread_mutex_t mutexOrders;
pthread_mutex_t mutexError;

void* customerThreadFunction(void* arg);
void* waiterThreadFunction(void* arg);

void* createSharedMemory(const char* name, size_t size);
Config* getConfigFromSHM();
OrdersBoard* getOrdersFromSHM(Config* config);
Menu* getMenuFromSHM(Config* config);
double get_elapsed_time(Config* config);

int main(int argc, char *argv[]) {
    if (argc != 5) {
        fprintf(stderr,"Usage: %s <num_menu_items> <num_waiters> <num_customers> but receive only %d parameters\n", argv[0], argc);
        exit(EXIT_FAILURE);
    }
    srand(time(NULL)); // initialize the random number generator
    // Get and validate command line arguments
    int simulationTime = atoi(argv[1]); int numItems = atoi(argv[2]);
    int numCustomers = atoi(argv[3]);   int numWaiters = atoi(argv[4]);
    if (numItems < 1 || numItems > MAX_ITEMS ||
        numWaiters < 1 || numWaiters > MAX_WAITERS ||
        numCustomers < 1 || numCustomers > MAX_CUSTOMERS ||
        simulationTime < 1 || simulationTime > 29) // as specified in the terms of reference
    {
        fprintf(stderr,"Invalid input parameters.\n");
        exit(EXIT_FAILURE);
    }
    // Output the simulation parameters
    printf("Simulation Parameters:\n");
    printf("-> Simulation time: %d\n", simulationTime);
    printf("-> Menu items count: %d\n", numItems);
    printf("-> Customers count: %d\n", numCustomers);
    printf("-> Waiters count: %d\n", numWaiters);
    printf("\n");
    // Save config to shared memory
    Config * config = createSharedMemory(SHM_NAME_CONFIG, CONFIG_SIZE);
    config->simulationTime = simulationTime;
    config->numItems = numItems;
    config->numCustomers = numCustomers;
    config->numWaiters = numWaiters;
    //Make menu in shared memory
    Menu* menu = createSharedMemory(SHM_NAME_MENU, MENU_SIZE);
    // Initialize menu
    char* potentialMenuItems[] = {"Pizza", "Salad", "Hamburger", "Spaghetti", "Pie", "Milkshake", "Banana", "Cucumber", "Orange", "Watermelon"};
    for (int i = 0; i < numItems; i++) {
        menu[i].id = i;
        snprintf(menu[i].name, MAX_NAME_LEN, "%s", potentialMenuItems[i]);
        menu[i].price = (float)(rand() % 101);
        menu[i].totalOrdered = 0;
    }
    printf("Restaurant menu:\n");
    for (int i = 0; i < numItems; i++) {
        printf("ID: %d Name: %s Price: %0.2f Ordered: %d\n", menu[i].id , menu[i].name, menu[i].price , menu[i].totalOrdered );
    }
    printf("\n");
    // Make Orders Board in shared memory
    OrdersBoard * orders = createSharedMemory(SHM_NAME_BOARD, numCustomers * ORDERS_BOARD_SIZE);
    // Initialize orders board
    for (int i = 0; i < numCustomers; i++) {
        orders[i].customerId = i;
        orders[i].itemId = -1;
        orders[i].amount = 0;
        orders[i].done = 1; // true initially
    }
    // Mutex's initit
    pthread_mutex_init(&mutexOutput, NULL);
    pthread_mutex_init(&mutexMenu, NULL);
    pthread_mutex_init(&mutexOrders, NULL);
    pthread_mutex_init(&mutexError, NULL);
    //  Save the start of simulation time
    clock_gettime(CLOCK_MONOTONIC, &config->startTime);
    // Run threads
    // For clients
    pthread_t threads[numCustomers];
    int thread_ids[numCustomers];
    for (int i = 0; i < numCustomers; ++i) {
        thread_ids[i] = i + 1; // thread id
        int result = pthread_create(&threads[i], NULL, customerThreadFunction, &thread_ids[i]);
        if (result != 0) {
            fprintf(stderr, "Failed to create thread: %s\n", strerror(result));
            exit(EXIT_FAILURE);
        }
    }
    // For waiters
    pthread_t threads2[numWaiters];
    int thread_ids2[numWaiters];
    for (int i = 0; i < numWaiters; ++i) {
        thread_ids2[i] = i + 1; // thread id
        int result = pthread_create(&threads2[i], NULL, waiterThreadFunction, &thread_ids2[i]);
        if (result != 0) {
            fprintf(stderr, "Failed to create thread: %s\n", strerror(result));
            exit(EXIT_FAILURE);
        }
    }
    // Waiting for customers threads end
    for (int i = 0; i < numCustomers; ++i) {
        pthread_join(threads[i], NULL);
    }
    // Waiting for waiters threads end
    for (int i = 0; i < numWaiters; ++i) {
        pthread_join(threads2[i], NULL);
    }
    // Display the total information of the simulation
    int totalOrders = 0;
    float totalIncome = 0.0;
    for (int i = 0; i < numItems; i++) {
        totalOrders += menu[i].totalOrdered;
        totalIncome += menu[i].totalOrdered * menu[i].price;
    }
    printf("\nRestaurant menu by the end:\n");
    for (int i = 0; i < numItems; i++) {
        printf("ID: %d Name: %s Price: %0.2f Ordered: %d\n", menu[i].id , menu[i].name, menu[i].price , menu[i].totalOrdered );
    }
    printf("\nTotal number of ordered items: %d\n", totalOrders);
    printf("Total income of the restaurant: %.2f shekels\n", totalIncome);
    // Destroy mutexes
    pthread_mutex_destroy(&mutexOutput);
    pthread_mutex_destroy(&mutexMenu);
    pthread_mutex_destroy(&mutexOrders);
    // Unmap shared memory
    munmap(config, CONFIG_SIZE);
    munmap(menu, numItems * MENU_SIZE);
    munmap(orders, numCustomers * ORDERS_BOARD_SIZE);
    // Remove shared memory objects
    shm_unlink(SHM_NAME_CONFIG);
    shm_unlink(SHM_NAME_MENU);
    shm_unlink(SHM_NAME_BOARD);
    return 0;
}

// Code for Customer thread
void* customerThreadFunction(void* arg) {
    int cust_id = *(int *)arg;
    Config *config = getConfigFromSHM();
    OrdersBoard *orders = getOrdersFromSHM(config);
    Menu *menu = getMenuFromSHM(config);
    while (get_elapsed_time(config) <= (double)(config->simulationTime)) { // until simulation time runs out
        sleep(rand()%(MAX_SLEEP_TIME_FOR_CUSTOMER - MIN_SLEEP_TIME_FOR_CUSTOMER + 1) + MIN_SLEEP_TIME_FOR_CUSTOMER);
        pthread_mutex_lock(&mutexOutput);
            printf("Time: %.3f, Customer %d (TID %ld) is reading the menu.\n", get_elapsed_time(config), cust_id, syscall(SYS_gettid));
        pthread_mutex_unlock(&mutexOutput);
        sleep(1); // reading menu for 1 second

        if (orders[cust_id].done == 0) continue;
        // 50% probability of ordering
        if (rand() % 2 && (get_elapsed_time(config) <= (double)(config->simulationTime))) {
            pthread_mutex_lock(&mutexOrders);
                orders[cust_id].itemId = rand() % config->numItems;
                orders[cust_id].amount = rand() % 4 + 1; // random amount 1-4
                orders[cust_id].done = 0; // set order to not done
            pthread_mutex_unlock(&mutexOrders);
            pthread_mutex_lock(&mutexOutput);
                printf("Time: %.3f, Customer %d (TID %ld) ordered %d of %s.\n",
                       get_elapsed_time(config), cust_id, syscall(SYS_gettid), orders[cust_id].amount, menu[orders[cust_id].itemId].name);
            pthread_mutex_unlock(&mutexOutput);
        }
    }
    pthread_mutex_lock(&mutexOutput);
        printf("Time: %.3f, Customer %d (TID %ld) gone to home.\n",
               get_elapsed_time(config), cust_id, syscall(SYS_gettid));
    pthread_mutex_unlock(&mutexOutput);
    munmap(orders, config->numCustomers * sizeof(OrdersBoard));
    munmap(menu, config->numItems * sizeof(Menu));
    munmap(config, CONFIG_SIZE);
    pthread_exit(NULL);
}

// Code for Waiter thread
void* waiterThreadFunction(void* arg) {
    int wait_id = *(int *)arg;
    Config *config = getConfigFromSHM();
    OrdersBoard *orders = getOrdersFromSHM(config);
    Menu *menu = getMenuFromSHM(config);
    while (get_elapsed_time(config) <= (double)(config->simulationTime)) { // until simulation time runs out
        sleep(rand() % 2 + 1); // random wait 1-2 seconds
        sleep(rand()%(MAX_SLEEP_TIME_FOR_WAITER - MIN_SLEEP_TIME_FOR_WAITER + 1) + MIN_SLEEP_TIME_FOR_WAITER);
        pthread_mutex_lock(&mutexOrders);
            for (int j = 0; j < config->numCustomers; j++) {
                if (orders[j].done == 0) {
                    menu[orders[j].itemId].totalOrdered += orders[j].amount;
                    orders[j].done = 1; // order is done
                    pthread_mutex_lock(&mutexOutput);
                        printf("Time: %.3f, Waiter %d (TID %ld) completed order for Customer %d.\n",
                               get_elapsed_time(config), wait_id, syscall(SYS_gettid), j);
                    pthread_mutex_unlock(&mutexOutput);
                    break;
                }
            }
        pthread_mutex_unlock(&mutexOrders);
    }

    pthread_mutex_lock(&mutexOutput);
        printf("Time: %.3f, Waiter %d (TID %ld) finished his work.\n",
               get_elapsed_time(config), wait_id, syscall(SYS_gettid));
    pthread_mutex_unlock(&mutexOutput);
    munmap(orders, config->numCustomers * sizeof(OrdersBoard));
    munmap(menu, config->numItems * sizeof(Menu));
    munmap(config, CONFIG_SIZE);
    pthread_exit(NULL);
}

void* createSharedMemory(const char* name, size_t size) {
    int fd = shm_open(name, O_CREAT | O_RDWR, 0666);
    if (fd == -1) {
        perror("shm_open");
        exit(EXIT_FAILURE);
    }
    if (ftruncate(fd, size) == -1) {
        perror("ftruncate");
        exit(EXIT_FAILURE);
    }
    void* addr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (addr == MAP_FAILED) {
        perror("mmap");
        exit(EXIT_FAILURE);
    }
    return addr;
}

Config* getConfigFromSHM(){
    int fd1 = shm_open(SHM_NAME_CONFIG, O_RDWR, 0666);
    if (fd1 == -1) {pthread_mutex_lock(&mutexError); fprintf(stderr, "Error in shm_open: %s\n", strerror(errno)); pthread_mutex_unlock(&mutexError); exit(EXIT_FAILURE);}
    Config *config = mmap(NULL, CONFIG_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd1, 0);
    if (config == MAP_FAILED) {pthread_mutex_lock(&mutexError); fprintf(stderr, "Error in shm mapping %s\n", strerror(errno)); pthread_mutex_unlock(&mutexError); exit(EXIT_FAILURE);}
    return config;
}

OrdersBoard* getOrdersFromSHM(Config* config){
    int fd2 = shm_open(SHM_NAME_BOARD, O_RDWR, 0666);
    if (fd2== -1) {pthread_mutex_lock(&mutexError); fprintf(stderr, "Error in shm_open: %s\n", strerror(errno)); pthread_mutex_unlock(&mutexError); exit(EXIT_FAILURE);}
    OrdersBoard *orders = mmap(NULL, config->numCustomers * ORDERS_BOARD_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd2, 0);
    if (orders == MAP_FAILED) {pthread_mutex_lock(&mutexError); perror("mmap"); pthread_mutex_unlock(&mutexError); exit(EXIT_FAILURE);}
    return orders;
}
Menu* getMenuFromSHM(Config* config){
    int fd3 = shm_open(SHM_NAME_MENU, O_RDWR, 0666);
    if (fd3== -1) {pthread_mutex_lock(&mutexError); fprintf(stderr, "Error in shm_open: %s\n", strerror(errno)); pthread_mutex_unlock(&mutexError); exit(EXIT_FAILURE);}
    Menu *menu = mmap(NULL, config->numItems * MENU_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd3, 0);
    if (menu == MAP_FAILED) {pthread_mutex_lock(&mutexError); fprintf(stderr, "Error in shm mapping %s\n", strerror(errno)); pthread_mutex_unlock(&mutexError); exit(EXIT_FAILURE);}
    return menu;
}

// Function to get time elapsed since startup in seconds
double get_elapsed_time(Config* config) {
    struct timespec current_time;
    clock_gettime(CLOCK_MONOTONIC, &current_time);
    return (current_time.tv_sec - config->startTime.tv_sec) +
           (current_time.tv_nsec - config->startTime.tv_nsec) / 1e9;
}
