#ifndef SIM_STATE_H
#define SIM_STATE_H

#include "types.h"

// Initialization
int init_simulation_state(SimulationState* sim, Config* config);
int allocate_families(SimulationState* sim);

// Cleanup
void cleanup_simulation_state(SimulationState* sim);

// Family management
void withdraw_family(Family* family, SimulationState* sim);

// Neighbor detection - CALL THIS AFTER MAZE IS LOADED AND FAMILY POSITIONS SET
void update_all_neighbors(SimulationState* sim);

#endif