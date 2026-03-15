#ifndef BABY_APE_H
#define BABY_APE_H

#include "types.h"

// Main thread function for baby ape behavior
// Handles: waiting for fights, stealing from opponents, eat/add decision
void* baby_ape_thread(void* arg);

#endif