#include "../include/sim_state.h"
#include "../include/sync.h"
#include "../include/maze.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ============================================================
 * Helper
 * ============================================================ */
static int border_index(Position p, int w, int h) {
    if (p.y == 0) return p.x;                                   // top
    if (p.x == w - 1) return (w - 1) + p.y;                    // right
    if (p.y == h - 1) return (w - 1) + (h - 1) + (w - 1 - p.x); // bottom
    return (w - 1) + (h - 1) + (w - 1) + (h - 1 - p.y);         // left
}

typedef struct {
    Family* family;
    int index;
} BorderFamily;

/* ============================================================
 * Initialization
 * ============================================================ */

int init_simulation_state(SimulationState* sim, Config* config) {
    if (!sim || !config) return 0;

    /* IMPORTANT: do not memset(sim) */
    sim->config = config;

    sim->simulation_running = 0;
    sim->stats_printed = 0;  // NEW: Flag to suppress logging after stats
    sim->withdrawn_count = 0;
    sim->start_time = time(NULL);

    memset(&sim->stats, 0, sizeof(SimulationStats));

    pthread_mutex_init(&sim->console_mutex, NULL);
    pthread_mutex_init(&sim->stats_mutex, NULL);
    
    // Initialize GUI event queue
    init_gui_event_queue(sim);

    if (!allocate_families(sim)) {
        pthread_mutex_destroy(&sim->console_mutex);
        pthread_mutex_destroy(&sim->stats_mutex);
        pthread_mutex_destroy(&sim->gui_events.mutex);
        return 0;
    }

    return 1;
}

int allocate_families(SimulationState* sim) {
    if (!sim || !sim->config) return 0;

    sim->num_families = sim->config->num_families;
    if (sim->num_families <= 0) return 0;

    sim->families = calloc((size_t)sim->num_families, sizeof(Family));
    if (!sim->families) return 0;

    for (int i = 0; i < sim->num_families; i++) {
        Family* family = &sim->families[i];

        family->family_id = i;
        family->is_withdrawn = 0;
        family->basket_bananas = 0;
        family->total_bananas_collected = 0;  // Track total ever collected
        family->fights_won = 0;               // Track fights won
        family->withdrawal_time = 0;          // NEW: 0 means still active
        // home_pos will be set later when maze is loaded

        pthread_mutex_init(&family->basket_mutex, NULL);
        pthread_cond_init(&family->withdrawal_cond, NULL);
        
        // Initialize fight signaling for babies
        pthread_mutex_init(&family->fight_mutex, NULL);
        pthread_cond_init(&family->fight_cond, NULL);
        family->fighting_opponent = NULL;

        /* ---------------- Female ---------------- */
        family->female.ape_id = i;
        family->female.family = family;
        family->female.sim = sim;
        family->female.state = STATE_IDLE;

        /* Runtime energy only — limits live in Config */
        family->female.energy = sim->config->female_energy_max;
        family->female.collected_bananas = 0;
        family->female.is_resting = 0;

        pthread_mutex_init(&family->female.rest_mutex, NULL);
        pthread_cond_init(&family->female.rest_cond, NULL);

        /* ---------------- Male ---------------- */
        family->male.ape_id = i;
        family->male.family = family;
        family->male.sim = sim;
        family->male.state = STATE_IDLE;

        family->male.energy = sim->config->male_energy_max;
        family->male.is_resting = 0;  // Not resting initially
        family->male.neighbor_families = NULL;
        family->male.neighbor_count = 0;

        /* ---------------- Babies ---------------- */
        // Random baby count: between 1 and baby_count_per_family
        int max_babies = sim->config->baby_count_per_family;
        if (max_babies > 0) {
            family->baby_count = (rand() % max_babies) + 1;  // Random 1 to N
        } else {
            family->baby_count = 0;
        }

        if (family->baby_count > 0) {
            family->babies =
                calloc((size_t)family->baby_count, sizeof(BabyApe));
            if (!family->babies) return 0;

            for (int j = 0; j < family->baby_count; j++) {
                family->babies[j].ape_id = j;
                family->babies[j].family = family;
                family->babies[j].sim = sim;
                family->babies[j].state = STATE_IDLE;
                family->babies[j].stolen_count = 0;
                family->babies[j].eaten_count = 0;
                family->babies[j].added_count = 0;  // NEW
            }
        } else {
            family->babies = NULL;
        }
    }

    // DO NOT call update_neighbors here - wait for maze and home positions
    return 1;
}

/* ============================================================
 * Neighbor detection (border-adjacent families only)
 * ============================================================ */

static void update_neighbors(SimulationState* sim) {
    if (!sim || !sim->families || sim->num_families < 2 || !sim->maze)
        return;

    int n = sim->num_families;
    int w = sim->maze->width;
    int h = sim->maze->height;

    BorderFamily* arr = malloc(sizeof(BorderFamily) * n);
    if (!arr) return;

    for (int i = 0; i < n; i++) {
        arr[i].family = &sim->families[i];
        arr[i].index = border_index(sim->families[i].home_pos, w, h);
    }

    /* Sort clockwise */
    for (int i = 0; i < n - 1; i++) {
        for (int j = i + 1; j < n; j++) {
            if (arr[j].index < arr[i].index) {
                BorderFamily tmp = arr[i];
                arr[i] = arr[j];
                arr[j] = tmp;
            }
        }
    }

    /* Assign left/right neighbors */
    for (int i = 0; i < n; i++) {
        MaleApe* m = &arr[i].family->male;

        free(m->neighbor_families);

        m->neighbor_count = 2;
        m->neighbor_families = malloc(sizeof(Family*) * 2);

        m->neighbor_families[0] = arr[(i - 1 + n) % n].family;
        m->neighbor_families[1] = arr[(i + 1) % n].family;
    }

    free(arr);
}

/* ============================================================
 * Public neighbor update - CALL THIS AFTER MAZE IS LOADED
 * ============================================================ */

void update_all_neighbors(SimulationState* sim) {
    if (!sim || !sim->maze || !sim->family_homes) {
        log_event(sim, "Warning: Cannot update neighbors - maze or homes not set\n");
        return;
    }

    // Set home positions for all families
    for (int i = 0; i < sim->num_families; i++) {
        sim->families[i].home_pos = sim->family_homes[i];
        // Also set female starting position
        sim->families[i].female.pos = sim->family_homes[i];
        // Initialize movement history (no previous position yet)
        sim->families[i].female.prev_pos.x = -1;
        sim->families[i].female.prev_pos.y = -1;
        // Initialize remembered cell (Change 3)
        sim->families[i].female.remembered_cell.x = -1;
        sim->families[i].female.remembered_cell.y = -1;
        sim->families[i].female.remembered_bananas = 0;
        sim->families[i].female.has_remembered_cell = 0;
    }

    // Now update neighbor relationships
    update_neighbors(sim);

    log_event(sim, "Neighbors updated for %d families\n", sim->num_families);
}

/* ============================================================
 * Family withdrawal
 * ============================================================ */

void withdraw_family(Family* family, SimulationState* sim) {
    if (!family || !sim) return;

    pthread_mutex_lock(&family->basket_mutex);

    if (family->is_withdrawn) {
        pthread_mutex_unlock(&family->basket_mutex);
        return;
    }

    family->is_withdrawn = 1;
    family->withdrawal_time = time(NULL);  // NEW: Record when family withdrew
    family->female.state = STATE_WITHDRAWN;
    family->male.state = STATE_WITHDRAWN;

    for (int i = 0; i < family->baby_count; i++) {
        family->babies[i].state = STATE_WITHDRAWN;
    }

    // Signal withdrawal to all family members
    pthread_cond_broadcast(&family->withdrawal_cond);

    pthread_mutex_unlock(&family->basket_mutex);

    pthread_mutex_lock(&sim->stats_mutex);
    sim->withdrawn_count++;
    pthread_mutex_unlock(&sim->stats_mutex);

    log_event(sim,
              "Family %d withdrawn. Basket=%d\n",
              family->family_id,
              family->basket_bananas);
}

/* ============================================================
 * Cleanup
 * ============================================================ */

void cleanup_simulation_state(SimulationState* sim) {
    if (!sim) return;

    sim->simulation_running = 0;

    if (sim->families) {
        for (int i = 0; i < sim->num_families; i++) {
            Family* f = &sim->families[i];

            pthread_mutex_destroy(&f->basket_mutex);
            pthread_cond_destroy(&f->withdrawal_cond);
            
            // NEW: Cleanup fight signaling
            pthread_mutex_destroy(&f->fight_mutex);
            pthread_cond_destroy(&f->fight_cond);

            pthread_mutex_destroy(&f->female.rest_mutex);
            pthread_cond_destroy(&f->female.rest_cond);

            free(f->male.neighbor_families);
            free(f->babies);
        }
        free(sim->families);
        sim->families = NULL;
    }

    if (sim->maze) {
        destroy_maze(sim->maze);
        sim->maze = NULL;
    }

    free(sim->family_homes);
    sim->family_homes = NULL;

    pthread_mutex_destroy(&sim->console_mutex);
    pthread_mutex_destroy(&sim->stats_mutex);
    pthread_mutex_destroy(&sim->gui_events.mutex);
}