#include "../include/simulation_threading.h"
#include "../include/sim_state.h"
#include "../include/sync.h"
#include "../include/maze.h"
#include "../include/female_ape.h"
#include "../include/male_ape.h"
#include "../include/baby_ape.h"
#include "../include/gui.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>

// Check if any termination condition is met
// Returns reason string if should terminate, NULL if should continue
const char* check_termination_conditions(SimulationState* sim) {
    if (!sim || !sim->config) return NULL;
    
    // Condition 1: Too many families withdrawn
    pthread_mutex_lock(&sim->stats_mutex);
    int withdrawn = sim->withdrawn_count;
    pthread_mutex_unlock(&sim->stats_mutex);
    
    if (withdrawn >= sim->config->withdrawn_family_threshold) {
        return "WITHDRAWN_FAMILIES_THRESHOLD";
    }
    
    // Condition 2: Only 1 family remaining (no point continuing)
    int active_families = sim->num_families - withdrawn;
    if (active_families <= 1) {
        return "ONLY_ONE_FAMILY_LEFT";
    }
    
    // Condition 3: A family has collected enough bananas
    for (int i = 0; i < sim->num_families; i++) {
        Family* family = &sim->families[i];
        
        pthread_mutex_lock(&family->basket_mutex);
        int basket = family->basket_bananas;
        pthread_mutex_unlock(&family->basket_mutex);
        
        if (basket >= sim->config->family_banana_threshold) {
            return "FAMILY_BASKET_THRESHOLD";
        }
    }
    
    // Condition 4: A baby has eaten too many bananas
    // This is checked inside baby_ape.c and calls signal_simulation_stop()
    // But we also check here for reporting
    for (int i = 0; i < sim->num_families; i++) {
        Family* family = &sim->families[i];
        
        for (int j = 0; j < family->baby_count; j++) {
            BabyApe* baby = &family->babies[j];
            
            if (baby->eaten_count >= sim->config->baby_eaten_threshold) {
                return "BABY_EATEN_THRESHOLD";
            }
        }
    }
    
    // Condition 5: Time limit exceeded
    time_t current_time = time(NULL);
    double elapsed = difftime(current_time, sim->start_time);
    
    if (elapsed >= sim->config->time_limit_seconds) {
        return "TIME_LIMIT";
    }
    
    return NULL; // No termination condition met
}

// Display detailed final statistics
void display_final_statistics(SimulationState* sim) {
    if (!sim) return;
    
    time_t end_time = time(NULL);
    double total_time = difftime(end_time, sim->start_time);
    
    log_event(sim, "\n");
    log_event(sim, "═══════════════════════════════════════════════════════════════\n");
    log_event(sim, "                   SIMULATION RESULTS                          \n");
    log_event(sim, "═══════════════════════════════════════════════════════════════\n\n");
    
    // Time
    log_event(sim, "⏱  Total Runtime: %.2f seconds (limit: %d seconds)\n\n", 
              total_time, sim->config->time_limit_seconds);
    
    // Banana statistics
    int bananas_in_maze = 0;
    int bananas_in_baskets = 0;
    
    // Count remaining bananas in maze
    if (sim->maze) {
        for (int y = 0; y < sim->maze->height; y++) {
            for (int x = 0; x < sim->maze->width; x++) {
                bananas_in_maze += sim->maze->cells[y][x].banana_count;
            }
        }
    }
    
    // Count bananas in all baskets
    for (int i = 0; i < sim->num_families; i++) {
        pthread_mutex_lock(&sim->families[i].basket_mutex);
        bananas_in_baskets += sim->families[i].basket_bananas;
        pthread_mutex_unlock(&sim->families[i].basket_mutex);
    }
    
    int total_bananas = sim->maze ? sim->maze->total_bananas : 0;
    int bananas_eaten = total_bananas - bananas_in_maze - bananas_in_baskets;
    if (bananas_eaten < 0) bananas_eaten = 0;
    
    log_event(sim, "🍌 BANANA STATISTICS:\n");
    log_event(sim, "   Total Bananas (start):    %d\n", total_bananas);
    log_event(sim, "   Remaining in Maze:        %d\n", bananas_in_maze);
    log_event(sim, "   Collected in Baskets:     %d\n", bananas_in_baskets);
    log_event(sim, "   Eaten by Babies:          %d\n\n", bananas_eaten);
    
    // Global statistics
    pthread_mutex_lock(&sim->stats_mutex);
    log_event(sim, "📊 GLOBAL STATISTICS:\n");
    log_event(sim, "   Female Fights:        %d\n", sim->stats.female_fights);
    log_event(sim, "   Male Fights:          %d\n", sim->stats.male_fights);
    log_event(sim, "   Baby Steals:          %d\n", sim->stats.baby_steals);
    log_event(sim, "   Families Withdrawn:   %d / %d\n\n", 
              sim->withdrawn_count, sim->num_families);
    pthread_mutex_unlock(&sim->stats_mutex);
    
    // Build ranking array with configurable criteria:
    // 1. Survivors first (is_active)
    // 2. Combined score from: total_collected * weight + time_survived * weight + fights_won * weight
    typedef struct {
        int family_id;
        int current_basket;      // NEW: Current bananas in basket
        int total_collected;
        int time_survived;
        int fights_won;
        int is_active;
        double score;
    } FamilyRanking;
    
    FamilyRanking* rankings = malloc(sizeof(FamilyRanking) * sim->num_families);
    
    // end_time already declared at start of function
    
    for (int i = 0; i < sim->num_families; i++) {
        Family* family = &sim->families[i];
        
        pthread_mutex_lock(&family->basket_mutex);
        rankings[i].current_basket = family->basket_bananas;
        rankings[i].total_collected = family->total_bananas_collected;
        pthread_mutex_unlock(&family->basket_mutex);
        
        rankings[i].family_id = i;
        rankings[i].is_active = !family->is_withdrawn;
        rankings[i].fights_won = family->fights_won;
        
        // Calculate time survived
        if (family->is_withdrawn && family->withdrawal_time > 0) {
            rankings[i].time_survived = (int)difftime(family->withdrawal_time, sim->start_time);
        } else {
            rankings[i].time_survived = (int)difftime(end_time, sim->start_time);
        }
        
        // Calculate combined score using configurable weights
        double current_score = rankings[i].current_basket * sim->config->rank_current_basket_weight;
        double collected_score = rankings[i].total_collected * sim->config->rank_total_collected_weight;
        double fight_score = rankings[i].fights_won * sim->config->rank_fights_weight;
        
        rankings[i].score = current_score + collected_score + fight_score;
    }
    
    // Sort with criteria and tiebreakers:
    // 1. Active families first (survivors before withdrawn)
    // 2. Higher combined score
    // TIEBREAKERS (when scores are equal):
    //   3. Higher current basket
    //   4. Longer time survived
    //   5. Lower family_id (deterministic)
    for (int i = 0; i < sim->num_families - 1; i++) {
        for (int j = i + 1; j < sim->num_families; j++) {
            int swap = 0;
            
            // Priority 1: Active families come before withdrawn
            if (rankings[j].is_active && !rankings[i].is_active) {
                swap = 1;
            }
            // Among same active status, compare by score and tiebreakers
            else if (rankings[j].is_active == rankings[i].is_active) {
                // Priority 2: Higher score wins
                if (rankings[j].score > rankings[i].score) {
                    swap = 1;
                }
                // TIEBREAKER CHAIN (when scores are equal)
                else if (rankings[j].score == rankings[i].score) {
                    // Tiebreaker 3: Higher current basket wins
                    if (rankings[j].current_basket > rankings[i].current_basket) {
                        swap = 1;
                    }
                    else if (rankings[j].current_basket == rankings[i].current_basket) {
                        // Tiebreaker 4: Longer time survived wins
                        if (rankings[j].time_survived > rankings[i].time_survived) {
                            swap = 1;
                        }
                        else if (rankings[j].time_survived == rankings[i].time_survived) {
                            // Tiebreaker 5: Lower family_id wins (deterministic)
                            if (rankings[j].family_id < rankings[i].family_id) {
                                swap = 1;
                            }
                        }
                    }
                }
            }
            
            if (swap) {
                FamilyRanking tmp = rankings[i];
                rankings[i] = rankings[j];
                rankings[j] = tmp;
            }
        }
    }
    
    // Display ranking criteria weights
    log_event(sim, "📊 RANKING CRITERIA (Option B: Current Basket Focus):\n");
    log_event(sim, "   Current Basket: %.1f | Total Collected: %.1f | Fights Won: %.1f/win\n",
              sim->config->rank_current_basket_weight,
              sim->config->rank_total_collected_weight,
              sim->config->rank_fights_weight);
    log_event(sim, "   Tiebreakers: current_basket → time_survived → family_id\n\n");
    
    // Display rankings
    log_event(sim, "🏆 FINAL RANKINGS:\n");
    log_event(sim, "─────────────────────────────────────────────────────────────\n");
    
    const char* medals[] = {"🥇 1st", "🥈 2nd", "🥉 3rd"};
    
    for (int i = 0; i < sim->num_families; i++) {
        FamilyRanking* r = &rankings[i];
        const char* status = r->is_active ? "SURVIVOR" : "WITHDRAWN";
        
        if (i < 3) {
            log_event(sim, "  %s Place: Family %d [%s] - Score: %.1f\n",
                      medals[i], r->family_id, status, r->score);
            log_event(sim, "           (basket=%d, collected=%d, time=%ds, wins=%d)\n",
                      r->current_basket, r->total_collected, r->time_survived, r->fights_won);
        } else {
            log_event(sim, "  %dth Place: Family %d [%s] - Score: %.1f (basket=%d, collected=%d, time=%ds, wins=%d)\n",
                      i + 1, r->family_id, status, r->score,
                      r->current_basket, r->total_collected, r->time_survived, r->fights_won);
        }
    }
    
    log_event(sim, "─────────────────────────────────────────────────────────────\n\n");
    
    // Per-family detailed statistics
    log_event(sim, "👨‍👩‍👧‍👦 DETAILED FAMILY STATISTICS:\n");
    log_event(sim, "─────────────────────────────────────────────────────────────\n");
    
    for (int i = 0; i < sim->num_families; i++) {
        Family* family = &sim->families[i];
        
        pthread_mutex_lock(&family->basket_mutex);
        int basket = family->basket_bananas;
        int total_collected = family->total_bananas_collected;
        pthread_mutex_unlock(&family->basket_mutex);
        
        const char* status = family->is_withdrawn ? "WITHDRAWN" : "ACTIVE";
        
        log_event(sim, "  Family %d [%s]:\n", i, status);
        log_event(sim, "    🍌 Total Collected: %d bananas\n", total_collected);
        log_event(sim, "    🍌 Current Basket:  %d bananas\n", basket);
        log_event(sim, "    💪 Male Energy:     %d / %d\n", 
                  family->male.energy, sim->config->male_energy_max);
        log_event(sim, "    🚺 Female Energy:   %d / %d\n", 
                  family->female.energy, sim->config->female_energy_max);
        log_event(sim, "    👶 Babies:          %d babies\n", family->baby_count);
        log_event(sim, "    ⚔️  Fights Won:     %d\n", family->fights_won);
        
        // Baby statistics
        int total_stolen = 0;
        int total_eaten = 0;
        int total_added = 0;
        
        for (int j = 0; j < family->baby_count; j++) {
            BabyApe* baby = &family->babies[j];
            total_stolen += baby->stolen_count;
            total_eaten += baby->eaten_count;
            total_added += baby->added_count;
        }
        
        if (family->baby_count > 0) {
            log_event(sim, "       Total Stolen: %d\n", total_stolen);
            log_event(sim, "       Total Eaten:  %d\n", total_eaten);
            log_event(sim, "       Total Added:  %d\n", total_added);
        }
        
        log_event(sim, "\n");
    }
    
    // Winner announcement
    log_event(sim, "─────────────────────────────────────────────────────────────\n");
    if (rankings[0].score > 0) {
        log_event(sim, "🏆 WINNER: Family %d with score %.1f! (%s)\n", 
                  rankings[0].family_id, rankings[0].score,
                  rankings[0].is_active ? "SURVIVOR" : "WITHDRAWN");
    } else {
        log_event(sim, "No clear winner - no bananas in baskets.\n");
    }
    
    free(rankings);
    
    log_event(sim, "═══════════════════════════════════════════════════════════════\n\n");
    
    // Set flag to suppress further logging from threads
    sim->stats_printed = 1;
}

// Main simulation function
int run_simulation_threading(SimulationState* sim) {
    if (!sim || !sim->maze || !sim->config) {
        printf("[ERROR] Simulation state not properly initialized.\n");
        return -1;
    }
    
    log_event(sim, "\n");
    log_event(sim, "═══════════════════════════════════════════════════════════════\n");
    log_event(sim, "        STARTING APE BANANA COLLECTION SIMULATION              \n");
    log_event(sim, "                  (Pure Threading Mode)                        \n");
    log_event(sim, "═══════════════════════════════════════════════════════════════\n\n");
    
    // Display configuration
    log_event(sim, "📋 CONFIGURATION:\n");
    log_event(sim, "   Maze Size:            %dx%d\n", sim->maze->width, sim->maze->height);
    log_event(sim, "   Families:             %d\n", sim->num_families);
    log_event(sim, "   Babies per Family:    1-%d (random)\n", sim->config->baby_count_per_family);
    log_event(sim, "   Time Limit:           %d seconds\n", sim->config->time_limit_seconds);
    log_event(sim, "   Bananas in Maze:      %d\n\n", sim->maze->total_bananas);
    
    log_event(sim, "🎯 TERMINATION CONDITIONS:\n");
    log_event(sim, "   - Withdrawn Families: %d\n", sim->config->withdrawn_family_threshold);
    log_event(sim, "   - Family Basket:      %d bananas\n", sim->config->family_banana_threshold);
    log_event(sim, "   - Baby Eaten:         %d bananas\n", sim->config->baby_eaten_threshold);
    log_event(sim, "   - Time Limit:         %d seconds\n\n", sim->config->time_limit_seconds);
    
    // Initialize simulation
    sim->simulation_running = 1;
    sim->start_time = time(NULL);
    sim->withdrawn_count = 0;
    
    // Count total threads
    int total_babies = 0;
    for (int i = 0; i < sim->num_families; i++) {
        total_babies += sim->families[i].baby_count;
    }
    int total_threads = sim->num_families * 2 + total_babies; // males + females + babies
    
    log_event(sim, "🚀 Creating %d threads (%d males, %d females, %d babies)...\n\n",
              total_threads, sim->num_families, sim->num_families, total_babies);
    
    // Create all threads
    for (int i = 0; i < sim->num_families; i++) {
        Family* family = &sim->families[i];
        
        // Create male thread
        if (pthread_create(&family->male.thread, NULL, male_ape_thread, &family->male) != 0) {
            log_event(sim, "[ERROR] Failed to create male thread for Family %d\n", i);
            signal_simulation_stop(sim);
            return -1;
        }
        
        // Create female thread
        if (pthread_create(&family->female.thread, NULL, female_ape_thread, &family->female) != 0) {
            log_event(sim, "[ERROR] Failed to create female thread for Family %d\n", i);
            signal_simulation_stop(sim);
            return -1;
        }
        
        // Create baby threads
        for (int j = 0; j < family->baby_count; j++) {
            BabyApe* baby = &family->babies[j];
            if (pthread_create(&baby->thread, NULL, baby_ape_thread, baby) != 0) {
                log_event(sim, "[ERROR] Failed to create baby thread %d for Family %d\n", j, i);
                signal_simulation_stop(sim);
                return -1;
            }
        }
    }
    
    log_event(sim, "✅ All threads created successfully!\n");
    log_event(sim, "🏃 Simulation running... (monitoring every 1 second)\n\n");
    log_event(sim, "─────────────────────────────────────────────────────────────\n\n");
    
    // Monitoring loop
    int loop_count = 0;
    const char* termination_reason = NULL;
    
    while (is_simulation_running(sim)) {
        sleep(1);
        loop_count++;
        
        // Check termination conditions
        termination_reason = check_termination_conditions(sim);
        
        if (termination_reason != NULL) {
            log_event(sim, "\n");
            log_event(sim, "🛑 TERMINATION CONDITION MET: %s\n", termination_reason);
            signal_simulation_stop(sim);
            break;
        }
        
        // Progress update every 10 seconds
        if (loop_count % 10 == 0) {
            time_t current_time = time(NULL);
            double elapsed = difftime(current_time, sim->start_time);
            
            pthread_mutex_lock(&sim->stats_mutex);
            log_event(sim, "[%.0fs] Status: %d fights (F:%d, M:%d), %d steals, %d withdrawn\n",
                      elapsed,
                      sim->stats.female_fights + sim->stats.male_fights,
                      sim->stats.female_fights,
                      sim->stats.male_fights,
                      sim->stats.baby_steals,
                      sim->withdrawn_count);
            pthread_mutex_unlock(&sim->stats_mutex);
        }
    }
    
    // Graceful shutdown
    log_event(sim, "\n─────────────────────────────────────────────────────────────\n");
    log_event(sim, "🛑 Stopping simulation...\n");
    
    // Make sure simulation_running is set to 0
    signal_simulation_stop(sim);
    
    // Wake up all sleeping threads
    log_event(sim, "   Broadcasting shutdown signals to all threads...\n");
    for (int i = 0; i < sim->num_families; i++) {
        Family* family = &sim->families[i];
        
        // Wake up female if resting
        pthread_cond_broadcast(&family->female.rest_cond);
        
        // Wake up babies waiting for fights
        pthread_mutex_lock(&family->fight_mutex);
        pthread_cond_broadcast(&family->fight_cond);
        pthread_mutex_unlock(&family->fight_mutex);
        
        // Wake up anyone waiting on withdrawal
        pthread_cond_broadcast(&family->withdrawal_cond);
    }
    
    // Join all threads
    log_event(sim, "   Joining threads...\n");
    for (int i = 0; i < sim->num_families; i++) {
        Family* family = &sim->families[i];
        
        pthread_join(family->male.thread, NULL);
        pthread_join(family->female.thread, NULL);
        
        for (int j = 0; j < family->baby_count; j++) {
            pthread_join(family->babies[j].thread, NULL);
        }
    }
    
    log_event(sim, "✅ All threads stopped successfully.\n\n");
    
    // Display final statistics
    display_final_statistics(sim);
    
    return 0;
}

// GUI-enabled simulation function
int run_simulation_with_gui(SimulationState* sim) {
    if (!sim || !sim->maze || !sim->config) {
        printf("[ERROR] Simulation state not properly initialized.\n");
        return -1;
    }
    
    log_event(sim, "\n");
    log_event(sim, "═══════════════════════════════════════════════════════════════\n");
    log_event(sim, "        STARTING APE BANANA COLLECTION SIMULATION              \n");
    log_event(sim, "                  (With GUI Visualization)                     \n");
    log_event(sim, "═══════════════════════════════════════════════════════════════\n\n");
    
    // Display configuration
    log_event(sim, "📋 CONFIGURATION:\n");
    log_event(sim, "   Maze Size:            %dx%d\n", sim->maze->width, sim->maze->height);
    log_event(sim, "   Families:             %d\n", sim->num_families);
    log_event(sim, "   Babies per Family:    1-%d (random)\n", sim->config->baby_count_per_family);
    log_event(sim, "   Time Limit:           %d seconds\n", sim->config->time_limit_seconds);
    log_event(sim, "   Bananas in Maze:      %d\n\n", sim->maze->total_bananas);
    
    log_event(sim, "🎯 TERMINATION CONDITIONS:\n");
    log_event(sim, "   - Withdrawn Families: %d\n", sim->config->withdrawn_family_threshold);
    log_event(sim, "   - Family Basket:      %d bananas\n", sim->config->family_banana_threshold);
    log_event(sim, "   - Baby Eaten:         %d bananas\n", sim->config->baby_eaten_threshold);
    log_event(sim, "   - Time Limit:         %d seconds\n\n", sim->config->time_limit_seconds);
    
    // Initialize simulation
    sim->simulation_running = 1;
    sim->start_time = time(NULL);
    sim->withdrawn_count = 0;
    
    // Count total threads
    int total_babies = 0;
    for (int i = 0; i < sim->num_families; i++) {
        total_babies += sim->families[i].baby_count;
    }
    int total_threads = sim->num_families * 2 + total_babies;
    
    log_event(sim, "🚀 Creating %d threads (%d males, %d females, %d babies)...\n\n",
              total_threads, sim->num_families, sim->num_families, total_babies);
    
    // Create all threads
    for (int i = 0; i < sim->num_families; i++) {
        Family* family = &sim->families[i];
        
        if (pthread_create(&family->male.thread, NULL, male_ape_thread, &family->male) != 0) {
            log_event(sim, "[ERROR] Failed to create male thread for Family %d\n", i);
            signal_simulation_stop(sim);
            return -1;
        }
        
        if (pthread_create(&family->female.thread, NULL, female_ape_thread, &family->female) != 0) {
            log_event(sim, "[ERROR] Failed to create female thread for Family %d\n", i);
            signal_simulation_stop(sim);
            return -1;
        }
        
        for (int j = 0; j < family->baby_count; j++) {
            BabyApe* baby = &family->babies[j];
            if (pthread_create(&baby->thread, NULL, baby_ape_thread, baby) != 0) {
                log_event(sim, "[ERROR] Failed to create baby thread %d for Family %d\n", j, i);
                signal_simulation_stop(sim);
                return -1;
            }
        }
    }
    
    log_event(sim, "✅ All threads created successfully!\n");
    log_event(sim, "🖥️  Opening GUI window...\n\n");
    
    // Open GUI window (THIS BLOCKS until window is closed)
    gui_show_simulation(sim);
    
    // When GUI closes, stop simulation
    log_event(sim, "\n─────────────────────────────────────────────────────────────\n");
    log_event(sim, "🛑 GUI closed. Stopping simulation...\n");
    
    signal_simulation_stop(sim);
    
    // Wake up all sleeping threads
    log_event(sim, "   Broadcasting shutdown signals to all threads...\n");
    for (int i = 0; i < sim->num_families; i++) {
        Family* family = &sim->families[i];
        pthread_cond_broadcast(&family->female.rest_cond);
        pthread_mutex_lock(&family->fight_mutex);
        pthread_cond_broadcast(&family->fight_cond);
        pthread_mutex_unlock(&family->fight_mutex);
        pthread_cond_broadcast(&family->withdrawal_cond);
    }
    
    // Join all threads
    log_event(sim, "   Joining threads...\n");
    for (int i = 0; i < sim->num_families; i++) {
        Family* family = &sim->families[i];
        pthread_join(family->male.thread, NULL);
        pthread_join(family->female.thread, NULL);
        for (int j = 0; j < family->baby_count; j++) {
            pthread_join(family->babies[j].thread, NULL);
        }
    }
    
    log_event(sim, "✅ All threads stopped successfully.\n\n");
    
    // Display final statistics
    display_final_statistics(sim);
    
    return 0;
}