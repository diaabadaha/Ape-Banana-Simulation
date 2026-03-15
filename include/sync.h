#ifndef SYNC_H
#define SYNC_H

#include "types.h"

// Locking protocols
int compare_positions(Position a, Position b);
void lock_cells_in_order(Maze* maze, Position p1, Position p2);
void unlock_cells_in_reverse(Maze* maze, Position p1, Position p2);
void lock_baskets_in_order(Family* f1, Family* f2);
void unlock_baskets_in_reverse(Family* f1, Family* f2);

// Console output synchronization
void log_event(SimulationState* sim, const char* format, ...);

// Statistics updates (thread-safe)
void update_female_fight_stats(SimulationState* sim);
void update_male_fight_stats(SimulationState* sim);
void update_baby_steal_stats(SimulationState* sim, int count);
void update_total_bananas(SimulationState* sim, int bananas);

// Simulation control
void signal_simulation_stop(SimulationState* sim);
int is_simulation_running(SimulationState* sim);

// Rest mechanism
void start_resting(FemaleApe* ape);
void end_resting(FemaleApe* ape);

// ============================================================
// GUI Event Queue Functions
// ============================================================

// Initialize the GUI event queue (call once at simulation start)
void init_gui_event_queue(SimulationState* sim);

// Add a baby event to the queue (thread-safe, called from baby threads)
void push_gui_event(SimulationState* sim, GuiEventType type, 
                    int baby_family_id, int baby_id, 
                    int victim_family_id, int amount);

// Add a male fight event to the queue (thread-safe, called from male threads)
void push_gui_fight_event(SimulationState* sim, GuiEventType type,
                          int initiator_family_id, int defender_family_id,
                          int winner_family_id);

// Add a male rest event to the queue (thread-safe, called from male threads)
void push_gui_male_rest_event(SimulationState* sim, int family_id, int energy_gained);

// Get next event from queue (returns 1 if event available, 0 if empty)
// Called from GUI thread
int pop_gui_event(SimulationState* sim, GuiEvent* out_event);

// Peek at how many events are pending
int gui_event_count(SimulationState* sim);

#endif