#include "../include/sync.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <time.h>

/* ============================================================
 * Lock ordering helpers
 * ============================================================ */

int compare_positions(Position a, Position b) {
    if (a.y < b.y) return -1;
    if (a.y > b.y) return 1;
    if (a.x < b.x) return -1;
    if (a.x > b.x) return 1;
    return 0;
}

void lock_cells_in_order(Maze* maze, Position p1, Position p2) {
    if (!maze || !maze->cells) return;

    Position first = p1, second = p2;
    if (compare_positions(p1, p2) > 0) {
        first = p2;
        second = p1;
    }

    pthread_mutex_lock(&maze->cells[first.y][first.x].cell_mutex);

    if (first.x != second.x || first.y != second.y) {
        pthread_mutex_lock(&maze->cells[second.y][second.x].cell_mutex);
    }
}

void unlock_cells_in_reverse(Maze* maze, Position p1, Position p2) {
    if (!maze || !maze->cells) return;

    Position first = p1, second = p2;
    if (compare_positions(p1, p2) > 0) {
        first = p2;
        second = p1;
    }

    if (first.x != second.x || first.y != second.y) {
        pthread_mutex_unlock(&maze->cells[second.y][second.x].cell_mutex);
    }
    pthread_mutex_unlock(&maze->cells[first.y][first.x].cell_mutex);
}

void lock_baskets_in_order(Family* f1, Family* f2) {
    if (!f1 || !f2) return;

    Family* first = f1;
    Family* second = f2;

    if (f1->family_id > f2->family_id) {
        first = f2;
        second = f1;
    }

    pthread_mutex_lock(&first->basket_mutex);
    if (first != second) {
        pthread_mutex_lock(&second->basket_mutex);
    }
}

void unlock_baskets_in_reverse(Family* f1, Family* f2) {
    if (!f1 || !f2) return;

    Family* first = f1;
    Family* second = f2;

    if (f1->family_id > f2->family_id) {
        first = f2;
        second = f1;
    }

    if (first != second) {
        pthread_mutex_unlock(&second->basket_mutex);
    }
    pthread_mutex_unlock(&first->basket_mutex);
}

/* ============================================================
 * Console output
 * ============================================================ */

void log_event(SimulationState* sim, const char* format, ...) {
    if (!sim || !format) return;

    pthread_mutex_lock(&sim->console_mutex);

    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
    fflush(stdout);

    pthread_mutex_unlock(&sim->console_mutex);
}

/* ============================================================
 * Stats updates (ONLY stats_mutex)
 * ============================================================ */

void update_female_fight_stats(SimulationState* sim) {
    if (!sim) return;
    pthread_mutex_lock(&sim->stats_mutex);
    sim->stats.female_fights++;
    pthread_mutex_unlock(&sim->stats_mutex);
}

void update_male_fight_stats(SimulationState* sim) {
    if (!sim) return;
    pthread_mutex_lock(&sim->stats_mutex);
    sim->stats.male_fights++;
    pthread_mutex_unlock(&sim->stats_mutex);
}

void update_baby_steal_stats(SimulationState* sim, int count) {
    if (!sim) return;
    if (count < 0) count = 0;
    pthread_mutex_lock(&sim->stats_mutex);
    sim->stats.baby_steals += count;
    pthread_mutex_unlock(&sim->stats_mutex);
}

void update_total_bananas(SimulationState* sim, int bananas) {
    if (!sim) return;
    pthread_mutex_lock(&sim->stats_mutex);
    sim->stats.total_bananas_collected += bananas;
    pthread_mutex_unlock(&sim->stats_mutex);
}

/* ============================================================
 * Simulation control
 * ============================================================ */

void signal_simulation_stop(SimulationState* sim) {
    if (!sim) return;
    sim->simulation_running = 0;
}

int is_simulation_running(SimulationState* sim) {
    return sim && sim->simulation_running;
}

/* ============================================================
 * Rest mechanism - IMPROVED to prevent rest loops
 * ============================================================ */

static struct timespec timespec_add_ms(struct timespec ts, long ms) {
    ts.tv_sec += ms / 1000;
    ts.tv_nsec += (ms % 1000) * 1000000L;
    if (ts.tv_nsec >= 1000000000L) {
        ts.tv_sec += 1;
        ts.tv_nsec -= 1000000000L;
    }
    return ts;
}

void start_resting(FemaleApe* ape) {
    if (!ape || !ape->sim || !ape->sim->config) return;

    const long REST_MS = 3000; // 3 seconds rest (reduced from 5)

    pthread_mutex_lock(&ape->rest_mutex);

    ape->state = STATE_RESTING;
    ape->is_resting = 1;

    log_event(ape->sim, "Female %d resting... (energy=%d)\n",
              ape->ape_id, ape->energy);

    struct timespec deadline;
    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline = timespec_add_ms(deadline, REST_MS);

    // Wait until either timeout, end_resting(), or simulation stop
    while (ape->is_resting && is_simulation_running(ape->sim)) {
        int rc = pthread_cond_timedwait(&ape->rest_cond, &ape->rest_mutex, &deadline);
        if (rc == ETIMEDOUT) {
            break;
        }
    }

    // IMPROVED: Ensure energy is well above threshold after rest
    // This prevents the rest loop where female rests, gains 10, moves 9 times, rests again
    int threshold = ape->sim->config->female_energy_threshold;
    int min_energy_after_rest = threshold + 30;  // At least 30 above threshold
    
    // Apply rest energy gain
    ape->energy += ape->sim->config->energy_gain_rest;
    
    // Ensure minimum energy level to prevent immediate re-rest
    if (ape->energy < min_energy_after_rest) {
        ape->energy = min_energy_after_rest;
    }
    
    // Cap at max
    if (ape->energy > ape->sim->config->female_energy_max) {
        ape->energy = ape->sim->config->female_energy_max;
    }

    ape->is_resting = 0;
    ape->state = STATE_SEARCHING;  // Resume SEARCHING, not IDLE

    pthread_mutex_unlock(&ape->rest_mutex);

    log_event(ape->sim, "Female %d finished resting (energy=%d)\n",
              ape->ape_id, ape->energy);
}

void end_resting(FemaleApe* ape) {
    if (!ape) return;

    pthread_mutex_lock(&ape->rest_mutex);
    ape->is_resting = 0;
    pthread_cond_signal(&ape->rest_cond);
    pthread_mutex_unlock(&ape->rest_mutex);
}

/* ============================================================
 * GUI Event Queue Functions
 * ============================================================ */

#include <string.h>

void init_gui_event_queue(SimulationState* sim) {
    if (!sim) return;
    
    pthread_mutex_init(&sim->gui_events.mutex, NULL);
    sim->gui_events.head = 0;
    sim->gui_events.count = 0;
    
    for (int i = 0; i < MAX_GUI_EVENTS; i++) {
        memset(&sim->gui_events.events[i], 0, sizeof(GuiEvent));
    }
}

void push_gui_event(SimulationState* sim, GuiEventType type,
                    int baby_family_id, int baby_id,
                    int victim_family_id, int amount) {
    if (!sim) return;
    
    pthread_mutex_lock(&sim->gui_events.mutex);
    
    // Find next slot (circular buffer, overwrite oldest if full)
    int idx = sim->gui_events.head;
    
    // Clear the event first
    memset(&sim->gui_events.events[idx], 0, sizeof(GuiEvent));
    
    sim->gui_events.events[idx].type = type;
    sim->gui_events.events[idx].baby_family_id = baby_family_id;
    sim->gui_events.events[idx].baby_id = baby_id;
    sim->gui_events.events[idx].victim_family_id = victim_family_id;
    sim->gui_events.events[idx].amount = amount;
    sim->gui_events.events[idx].initiator_family_id = -1;
    sim->gui_events.events[idx].defender_family_id = -1;
    sim->gui_events.events[idx].winner_family_id = -1;
    sim->gui_events.events[idx].timestamp = time(NULL);
    
    // Advance head
    sim->gui_events.head = (sim->gui_events.head + 1) % MAX_GUI_EVENTS;
    
    // Update count (cap at MAX_GUI_EVENTS)
    if (sim->gui_events.count < MAX_GUI_EVENTS) {
        sim->gui_events.count++;
    }
    
    pthread_mutex_unlock(&sim->gui_events.mutex);
}

void push_gui_fight_event(SimulationState* sim, GuiEventType type,
                          int initiator_family_id, int defender_family_id,
                          int winner_family_id) {
    if (!sim) return;
    
    pthread_mutex_lock(&sim->gui_events.mutex);
    
    // Find next slot (circular buffer, overwrite oldest if full)
    int idx = sim->gui_events.head;
    
    // Clear the event first
    memset(&sim->gui_events.events[idx], 0, sizeof(GuiEvent));
    
    sim->gui_events.events[idx].type = type;
    sim->gui_events.events[idx].initiator_family_id = initiator_family_id;
    sim->gui_events.events[idx].defender_family_id = defender_family_id;
    sim->gui_events.events[idx].winner_family_id = winner_family_id;
    sim->gui_events.events[idx].baby_family_id = -1;
    sim->gui_events.events[idx].baby_id = -1;
    sim->gui_events.events[idx].victim_family_id = -1;
    sim->gui_events.events[idx].amount = 0;
    sim->gui_events.events[idx].timestamp = time(NULL);
    
    // Advance head
    sim->gui_events.head = (sim->gui_events.head + 1) % MAX_GUI_EVENTS;
    
    // Update count (cap at MAX_GUI_EVENTS)
    if (sim->gui_events.count < MAX_GUI_EVENTS) {
        sim->gui_events.count++;
    }
    
    pthread_mutex_unlock(&sim->gui_events.mutex);
}

void push_gui_male_rest_event(SimulationState* sim, int family_id, int energy_gained) {
    if (!sim) return;
    
    pthread_mutex_lock(&sim->gui_events.mutex);
    
    // Find next slot (circular buffer, overwrite oldest if full)
    int idx = sim->gui_events.head;
    
    // Clear the event first
    memset(&sim->gui_events.events[idx], 0, sizeof(GuiEvent));
    
    sim->gui_events.events[idx].type = GUI_EVENT_MALE_REST;
    sim->gui_events.events[idx].initiator_family_id = family_id;
    sim->gui_events.events[idx].defender_family_id = -1;
    sim->gui_events.events[idx].winner_family_id = -1;
    sim->gui_events.events[idx].baby_family_id = -1;
    sim->gui_events.events[idx].baby_id = -1;
    sim->gui_events.events[idx].victim_family_id = -1;
    sim->gui_events.events[idx].amount = energy_gained;  // Store energy gained in amount field
    sim->gui_events.events[idx].timestamp = time(NULL);
    
    // Advance head
    sim->gui_events.head = (sim->gui_events.head + 1) % MAX_GUI_EVENTS;
    
    // Update count (cap at MAX_GUI_EVENTS)
    if (sim->gui_events.count < MAX_GUI_EVENTS) {
        sim->gui_events.count++;
    }
    
    pthread_mutex_unlock(&sim->gui_events.mutex);
}

int pop_gui_event(SimulationState* sim, GuiEvent* out_event) {
    if (!sim || !out_event) return 0;
    
    pthread_mutex_lock(&sim->gui_events.mutex);
    
    if (sim->gui_events.count == 0) {
        pthread_mutex_unlock(&sim->gui_events.mutex);
        return 0;
    }
    
    // Calculate read position (head - count gives oldest event)
    int read_idx = (sim->gui_events.head - sim->gui_events.count + MAX_GUI_EVENTS) % MAX_GUI_EVENTS;
    
    *out_event = sim->gui_events.events[read_idx];
    sim->gui_events.count--;
    
    pthread_mutex_unlock(&sim->gui_events.mutex);
    return 1;
}

int gui_event_count(SimulationState* sim) {
    if (!sim) return 0;
    
    pthread_mutex_lock(&sim->gui_events.mutex);
    int count = sim->gui_events.count;
    pthread_mutex_unlock(&sim->gui_events.mutex);
    
    return count;
}