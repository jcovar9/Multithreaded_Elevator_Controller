#include"elevator.h"
#include<unistd.h>
#include<stdlib.h>
#include<stdio.h>
#include<pthread.h>

// PASSENGER //
struct passenger {
    int id;
    int from_floor;
    int to_floor;
    int E_id;
    bool exited;
    pthread_cond_t P_signal;
    pthread_mutex_t P_lock;
}Plist[PASSENGERS];

// ELEVATOR //
struct elevator {
    int id;
    int currfloor;
    int direction;
    int numPassengers;
    int is_open;
    pthread_cond_t E_signal;
    pthread_mutex_t E_lock;
}Elist[ELEVATORS];

/////////////////////////////////////////////////////////////////////////////////////
//// HELPER FUNCTIONS ////
// Returns true if there is an open elevator on "floor" that doesn't match "id" // ** LOCKS/UNLOCKS Elist AS IT LOOPS
bool other_elev_open(int id, int floor) {
    for(int i = 0; i < ELEVATORS; i++) {
        pthread_mutex_lock(&Elist[i].E_lock);
        if((Elist[i].is_open == 1 && i != id && Elist[i].currfloor == floor) ||
            Elist[i].currfloor == floor && i != id && i < id) {
            pthread_mutex_unlock(&Elist[i].E_lock);
            return true;
        }
        pthread_mutex_unlock(&Elist[i].E_lock);
    }
    return false;
}

// Returns id of first open elevator on specified floor // ** LOCKS/UNLOCKS Elist AS IT LOOPS
int elev_open_here(int floor) {
    int num = -1;
    for(int i = 0; i < ELEVATORS; i++) {
        pthread_mutex_lock(&Elist[i].E_lock);
        if(Elist[i].currfloor == floor && Elist[i].is_open == 1 && Elist[i].numPassengers < MAX_CAPACITY) {
            num = Elist[i].id;
            pthread_mutex_unlock(&Elist[i].E_lock);
            return num;
        }
        pthread_mutex_unlock(&Elist[i].E_lock);
    }
    return num;
}

// Counts the number of passengers wanting to get on // ** LOCKS/UNLOCKS Plist AS IT LOOPS
int numgettingon(int floor) {
    int count = 0;
    for(int i = 0; i < PASSENGERS; i++) {
        pthread_mutex_lock(&Plist[i].P_lock);
        if(Plist[i].from_floor == floor && Plist[i].E_id == -1 && Plist[i].exited == false) {
            count++;
        }
        pthread_mutex_unlock(&Plist[i].P_lock);
    }
    return count;
}

// Returns the first index of a passenger that needs to enter // ** LOCKS/UNLOCKS Plist AS IT LOOPS
int get_entering_p(int floor) {
    for(int i = 0; i < PASSENGERS; i++) {
        pthread_mutex_lock(&Plist[i].P_lock);
        if(Plist[i].from_floor == floor && Plist[i].E_id == -1 && Plist[i].exited == false) {
            pthread_mutex_unlock(&Plist[i].P_lock);
            return i;
        }
        pthread_mutex_unlock(&Plist[i].P_lock);
    }
    return -1;
}

// Counts the number of passengers getting off // ** LOCKS/UNLOCKS Plist AS IT LOOPS
int numgettingoff(int id, int floor) {
    int count = 0;
    for(int i = 0; i < PASSENGERS; i++) {
        pthread_mutex_lock(&Plist[i].P_lock);
        if(Plist[i].E_id == id && Plist[i].to_floor == floor && Plist[i].exited == false) {
            count++;
        }
        pthread_mutex_unlock(&Plist[i].P_lock);
    }
    return count;
}

// Returns the first index of a passenger that needs to exit // ** LOCKS/UNLOCKS Plist AS IT LOOPS
int get_exiting_p(int E_id, int floor) {
    for(int i = 0; i < PASSENGERS; i++) {
        pthread_mutex_lock(&Plist[i].P_lock);
        if(Plist[i].E_id == E_id && Plist[i].to_floor == floor && Plist[i].exited == false) {
            pthread_mutex_unlock(&Plist[i].P_lock);
            return i;
        }
        pthread_mutex_unlock(&Plist[i].P_lock);
    }
    return -1;
}
/////////////////////////////////////////////////////////////////////////////////////

// Initializes the locks and all the elevators and passengers //
void scheduler_init() {
    for(int i = 0; i < ELEVATORS; i++) {
        Elist[i].id = i;
        Elist[i].currfloor = 0;
        Elist[i].direction = 1;
        Elist[i].is_open = 0;
        Elist[i].numPassengers = 0;
        pthread_cond_init(&Elist[i].E_signal, 0);
        pthread_mutex_init(&Elist[i].E_lock, 0);
    }
    for(int i = 0; i < PASSENGERS; i++) {
        Plist[i].id = i;
        Plist[i].from_floor = -1;
        Plist[i].to_floor = -1;
        Plist[i].E_id = -1;
        Plist[i].exited = false;
        pthread_cond_init(&Plist[i].P_signal, 0);
        pthread_mutex_init(&Plist[i].P_lock, 0);
    }
}

void passenger_request(int passenger, int from_floor, int to_floor, void (*enter)(int, int), void(*exit)(int, int)) {

    // Initialize this passenger
    pthread_mutex_lock(&Plist[passenger].P_lock);               // lock P //
    Plist[passenger].from_floor = from_floor;
    Plist[passenger].to_floor = to_floor;
    Plist[passenger].exited = false;
    Plist[passenger].E_id = -1;
    // Save pthread vars
    pthread_cond_t* mysignal = &Plist[passenger].P_signal;
    pthread_mutex_unlock(&Plist[passenger].P_lock);             // unlock P //

    // Wait for an elevator to send me a signal
    int elev = -1;
    while(elev == -1 ) {
        pthread_mutex_lock(&Plist[passenger].P_lock);           // lock P //
        pthread_cond_wait(&Plist[passenger].P_signal, &Plist[passenger].P_lock);
        pthread_mutex_unlock(&Plist[passenger].P_lock);         // unlock P //
        elev = elev_open_here(from_floor);
    }
    if(from_floor == to_floor){
        pthread_mutex_lock(&Plist[passenger].P_lock);           // lock P //
        enter(passenger, elev);
        exit(passenger, elev);
        Plist[passenger].exited = true;
        pthread_mutex_unlock(&Plist[passenger].P_lock);         // unlock P //

        pthread_mutex_lock(&Elist[elev].E_lock);                // lock E //
        usleep(100);
        pthread_cond_broadcast(&Elist[elev].E_signal);
        pthread_mutex_unlock(&Elist[elev].E_lock);                // lock E //
    } else {

    // Enter the elevator
    pthread_mutex_lock(&Plist[passenger].P_lock);               // lock P //
    enter(passenger, elev);
    Plist[passenger].E_id = elev;
    pthread_mutex_unlock(&Plist[passenger].P_lock);             // unlock P //
    pthread_mutex_lock(&Elist[elev].E_lock);                    // lock E //
    Elist[elev].numPassengers++;

    // I let elevator know I have entered
    pthread_cond_broadcast(&Elist[elev].E_signal);

    // I wait for the elevator to signal me to exit
    pthread_mutex_lock(&Plist[passenger].P_lock);               // lock P //
    while(!(Elist[elev].currfloor == to_floor && Elist[elev].is_open == 1)){
        pthread_mutex_unlock(&Elist[elev].E_lock);                  // unlock E //
        pthread_cond_wait(&Plist[passenger].P_signal, &Plist[passenger].P_lock);
        pthread_mutex_lock(&Elist[elev].E_lock);                    // lock E //
    }
    pthread_mutex_unlock(&Elist[elev].E_lock);                  // unlock E //
    // I exit elevator
    exit(passenger, elev);
    pthread_mutex_unlock(&Plist[passenger].P_lock);             // unlock P //


    pthread_mutex_lock(&Elist[elev].E_lock);                    // lock E //
    Elist[elev].numPassengers--;

    // I let elevator know I have exited
    pthread_cond_broadcast(&Elist[elev].E_signal);
    pthread_mutex_unlock(&Elist[elev].E_lock);                  // unlock E //

    pthread_mutex_lock(&Plist[passenger].P_lock);               // lock P //
    Plist[passenger].exited = true;
    pthread_mutex_unlock(&Plist[passenger].P_lock);             // unlock P //
    }
}

void elevator_ready(int elevator, int at_floor, void(*move_direction)(int, int), void(*door_open)(int), void(*door_close)(int)) {

    pthread_mutex_lock(&Elist[elevator].E_lock);                // lock E //
    // Change elevator direction if need be //
    if(at_floor == FLOORS-1)
        Elist[elevator].direction = -1;
    if(at_floor == 0)  
        Elist[elevator].direction = 1;
    pthread_mutex_unlock(&Elist[elevator].E_lock);              // unlock E //

    ///////////////////////////////////////////////////
    // GETTING OFF //

    // Get number of exiting passengers for this floor
    int num_exiting = numgettingoff(elevator, at_floor);

    if(num_exiting > 0) {

        // Open doors and set local vars
        pthread_mutex_lock(&Elist[elevator].E_lock);            // lock E //
        door_open(elevator);
        Elist[elevator].is_open = 1;
        int old_p_count = Elist[elevator].numPassengers;
        pthread_mutex_unlock(&Elist[elevator].E_lock);          // unlock E //
        int new_p_count = old_p_count;

        // Loop through all passengers that need to exit
        while(num_exiting > 0) {
            // Get exiting passenger and signal them to exit
            int exiting_p = get_exiting_p(elevator, at_floor);
            if(exiting_p == -1) {
                break;
            }
            pthread_mutex_lock(&Plist[exiting_p].P_lock);       // lock P //
            pthread_mutex_lock(&Elist[elevator].E_lock);        // lock E //
            pthread_cond_broadcast(&Plist[exiting_p].P_signal);
            pthread_mutex_unlock(&Plist[exiting_p].P_lock);     // unlock P //

            // Wait for the exiting passenger to exit
            while(old_p_count == new_p_count) {
                pthread_cond_wait(&Elist[elevator].E_signal, &Elist[elevator].E_lock);
                new_p_count = Elist[elevator].numPassengers;
            }
            pthread_mutex_unlock(&Elist[elevator].E_lock);      // unlock E //

            num_exiting--;
            old_p_count = new_p_count;
        }
    }

    ///////////////////////////////////////////////////
    // GETTING ON //

    // Get number of entering passengers for this floor and local vars
    
    pthread_mutex_lock(&Elist[elevator].E_lock);                // lock E //
    int room = MAX_CAPACITY - Elist[elevator].numPassengers;
    pthread_mutex_unlock(&Elist[elevator].E_lock);              // unlock E //

    int num_entering = numgettingon(at_floor);

    if(num_entering > 0 && room > 0 && (!other_elev_open(elevator, at_floor)) ) {
        
        // Open elevator if need be
        pthread_mutex_lock(&Elist[elevator].E_lock);            // lock E //
        if(Elist[elevator].is_open == 0) {
            door_open(elevator);
            Elist[elevator].is_open = 1;
        }
        int old_p_count = Elist[elevator].numPassengers;

        pthread_mutex_unlock(&Elist[elevator].E_lock);          // unlock E //
        int new_p_count = old_p_count;

        // Allow as many passengers to enter as possible
        while( room > 0 && num_entering > 0) {

            // Get entering passenger and signal them to enter
            int entering_p = get_entering_p(at_floor);
            if(entering_p == -1) {
                break;
            }

            pthread_mutex_lock(&Elist[elevator].E_lock);        // lock E //
            pthread_mutex_lock(&Plist[entering_p].P_lock);      // lock P //
            pthread_cond_broadcast(&Plist[entering_p].P_signal);

            // Wait for entering passenger to enter
            if(Plist[entering_p].from_floor == Plist[entering_p].to_floor) {
                pthread_mutex_unlock(&Plist[entering_p].P_lock);    // unlock P //
                new_p_count = Elist[elevator].numPassengers;
            } else {
                pthread_mutex_unlock(&Plist[entering_p].P_lock);    // unlock P //
                while(old_p_count == new_p_count) {
                    pthread_cond_wait(&Elist[elevator].E_signal, &Elist[elevator].E_lock);
                    new_p_count = Elist[elevator].numPassengers;
                }
            }
            room = MAX_CAPACITY - Elist[elevator].numPassengers;
            pthread_mutex_unlock(&Elist[elevator].E_lock);      // unlock E //
            old_p_count = new_p_count;
            num_entering--;
        }
    }

    ///////////////////////////////////////////////////
    
    pthread_mutex_lock(&Elist[elevator].E_lock);                // lock E //
    if(Elist[elevator].is_open == 1) {
        door_close(elevator);
        Elist[elevator].is_open = 0;
    }
    Elist[elevator].currfloor += Elist[elevator].direction;
    move_direction(elevator,Elist[elevator].direction);
    pthread_mutex_unlock(&Elist[elevator].E_lock);              // unlock E //
}