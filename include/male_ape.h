#ifndef MALE_APE_H
#define MALE_APE_H

#include "types.h"

// Main thread function for male ape behavior
// Handles: guarding, neighbor detection, fighting, energy management, withdrawal
void* male_ape_thread(void* arg);

// Calculate fight probability based on basket contents and maze total
// Returns probability between 0.0 and 1.0
double calculate_fight_probability(Family* my_family, Family* neighbor_family, 
                            const Config* config, const Maze* maze);

#endif