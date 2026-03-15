#include "../include/maze.h"
#include "../include/utils.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

/* ============================================================
 * REAL MAZE SYSTEM - Walls Between Cells
 * Uses recursive backtracking + partial wall removal
 * ============================================================ */

/* ============================================================
 * Wall Manipulation Functions
 * ============================================================ */

// Check if a specific wall exists in a cell
static int has_wall(uint8_t walls, uint8_t direction) {
    return (walls & direction) != 0;
}

// Remove wall between two adjacent cells (synchronizes both sides)
static void remove_wall_between(Maze* maze, Position a, Position b) {
    int dx = b.x - a.x;
    int dy = b.y - a.y;
    
    if (dx == 1) {
        // b is EAST of a
        maze->cells[a.y][a.x].walls &= ~WALL_EAST;
        maze->cells[b.y][b.x].walls &= ~WALL_WEST;
    } else if (dx == -1) {
        // b is WEST of a
        maze->cells[a.y][a.x].walls &= ~WALL_WEST;
        maze->cells[b.y][b.x].walls &= ~WALL_EAST;
    } else if (dy == 1) {
        // b is SOUTH of a
        maze->cells[a.y][a.x].walls &= ~WALL_SOUTH;
        maze->cells[b.y][b.x].walls &= ~WALL_NORTH;
    } else if (dy == -1) {
        // b is NORTH of a
        maze->cells[a.y][a.x].walls &= ~WALL_NORTH;
        maze->cells[b.y][b.x].walls &= ~WALL_SOUTH;
    }
}

// Check if movement is possible between two adjacent cells
int can_move_between(const Maze* maze, Position from, Position to) {
    // Check bounds
    if (to.x < 0 || to.x >= maze->width) return 0;
    if (to.y < 0 || to.y >= maze->height) return 0;
    
    // Check adjacency (must be exactly 1 step away)
    int dx = to.x - from.x;
    int dy = to.y - from.y;
    
    if (abs(dx) + abs(dy) != 1) return 0;  // Not adjacent
    
    // Check for wall between cells
    uint8_t walls = maze->cells[from.y][from.x].walls;
    
    if (dx == 1)  return !has_wall(walls, WALL_EAST);
    if (dx == -1) return !has_wall(walls, WALL_WEST);
    if (dy == 1)  return !has_wall(walls, WALL_SOUTH);
    if (dy == -1) return !has_wall(walls, WALL_NORTH);
    
    return 0;
}

/* ============================================================
 * Recursive Backtracking Maze Generation
 * ============================================================ */

// Allocate 2D boolean array
static int** allocate_visited(int height, int width) {
    int** visited = (int**)malloc(height * sizeof(int*));
    for (int i = 0; i < height; i++) {
        visited[i] = (int*)calloc(width, sizeof(int));
    }
    return visited;
}

// Free 2D boolean array
static void free_visited(int** visited, int height) {
    for (int i = 0; i < height; i++) {
        free(visited[i]);
    }
    free(visited);
}

// Generate perfect maze using recursive backtracking
static void generate_perfect_maze(Maze* maze) {
    // Initialize all cells with ALL walls
    for (int y = 0; y < maze->height; y++) {
        for (int x = 0; x < maze->width; x++) {
            maze->cells[y][x].walls = WALL_ALL;
        }
    }
    
    // Allocate visited tracking
    int** visited = allocate_visited(maze->height, maze->width);
    
    // Stack for backtracking (max size = width * height)
    Position* stack = (Position*)malloc(sizeof(Position) * maze->width * maze->height);
    int stack_size = 0;
    
    // Start from random position
    Position start = {rand() % maze->width, rand() % maze->height};
    stack[stack_size++] = start;
    visited[start.y][start.x] = 1;
    
    // Recursive backtracking loop
    while (stack_size > 0) {
        Position current = stack[stack_size - 1];
        
        // Collect unvisited neighbors
        Position neighbors[4];
        int neighbor_count = 0;
        
        Position candidates[4] = {
            {current.x + 1, current.y},  // East
            {current.x - 1, current.y},  // West
            {current.x, current.y + 1},  // South
            {current.x, current.y - 1}   // North
        };
        
        for (int i = 0; i < 4; i++) {
            Position n = candidates[i];
            if (n.x >= 0 && n.x < maze->width &&
                n.y >= 0 && n.y < maze->height &&
                !visited[n.y][n.x]) {
                neighbors[neighbor_count++] = n;
            }
        }
        
        if (neighbor_count == 0) {
            // Dead end - backtrack
            stack_size--;
        } else {
            // Choose random unvisited neighbor
            Position next = neighbors[rand() % neighbor_count];
            
            // Remove wall between current and next
            remove_wall_between(maze, current, next);
            
            // Mark as visited and push to stack
            visited[next.y][next.x] = 1;
            stack[stack_size++] = next;
        }
    }
    
    // Cleanup
    free(stack);
    free_visited(visited, maze->height);
}

/* ============================================================
 * Partial Wall Removal (for multiple paths)
 * ============================================================ */

static void remove_extra_walls(Maze* maze, double removal_percent) {
    if (removal_percent <= 0.0 || removal_percent >= 1.0) return;
    
    // Count removable internal walls
    int wall_count = 0;
    
    for (int y = 0; y < maze->height; y++) {
        for (int x = 0; x < maze->width; x++) {
            // Count EAST walls (to avoid double-counting)
            if (x < maze->width - 1 && has_wall(maze->cells[y][x].walls, WALL_EAST)) {
                wall_count++;
            }
            // Count SOUTH walls
            if (y < maze->height - 1 && has_wall(maze->cells[y][x].walls, WALL_SOUTH)) {
                wall_count++;
            }
        }
    }
    
    int to_remove = (int)(wall_count * removal_percent);
    int removed = 0;
    int attempts = 0;
    int max_attempts = to_remove * 10;  // Prevent infinite loop
    
    while (removed < to_remove && attempts < max_attempts) {
        attempts++;
        
        int x = rand() % maze->width;
        int y = rand() % maze->height;
        
        // Try removing EAST or SOUTH wall randomly
        if (rand() % 2 == 0) {
            // Try EAST wall
            if (x < maze->width - 1 && has_wall(maze->cells[y][x].walls, WALL_EAST)) {
                remove_wall_between(maze, (Position){x, y}, (Position){x + 1, y});
                removed++;
            }
        } else {
            // Try SOUTH wall
            if (y < maze->height - 1 && has_wall(maze->cells[y][x].walls, WALL_SOUTH)) {
                remove_wall_between(maze, (Position){x, y}, (Position){x, y + 1});
                removed++;
            }
        }
    }
}

/* ============================================================
 * Edge Openings for Families
 * ============================================================ */

static void create_family_openings(Maze* maze, const Position* family_homes, int num_families) {
    for (int i = 0; i < num_families; i++) {
        Position home = family_homes[i];
        
        // Remove outer wall at home position
        if (home.x == 0) {
            // Left edge
            maze->cells[home.y][home.x].walls &= ~WALL_WEST;
        } else if (home.x == maze->width - 1) {
            // Right edge
            maze->cells[home.y][home.x].walls &= ~WALL_EAST;
        }
        
        if (home.y == 0) {
            // Top edge
            maze->cells[home.y][home.x].walls &= ~WALL_NORTH;
        } else if (home.y == maze->height - 1) {
            // Bottom edge
            maze->cells[home.y][home.x].walls &= ~WALL_SOUTH;
        }
    }
}

/* ============================================================
 * Banana Distribution
 * ============================================================ */

void distribute_bananas_in_maze(Maze* maze, double banana_density, int max_per_cell) {
    if (!maze || banana_density <= 0.0 || max_per_cell <= 0) return;
    
    // Clear existing bananas
    int total_cells = 0;
    for (int y = 0; y < maze->height; y++) {
        for (int x = 0; x < maze->width; x++) {
            maze->cells[y][x].banana_count = 0;
            total_cells++;
        }
    }
    
    // Calculate target total bananas
    int target_total = (int)(total_cells * banana_density * (max_per_cell / 2.0));
    
    int placed = 0;
    int guard = 0;
    const int MAX_GUARD = total_cells * 200;
    
    while (placed < target_total && guard < MAX_GUARD) {
        guard++;
        
        int x = rand() % maze->width;
        int y = rand() % maze->height;
        
        MazeCell* cell = &maze->cells[y][x];
        if (cell->banana_count >= max_per_cell) continue;
        
        int room = max_per_cell - cell->banana_count;
        int to_add = (rand() % room) + 1;
        if (to_add > target_total - placed) {
            to_add = target_total - placed;
        }
        
        cell->banana_count += to_add;
        placed += to_add;
    }
    
    maze->total_bananas = placed;
}

// Continued in part 2...
// Part 2 of maze.c - Family positions, maze creation, I/O

/* ============================================================
 * Edge Cell Functions
 * ============================================================ */

static int is_edge_cell(const Maze* maze, int x, int y) {
    return (x == 0 || x == maze->width - 1 || 
            y == 0 || y == maze->height - 1);
}

// Check if edge cell has interior access (no walls blocking entry)
static int is_valid_home_position(const Maze* maze, int x, int y) {
    if (!maze) return 0;
    if (!is_edge_cell(maze, x, y)) return 0;
    
    // Check for interior access (at least one non-walled neighbor)
    Position neighbors[4] = {
        {x + 1, y},
        {x - 1, y},
        {x, y + 1},
        {x, y - 1}
    };
    
    for (int i = 0; i < 4; i++) {
        Position n = neighbors[i];
        if (n.x >= 0 && n.x < maze->width && 
            n.y >= 0 && n.y < maze->height) {
            // Check if can move to this neighbor
            if (can_move_between(maze, (Position){x, y}, n)) {
                return 1;  // Has access
            }
        }
    }
    
    return 0;  // No access
}

/* ============================================================
 * Family Position Assignment
 * ============================================================ */

int assign_family_positions_edges(const Maze* maze,
                                  int num_families,
                                  Position* out_positions) {
    if (!maze || !out_positions || num_families <= 0) return 0;
    
    // Count valid edge positions
    int valid_count = 0;
    for (int y = 0; y < maze->height; y++) {
        for (int x = 0; x < maze->width; x++) {
            if (is_edge_cell(maze, x, y) && is_valid_home_position(maze, x, y)) {
                valid_count++;
            }
        }
    }
    
    if (valid_count < num_families) {
        printf("[WARNING] Only %d valid edge positions for %d families!\n",
               valid_count, num_families);
        return 0;
    }
    
    // Collect valid positions
    Position* candidates = (Position*)malloc(valid_count * sizeof(Position));
    int idx = 0;
    
    for (int y = 0; y < maze->height; y++) {
        for (int x = 0; x < maze->width; x++) {
            if (is_edge_cell(maze, x, y) && is_valid_home_position(maze, x, y)) {
                candidates[idx++] = (Position){x, y};
            }
        }
    }
    
    // Shuffle candidates
    for (int i = valid_count - 1; i > 0; i--) {
        int j = rand() % (i + 1);
        Position temp = candidates[i];
        candidates[i] = candidates[j];
        candidates[j] = temp;
    }
    
    // Assign positions
    for (int i = 0; i < num_families; i++) {
        out_positions[i] = candidates[i];
    }
    
    free(candidates);
    return 1;
}

/* ============================================================
 * Maze Creation
 * ============================================================ */

Maze* create_maze(int width, int height) {
    if (width <= 0 || height <= 0) return NULL;
    
    Maze* maze = (Maze*)malloc(sizeof(Maze));
    if (!maze) return NULL;
    
    maze->width = width;
    maze->height = height;
    maze->total_bananas = 0;
    
    // Allocate rows
    maze->cells = (MazeCell**)malloc(height * sizeof(MazeCell*));
    if (!maze->cells) {
        free(maze);
        return NULL;
    }
    
    // Allocate columns
    for (int y = 0; y < height; y++) {
        maze->cells[y] = (MazeCell*)malloc(width * sizeof(MazeCell));
        if (!maze->cells[y]) {
            for (int j = 0; j < y; j++) free(maze->cells[j]);
            free(maze->cells);
            free(maze);
            return NULL;
        }
    }
    
    // Initialize cells
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            maze->cells[y][x].walls = WALL_ALL;  // Start with all walls
            maze->cells[y][x].banana_count = 0;
            maze->cells[y][x].occupant = NULL;
            pthread_mutex_init(&maze->cells[y][x].cell_mutex, NULL);
        }
    }
    
    return maze;
}

void destroy_maze(Maze* maze) {
    if (!maze) return;
    
    for (int y = 0; y < maze->height; y++) {
        for (int x = 0; x < maze->width; x++) {
            pthread_mutex_destroy(&maze->cells[y][x].cell_mutex);
        }
        free(maze->cells[y]);
    }
    
    free(maze->cells);
    free(maze);
}

/* ============================================================
 * Random Maze Generation (Main Function)
 * ============================================================ */

Maze* generate_random_maze(int size, double wall_removal_percent,
                           double banana_density, int max_bananas_per_cell,
                           const Position* family_homes, int num_families) {
    Maze* maze = create_maze(size, size);
    if (!maze) return NULL;
    
    // Step 1: Generate perfect maze using recursive backtracking
    generate_perfect_maze(maze);
    
    // Step 2: Remove extra walls for multiple paths
    remove_extra_walls(maze, wall_removal_percent);
    
    // Step 3: Create openings at family home positions
    if (family_homes && num_families > 0) {
        create_family_openings(maze, family_homes, num_families);
    }
    
    // Step 4: Distribute bananas
    distribute_bananas_in_maze(maze, banana_density, max_bananas_per_cell);
    
    return maze;
}

/* ============================================================
 * Terminal Rendering
 * ============================================================ */

void print_maze_terminal(const Maze* maze,
                        const Position* family_homes,
                        int num_families,
                        pthread_mutex_t* console_mutex) {
    if (!maze) return;
    
    pthread_mutex_lock(console_mutex);
    
    printf("\n=== Maze (%dx%d) - Real Maze with Walls ===\n",
           maze->width, maze->height);
    
    // Print top border
    for (int x = 0; x < maze->width * 4 + 1; x++) printf("─");
    printf("\n");
    
    for (int y = 0; y < maze->height; y++) {
        // Print cells and vertical walls
        for (int x = 0; x < maze->width; x++) {
            // Left wall or border
            if (x == 0 || has_wall(maze->cells[y][x].walls, WALL_WEST)) {
                printf("│");
            } else {
                printf(" ");
            }
            
            // Cell content
            int is_home = 0;
            for (int i = 0; i < num_families; i++) {
                if (family_homes && 
                    family_homes[i].x == x && 
                    family_homes[i].y == y) {
                    is_home = 1;
                    break;
                }
            }
            
            if (is_home) {
                printf(" H ");
            } else {
                const MazeCell* c = &maze->cells[y][x];
                if (c->banana_count == 0) {
                    printf("   ");
                } else if (c->banana_count < 10) {
                    printf(" %d ", c->banana_count);
                } else {
                    printf(" + ");
                }
            }
        }
        
        // Right border
        if (has_wall(maze->cells[y][maze->width - 1].walls, WALL_EAST)) {
            printf("│");
        } else {
            printf(" ");
        }
        printf("\n");
        
        // Print horizontal walls
        if (y < maze->height - 1) {
            for (int x = 0; x < maze->width; x++) {
                // Corner
                printf("│");
                
                // Bottom wall
                if (has_wall(maze->cells[y][x].walls, WALL_SOUTH)) {
                    printf("───");
                } else {
                    printf("   ");
                }
            }
            printf("│\n");
        }
    }
    
    // Print bottom border
    for (int x = 0; x < maze->width * 4 + 1; x++) printf("─");
    printf("\n");
    
    printf("Total bananas: %d\n", maze->total_bananas);
    printf("====================\n");
    
    pthread_mutex_unlock(console_mutex);
}

/* ============================================================
 * File I/O (Simplified for walls)
 * ============================================================ */

Maze* load_maze_from_file(const char* filepath) {
    FILE* f = fopen(filepath, "r");
    if (!f) return NULL;
    
    int w = 0, h = 0;
    if (fscanf(f, "%d %d", &w, &h) != 2 || w <= 0 || h <= 0) {
        fclose(f);
        return NULL;
    }
    
    Maze* maze = create_maze(w, h);
    if (!maze) {
        fclose(f);
        return NULL;
    }
    
    // Read wall data and banana counts
    int total = 0;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int walls = 0, bananas = 0;
            if (fscanf(f, "%d %d", &walls, &bananas) != 2) {
                destroy_maze(maze);
                fclose(f);
                return NULL;
            }
            maze->cells[y][x].walls = (uint8_t)walls;
            maze->cells[y][x].banana_count = bananas;
            total += bananas;
        }
    }
    
    // Set total_bananas (this was missing before!)
    maze->total_bananas = total;
    
    fclose(f);
    return maze;
}

int save_maze_to_file(const Maze* maze, const char* filepath) {
    if (!maze || !filepath) return 0;
    
    FILE* f = fopen(filepath, "w");
    if (!f) return 0;
    
    fprintf(f, "%d %d\n", maze->width, maze->height);
    
    for (int y = 0; y < maze->height; y++) {
        for (int x = 0; x < maze->width; x++) {
            fprintf(f, "%d %d ", 
                   (int)maze->cells[y][x].walls,
                   maze->cells[y][x].banana_count);
        }
        fprintf(f, "\n");
    }
    
    fclose(f);
    return 1;
}

/* ============================================================
 * Connectivity Check (BFS)
 * ============================================================ */

int is_maze_connected(const Maze* maze) {
    if (!maze || maze->width <= 0 || maze->height <= 0) return 0;
    
    int** visited = allocate_visited(maze->height, maze->width);
    
    // BFS from (0,0)
    Position* queue = (Position*)malloc(sizeof(Position) * maze->width * maze->height);
    int front = 0, rear = 0;
    
    queue[rear++] = (Position){0, 0};
    visited[0][0] = 1;
    int visited_count = 1;
    
    while (front < rear) {
        Position current = queue[front++];
        
        Position neighbors[4] = {
            {current.x + 1, current.y},
            {current.x - 1, current.y},
            {current.x, current.y + 1},
            {current.x, current.y - 1}
        };
        
        for (int i = 0; i < 4; i++) {
            Position next = neighbors[i];
            
            if (next.x < 0 || next.x >= maze->width) continue;
            if (next.y < 0 || next.y >= maze->height) continue;
            if (visited[next.y][next.x]) continue;
            
            // Check if can move between
            if (can_move_between(maze, current, next)) {
                visited[next.y][next.x] = 1;
                visited_count++;
                queue[rear++] = next;
            }
        }
    }
    
    free(queue);
    free_visited(visited, maze->height);
    
    int total_cells = maze->width * maze->height;
    return (visited_count == total_cells);
}

int validate_maze_for_families(const Maze* maze, int num_families) {
    if (!maze || num_families <= 0) return 0;
    if (!is_maze_connected(maze)) return 0;
    
    // Count valid home positions
    int valid_count = 0;
    for (int y = 0; y < maze->height; y++) {
        for (int x = 0; x < maze->width; x++) {
            if (is_edge_cell(maze, x, y) && is_valid_home_position(maze, x, y)) {
                valid_count++;
            }
        }
    }
    
    return (valid_count >= num_families);
}

/* ============================================================
 * Count Total Bananas in Maze
 * ============================================================ */

int count_maze_bananas(const Maze* maze) {
    if (!maze) return 0;
    
    int total = 0;
    for (int y = 0; y < maze->height; y++) {
        for (int x = 0; x < maze->width; x++) {
            total += maze->cells[y][x].banana_count;
        }
    }
    return total;
}