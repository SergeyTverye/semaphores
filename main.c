#include <stdio.h>
#include <stdlib.h>
#include <pthread.h> // for threadns and mutex
#include <sys/mman.h> // for shared memory
#include <fcntl.h> // for shared memory
#include <unistd.h>
//#include <bits/syscall.h>
#include <sys/syscall.h>

// Parameters
#define MAX_ITEMS 10
#define MAX_CUSTOMERS 10
#define MAX_WAITERS 3
#define MAX_NAME_LEN 15

// Catalogs for common memory
#define SHM_NAME_MENU "/my_shm_menu"
#define SHM_NAME_BOARD "/my_shm_board"

typedef struct {
    int id;
    char name[MAX_NAME_LEN];
    float price;
    int totalOrdered;
} MenuItem;

typedef struct {
    int customerId;
    int itemId;
    int amount;
    int done; // true if no active order
} OrderBoard;

typedef struct {
    int simulationTime;
    struct timespec startTime;
    int numItems;
    int numCustomers;
    int numWaiters;
} Config;

#define SHM_NAME_COMFIG "/my_shm_config"
#define CONFIG_SIZE sizeof(Config)

pthread_mutex_t mutexOutput;
pthread_mutex_t mutexMenu;
pthread_mutex_t mutexOrders;
pthread_mutex_t mutexError;

void* customerThreadFunction(void* arg);
void* waiterThreadFunction(void* arg);

Config* getConfigFromSHM();
OrderBoard* getOrdersFromSHM(Config* config);
MenuItem* getMenuFromSHM(Config* config);
double get_elapsed_time(Config* config);

int main(int argc, char *argv[]) {
    if (argc != 5) {
        fprintf(stderr,"Usage: %s <num_menu_items> <num_waiters> <num_customers> but receive only %d parameters\n", argv[0], argc);
        exit(EXIT_FAILURE);
    }
    srand(time(NULL)); // инициализируем генератор случайных чисел
    // Validate and get command line arguments
    int simulationTime = atoi(argv[1]);
    int numItems = atoi(argv[2]);
    int numCustomers = atoi(argv[3]);
    int numWaiters = atoi(argv[4]);
    // Validation of inputs
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
    // Save config to common memory
    // Создание shared memory
    int fd = shm_open(SHM_NAME_COMFIG, O_CREAT | O_RDWR, 0666);
    if (fd == -1) {
        perror("shm_open");
        exit(EXIT_FAILURE);
    }
    // Установка размера shared memory
    if (ftruncate(fd, CONFIG_SIZE) == -1) {
        perror("ftruncate");
        exit(EXIT_FAILURE);
    }
    // Отображение shared memory в адресное пространство процесса
    Config *config = mmap(NULL, CONFIG_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (config == MAP_FAILED) {
        perror("mmap");
        exit(EXIT_FAILURE);
    }
    config->simulationTime = simulationTime;
    config->numItems = numItems;
    config->numCustomers = numCustomers;
    config->numWaiters = numWaiters;
    //// Make menu in shared memory
    // Open shared memory object
    int fd2 = shm_open(SHM_NAME_MENU, O_CREAT | O_RDWR, 0666);
    if (fd2 == -1) {
        perror("shm_open");
        exit(EXIT_FAILURE);
    }
    // Set the size of the shared memory object
    if (ftruncate(fd2, numItems * sizeof(MenuItem)) == -1) {
        perror("ftruncate");
        exit(EXIT_FAILURE);
    }
    // Map the shared memory object into this process's memory
    MenuItem *menu = mmap(NULL, numItems * sizeof(MenuItem), PROT_READ | PROT_WRITE, MAP_SHARED, fd2, 0);
    if (menu == MAP_FAILED) {
        perror("mmap");
        exit(EXIT_FAILURE);
    }
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
    //// Make Orders Board in shared memory
    // Open shared memory object
    int fd3 = shm_open(SHM_NAME_BOARD, O_CREAT | O_RDWR, 0666);
    if (fd3== -1) {
        perror("shm_open");
        exit(EXIT_FAILURE);
    }
    // Set the size of the shared memory object
    if (ftruncate(fd3, numCustomers * sizeof(OrderBoard)) == -1) {
        perror("ftruncate");
        exit(EXIT_FAILURE);
    }
    // Map the shared memory object into this process's memory
    OrderBoard *orders = mmap(NULL, numCustomers * sizeof(OrderBoard), PROT_READ | PROT_WRITE, MAP_SHARED, fd3, 0);
    if (orders == MAP_FAILED) {
        perror("mmap");
        exit(EXIT_FAILURE);
    }
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
        thread_ids[i] = i + 1; // Идентификатор потока
        pthread_create(&threads[i], NULL, customerThreadFunction, &thread_ids[i]);
    }
    // For waiters
    pthread_t threads2[numWaiters];
    int thread_ids2[numWaiters];
    for (int i = 0; i < numWaiters; ++i) {
        thread_ids2[i] = i + 1; // Идентификатор потока
        pthread_create(&threads2[i], NULL, waiterThreadFunction, &thread_ids2[i]);
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
    munmap(menu, numItems * sizeof(MenuItem));
    munmap(orders, numCustomers * sizeof(OrderBoard));
    // Close the shared memory objects
    close(fd);
    close(fd2);
    close(fd3);
    // Remove shared memory objects
    shm_unlink(SHM_NAME_COMFIG);
    shm_unlink(SHM_NAME_MENU);
    shm_unlink(SHM_NAME_BOARD);
    return 0;
}

// Code for Customer thread
void* customerThreadFunction(void* arg) {
    int cust_id = *(int *)arg;
    Config *config = getConfigFromSHM();
    OrderBoard *orders = getOrdersFromSHM(config);
    MenuItem *menu = getMenuFromSHM(config);
    while (get_elapsed_time(config) <= (double)(config->simulationTime)) { // until simulation time runs out
        sleep(rand()%4 + 3); // random wait 3-6 seconds
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
    munmap(orders, config->numCustomers * sizeof(OrderBoard));
    munmap(menu, config->numItems * sizeof(MenuItem));
    munmap(config, CONFIG_SIZE);
    pthread_exit(NULL);
}

// Code for Waiter thread
void* waiterThreadFunction(void* arg) {
    int wait_id = *(int *)arg;
    Config *config = getConfigFromSHM();
    OrderBoard *orders = getOrdersFromSHM(config);
    MenuItem *menu = getMenuFromSHM(config);

    while (get_elapsed_time(config) <= (double)(config->simulationTime)) { // until simulation time runs out
        sleep(rand() % 2 + 1); // random wait 1-2 seconds
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
    munmap(orders, config->numCustomers * sizeof(OrderBoard));
    munmap(menu, config->numItems * sizeof(MenuItem));
    munmap(config, CONFIG_SIZE);
    pthread_exit(NULL);
}

Config* getConfigFromSHM(){
    int fd1 = shm_open(SHM_NAME_COMFIG, O_RDWR, 0666);
    if (fd1 == -1) {pthread_mutex_lock(&mutexError); perror("shm_open"); pthread_mutex_unlock(&mutexError); exit(EXIT_FAILURE);}
    Config *config = mmap(NULL, CONFIG_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd1, 0);
    if (config == MAP_FAILED) {pthread_mutex_lock(&mutexError); perror("mmap"); pthread_mutex_unlock(&mutexError); exit(EXIT_FAILURE);}
    return config;
}

OrderBoard* getOrdersFromSHM(Config* config){
    int fd2 = shm_open(SHM_NAME_BOARD, O_RDWR, 0666);
    if (fd2== -1) {pthread_mutex_lock(&mutexError); perror("shm_open"); pthread_mutex_unlock(&mutexError); exit(EXIT_FAILURE);}
    OrderBoard *orders = mmap(NULL, config->numCustomers * sizeof(OrderBoard), PROT_READ | PROT_WRITE, MAP_SHARED, fd2, 0);
    if (orders == MAP_FAILED) {pthread_mutex_lock(&mutexError); perror("mmap"); pthread_mutex_unlock(&mutexError); exit(EXIT_FAILURE);}
    return orders;
}
MenuItem* getMenuFromSHM(Config* config){
    int fd3 = shm_open(SHM_NAME_MENU, O_RDWR, 0666);
    if (fd3== -1) {pthread_mutex_lock(&mutexError); perror("shm_open"); pthread_mutex_unlock(&mutexError); exit(EXIT_FAILURE);}
    MenuItem *menu = mmap(NULL, config->numItems * sizeof(MenuItem), PROT_READ | PROT_WRITE, MAP_SHARED, fd3, 0);
    if (menu == MAP_FAILED) {pthread_mutex_lock(&mutexError); perror("mmap"); pthread_mutex_unlock(&mutexError); exit(EXIT_FAILURE);}
    return menu;
}

// Функция для получения времени, прошедшего с момента запуска в секундах
double get_elapsed_time(Config* config) {
    struct timespec current_time;
    clock_gettime(CLOCK_MONOTONIC, &current_time);
    return (current_time.tv_sec - config->startTime.tv_sec) +
           (current_time.tv_nsec - config->startTime.tv_nsec) / 1e9;
}
