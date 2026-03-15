#ifndef SIMULATION_THREADING_H
#define SIMULATION_THREADING_H

#include "types.h"

// Main simulation function for pure threading mode (terminal only)
// Creates all threads, monitors termination conditions, handles graceful shutdown
// Returns 0 on success, non-zero on error
int run_simulation_threading(SimulationState* sim);

// Main simulation function WITH GUI visualization
// Same as run_simulation_threading but opens OpenGL window for real-time visualization
// Returns 0 on success, non-zero on error
int run_simulation_with_gui(SimulationState* sim);

// Helper function to check if any termination condition is met
// Returns termination reason string, or NULL if simulation should continue
const char* check_termination_conditions(SimulationState* sim);

// Display final simulation statistics
void display_final_statistics(SimulationState* sim);

#endif