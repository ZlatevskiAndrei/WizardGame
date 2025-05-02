#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <assert.h>
#include <math.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <signal.h>
#include <sys/mman.h>


#define NUM_WIZARDS 20

#define ATTACK_DMG 2
#define HEAL_PTS 1

#define ATTACK 1
#define HEAL 0


struct Wizard {
    pid_t pid;
    int health;
    int team;
};

struct WizardAction {
    long pidFrom;
    long pidTo;
    int action;
};

struct Wizard* wizardList;
sigset_t mainsigset;
pid_t* mainPid;
struct WizardAction* action;
int* wizards_left;

pthread_mutex_t* mutex; 

void initialize_shared_mutex() {
    mutex = mmap(NULL, sizeof(pthread_mutex_t),PROT_READ | PROT_WRITE,MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (mutex == MAP_FAILED) {
        perror("mmap");
        exit(EXIT_FAILURE);
    }
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
    pthread_mutex_init(mutex, &attr);
    pthread_mutexattr_destroy(&attr);
}


void unmaskSignal(int sig, sigset_t* sigset){
    if(sigdelset(sigset,sig) != 0){
        fprintf(stderr, "Error whilst masking signal\n");
        exit(EXIT_FAILURE);
    }
    sigprocmask(SIG_SETMASK, sigset, NULL);
}

struct Wizard* getWizard(long pid){
    for(int i=0; i<NUM_WIZARDS; i++){
        if(wizardList[i].pid == pid) return &wizardList[i];
    }
    return NULL;
}

int getWizardIndex(struct Wizard* wizard){
    for(int i=0; i<NUM_WIZARDS; i++){
        if(&wizardList[i] == wizard) return i;
    }
    return -1;
}

void randomSleep(){
    sleep(rand()%5);
}

void wizard_routine(int sig){
    if(sig == SIGUSR1){
        long wizardPid = (long)getpid();
        struct Wizard* current_wizard = getWizard(wizardPid);
        assert(current_wizard != NULL);
        while(current_wizard->health > 0){
            randomSleep();
            if (pthread_mutex_lock(mutex)) {
                fprintf(stderr, "Lock Error\n");
                exit(EXIT_FAILURE);
            }
            struct Wizard* randomWizard; 
            if(*wizards_left == 1) {
                printf("WIZARD %ld WINS THE ROUND!\n",(long) current_wizard->pid);
                current_wizard = NULL;
                *wizards_left -= 1;
                if(kill(*mainPid,SIGUSR2) == -1){
                    fprintf(stderr,"Error whilst signaling main process\n");
                }
            }
            do {
                randomWizard = &wizardList[rand() % NUM_WIZARDS];
            }while(randomWizard == current_wizard || randomWizard == NULL);
            if(randomWizard->team != current_wizard->team){
                action->action = ATTACK;
                action->pidFrom = (long) current_wizard->pid;
                action->pidTo = (long) randomWizard->pid;
            }
    
            else {
                action->action = HEAL;
                action->pidFrom = (long) current_wizard->pid;
                action->pidTo = (long) randomWizard->pid;
            }
        
            if(kill(*mainPid,SIGUSR2) == -1){
                fprintf(stderr,"Error whilst signaling main process\n");
            }
    
            if (pthread_mutex_unlock(mutex)) {
                fprintf(stderr, "Unlock Error\n");
                exit(EXIT_FAILURE);
            }
        }
    }
}


void createXmagicians(int num_wizards){
    pid_t randomWizardPID;
    for(int i=0; i<num_wizards; i++){
        randomWizardPID = fork();
        if(randomWizardPID == -1){
            fprintf(stderr,"Error whilst creating a wizard number: %d\n",i);
            exit(EXIT_FAILURE);
        }
        if(randomWizardPID == 0){  
            sigset_t sigset;
            if(sigfillset(&sigset) != 0) {
                fprintf(stderr, "Signal set initialization error\n");
                exit(EXIT_FAILURE);
            }
            unmaskSignal(SIGUSR1,&sigset);
            unmaskSignal(SIGINT,&sigset);
            signal(SIGUSR1, wizard_routine);
            signal(SIGINT, exit);
            while (1) {
                pause();
            }
            exit(EXIT_SUCCESS);
        }
        else {
            fprintf(stdout,"Creating wizard %ld\n",(long)randomWizardPID);
            struct Wizard randomWizard = {randomWizardPID,10,-1};
            wizardList[i] = randomWizard;
        }
    }
    *wizards_left = NUM_WIZARDS;
}


int convertPidToInt(pid_t pid){
    long convertedPid = (long) pid;
    int result = 0;
    while(convertedPid){
        result += convertedPid % 10;
        convertedPid /= 10;
    }
    return result;
}

void initializeTeams() {
    for (int i = 0; i < NUM_WIZARDS; i++) {
        if (convertPidToInt(wizardList[i].pid) % 2 == 0) {
            wizardList[i].team = 2;
        } else {
            wizardList[i].team = 1;
        }
    }
}


void main_routine(int sig) {
    if(sig == SIGUSR2){
        if(*wizards_left <= 0){
            kill_all_processes(SIGQUIT);
            printf("Game over!\nTerminating main process...\n");
            exit(EXIT_SUCCESS);
        }
        if (pthread_mutex_lock(mutex)) {
            fprintf(stderr, "Lock Error\n");
            exit(EXIT_FAILURE);
        }
        long affectedWizardPid = action->pidTo;
        struct Wizard* affectedWizard = getWizard(affectedWizardPid);
        assert(affectedWizard != NULL);
    
        if (action->action == ATTACK) {
            affectedWizard->health -= ATTACK_DMG;
            printf("Wizard %ld attacked Wizard %ld! Health is now %d.\n",
                   action->pidFrom, action->pidTo, affectedWizard->health);
        } else {
            affectedWizard->health += HEAL_PTS;
            if (affectedWizard->health >= 10) {
                affectedWizard->health = 10;
            }
            printf("Wizard %ld healed Wizard %ld! Health is now %d.\n",
            action->pidFrom, action->pidTo, affectedWizard->health);
        }
    
        if (affectedWizard->health <= 0) {
            printf("Wizard %ld has been defeated by Wizard %ld!\n",action->pidTo, action->pidFrom);
            affectedWizard = NULL;
            *wizards_left -= 1;
            if (kill((pid_t)action->pidTo, SIGINT) == -1) {
                fprintf(stderr, "Error whilst signaling child process\n");
            }
            else {
                printf("Killed the wizard process: %ld\n",(long)action->pidTo);
            }
        }
    
        if (pthread_mutex_unlock(mutex)) {
            fprintf(stderr, "Unlock Error\n");
            exit(EXIT_FAILURE);
        }   
    }
}

void kill_all_processes(int sig){
    printf("Received SIGQUIT\n");
    if(sig == SIGQUIT){
        for(int i=0; i<NUM_WIZARDS; i++){
            if(kill(wizardList[i].pid,SIGINT) == -1){
                fprintf(stderr,"Error whilst signaling child process\n");
            }
            else {
                printf("Sent SIGINT to wizard %ld\n", (long)wizardList[i].pid);
            }
        }
    }
}

void* mmap_wrapper(size_t element_size, int arraySpots) {
    void* addr = mmap(NULL, element_size * arraySpots, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (addr == MAP_FAILED) {
        perror("mmap");
        exit(EXIT_FAILURE);
    }
    return addr;
}

int main(){
    srand(time(NULL));
    wizards_left = mmap_wrapper(sizeof(int),1);
    action = mmap_wrapper(sizeof(struct WizardAction), 1);
    mainPid = mmap_wrapper(sizeof(pid_t), 1);
    wizardList = mmap_wrapper(sizeof(struct Wizard), NUM_WIZARDS);
    *mainPid = getpid();
    printf("Main pid: %ld\n",(long) *mainPid);
    initialize_shared_mutex();
    if(sigfillset(&mainsigset) != 0) {
        fprintf(stderr, "Signal set initialization error\n");
        exit(EXIT_FAILURE);
    }
    unmaskSignal(SIGUSR2,&mainsigset);
    unmaskSignal(SIGINT,&mainsigset);
    unmaskSignal(SIGQUIT,&mainsigset);
    signal(SIGUSR2,main_routine);
    signal(SIGQUIT,kill_all_processes);
    createXmagicians(NUM_WIZARDS);
    initializeTeams();
    for(int i=0; i<NUM_WIZARDS; i++){
        if(kill(wizardList[i].pid,SIGUSR1) == -1){
            fprintf(stderr,"Error whilst signaling child process\n");
        }
        else {
            printf("Sent SIGUSR1 to wizard %ld\n", (long)wizardList[i].pid);
        }
    }
    while(1){
        pause();
    }
    pthread_mutex_destroy(mutex);
    munmap(mutex, sizeof(pthread_mutex_t));
    munmap(wizardList, sizeof(struct Wizard) * NUM_WIZARDS);
    munmap(action,sizeof(struct WizardAction));
    munmap(wizards_left,sizeof(int));
    munmap(mainPid,sizeof(pid_t));
    return 0;
}
