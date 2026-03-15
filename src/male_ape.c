#include "../include/male_ape.h"
#include "../include/fight.h"
#include "../include/sim_state.h" 
#include "../include/sync.h"
#include <stdlib.h>
#include <unistd.h>

// Calculate fight probability using quadratic scaling for endgame chaos
double calculate_fight_probability(Family* my_family, Family* neighbor_family,
                                  const Config* config, const Maze* maze) {
    if (!my_family || !neighbor_family || !config || !maze) return 0.0;
    
    int my_bananas = my_family->basket_bananas;
    int neighbor_bananas = neighbor_family->basket_bananas;
    int total_basket_bananas = my_bananas + neighbor_bananas;
    
    if (total_basket_bananas == 0) return 0.0;
    
    // Quadratic scaling: peaceful early game, explosive endgame
    // normalized: 0.0 (no bananas) to 1.0+ (all bananas collected)
    double normalized = (double)total_basket_bananas / (maze->total_bananas * 1.0);
    
    // prob = base + (normalized^2 * 0.7)
    // Examples (500 banana maze):
    //   0 bananas:   30%
    //   100 bananas: 34%
    //   200 bananas: 56%
    //   300 bananas: 91%
    double prob = config->fight_probability_base + (normalized * normalized * 0.7);
    
    if (prob > 1.0) prob = 1.0;
    return prob;
}

/* ============================================================
 * Dynamic neighbor detection - finds active neighbors
 * This handles the case where families have withdrawn
 * ============================================================ */

// Helper to get border index for sorting families clockwise around maze
static int get_border_index(Position p, int w, int h) {
    if (p.y == 0) return p.x;                                   // top
    if (p.x == w - 1) return (w - 1) + p.y;                    // right
    if (p.y == h - 1) return (w - 1) + (h - 1) + (w - 1 - p.x); // bottom
    return (w - 1) + (h - 1) + (w - 1) + (h - 1 - p.y);         // left
}

// Find active neighbors dynamically, skipping withdrawn families
// Returns the number of active neighbors found (0, 1, or 2)
// out_neighbors must be an array of at least 2 Family pointers
static int find_active_neighbors(MaleApe* male, SimulationState* sim, Family** out_neighbors) {
    if (!male || !sim || !out_neighbors || !sim->maze) return 0;
    
    Family* my_family = male->family;
    int num_families = sim->num_families;
    int w = sim->maze->width;
    int h = sim->maze->height;
    
    // Build sorted list of all families by border position
    typedef struct {
        Family* family;
        int index;
    } BorderFamily;
    
    BorderFamily* sorted = malloc(sizeof(BorderFamily) * num_families);
    if (!sorted) return 0;
    
    for (int i = 0; i < num_families; i++) {
        sorted[i].family = &sim->families[i];
        sorted[i].index = get_border_index(sim->families[i].home_pos, w, h);
    }
    
    // Sort by border index (clockwise order)
    for (int i = 0; i < num_families - 1; i++) {
        for (int j = i + 1; j < num_families; j++) {
            if (sorted[j].index < sorted[i].index) {
                BorderFamily tmp = sorted[i];
                sorted[i] = sorted[j];
                sorted[j] = tmp;
            }
        }
    }
    
    // Find my position in the sorted array
    int my_pos = -1;
    for (int i = 0; i < num_families; i++) {
        if (sorted[i].family == my_family) {
            my_pos = i;
            break;
        }
    }
    
    if (my_pos < 0) {
        free(sorted);
        return 0;
    }
    
    int neighbor_count = 0;
    out_neighbors[0] = NULL;
    out_neighbors[1] = NULL;
    
    // Find left neighbor (search counter-clockwise, skipping withdrawn)
    for (int offset = 1; offset < num_families; offset++) {
        int idx = (my_pos - offset + num_families) % num_families;
        Family* candidate = sorted[idx].family;
        
        if (!candidate->is_withdrawn && candidate != my_family) {
            out_neighbors[0] = candidate;
            neighbor_count++;
            break;
        }
    }
    
    // Find right neighbor (search clockwise, skipping withdrawn)
    for (int offset = 1; offset < num_families; offset++) {
        int idx = (my_pos + offset) % num_families;
        Family* candidate = sorted[idx].family;
        
        if (!candidate->is_withdrawn && candidate != my_family) {
            // Make sure it's not the same as left neighbor
            if (candidate != out_neighbors[0]) {
                out_neighbors[1] = candidate;
                neighbor_count++;
            }
            break;
        }
    }
    
    free(sorted);
    return neighbor_count;
}

// Check if we should initiate fight with this neighbor
// Returns 1 if fight should happen, 0 otherwise
static int should_fight_neighbor(MaleApe* male, Family* neighbor, SimulationState* sim) {
    if (!male || !neighbor || !sim) return 0;
    
    // Don't fight if neighbor is withdrawn
    if (neighbor->is_withdrawn) return 0;
    
    // Don't fight if neighbor male is already fighting
    if (neighbor->male.state == STATE_FIGHTING) return 0;
    
    // Don't fight if our family is withdrawn
    if (male->family->is_withdrawn) return 0;
    
    // Calculate fight probability
    double prob = calculate_fight_probability(male->family, neighbor, 
                                             sim->config, sim->maze);
    
    // Roll for fight
    int roll = rand() % 100;
    int threshold = (int)(prob * 100);
    
    return (roll < threshold);
}

// Main male ape thread function
void* male_ape_thread(void* arg) {
    MaleApe* male = (MaleApe*)arg;
    if (!male || !male->sim || !male->family) return NULL;
    
    SimulationState* sim = male->sim;
    Family* family = male->family;
    
    log_event(sim, "Male %d started guarding (energy=%d)\n",
              male->ape_id, male->energy);
    
    // Main guard loop
    while (is_simulation_running(sim) && !family->is_withdrawn) {
        
        // Check energy first - if below threshold, family withdraws
        if (male->energy < sim->config->male_energy_threshold) {
            log_event(sim, "Male %d energy exhausted (%d < %d), family withdrawing\n",
                      male->ape_id, male->energy, sim->config->male_energy_threshold);
            withdraw_family(family, sim);
            break;
        }
        
        // Lose energy from guarding (passive drain)
        male->energy -= sim->config->energy_loss_guard;
        if (male->energy < 0) male->energy = 0;
         // CHECK IF RESTING: Skip fight initiation while recovering
        if (male->is_resting) {
            // Rest for configured duration
            usleep(sim->config->male_rest_duration_ms * 1000);  // Convert ms to us
            
            // Clear rest flag - ready to fight again next round
            male->is_resting = 0;
            male->state = STATE_IDLE;
            
            log_event(sim, "[MALE REST] Male %d finished resting (E:%d)\n",
                      male->ape_id, male->energy);
            
            // Skip to next iteration - don't fight this round
            continue;
        }
        
        // DYNAMIC NEIGHBOR DETECTION: Find current active neighbors
        // This automatically skips withdrawn families!
        Family* active_neighbors[2] = {NULL, NULL};
        int neighbor_count = find_active_neighbors(male, sim, active_neighbors);
        
        // WEIGHTED TARGET SELECTION: Choose fight target based on basket wealth AND distance
        // Configurable weights from arguments.txt
        Family* fight_target = NULL;
        
        if (neighbor_count == 1 && active_neighbors[0] != NULL) {
            // Only one neighbor - check if we should fight them
            if (should_fight_neighbor(male, active_neighbors[0], sim)) {
                fight_target = active_neighbors[0];
            }
        } else if (neighbor_count == 2) {
            // Two neighbors - use weighted selection based on basket AND distance
            Family* n0 = active_neighbors[0];
            Family* n1 = active_neighbors[1];
            
            // Skip neighbors already fighting
            int n0_available = (n0 && !n0->is_withdrawn && n0->male.state != STATE_FIGHTING);
            int n1_available = (n1 && !n1->is_withdrawn && n1->male.state != STATE_FIGHTING);
            
            if (n0_available && n1_available) {
                // Get basket counts
                pthread_mutex_lock(&n0->basket_mutex);
                int basket0 = n0->basket_bananas;
                pthread_mutex_unlock(&n0->basket_mutex);
                
                pthread_mutex_lock(&n1->basket_mutex);
                int basket1 = n1->basket_bananas;
                pthread_mutex_unlock(&n1->basket_mutex);
                
                // Calculate BORDER distance (how far apart along the maze perimeter)
                int w = sim->maze->width;
                int h = sim->maze->height;
                int perimeter = 2 * (w + h) - 4;  // Total positions around border
                
                int my_idx = get_border_index(family->home_pos, w, h);
                int idx0 = get_border_index(n0->home_pos, w, h);
                int idx1 = get_border_index(n1->home_pos, w, h);
                
                // Border distance is the shorter path (clockwise or counter-clockwise)
                int raw_dist0 = abs(my_idx - idx0);
                int raw_dist1 = abs(my_idx - idx1);
                
                int dist0 = (raw_dist0 < perimeter - raw_dist0) ? raw_dist0 : (perimeter - raw_dist0);
                int dist1 = (raw_dist1 < perimeter - raw_dist1) ? raw_dist1 : (perimeter - raw_dist1);
                
                // Avoid division by zero - minimum distance of 1
                if (dist0 < 1) dist0 = 1;
                if (dist1 < 1) dist1 = 1;
                
                // Get weights from config
                double basket_weight = sim->config->fight_target_basket_weight;
                double distance_weight = sim->config->fight_target_distance_weight;
                
                // Calculate scores:
                // - Higher basket = higher score (more attractive)
                // - Lower distance = higher score (closer is better, so use inverse)
                double basket_score0 = (basket0 + 10) * basket_weight;  // +10 base so empty baskets have some chance
                double basket_score1 = (basket1 + 10) * basket_weight;
                
                double distance_score0 = (100.0 / dist0) * distance_weight;  // Inverse: closer = higher
                double distance_score1 = (100.0 / dist1) * distance_weight;
                
                double total_score0 = basket_score0 + distance_score0;
                double total_score1 = basket_score1 + distance_score1;
                
                // Convert to integer weights for random selection
                int weight0 = (int)(total_score0 * 100);
                int weight1 = (int)(total_score1 * 100);
                
                // Ensure minimum weight
                if (weight0 < 1) weight0 = 1;
                if (weight1 < 1) weight1 = 1;
                
                int total_weight = weight0 + weight1;
                
                // Random selection weighted by combined score
                int roll = rand() % total_weight;
                Family* selected = (roll < weight0) ? n0 : n1;
                
                // Check if we should fight the selected neighbor
                if (should_fight_neighbor(male, selected, sim)) {
                    fight_target = selected;
                    log_event(sim, "[FIGHT SELECT] Male %d chose Family %d (basket=%d, dist=%d, score=%.1f) over Family %d (basket=%d, dist=%d, score=%.1f)\n",
                              male->ape_id, 
                              selected->family_id, 
                              (selected == n0) ? basket0 : basket1,
                              (selected == n0) ? dist0 : dist1,
                              (selected == n0) ? total_score0 : total_score1,
                              (selected == n0) ? n1->family_id : n0->family_id,
                              (selected == n0) ? basket1 : basket0,
                              (selected == n0) ? dist1 : dist0,
                              (selected == n0) ? total_score1 : total_score0);
                }
            } else if (n0_available) {
                if (should_fight_neighbor(male, n0, sim)) {
                    fight_target = n0;
                }
            } else if (n1_available) {
                if (should_fight_neighbor(male, n1, sim)) {
                    fight_target = n1;
                }
            }
        }
        
        // Execute fight if we have a target
        if (fight_target != NULL) {
            male_fight(male, &fight_target->male, sim);
            
            // After fight, check if we should withdraw
            if (male->energy < sim->config->male_energy_threshold) {
                log_event(sim, "Male %d exhausted after fight, family withdrawing\n",
                          male->ape_id);
                withdraw_family(family, sim);
                return NULL;
            }
        }
        
        // Sleep for guard interval
        sleep(sim->config->male_guard_interval);
    }
    
    log_event(sim, "Male %d stopped guarding\n", male->ape_id);
    return NULL;
}