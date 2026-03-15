// src/menu_handler.c
#include "../include/menu_handler.h"
#include "../include/maze.h"
#include "../include/config.h"
#include "../include/sim_state.h"
#include "../include/sync.h"
#include "../include/simulation_threading.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* =========================================================
 * Internal helpers
 * ========================================================= */

static void replace_maze_in_sim(SimulationState* sim, Maze* new_maze) {
    if (!sim) return;

    if (sim->maze) {
        destroy_maze(sim->maze);
        sim->maze = NULL;
    }

    if (sim->family_homes) {
        free(sim->family_homes);
        sim->family_homes = NULL;
    }

    sim->maze = new_maze;
}

static int allocate_family_homes(SimulationState* sim) {
    if (!sim || !sim->config) return 0;

    sim->num_families = sim->config->num_families;
    if (sim->num_families <= 0) return 0;

    sim->family_homes =
        (Position*)malloc((size_t)sim->num_families * sizeof(Position));

    return sim->family_homes != NULL;
}

/*
 * Maze generator using wall-based system
 * Uses recursive backtracking from maze.c
 */
static Maze* generate_valid_maze_from_config(const Config* cfg) {
    if (!cfg) return NULL;

    // Create maze with wall-based generation
    Maze* maze = generate_random_maze(
        cfg->maze_size,
        cfg->wall_removal_percent,  // NEW: Use wall removal instead of obstacles
        cfg->banana_density,
        cfg->max_bananas_per_cell,
        NULL,  // Family homes assigned later
        0      // No families yet
    );
    
    if (!maze) return NULL;

    // Validate connectivity and edge positions
    if (!validate_maze_for_families(maze, cfg->num_families)) {
        destroy_maze(maze);
        return NULL;
    }

    return maze;
}

/* =========================================================
 * Unimplemented simulation modes (future steps)
 * ========================================================= */

static void run_threading_mode(SimulationState* sim) {
    // Check if maze is loaded (use printf, not log_event, since sim might not be initialized)
    if (!sim || !sim->maze) {
        printf("\n[ERROR] No maze loaded. Please load a maze first (options 4-7).\n\n");
        return;
    }

    // Initialize simulation state
    if (!init_simulation_state(sim, sim->config)) {
        printf("[ERROR] Failed to initialize simulation state.\n");
        return;
    }

    // Assign family positions to the maze edges
    if (!allocate_family_homes(sim) ||
        !assign_family_positions_edges(sim->maze,
                                      sim->num_families,
                                      sim->family_homes)) {
        log_event(sim, "[ERROR] Failed to assign family positions.\n");
        cleanup_simulation_state(sim);
        return;
    }

    // CRITICAL: Update neighbor relationships after positions are set
    update_all_neighbors(sim);

    // Run the full simulation
    run_simulation_threading(sim);

    // Cleanup
    cleanup_simulation_state(sim);
}

static void run_gui_mode(SimulationState* sim) {
    // Check if maze is loaded
    if (!sim || !sim->maze) {
        printf("\n[ERROR] No maze loaded. Maze should be auto-loaded at startup.\n\n");
        return;
    }

    // Initialize simulation state
    if (!init_simulation_state(sim, sim->config)) {
        printf("[ERROR] Failed to initialize simulation state.\n");
        return;
    }

    // Assign family positions to the maze edges
    if (!allocate_family_homes(sim) ||
        !assign_family_positions_edges(sim->maze,
                                      sim->num_families,
                                      sim->family_homes)) {
        log_event(sim, "[ERROR] Failed to assign family positions.\n");
        cleanup_simulation_state(sim);
        return;
    }

    // CRITICAL: Update neighbor relationships after positions are set
    update_all_neighbors(sim);

    // Run the simulation WITH GUI
    run_simulation_with_gui(sim);

    // Cleanup
    cleanup_simulation_state(sim);
}

static void run_hybrid_mode(SimulationState* sim) {
    (void)sim;
    printf("[TODO] run_hybrid_mode not implemented yet.\n");
}

static void compare_results(SimulationState* sim) {
    (void)sim;
    printf("[TODO] compare_results not implemented yet.\n");
}

static void edit_config(void) {
    system("nano config/arguments.txt");
    printf("[INFO] Config edited. Restart program to reload.\n");
}

/* =========================================================
 * Step 4 – Maze menu actions (updated with neighbor update)
 * ========================================================= */

static void load_default_maze(SimulationState* sim) {
    if (!sim || !sim->config) return;

    Maze* maze = load_maze_from_file("config/maze.txt");

    if (!maze || !validate_maze_for_families(maze, sim->config->num_families)) {
        printf("[INFO] Default maze missing or invalid. Generating random maze...\n");

        if (maze) destroy_maze(maze);
        maze = generate_valid_maze_from_config(sim->config);

        if (!maze) {
            printf("[ERROR] Failed to generate a valid maze.\n");
            return;
        }

        save_maze_to_file(maze, "config/maze.txt");
    }

    replace_maze_in_sim(sim, maze);

    if (!allocate_family_homes(sim) ||
        !assign_family_positions_edges(sim->maze,
                                       sim->num_families,
                                       sim->family_homes)) {
        printf("[ERROR] Failed to assign family start positions.\n");
        return;
    }

    // UPDATE: Set neighbor relationships after loading maze
    if (sim->families) {
        update_all_neighbors(sim);
    }

    print_maze_terminal(sim->maze,
                        sim->family_homes,
                        sim->num_families,
                        &sim->console_mutex);
}

static void handle_generate_maze(SimulationState* sim) {
    if (!sim || !sim->config) return;

    Maze* maze = generate_valid_maze_from_config(sim->config);
    if (!maze) {
        printf("[ERROR] Failed to generate a valid maze.\n");
        return;
    }

    replace_maze_in_sim(sim, maze);

    if (!allocate_family_homes(sim) ||
        !assign_family_positions_edges(sim->maze,
                                       sim->num_families,
                                       sim->family_homes)) {
        printf("[ERROR] Failed to assign family start positions.\n");
        return;
    }

    // UPDATE: Set neighbor relationships after generating maze
    if (sim->families) {
        update_all_neighbors(sim);
    }

    print_maze_terminal(sim->maze,
                        sim->family_homes,
                        sim->num_families,
                        &sim->console_mutex);
}

static void load_custom_maze(SimulationState* sim) {
    if (!sim || !sim->config) return;

    char path[256];
    printf("Enter maze file path: ");
    if (scanf("%255s", path) != 1) {
        printf("Invalid input.\n");
        return;
    }

    Maze* maze = load_maze_from_file(path);
    if (!maze) {
        printf("[ERROR] Cannot load maze from '%s'\n", path);
        return;
    }

    if (!validate_maze_for_families(maze, sim->config->num_families)) {
        printf("[ERROR] Loaded maze is not valid for %d families.\n",
               sim->config->num_families);
        destroy_maze(maze);
        return;
    }

    replace_maze_in_sim(sim, maze);

    if (!allocate_family_homes(sim) ||
        !assign_family_positions_edges(sim->maze,
                                       sim->num_families,
                                       sim->family_homes)) {
        printf("[ERROR] Failed to assign family start positions.\n");
        return;
    }

    // UPDATE: Set neighbor relationships after loading custom maze
    if (sim->families) {
        update_all_neighbors(sim);
    }

    print_maze_terminal(sim->maze,
                        sim->family_homes,
                        sim->num_families,
                        &sim->console_mutex);
}

static void save_current_maze(SimulationState* sim) {
    if (!sim || !sim->maze) {
        printf("[INFO] No maze in memory.\n");
        return;
    }

    char path[256];
    printf("Enter output file path: ");
    if (scanf("%255s", path) != 1) {
        printf("Invalid input.\n");
        return;
    }

    if (!save_maze_to_file(sim->maze, path)) {
        printf("[ERROR] Could not save maze.\n");
        return;
    }

    printf("[OK] Maze saved to '%s'\n", path);
}

/* =========================================================
 * Menu dispatcher
 * ========================================================= */

void execute_menu_function(const char* function_name, SimulationState* sim) {
    if (strcmp(function_name, "run_threading_mode") == 0) {
        run_threading_mode(sim);
    } else if (strcmp(function_name, "run_gui_mode") == 0) {
        run_gui_mode(sim);
    } else if (strcmp(function_name, "run_hybrid_mode") == 0) {
        run_hybrid_mode(sim);
    } else if (strcmp(function_name, "compare_results") == 0) {
        compare_results(sim);
    } else if (strcmp(function_name, "edit_config") == 0) {
        edit_config();
    } else if (strcmp(function_name, "exit_program") == 0) {
        exit(0);
    } else if (strcmp(function_name, "load_default_maze") == 0) {
        load_default_maze(sim);
    } else if (strcmp(function_name, "generate_random_maze") == 0) {
        handle_generate_maze(sim);
    } else if (strcmp(function_name, "load_custom_maze") == 0) {
        load_custom_maze(sim);
    } else if (strcmp(function_name, "save_current_maze") == 0) {
        save_current_maze(sim);
    } else {
        printf("Unknown function: %s\n", function_name);
    }
}