#include "../include/female_ape.h"
#include "../include/fight.h"
#include "../include/sync.h"
#include "../include/maze.h"
#include <stdlib.h>
#include <unistd.h>
#include <math.h>
#include <time.h>
#include <string.h>

// Anti-stuck tracking
#define MAX_STUCK_ITERATIONS 30  // FIXED: Increased from 15 for less teleporting
#define OSCILLATION_HISTORY 6    // Track last 6 positions for pattern detection

// BFS configuration
#define MAX_BFS_QUEUE 400

// BFS node for pathfinding
typedef struct {
    Position pos;
    int distance;
} BFSNode;

// Position history for oscillation detection
typedef struct {
    Position positions[OSCILLATION_HISTORY];
    int count;
    int index;
} PositionHistory;

// Check if position is within bounds
static int is_in_bounds(Maze* maze, Position pos) {
    if (pos.x < 0 || pos.x >= maze->width) return 0;
    if (pos.y < 0 || pos.y >= maze->height) return 0;
    return 1;
}

// Check if female is at a valid exit point
static int is_valid_exit(Position current, Position home, int maze_width, int maze_height) {
    if (current.x == home.x && current.y == home.y) return 1;
    
    if (home.y == 0) {
        return (current.y == 0 && abs(current.x - home.x) <= 1);
    }
    if (home.y == maze_height - 1) {
        return (current.y == maze_height - 1 && abs(current.x - home.x) <= 1);
    }
    if (home.x == 0) {
        return (current.x == 0 && abs(current.y - home.y) <= 1);
    }
    if (home.x == maze_width - 1) {
        return (current.x == maze_width - 1 && abs(current.y - home.y) <= 1);
    }
    
    return 0;
}

// Add position to history
static void add_to_history(PositionHistory* hist, Position pos) {
    hist->positions[hist->index] = pos;
    hist->index = (hist->index + 1) % OSCILLATION_HISTORY;
    if (hist->count < OSCILLATION_HISTORY) hist->count++;
}

// BFS to find nearest banana within limited radius
static Position* bfs_find_banana_path(FemaleApe* ape, Maze* maze, int* path_length) {
    *path_length = 0;
    
    // Increase search radius for better banana finding
    int search_radius = (maze->width + maze->height) / 2;
    
    // Allocate visited array
    int** visited = (int**)calloc(maze->height, sizeof(int*));
    for (int i = 0; i < maze->height; i++) {
        visited[i] = (int*)calloc(maze->width, sizeof(int));
    }
    
    // Allocate parent array
    Position** parent = (Position**)malloc(maze->height * sizeof(Position*));
    for (int i = 0; i < maze->height; i++) {
        parent[i] = (Position*)malloc(maze->width * sizeof(Position));
        for (int j = 0; j < maze->width; j++) {
            parent[i][j].x = -1;
            parent[i][j].y = -1;
        }
    }
    
    // BFS queue
    BFSNode* queue = (BFSNode*)malloc(MAX_BFS_QUEUE * sizeof(BFSNode));
    int front = 0, rear = 0;
    
    // Start from current position
    queue[rear++] = (BFSNode){ape->pos, 0};
    visited[ape->pos.y][ape->pos.x] = 1;
    parent[ape->pos.y][ape->pos.x] = ape->pos;
    
    Position target = {-1, -1};
    
    // BFS search
    while (front < rear && rear < MAX_BFS_QUEUE) {
        BFSNode current = queue[front++];
        Position pos = current.pos;
        
        // Check if distance exceeds radius
        int dx = abs(pos.x - ape->pos.x);
        int dy = abs(pos.y - ape->pos.y);
        if (dx + dy > search_radius) continue;
        
        // Check if cell has bananas (skip current position)
        if (pos.x != ape->pos.x || pos.y != ape->pos.y) {
            pthread_mutex_lock(&maze->cells[pos.y][pos.x].cell_mutex);
            int banana_count = maze->cells[pos.y][pos.x].banana_count;
            pthread_mutex_unlock(&maze->cells[pos.y][pos.x].cell_mutex);
            
            if (banana_count > 0) {
                target = pos;
                break;
            }
        }
        
        // Explore neighbors
        Position neighbors[4] = {
            {pos.x + 1, pos.y},
            {pos.x - 1, pos.y},
            {pos.x, pos.y + 1},
            {pos.x, pos.y - 1}
        };
        
        for (int i = 0; i < 4; i++) {
            Position next = neighbors[i];
            
            if (!is_in_bounds(maze, next)) continue;
            if (visited[next.y][next.x]) continue;
            
            // WALL CHECK: Can we move between current and next?
            if (!can_move_between(maze, pos, next)) continue;
            
            visited[next.y][next.x] = 1;
            parent[next.y][next.x] = pos;
            
            if (rear < MAX_BFS_QUEUE) {
                queue[rear++] = (BFSNode){next, current.distance + 1};
            }
        }
    }
    
    // Cleanup
    for (int i = 0; i < maze->height; i++) {
        free(visited[i]);
    }
    free(visited);
    free(queue);
    
    if (target.x == -1) {
        // No bananas found
        for (int i = 0; i < maze->height; i++) {
            free(parent[i]);
        }
        free(parent);
        return NULL;
    }
    
    // Reconstruct path
    Position* path = (Position*)malloc(sizeof(Position) * (maze->width + maze->height));
    int count = 0;
    Position current = target;
    
    while (current.x != ape->pos.x || current.y != ape->pos.y) {
        path[count++] = current;
        Position p = parent[current.y][current.x];
        if (p.x == -1) break;  // Safety check
        current = p;
    }
    
    // Cleanup parent array
    for (int i = 0; i < maze->height; i++) {
        free(parent[i]);
    }
    free(parent);
    
    if (count == 0) {
        free(path);
        return NULL;
    }
    
    // Reverse path (start to target)
    for (int i = 0; i < count / 2; i++) {
        Position temp = path[i];
        path[i] = path[count - 1 - i];
        path[count - 1 - i] = temp;
    }
    
    *path_length = count;
    return path;
}

// BFS to find path home
static Position* bfs_find_path_home(FemaleApe* ape, Maze* maze, int* path_length) {
    *path_length = 0;
    Position home = ape->family->home_pos;
    
    // Allocate visited array
    int** visited = (int**)calloc(maze->height, sizeof(int*));
    for (int i = 0; i < maze->height; i++) {
        visited[i] = (int*)calloc(maze->width, sizeof(int));
    }
    
    // Allocate parent array
    Position** parent = (Position**)malloc(maze->height * sizeof(Position*));
    for (int i = 0; i < maze->height; i++) {
        parent[i] = (Position*)malloc(maze->width * sizeof(Position));
        for (int j = 0; j < maze->width; j++) {
            parent[i][j].x = -1;
            parent[i][j].y = -1;
        }
    }
    
    // BFS queue
    BFSNode* queue = (BFSNode*)malloc(MAX_BFS_QUEUE * sizeof(BFSNode));
    int front = 0, rear = 0;
    
    // Start from current position
    queue[rear++] = (BFSNode){ape->pos, 0};
    visited[ape->pos.y][ape->pos.x] = 1;
    parent[ape->pos.y][ape->pos.x] = ape->pos;
    
    int found = 0;
    
    // BFS search
    while (front < rear && rear < MAX_BFS_QUEUE) {
        BFSNode current = queue[front++];
        Position pos = current.pos;
        
        // Check if reached home
        if (pos.x == home.x && pos.y == home.y) {
            found = 1;
            break;
        }
        
        // Explore neighbors
        Position neighbors[4] = {
            {pos.x + 1, pos.y},
            {pos.x - 1, pos.y},
            {pos.x, pos.y + 1},
            {pos.x, pos.y - 1}
        };
        
        for (int i = 0; i < 4; i++) {
            Position next = neighbors[i];
            
            if (!is_in_bounds(maze, next)) continue;
            if (visited[next.y][next.x]) continue;
            if (!can_move_between(maze, pos, next)) continue;
            
            visited[next.y][next.x] = 1;
            parent[next.y][next.x] = pos;
            
            if (rear < MAX_BFS_QUEUE) {
                queue[rear++] = (BFSNode){next, current.distance + 1};
            }
        }
    }
    
    // Cleanup
    for (int i = 0; i < maze->height; i++) {
        free(visited[i]);
    }
    free(visited);
    free(queue);
    
    if (!found) {
        for (int i = 0; i < maze->height; i++) {
            free(parent[i]);
        }
        free(parent);
        return NULL;
    }
    
    // Reconstruct path
    Position* path = (Position*)malloc(sizeof(Position) * (maze->width + maze->height));
    int count = 0;
    Position current = home;
    
    while (current.x != ape->pos.x || current.y != ape->pos.y) {
        path[count++] = current;
        Position p = parent[current.y][current.x];
        if (p.x == -1) break;  // Safety check
        current = p;
    }
    
    // Cleanup parent array
    for (int i = 0; i < maze->height; i++) {
        free(parent[i]);
    }
    free(parent);
    
    if (count == 0) {
        free(path);
        return NULL;
    }
    
    // Reverse path (start to home)
    for (int i = 0; i < count / 2; i++) {
        Position temp = path[i];
        path[i] = path[count - 1 - i];
        path[count - 1 - i] = temp;
    }
    
    *path_length = count;
    return path;
}

// Get one step toward target with wall awareness
static Position get_step_toward(Position from, Position to, Maze* maze) {
    int dx = to.x - from.x;
    int dy = to.y - from.y;
    
    Position candidates[4];
    int num_candidates = 0;
    
    if (abs(dx) > abs(dy)) {
        if (dx > 0) {
            candidates[num_candidates++] = (Position){from.x + 1, from.y};
        } else if (dx < 0) {
            candidates[num_candidates++] = (Position){from.x - 1, from.y};
        }
        
        if (dy > 0) {
            candidates[num_candidates++] = (Position){from.x, from.y + 1};
        } else if (dy < 0) {
            candidates[num_candidates++] = (Position){from.x, from.y - 1};
        }
    } else {
        if (dy > 0) {
            candidates[num_candidates++] = (Position){from.x, from.y + 1};
        } else if (dy < 0) {
            candidates[num_candidates++] = (Position){from.x, from.y - 1};
        }
        
        if (dx > 0) {
            candidates[num_candidates++] = (Position){from.x + 1, from.y};
        } else if (dx < 0) {
            candidates[num_candidates++] = (Position){from.x - 1, from.y};
        }
    }
    
    for (int i = 0; i < num_candidates; i++) {
        if (is_in_bounds(maze, candidates[i]) && 
            can_move_between(maze, from, candidates[i])) {
            return candidates[i];
        }
    }
    
    // Try all neighbors
    Position neighbors[4] = {
        {from.x + 1, from.y},
        {from.x - 1, from.y},
        {from.x, from.y + 1},
        {from.x, from.y - 1}
    };
    
    for (int i = 0; i < 4; i++) {
        if (is_in_bounds(maze, neighbors[i]) && 
            can_move_between(maze, from, neighbors[i])) {
            return neighbors[i];
        }
    }
    
    return from;
}

// Get random valid neighbor (avoiding previous position and recent positions)
static Position get_random_valid_neighbor(Position pos, Position prev_pos, Maze* maze, 
                                          PositionHistory* hist) {
    Position neighbors[4] = {
        {pos.x + 1, pos.y},
        {pos.x - 1, pos.y},
        {pos.x, pos.y + 1},
        {pos.x, pos.y - 1}
    };
    
    Position valid_fresh[4];
    int fresh_count = 0;
    
    Position valid_all[4];
    int all_count = 0;
    
    for (int i = 0; i < 4; i++) {
        if (is_in_bounds(maze, neighbors[i]) && 
            can_move_between(maze, pos, neighbors[i])) {
            valid_all[all_count++] = neighbors[i];
            
            // Check if this position is in recent history
            int in_history = 0;
            if (hist) {
                for (int h = 0; h < hist->count; h++) {
                    if (hist->positions[h].x == neighbors[i].x && 
                        hist->positions[h].y == neighbors[i].y) {
                        in_history = 1;
                        break;
                    }
                }
            }
            
            // Also check prev_pos
            if (neighbors[i].x == prev_pos.x && neighbors[i].y == prev_pos.y) {
                in_history = 1;
            }
            
            if (!in_history) {
                valid_fresh[fresh_count++] = neighbors[i];
            }
        }
    }
    
    if (fresh_count > 0) {
        return valid_fresh[rand() % fresh_count];
    }
    
    if (all_count > 0) {
        return valid_all[rand() % all_count];
    }
    
    return pos;
}

// Movement for returning home - use BFS for optimal path
static Position move_toward_home(FemaleApe* ape, Maze* maze, PositionHistory* hist) {
    Position home = ape->family->home_pos;
    
    // Use direct step if possible
    Position step = get_step_toward(ape->pos, home, maze);
    
    // If stuck, try random valid neighbor
    if (step.x == ape->pos.x && step.y == ape->pos.y) {
        step = get_random_valid_neighbor(ape->pos, ape->prev_pos, maze, hist);
    }
    
    return step;
}

// Execute movement with locking
static int attempt_move(FemaleApe* ape, Position new_pos, Maze* maze) {
    Position old_pos = ape->pos;
    
    // Don't move if our family is withdrawn
    if (ape->family->is_withdrawn) {
        return 0;
    }
    
    if (new_pos.x == old_pos.x && new_pos.y == old_pos.y) {
        return 0; // No movement
    }
    
    lock_cells_in_order(maze, old_pos, new_pos);
    
    // Check bounds and wall between cells
    if (!is_in_bounds(maze, new_pos) || !can_move_between(maze, old_pos, new_pos)) {
        unlock_cells_in_reverse(maze, old_pos, new_pos);
        return 0;
    }
    
    FemaleApe* other = (FemaleApe*)maze->cells[new_pos.y][new_pos.x].occupant;
    
    if (other != NULL && other != ape) {
        // Don't interact with withdrawn females - they shouldn't be here
        if (other->family->is_withdrawn) {
            // Clear the occupant since they're withdrawn
            maze->cells[new_pos.y][new_pos.x].occupant = NULL;
            // Now we can move there
            maze->cells[old_pos.y][old_pos.x].occupant = NULL;
            maze->cells[new_pos.y][new_pos.x].occupant = ape;
            ape->prev_pos = ape->pos;
            ape->pos = new_pos;
            unlock_cells_in_reverse(maze, old_pos, new_pos);
            ape->energy -= ape->sim->config->energy_loss_move;
            if (ape->energy < 0) ape->energy = 0;
            return 1;
        }
        
        if (other->state == STATE_RESTING) {
            end_resting(other);
            unlock_cells_in_reverse(maze, old_pos, new_pos);
            return 0;
        }
        
        // Only fight if both have bananas to fight over
        if ((ape->state == STATE_RETURNING || other->state == STATE_RETURNING) &&
            (ape->collected_bananas > 0 || other->collected_bananas > 0)) {
            int total_bananas = ape->collected_bananas + other->collected_bananas;
            double base_prob = ape->sim->config->fight_probability_base;
            double prob = base_prob + (total_bananas / 100.0);
            if (prob > 1.0) prob = 1.0;
            
            if ((rand() % 100) < (int)(prob * 100)) {
                unlock_cells_in_reverse(maze, old_pos, new_pos);
                female_fight(ape, other, ape->sim);
                return 0;
            }
        }
        
        unlock_cells_in_reverse(maze, old_pos, new_pos);
        return 0;
    }
    
    maze->cells[old_pos.y][old_pos.x].occupant = NULL;
    maze->cells[new_pos.y][new_pos.x].occupant = ape;
    
    ape->prev_pos = ape->pos;
    ape->pos = new_pos;
    
    unlock_cells_in_reverse(maze, old_pos, new_pos);
    
    ape->energy -= ape->sim->config->energy_loss_move;
    if (ape->energy < 0) ape->energy = 0;
    
    return 1; // Movement successful
}

// Collect bananas with partial collection logic
static void collect_bananas(FemaleApe* ape, Maze* maze) {
    Position pos = ape->pos;
    
    pthread_mutex_lock(&maze->cells[pos.y][pos.x].cell_mutex);
    
    int available = maze->cells[pos.y][pos.x].banana_count;
    int space_left = ape->sim->config->bananas_per_trip - ape->collected_bananas;
    
    // Also consider basket threshold - don't collect more than needed
    pthread_mutex_lock(&ape->family->basket_mutex);
    int basket_current = ape->family->basket_bananas;
    pthread_mutex_unlock(&ape->family->basket_mutex);
    
    int threshold = ape->sim->config->family_banana_threshold;
    int needed_for_win = threshold - basket_current - ape->collected_bananas;
    
    // Limit collection to what's needed
    if (needed_for_win > 0 && space_left > needed_for_win) {
        space_left = needed_for_win;
    }
    
    int to_collect = (available < space_left) ? available : space_left;
    if (to_collect < 0) to_collect = 0;
    
    if (to_collect > 0) {
        maze->cells[pos.y][pos.x].banana_count -= to_collect;
        ape->collected_bananas += to_collect;
        
        // Check if we left bananas behind
        int remaining = maze->cells[pos.y][pos.x].banana_count;
        
        if (remaining > 0 && ape->collected_bananas >= ape->sim->config->bananas_per_trip) {
            // We're full and left bananas - REMEMBER this cell
            ape->remembered_cell = pos;
            ape->remembered_bananas = remaining;
            ape->has_remembered_cell = 1;
            
            log_event(ape->sim, "[REMEMBER] Female %d left %d bananas at (%d,%d)\n",
                     ape->family->family_id, remaining, pos.x, pos.y);
        }
        
        // If this is the remembered cell and we collected all remaining
        if (ape->has_remembered_cell && 
            pos.x == ape->remembered_cell.x && 
            pos.y == ape->remembered_cell.y &&
            remaining == 0) {
            // Collected all from remembered cell
            ape->has_remembered_cell = 0;
            log_event(ape->sim, "[COLLECTED] Female %d collected all from remembered cell\n",
                     ape->family->family_id);
        }
        
        // If inventory full OR we have enough to win, start returning
        if (ape->collected_bananas >= ape->sim->config->bananas_per_trip ||
            (basket_current + ape->collected_bananas >= threshold)) {
            ape->state = STATE_RETURNING;
        }
    }
    
    pthread_mutex_unlock(&maze->cells[pos.y][pos.x].cell_mutex);
    
    // Small energy cost for collecting
    ape->energy -= 1;
    if (ape->energy < 0) ape->energy = 0;
}

// Deliver bananas to basket
static void deliver_to_basket(FemaleApe* ape) {
    if (ape->collected_bananas == 0) {
        ape->state = STATE_SEARCHING;
        return;
    }
    
    Family* family = ape->family;
    int threshold = ape->sim->config->family_banana_threshold;
    
    pthread_mutex_lock(&family->basket_mutex);
    
    // Calculate how many bananas are needed to reach threshold
    int current_basket = family->basket_bananas;
    int needed = threshold - current_basket;
    
    // Only deliver what's needed (don't exceed threshold)
    int to_deliver = ape->collected_bananas;
    if (needed > 0 && to_deliver > needed) {
        to_deliver = needed;  // Only deliver what's needed
    }
    
    family->basket_bananas += to_deliver;
    family->total_bananas_collected += to_deliver;  // Track total ever collected
    int final_basket = family->basket_bananas;
    
    pthread_mutex_unlock(&family->basket_mutex);
    
    pthread_mutex_lock(&ape->sim->stats_mutex);
    ape->sim->stats.total_bananas_collected += to_deliver;
    pthread_mutex_unlock(&ape->sim->stats_mutex);
    
    log_event(ape->sim, "[DELIVER] Female %d delivered %d bananas (basket=%d)\n",
              ape->family->family_id, to_deliver, final_basket);
    
    // Keep excess bananas if we couldn't deliver all
    ape->collected_bananas -= to_deliver;
    
    // If basket reached threshold, female can stop
    if (final_basket >= threshold) {
        log_event(ape->sim, "[WIN] Family %d reached basket threshold of %d!\n",
                  family->family_id, threshold);
    }
    
    ape->state = STATE_SEARCHING;
}

// Main female ape thread
void* female_ape_thread(void* arg) {
    FemaleApe* ape = (FemaleApe*)arg;
    Maze* maze = ape->sim->maze;
    
    // Position tracking
    PositionHistory history;
    memset(&history, 0, sizeof(PositionHistory));
    
    Position last_pos = ape->pos;
    int stuck_count = 0;
    (void)stuck_count;  // Suppress unused warning - kept for debugging
    
    // PATH STORAGE - follow complete path instead of recalculating each step
    Position* current_path = NULL;
    int path_length = 0;
    int path_index = 0;
    
    log_event(ape->sim, "Female %d started at (%d,%d)\n", 
              ape->family->family_id, ape->pos.x, ape->pos.y);
    
    while (is_simulation_running(ape->sim) && !ape->family->is_withdrawn) {
        
        // Record position in history (kept for debugging, not for teleport)
        add_to_history(&history, ape->pos);
        
        // Track if stuck (for debugging only, no teleport)
        if (ape->pos.x == last_pos.x && ape->pos.y == last_pos.y && 
            ape->state != STATE_RESTING && ape->state != STATE_FIGHTING) {
            stuck_count++;
        } else {
            stuck_count = 0;
            last_pos = ape->pos;
        }
        
        // Energy check - rest if below threshold
        int energy_threshold = ape->sim->config->female_energy_threshold;
        if (ape->energy < energy_threshold && ape->state != STATE_RESTING) {
            start_resting(ape);
            memset(&history, 0, sizeof(PositionHistory));
            // Clear path when resting
            if (current_path) { free(current_path); current_path = NULL; }
            path_length = 0; path_index = 0;
            continue;
        }
        
        // Ensure not stuck in IDLE
        if (ape->state == STATE_IDLE) {
            ape->state = STATE_SEARCHING;
        }
        
        // State machine
        switch (ape->state) {
            case STATE_SEARCHING: {
                // if already full, return home
                if (ape->collected_bananas >= ape->sim->config->bananas_per_trip) {
                    ape->state = STATE_RETURNING;
                    // Clear search path
                    if (current_path) { free(current_path); current_path = NULL; }
                    path_length = 0; path_index = 0;
                    break;
                }
                
                // Check current cell for bananas
                Position current = ape->pos;
                
                pthread_mutex_lock(&maze->cells[current.y][current.x].cell_mutex);
                int bananas_here = maze->cells[current.y][current.x].banana_count;
                pthread_mutex_unlock(&maze->cells[current.y][current.x].cell_mutex);
                
                if (bananas_here > 0) {
                    ape->state = STATE_COLLECTING;
                    // Clear path when collecting
                    if (current_path) { free(current_path); current_path = NULL; }
                    path_length = 0; path_index = 0;
                } else {
                    // Do we have a valid path to follow?
                    int need_new_path = 0;
                    
                    if (current_path && path_index < path_length) {
                        // Verify we're still on the path (might have been pushed by fight)
                        if (path_index > 0) {
                            Position expected = current_path[path_index - 1];
                            if (ape->pos.x != expected.x || ape->pos.y != expected.y) {
                                // Off path - recalculate
                                free(current_path); current_path = NULL;
                                path_length = 0; path_index = 0;
                                need_new_path = 1;
                            }
                        }
                    } else {
                        need_new_path = 1;
                    }
                    
                    // Need new path?
                    if (need_new_path) {
                        if (current_path) { free(current_path); current_path = NULL; }
                        current_path = bfs_find_banana_path(ape, maze, &path_length);
                        path_index = 0;
                    }
                    
                    // Follow path or random walk
                    if (current_path && path_index < path_length) {
                        Position next = current_path[path_index];
                        if (attempt_move(ape, next, maze)) {
                            path_index++;
                        } else {
                            // Move failed - path blocked, try random move this iteration
                            free(current_path); current_path = NULL;
                            path_length = 0; path_index = 0;
                            // Do random move now instead of waiting
                            Position next_rand = get_random_valid_neighbor(ape->pos, ape->prev_pos, maze, &history);
                            attempt_move(ape, next_rand, maze);
                        }
                    } else {
                        // No path found - random walk (continue exploring)
                        Position next = get_random_valid_neighbor(ape->pos, ape->prev_pos, maze, &history);
                        attempt_move(ape, next, maze);
                    }
                }
                break;
            }
            
            case STATE_COLLECTING:
                collect_bananas(ape, maze);
                if (ape->state != STATE_RETURNING) {
                    ape->state = STATE_SEARCHING;
                }
                break;
                
            case STATE_RETURNING: {
                if (is_valid_exit(ape->pos, ape->family->home_pos, 
                                 maze->width, maze->height)) {
                    deliver_to_basket(ape);
                    memset(&history, 0, sizeof(PositionHistory));
                    // Clear path after delivery
                    if (current_path) { free(current_path); current_path = NULL; }
                    path_length = 0; path_index = 0;
                } else {
                    // Need path home?
                    if (!current_path || path_index >= path_length) {
                        if (current_path) free(current_path);
                        // Use BFS to find path home
                        current_path = bfs_find_path_home(ape, maze, &path_length);
                        path_index = 0;
                    }
                    
                    // Follow path or use greedy
                    if (current_path && path_index < path_length) {
                        Position next = current_path[path_index];
                        if (attempt_move(ape, next, maze)) {
                            path_index++;
                        } else {
                            // Path blocked - recalculate
                            free(current_path); current_path = NULL;
                            path_length = 0; path_index = 0;
                        }
                    } else {
                        // Fallback to greedy movement
                        Position next = move_toward_home(ape, maze, &history);
                        attempt_move(ape, next, maze);
                    }
                }
                break;
            }
            
            case STATE_FIGHTING:
                // Clear path when fighting
                if (current_path) { free(current_path); current_path = NULL; }
                path_length = 0; path_index = 0;
                break;
                
            case STATE_RESTING:
                break;
                
            case STATE_WITHDRAWN:
                if (current_path) free(current_path);
                log_event(ape->sim, "[WITHDRAW] Female %d withdrawing with %d bananas lost\n",
                         ape->family->family_id, ape->collected_bananas);
                ape->collected_bananas = 0;
                return NULL;
                
            default:
                break;
        }
        
        usleep(100000);  // 100ms between iterations
    }
    
    // Cleanup
    if (current_path) free(current_path);
    
    // If withdrawn, bananas disappear
    if (ape->family->is_withdrawn && ape->collected_bananas > 0) {
        log_event(ape->sim, "[WITHDRAW] Female %d lost %d bananas on withdrawal\n",
                 ape->family->family_id, ape->collected_bananas);
        ape->collected_bananas = 0;
    }
    
    return NULL;
}