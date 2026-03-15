#include "../include/baby_ape.h"
#include "../include/sync.h"
#include "../include/sim_state.h"
#include <stdlib.h>
#include <unistd.h>
#include <math.h>

// Calculate success probability for stealing attempt
// Multi-factor formula based on:
// 1. Number of babies (more babies = harder to watch)
// 2. Male energy (exhausted males = less alert)
// 3. Basket size (large baskets = hard to notice theft)
static double calculate_steal_success_probability(BabyApe* baby, 
                                                  Family* opponent,
                                                  SimulationState* sim) {
    if (!baby || !opponent || !sim) return 0.0;
    
    Family* my_family = baby->family;
    
    // Base success rate (50%)
    double base_rate = sim->config->baby_steal_base_rate;
    
    // Factor 1: More babies = harder to watch all of them
    // +15% per extra baby (beyond first)
    double baby_bonus = (my_family->baby_count - 1) * 0.15;
    
    // Factor 2: Exhausted males during fight = less alert
    // Calculate average energy of both fighting males
    double my_male_energy = my_family->male.energy;
    double opponent_male_energy = opponent->male.energy;
    double avg_energy = (my_male_energy + opponent_male_energy) / 2.0;
    double max_energy = (double)sim->config->male_energy_max;
    
    // Intensity: 0.0 (fresh) to 1.0 (exhausted)
    double intensity = 1.0 - (avg_energy / max_energy);
    double intensity_bonus = intensity * 0.3;  // Up to +30%
    
    // Factor 3: Large basket = harder to notice small theft
    // Up to +20% for baskets with 250+ bananas
    double basket_size = (double)opponent->basket_bananas;
    double basket_bonus = fmin(basket_size / 500.0, 0.2);  // Cap at 20%
    
    // Total success probability
    double success = base_rate + baby_bonus + intensity_bonus + basket_bonus;
    
    // Clamp between 30% and 95%
    if (success < 0.3) success = 0.3;
    if (success > 0.95) success = 0.95;
    
    return success;
}

// Decide whether to eat bananas or add to dad's basket
// Uses random probability (configurable):
// - baby_eat_probability% chance to eat
// - baby_add_probability% chance to add to basket
// Returns: 1 if added to basket, 0 if eaten
static int decide_eat_or_add(BabyApe* baby, int stolen_amount, Family* victim, SimulationState* sim) {
    Family* my_family = baby->family;
    int added = 0;
    
    // Roll for eat vs add decision
    double roll = (double)rand() / RAND_MAX;
    double eat_prob = sim->config->baby_eat_probability;
    
    // Try to lock dad's basket
    pthread_mutex_lock(&my_family->basket_mutex);
    
    if (roll >= eat_prob) {
        // Add to basket (help family win)
        my_family->basket_bananas += stolen_amount;
        baby->added_count += stolen_amount;
        added = 1;
        
        // Push GUI event for ADDING to basket
        push_gui_event(sim, GUI_EVENT_BABY_ADD,
                       my_family->family_id, baby->ape_id,
                       victim->family_id, stolen_amount);
    } else {
        // Eat for yourself!
        baby->eaten_count += stolen_amount;
        added = 0;
        
        // Push GUI event for EATING
        push_gui_event(sim, GUI_EVENT_BABY_EAT,
                       my_family->family_id, baby->ape_id,
                       victim->family_id, stolen_amount);
    }
    
    pthread_mutex_unlock(&my_family->basket_mutex);
    
    return added;
}

// Attempt to steal from opponent's basket
// Returns 1 on success, 0 on failure
static int attempt_steal(BabyApe* baby, Family* opponent, SimulationState* sim) {
    if (!baby || !opponent || !sim) return 0;
    
    Family* my_family = baby->family;
    
    // Permission check - can only steal if dad is fighting this opponent
    pthread_mutex_lock(&my_family->fight_mutex);
    Family* fighting_opponent = my_family->fighting_opponent;
    pthread_mutex_unlock(&my_family->fight_mutex);
    
    if (fighting_opponent != opponent) {
        // Can't steal - either not fighting, or fighting someone else
        return 0;
    }
    
    // Check if family withdrawn
    if (my_family->is_withdrawn || opponent->is_withdrawn) {
        return 0;
    }
    
    // Try non-blocking lock on opponent's basket (low priority, opportunistic)
    if (pthread_mutex_trylock(&opponent->basket_mutex) != 0) {
        // Basket locked - silently fail
        return 0;
    }
    
    // Got the lock! Now try to steal
    
    // Check if there's anything to steal
    if (opponent->basket_bananas <= 0) {
        pthread_mutex_unlock(&opponent->basket_mutex);
        return 0;
    }
    
    // Calculate steal amount (random 1-10, or whatever is available)
    int max_steal = sim->config->baby_steal_max;
    int min_steal = sim->config->baby_steal_min;
    int steal_amount = (rand() % (max_steal - min_steal + 1)) + min_steal;
    
    if (steal_amount > opponent->basket_bananas) {
        steal_amount = opponent->basket_bananas;
    }
    
    // Calculate success probability
    double success_prob = calculate_steal_success_probability(baby, opponent, sim);
    
    // Roll for success
    double roll = (double)rand() / RAND_MAX;
    
    if (roll >= success_prob) {
        // Caught! Steal fails - track it silently
        baby->caught_count++;
        pthread_mutex_unlock(&opponent->basket_mutex);
        
        // Push GUI event for CAUGHT
        push_gui_event(sim, GUI_EVENT_BABY_CAUGHT,
                       my_family->family_id, baby->ape_id,
                       opponent->family_id, steal_amount);
        
        return 0;
    }
    
    // Success! Steal the bananas
    opponent->basket_bananas -= steal_amount;
    baby->stolen_count += steal_amount;
    
    // Push GUI event for STEAL SUCCESS
    push_gui_event(sim, GUI_EVENT_BABY_STEAL,
                   my_family->family_id, baby->ape_id,
                   opponent->family_id, steal_amount);
    
    pthread_mutex_unlock(&opponent->basket_mutex);
    
    // Now decide: eat or add to dad's basket?
    decide_eat_or_add(baby, steal_amount, opponent, sim);
    
    // Update simulation statistics (increment by 1 for this steal)
    update_baby_steal_stats(sim, 1);
    
    // Check termination condition: has this baby eaten too much?
    if (baby->eaten_count >= sim->config->baby_eaten_threshold) {
        log_event(sim, "\n!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
        log_event(sim, "[TERMINATION] Baby %d-%d has eaten %d bananas!\n",
                  my_family->family_id, baby->ape_id, baby->eaten_count);
        log_event(sim, "[TERMINATION] Baby gorged itself! Simulation ending.\n");
        log_event(sim, "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n\n");
        
        // Signal simulation to stop
        signal_simulation_stop(sim);
    }
    
    return 1;
}

// Main baby ape thread function
void* baby_ape_thread(void* arg) {
    BabyApe* baby = (BabyApe*)arg;
    if (!baby || !baby->sim || !baby->family) return NULL;
    
    SimulationState* sim = baby->sim;
    Family* family = baby->family;
    
    // Main loop: wait for fights, steal, repeat
    while (is_simulation_running(sim) && !family->is_withdrawn) {
        
        // Wait for dad to start fighting
        pthread_mutex_lock(&family->fight_mutex);
        
        while (family->fighting_opponent == NULL && 
               is_simulation_running(sim) && 
               !family->is_withdrawn) {
            // Wait for signal from dad
            pthread_cond_wait(&family->fight_cond, &family->fight_mutex);
        }
        
        // Check if we should exit
        if (!is_simulation_running(sim) || family->is_withdrawn) {
            pthread_mutex_unlock(&family->fight_mutex);
            break;
        }
        
        // Dad is fighting! Get opponent family
        Family* opponent = family->fighting_opponent;
        pthread_mutex_unlock(&family->fight_mutex);
        
        if (!opponent) continue;  // Sanity check
        
        // Steal continuously while fight ongoing
        while (is_simulation_running(sim) && !family->is_withdrawn) {
            // Check if fight still ongoing
            pthread_mutex_lock(&family->fight_mutex);
            Family* current_opponent = family->fighting_opponent;
            pthread_mutex_unlock(&family->fight_mutex);
            
            if (current_opponent != opponent || current_opponent == NULL) {
                // Fight ended
                break;
            }
            
            // Attempt steal
            attempt_steal(baby, opponent, sim);
            
            // Wait before next attempt (100ms = 10 attempts per second)
            usleep(100000);
        }
    }
    
    log_event(sim, "Baby %d-%d stopped (Total: Stolen=%d, Eaten=%d, Added=%d, Caught=%d)\n",
              family->family_id, baby->ape_id,
              baby->stolen_count, baby->eaten_count, baby->added_count, baby->caught_count);
    
    return NULL;
}