#include "../include/fight.h"
#include "../include/sync.h"
#include "../include/sim_state.h"
#include <stdlib.h>
#include <unistd.h>  // ADDED: for usleep()

// Generic fight mechanics used by both female and male fights
// Implements energy-weighted probability system with strength system
// Fight ends when one ape drops below threshold OR other conditions met
static void generic_fight(int* energy1, int* energy2, 
                         int* damage_taken1, int* damage_taken2,
                         int threshold,
                         int max_strength, int base_damage,
                         int damage_cap, int max_rounds) {
    int strength1 = 1;
    int strength2 = 1;
    int rounds = 0;
    
    // Fight until termination condition
    while (*energy1 >= threshold && *energy2 >= threshold &&
           *damage_taken1 < damage_cap && *damage_taken2 < damage_cap &&
           rounds < max_rounds) {
        
        int total = *energy1 + *energy2;
        if (total <= 0) break;
        
        int roll = rand() % total;
        
        if (roll < *energy1) {
            // Ape1 lands a hit
            int damage = base_damage * strength1;
            *energy2 -= damage;
            if (*energy2 < 0) *energy2 = 0;  // FIXED: Cap at zero
            *damage_taken2 += damage;
            
            // Winner builds momentum (strength increases, max 3)
            strength1 = (strength1 < max_strength) ? strength1 + 1 : max_strength;
            strength2 = 1;  // Loser's strength resets
        } else {
            // Ape2 lands a hit
            int damage = base_damage * strength2;
            *energy1 -= damage;
            if (*energy1 < 0) *energy1 = 0;  // FIXED: Cap at zero
            *damage_taken1 += damage;
            
            // Momentum switches
            strength2 = (strength2 < max_strength) ? strength2 + 1 : max_strength;
            strength1 = 1;
        }
        
        rounds++;
        
        // ADDED: 300ms delay per round so fights are visible in GUI
        // This gives babies time to steal and users time to see the fight
        usleep(300000);
    }
}

void female_fight(FemaleApe* ape1, FemaleApe* ape2, SimulationState* sim) {
    if (!ape1 || !ape2 || !sim) return;
    
    // Don't fight if either family is withdrawn
    if (ape1->family->is_withdrawn || ape2->family->is_withdrawn) {
        return;
    }
    
    int threshold = sim->config->female_energy_threshold;
    int bananas_per_trip = sim->config->bananas_per_trip;  // Usually 10
    
    log_event(sim, "[FIGHT] Female %d (E:%d B:%d) vs Female %d (E:%d B:%d)\n",
              ape1->ape_id, ape1->energy, ape1->collected_bananas,
              ape2->ape_id, ape2->energy, ape2->collected_bananas);
    
    // Both enter fighting state
    ape1->state = STATE_FIGHTING;
    ape2->state = STATE_FIGHTING;
    
    int damage1 = 0, damage2 = 0;
    
    // Execute the fight (modifies energies until one hits threshold)
    generic_fight(&ape1->energy, &ape2->energy, 
                 &damage1, &damage2,
                 threshold,
                 sim->config->fight_max_strength,
                 sim->config->fight_base_damage,
                 999999, // No damage cap for females
                 999);   // No round limit for females
    
    // Determine winner: whoever still has energy >= threshold
    FemaleApe* winner = (ape1->energy >= threshold) ? ape1 : ape2;
    FemaleApe* loser = (ape1->energy >= threshold) ? ape2 : ape1;
    
    // FIX: Winner takes bananas but ONLY up to the threshold (bananas_per_trip)
    int space_left = bananas_per_trip - winner->collected_bananas;
    int to_take = loser->collected_bananas;
    if (to_take > space_left) {
        to_take = space_left;  // Only take what fits
    }
    if (to_take < 0) to_take = 0;
    
    winner->collected_bananas += to_take;
    loser->collected_bananas -= to_take;  // Loser keeps the rest
    
    // Log final state
    log_event(sim, "[FIGHT] Result: Female %d (E:%d B:%d) | Female %d (E:%d B:%d)\n",
              ape1->ape_id, ape1->energy, ape1->collected_bananas,
              ape2->ape_id, ape2->energy, ape2->collected_bananas);
    
    // Winner continues with whatever energy they have left
    winner->state = (winner->collected_bananas >= bananas_per_trip) 
                    ? STATE_RETURNING : STATE_SEARCHING;
    
    // FIX: Loser retreats to previous position to avoid immediate re-fight
    // First rest to recover energy
    loser->state = STATE_RESTING;
    start_resting(loser);  // Single rest cycle
    
    // After rest, if still below threshold keep resting
    while (loser->energy < threshold &&
           is_simulation_running(sim) &&
           !loser->family->is_withdrawn) {
        start_resting(loser);
    }
    
    // Check if loser's family withdrew during rest
    if (loser->family->is_withdrawn) {
        loser->state = STATE_WITHDRAWN;
    } else {
        loser->state = STATE_SEARCHING;
    }
    
    // Update fight statistics
    update_female_fight_stats(sim);
}

void male_fight(MaleApe* initiator, MaleApe* defender, SimulationState* sim) {
    if (!initiator || !defender || !sim) return;
    
    Family* initiator_family = initiator->family;
    Family* defender_family = defender->family;
    
    if (!initiator_family || !defender_family) return;
    
    // FIXED: Do NOT lock baskets at start - babies need to steal during fight
    int initial_basket1 = initiator_family->basket_bananas;
    int initial_basket2 = defender_family->basket_bananas;
    
    // Save baby stats BEFORE fight for summary (max 10 babies per family)
    #define MAX_BABIES_TRACK 10
    int init_stolen[MAX_BABIES_TRACK], init_eaten[MAX_BABIES_TRACK], init_added[MAX_BABIES_TRACK], init_caught[MAX_BABIES_TRACK];
    int def_stolen[MAX_BABIES_TRACK], def_eaten[MAX_BABIES_TRACK], def_added[MAX_BABIES_TRACK], def_caught[MAX_BABIES_TRACK];
    
    for (int i = 0; i < initiator_family->baby_count && i < MAX_BABIES_TRACK; i++) {
        init_stolen[i] = initiator_family->babies[i].stolen_count;
        init_eaten[i] = initiator_family->babies[i].eaten_count;
        init_added[i] = initiator_family->babies[i].added_count;
        init_caught[i] = initiator_family->babies[i].caught_count;
    }
    for (int i = 0; i < defender_family->baby_count && i < MAX_BABIES_TRACK; i++) {
        def_stolen[i] = defender_family->babies[i].stolen_count;
        def_eaten[i] = defender_family->babies[i].eaten_count;
        def_added[i] = defender_family->babies[i].added_count;
        def_caught[i] = defender_family->babies[i].caught_count;
    }
    
    // Signal babies that fight is starting (they can check fighting_opponent)
    pthread_mutex_lock(&initiator_family->fight_mutex);
    initiator_family->fighting_opponent = defender_family;
    pthread_cond_broadcast(&initiator_family->fight_cond);
    pthread_mutex_unlock(&initiator_family->fight_mutex);
    
    pthread_mutex_lock(&defender_family->fight_mutex);
    defender_family->fighting_opponent = initiator_family;
    pthread_cond_broadcast(&defender_family->fight_cond);
    pthread_mutex_unlock(&defender_family->fight_mutex);
    
    // Log with initiator clearly identified
    log_event(sim, "[MALE FIGHT] Male %d INITIATED fight against Male %d!\n",
              initiator->ape_id, defender->ape_id);
    log_event(sim, "[MALE FIGHT] Initiator Male %d (E:%d Basket:%d) vs Defender Male %d (E:%d Basket:%d)\n",
              initiator->ape_id, initiator->energy, initial_basket1,
              defender->ape_id, defender->energy, initial_basket2);
    
    // Push GUI event for fight start
    push_gui_fight_event(sim, GUI_EVENT_MALE_FIGHT_START,
                         initiator_family->family_id, defender_family->family_id, -1);
    
    // Set fighting state
    initiator->state = STATE_FIGHTING;
    defender->state = STATE_FIGHTING;
    
    int damage1 = 0, damage2 = 0;
    
    // Execute fight - baskets NOT locked, babies can steal!
    generic_fight(&initiator->energy, &defender->energy,
                 &damage1, &damage2,
                 sim->config->male_energy_threshold,
                 sim->config->fight_max_strength,
                 sim->config->fight_base_damage,
                 sim->config->fight_damage_cap,
                 sim->config->max_fight_rounds);
    
    // FIXED: Now lock baskets only for the transfer at the end
    lock_baskets_in_order(initiator_family, defender_family);
    
    // Determine fight outcome based on multiple conditions
    MaleApe* winner = NULL;
    MaleApe* loser __attribute__((unused)) = NULL;
    Family* winner_family = NULL;
    Family* loser_family = NULL;
    const char* outcome_reason = "";
    int need_withdraw = 0;
    
    // Condition 1: Energy below threshold → WITHDRAWAL
    if (initiator->energy < sim->config->male_energy_threshold) {
        winner = defender;
        loser = initiator;
        winner_family = defender_family;
        loser_family = initiator_family;
        outcome_reason = "ENERGY DEPLETION (Family withdraws)";
        need_withdraw = 1;
        
    } else if (defender->energy < sim->config->male_energy_threshold) {
        winner = initiator;
        loser = defender;
        winner_family = initiator_family;
        loser_family = defender_family;
        outcome_reason = "ENERGY DEPLETION (Family withdraws)";
        need_withdraw = 1;
        
    // Condition 2: Damage cap exceeded → DEFEAT (no withdrawal)
    } else if (damage1 >= sim->config->fight_damage_cap) {
        winner = defender;
        loser = initiator;
        winner_family = defender_family;
        loser_family = initiator_family;
        outcome_reason = "DAMAGE CAP (Defeated, no withdrawal)";
        
    } else if (damage2 >= sim->config->fight_damage_cap) {
        winner = initiator;
        loser = defender;
        winner_family = initiator_family;
        loser_family = defender_family;
        outcome_reason = "DAMAGE CAP (Defeated, no withdrawal)";
        
    // Condition 3: Max rounds → DRAW
    } else {
        outcome_reason = "MAX ROUNDS (Draw)";
        
        unlock_baskets_in_reverse(initiator_family, defender_family);
        
        // Both males rest after draw too
        initiator->state = STATE_RESTING;
        initiator->is_resting = 1;
        defender->state = STATE_RESTING;
        defender->is_resting = 1;
        
        // Give energy to both fighters for resting
        int energy_gain = sim->config->male_rest_energy_gain;
        int max_energy = sim->config->male_energy_max;
        
        initiator->energy += energy_gain;
        if (initiator->energy > max_energy) initiator->energy = max_energy;
        
        defender->energy += energy_gain;
        if (defender->energy > max_energy) defender->energy = max_energy;
        
        pthread_mutex_lock(&initiator_family->fight_mutex);
        initiator_family->fighting_opponent = NULL;
        pthread_cond_broadcast(&initiator_family->fight_cond);
        pthread_mutex_unlock(&initiator_family->fight_mutex);
        
        pthread_mutex_lock(&defender_family->fight_mutex);
        defender_family->fighting_opponent = NULL;
        pthread_cond_broadcast(&defender_family->fight_cond);
        pthread_mutex_unlock(&defender_family->fight_mutex);
        
        // Print baby stealing summary for DRAW
        log_event(sim, "[BABY SUMMARY] During fight between Family %d and Family %d:\n",
                  initiator_family->family_id, defender_family->family_id);
        
        int total_from_initiator = 0, total_from_defender = 0;
        
        for (int i = 0; i < initiator_family->baby_count && i < MAX_BABIES_TRACK; i++) {
            int d_stolen = initiator_family->babies[i].stolen_count - init_stolen[i];
            int d_eaten = initiator_family->babies[i].eaten_count - init_eaten[i];
            int d_added = initiator_family->babies[i].added_count - init_added[i];
            int d_caught = initiator_family->babies[i].caught_count - init_caught[i];
            
            if (d_stolen > 0 || d_caught > 0) {
                log_event(sim, "  Baby %d-%d: stole %d, ate %d, added %d, caught %d times (from Family %d)\n",
                          initiator_family->family_id, i, d_stolen, d_eaten, d_added, d_caught,
                          defender_family->family_id);
                total_from_defender += d_stolen;
            }
        }
        
        for (int i = 0; i < defender_family->baby_count && i < MAX_BABIES_TRACK; i++) {
            int d_stolen = defender_family->babies[i].stolen_count - def_stolen[i];
            int d_eaten = defender_family->babies[i].eaten_count - def_eaten[i];
            int d_added = defender_family->babies[i].added_count - def_added[i];
            int d_caught = defender_family->babies[i].caught_count - def_caught[i];
            
            if (d_stolen > 0 || d_caught > 0) {
                log_event(sim, "  Baby %d-%d: stole %d, ate %d, added %d, caught %d times (from Family %d)\n",
                          defender_family->family_id, i, d_stolen, d_eaten, d_added, d_caught,
                          initiator_family->family_id);
                total_from_initiator += d_stolen;
            }
        }
        
        log_event(sim, "  Total stolen from Family %d: %d bananas\n", initiator_family->family_id, total_from_initiator);
        log_event(sim, "  Total stolen from Family %d: %d bananas\n", defender_family->family_id, total_from_defender);
        
        log_event(sim, "[MALE FIGHT] DRAW - Max rounds reached, no winner\n");
        log_event(sim, "[MALE REST] Both males resting after draw (+%d energy each)\n", energy_gain);
        
        // Push GUI event for fight end (draw = winner_family_id = -1)
        push_gui_fight_event(sim, GUI_EVENT_MALE_FIGHT_END,
                             initiator_family->family_id, defender_family->family_id, -1);
        
        // Push rest events for both males
        push_gui_male_rest_event(sim, initiator_family->family_id, energy_gain);
        push_gui_male_rest_event(sim, defender_family->family_id, energy_gain);
        
        update_male_fight_stats(sim);
        return;
    }
    
    // Handle withdrawal (unlock first to avoid deadlock)
    if (need_withdraw) {
        unlock_baskets_in_reverse(initiator_family, defender_family);
        withdraw_family(loser_family, sim);
        lock_baskets_in_order(initiator_family, defender_family);
    }
    
    // Winner takes ALL loser's bananas
    if (loser_family->basket_bananas > 0) {
        int stolen = loser_family->basket_bananas;
        winner_family->basket_bananas += stolen;
        loser_family->basket_bananas = 0;
        
        log_event(sim, "[MALE FIGHT] %s: Male %d wins! Took %d bananas\n",
                  outcome_reason, winner->ape_id, stolen);
    } else {
        log_event(sim, "[MALE FIGHT] %s: Male %d wins! (No bananas to take)\n",
                  outcome_reason, winner->ape_id);
    }
    
    // Track fights won for ranking
    winner_family->fights_won++;
    
    unlock_baskets_in_reverse(initiator_family, defender_family);
    
    // Clear fighting state - set to RESTING instead of IDLE
    // Both males need rest after a fight to recover energy
    initiator->state = STATE_RESTING;
    initiator->is_resting = 1;
    defender->state = STATE_RESTING;
    defender->is_resting = 1;
    
    // Give energy to both fighters for resting
    int energy_gain = sim->config->male_rest_energy_gain;
    int max_energy = sim->config->male_energy_max;
    
    initiator->energy += energy_gain;
    if (initiator->energy > max_energy) initiator->energy = max_energy;
    
    defender->energy += energy_gain;
    if (defender->energy > max_energy) defender->energy = max_energy;
    
    // Signal babies that fight is over
    pthread_mutex_lock(&initiator_family->fight_mutex);
    initiator_family->fighting_opponent = NULL;
    pthread_cond_broadcast(&initiator_family->fight_cond);
    pthread_mutex_unlock(&initiator_family->fight_mutex);
    
    pthread_mutex_lock(&defender_family->fight_mutex);
    defender_family->fighting_opponent = NULL;
    pthread_cond_broadcast(&defender_family->fight_cond);
    pthread_mutex_unlock(&defender_family->fight_mutex);
    
    // Print baby stealing summary
    log_event(sim, "[BABY SUMMARY] During fight between Family %d and Family %d:\n",
              initiator_family->family_id, defender_family->family_id);
    
    int total_from_initiator = 0, total_from_defender = 0;
    
    // Initiator family babies (stole from defender)
    for (int i = 0; i < initiator_family->baby_count && i < MAX_BABIES_TRACK; i++) {
        int d_stolen = initiator_family->babies[i].stolen_count - init_stolen[i];
        int d_eaten = initiator_family->babies[i].eaten_count - init_eaten[i];
        int d_added = initiator_family->babies[i].added_count - init_added[i];
        int d_caught = initiator_family->babies[i].caught_count - init_caught[i];
        
        if (d_stolen > 0 || d_caught > 0) {
            log_event(sim, "  Baby %d-%d: stole %d, ate %d, added %d, caught %d times (from Family %d)\n",
                      initiator_family->family_id, i, d_stolen, d_eaten, d_added, d_caught,
                      defender_family->family_id);
            total_from_defender += d_stolen;
        }
    }
    
    // Defender family babies (stole from initiator)
    for (int i = 0; i < defender_family->baby_count && i < MAX_BABIES_TRACK; i++) {
        int d_stolen = defender_family->babies[i].stolen_count - def_stolen[i];
        int d_eaten = defender_family->babies[i].eaten_count - def_eaten[i];
        int d_added = defender_family->babies[i].added_count - def_added[i];
        int d_caught = defender_family->babies[i].caught_count - def_caught[i];
        
        if (d_stolen > 0 || d_caught > 0) {
            log_event(sim, "  Baby %d-%d: stole %d, ate %d, added %d, caught %d times (from Family %d)\n",
                      defender_family->family_id, i, d_stolen, d_eaten, d_added, d_caught,
                      initiator_family->family_id);
            total_from_initiator += d_stolen;
        }
    }
    
    log_event(sim, "  Total stolen from Family %d: %d bananas\n", initiator_family->family_id, total_from_initiator);
    log_event(sim, "  Total stolen from Family %d: %d bananas\n", defender_family->family_id, total_from_defender);
    
    log_event(sim, "[MALE FIGHT] Final: Male %d (E:%d Basket:%d) | Male %d (E:%d Basket:%d)\n",
              initiator->ape_id, initiator->energy, initiator_family->basket_bananas,
              defender->ape_id, defender->energy, defender_family->basket_bananas);
    
    log_event(sim, "[MALE REST] Both males resting after fight (+%d energy each)\n", energy_gain);
    
    // Push GUI events for fight end and rest notification
    push_gui_fight_event(sim, GUI_EVENT_MALE_FIGHT_END,
                         initiator_family->family_id, defender_family->family_id,
                         winner_family->family_id);
    
    // Push rest events for both males
    push_gui_male_rest_event(sim, initiator_family->family_id, energy_gain);
    push_gui_male_rest_event(sim, defender_family->family_id, energy_gain);
    
    update_male_fight_stats(sim);
}