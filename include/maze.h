#ifndef MAZE_H
#define MAZE_H

#include "types.h"
#include <pthread.h>

// Allocation / cleanup
Maze* create_maze(int width, int height);
void destroy_maze(Maze* maze);  // Updated name

// File I/O
Maze* load_maze_from_file(const char* filepath);
int save_maze_to_file(const Maze* maze, const char* filepath);

// NEW: Wall-based maze generation
Maze* generate_random_maze(int size, double wall_removal_percent,
                          double banana_density, int max_bananas_per_cell,
                          const Position* family_homes, int num_families);

// NEW: Wall checking
int can_move_between(const Maze* maze, Position from, Position to);

// Banana distribution
void distribute_bananas_in_maze(Maze* maze, double banana_density, int max_per_cell);

// Connectivity / validation
int is_maze_connected(const Maze* maze);

/*
 * Place families on edges (unique positions, no obstacles).
 * Returns 1 on success, 0 on failure (e.g., not enough edge open cells).
 */
int assign_family_positions_edges(const Maze* maze, int num_families, Position* out_positions);

/*
 * Stronger validation:
 * - Must be connected
 * - Must have enough edge openings for families
 */
int validate_maze_for_families(const Maze* maze, int num_families);

//Terminal view
void print_maze_terminal(const Maze* maze,
                         const Position* family_homes,
                         int num_families,
                         pthread_mutex_t* console_mutex);

Maze* generate_valid_maze(const Config* cfg);

// a function to count the total number of bananas in the maze
int count_maze_bananas(const Maze* maze);


#endif