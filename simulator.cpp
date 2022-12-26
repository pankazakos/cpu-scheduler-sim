#include <cmath>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <iostream>
#include <list>
#include <semaphore.h>
#include <string>
#include <unistd.h>
#include <vector>

// Define an upper bound of timeslots to prevent infinite loop in case something
// breaks
#define UPPER_BOUND 50
// Change to 0 for seed = time(NULL)
#define DEFAULT_SEED 0

using namespace std;

struct process {
  int arrival;
  int lifetime;
  int priority;
  double k;
  int index; // remember the index of process when accessed by queue
  bool started;
  int uptime;  // time to execute up
  int idx_sem; // remember semaphore to execute up
  bool waiting;
  bool deleted;
  process(int arrival, int lifetime, int priority, double k, int index)
      : arrival(arrival), lifetime(lifetime), priority(priority), k(k),
        index(index), started(false), uptime(-1), waiting(false),
        deleted(false){};
  ~process(){};
  void down(sem_t *sem) { sem_wait(sem); }
  void up(sem_t *sem) { sem_post(sem); }
};

// exponential distribution
int exp_dist(double m) {
  double u;
  u = rand() / (RAND_MAX + 1.0);
  return round(-m * log(1 - u));
}

// uniform distribution
int uni_dist(int a, int b) {
  double u;
  u = rand() / (RAND_MAX + 1.0);
  return round(a + u * (b - a));
}

int main(int argc, char *argv[]) {

  if (argc - 1 != 6) {
    cerr << "Expected 6 arguments" << endl;
    return 1;
  }

  if (DEFAULT_SEED) {
    srand(1220);
  } else {
    srand(time(NULL));
  }

  const double mean_proc = atof(argv[1]);
  const double mean_lifetime = atof(argv[2]);
  const double mean_exec = atof(argv[3]);
  const int num_processes = atoi(argv[4]);
  const double k = atof(argv[5]);
  const int S = atoi(argv[6]);

  // Create S unnamed semaphores
  sem_t **sem = new sem_t *[S];

  for (int i = 0; i < S; i++) {
    sem[i] = new sem_t;
    if (sem_init(sem[i], 0, 1) < 0) {
      cerr << "sem_init(3): failed" << endl;
      exit(EXIT_FAILURE);
    }
  }

  // Create array of processes
  process **p = new process *[num_processes];

  // Assert lifetime is not 0
  int lifetime = 0;
  while (lifetime == 0) {
    lifetime = exp_dist(mean_lifetime);
  }

  p[0] = new process(exp_dist(mean_proc), lifetime, uni_dist(1, 7), k, 0);

  for (int i = 1; i < num_processes; i++) {
    int offset = p[i - 1]->arrival;
    // Assert lifetime is not 0
    int lifetime = 0;
    while (lifetime == 0) {
      lifetime = exp_dist(mean_lifetime);
    }
    p[i] = new process(exp_dist(mean_proc) + offset, lifetime, uni_dist(1, 7),
                       k, i);
  }

  // Print data of processes
  cout << "Process \t Arrival \t lifetime \t priority\n";
  for (int i = 0; i < num_processes; i++) {
    cout << i << " \t\t " << p[i]->arrival << " \t\t " << p[i]->lifetime
         << " \t\t " << p[i]->priority << endl;
  }

  // Create multilevel queue consisting of 7 queues (1-7 -> 0-6)
  list<process *> **queues = new list<process *> *[7];

  for (int i = 0; i < 7; i++) {
    queues[i] = new list<process *>;
  }

  int processes_stopped = 0;

  // Execute the cpu scheduler simulator while there are active processes
  int timeslot = 0;
  while (processes_stopped < num_processes && timeslot <= UPPER_BOUND) {
    cout << "------------------------------------------------------------------"
            "---------------\n";
    cout << "timeslot: " << timeslot << endl;

    // For each timeslot do various checks on all processes
    for (int i = 0; i < num_processes; i++) {

      if (p[i]->deleted) {
        continue;
      }

      // Time to execute up from process i
      if (p[i]->uptime == timeslot) {
        cout << "Process " << i << " is executing up() on semaphore "
             << p[i]->idx_sem << endl;
        p[i]->up(sem[p[i]->idx_sem]);
        p[i]->uptime = -1;
        p[i]->waiting = false;
      }

      // Put processes that will be waiting inside the correct queue
      if (p[i]->arrival <= timeslot && !p[i]->waiting) {
        int level = p[i]->priority - 1;
        queues[level]->push_back(p[i]);
        p[i]->waiting = true;
      }

      // Decrease lifetime and remove processes with 0 lifetime left
      if (p[i]->started) {

        p[i]->lifetime--;

        if (p[i]->lifetime == 0) {
          // Remove process waiting in queue when it has no lifetime left
          int level = p[i]->priority - 1;
          for (list<process *>::iterator it = queues[level]->begin();
               it != queues[level]->end(); it++) {
            if ((*it)->index == p[i]->index) {
              queues[level]->erase(it);
              break;
            }
          }
          // Remove process from queue
          cout << "\033[1;31mRemoved process " << p[i]->index << "\033[m"
               << endl;
          p[i]->deleted = true;
          processes_stopped++;
        }
      }
    }

    int value;
    int r = rand() % S;

    // Check value of the random semaphore before executing down to prevent
    // blocking the simulator
    sem_getvalue(sem[r], &value);

    if (value) {
      // Begin checking from biggest priority and continue to the rest
      for (int i = 0; i < 7; i++) {
        if (queues[i]->empty()) {
          continue;
        }
        bool stop = false;
        // Iterate each queue
        for (list<process *>::iterator it = queues[i]->begin();
             it != queues[i]->end(); ++it) {

          // take a random number u and check whether probability succeeds or
          // fails
          double u = rand() / (RAND_MAX + 1.0);
          cout << "Process " << (*it)->index << " got random number u: " << u
               << " (";
          if (u < (*it)->k) {
            cout << "\033[1;32msucceeded\033[m)" << endl;

            // Assert exectime is not 0 or bigger than lifetime at first attempt
            // to not prevent it from starting at arrival
            int exectime = 0;
            while (exectime == 0 ||
                   (!(*it)->started && exectime > (*it)->lifetime)) {
              exectime = exp_dist(mean_exec);
            }
            // Cannot allow down for more seconds than the lifetime left of a
            // process
            if (exectime > (*it)->lifetime) {
              cout << "Process " << (*it)->index << " tried to down semaphore "
                   << r << " for " << exectime
                   << " seconds but has not enough lifetime(" << (*it)->lifetime
                   << ")" << endl;
            }
            // Execute down
            else {
              cout << "Process " << (*it)->index
                   << " is executing down() on semaphore " << r << " for "
                   << exectime << " seconds" << endl;
              int idx = (*it)->index;
              p[idx]->idx_sem = r;
              p[idx]->down(sem[r]);
              p[idx]->uptime = timeslot + exectime;
              p[idx]->started = true;
              queues[p[idx]->priority - 1]->erase(it);
              stop = true;
              break;
            }
          } else {
            cout << "\033[1;31mfailed\033[m)" << endl;
          }
        }
        if (stop) {
          break;
        }
      }
    }
    cout << endl;
    timeslot++;
  }

  // Deallocate multilevel queue
  for (int i = 0; i < 7; i++) {
    delete queues[i];
  }

  delete[] queues;

  // Destroy unnamed semaphores
  for (int i = 0; i < S; i++) {
    if (sem_destroy(sem[i]) < 0) {
      cerr << "sem_destroy(3): failed" << endl;
      exit(EXIT_FAILURE);
    }
    delete sem[i];
  }

  delete[] sem;

  // Destroy processes
  for (int i = 0; i < num_processes; i++) {
    delete p[i];
  }

  delete[] p;

  return 0;
}
