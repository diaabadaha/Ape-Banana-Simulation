#include "config.h"
#include "menu_parser.h"
#include "menu_handler.h"
#include "maze.h"
#include "types.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static const MenuItem* find_menu_item(const Menu* menu, int id) {
    for (int i = 0; i < menu->count; i++) {
        if (menu->items[i].id == id && menu->items[i].enabled) {
            return &menu->items[i];
        }
    }
    return NULL;
}

int main(int argc, char* argv[]) {
    (void)argc; (void)argv;

    // Load config (create default if missing)
    Config cfg = parse_config("config/arguments.txt");
    warn_suspicious_config(&cfg);

    // Init simulation state with ALL fields initialized to safe values
    SimulationState sim;
    memset(&sim, 0, sizeof(SimulationState));  // Zero out everything first
    
    sim.config = &cfg;
    sim.simulation_running = 0;
    sim.withdrawn_count = 0;
    sim.maze = NULL;              // Critical: initialize pointers to NULL
    sim.families = NULL;
    sim.family_homes = NULL;
    sim.num_families = 0;

    pthread_mutex_init(&sim.console_mutex, NULL);
    pthread_mutex_init(&sim.stats_mutex, NULL);

    // AUTOMATICALLY LOAD DEFAULT MAZE AT STARTUP
    printf("Loading default maze from: %s\n", cfg.maze_path);
    
    Maze* maze = load_maze_from_file(cfg.maze_path);
    if (!maze) {
        printf("Default maze not found. Generating random maze...\n");
        
        // Use new wall-based maze generation
        maze = generate_random_maze(
            cfg.maze_size,
            cfg.wall_removal_percent,
            cfg.banana_density,
            cfg.max_bananas_per_cell,
            NULL,  // Family homes assigned later
            0      // No families yet
        );
        
        if (maze) {
            if (validate_maze_for_families(maze, cfg.num_families)) {
                // Save generated maze as default
                save_maze_to_file(maze, cfg.maze_path);
                printf("Generated and saved new maze to %s\n", cfg.maze_path);
            } else {
                printf("Warning: Generated maze may not be suitable for %d families\n", cfg.num_families);
            }
        }
    } else {
        printf("Maze loaded successfully!\n");
    }
    
    sim.maze = maze;
    
    if (sim.maze) {
        printf("Maze: %dx%d with %d bananas\n\n", 
               sim.maze->width, sim.maze->height, sim.maze->total_bananas);
    } else {
        printf("ERROR: Failed to initialize maze. Exiting.\n");
        return 1;
    }

    // Load menu
    Menu menu = parse_menu("config/menu.txt");

    // Menu loop
    while (1) {
        display_menu(&menu);

        printf("Enter choice: ");
        int choice = 0;
        if (scanf("%d", &choice) != 1) {
            // clear invalid input
            int c;
            while ((c = getchar()) != '\n' && c != EOF) {}
            printf("Invalid input.\n");
            continue;
        }

        const MenuItem* item = find_menu_item(&menu, choice);
        if (!item) {
            printf("Invalid choice.\n");
            continue;
        }

        execute_menu_function(item->function, &sim);
    }

    // never reached in current design
    if (sim.maze) {
        destroy_maze(sim.maze);
    }
    free_menu(&menu);
    pthread_mutex_destroy(&sim.console_mutex);
    pthread_mutex_destroy(&sim.stats_mutex);

    return 0;
}