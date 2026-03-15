#include "../include/config.h"
#include "../include/utils.h"
#include <stdio.h>
#include <string.h>

static Config make_default_config(void) {
    Config c;
    
    // Maze settings
    strncpy(c.maze_path, DEFAULT_MAZE_PATH, sizeof(c.maze_path) - 1);
    c.maze_path[sizeof(c.maze_path) - 1] = '\0';  // Ensure null termination
    c.maze_size = DEFAULT_MAZE_SIZE;
    c.obstacle_density = DEFAULT_OBSTACLE_DENSITY;  // Deprecated
    c.banana_density = DEFAULT_BANANA_DENSITY;
    c.max_bananas_per_cell = DEFAULT_MAX_BANANAS_PER_CELL;
    c.wall_removal_percent = DEFAULT_WALL_REMOVAL_PERCENT;

    // Family configuration
    c.num_families = DEFAULT_NUM_FAMILIES;
    c.baby_count_per_family = DEFAULT_BABY_COUNT_PER_FAMILY;
    c.bananas_per_trip = DEFAULT_BANANAS_PER_TRIP;

    // Energy system
    c.female_energy_max = DEFAULT_FEMALE_ENERGY_MAX;
    c.male_energy_max = DEFAULT_MALE_ENERGY_MAX;
    c.female_energy_threshold = DEFAULT_FEMALE_ENERGY_THRESHOLD;
    c.male_energy_threshold = DEFAULT_MALE_ENERGY_THRESHOLD;
    c.energy_loss_move = DEFAULT_ENERGY_LOSS_MOVE;
    c.energy_loss_fight = DEFAULT_ENERGY_LOSS_FIGHT;
    c.energy_gain_rest = DEFAULT_ENERGY_GAIN_REST;
    c.energy_loss_guard = DEFAULT_ENERGY_LOSS_GUARD;

    // Fight mechanics
    c.fight_probability_base = DEFAULT_FIGHT_PROBABILITY_BASE;
    c.fight_base_damage = DEFAULT_FIGHT_BASE_DAMAGE;
    c.fight_max_strength = DEFAULT_FIGHT_MAX_STRENGTH;
    c.fight_scaling_factor = DEFAULT_FIGHT_SCALING_FACTOR;
    c.fight_damage_cap = DEFAULT_FIGHT_DAMAGE_CAP;
    c.max_fight_rounds = DEFAULT_MAX_FIGHT_ROUNDS;

    // Movement
    c.sensing_radius = DEFAULT_SENSING_RADIUS;

    // Male guard
    c.male_guard_interval = DEFAULT_MALE_GUARD_INTERVAL;
    c.male_rest_energy_gain = DEFAULT_MALE_REST_ENERGY_GAIN;
    c.male_rest_duration_ms = DEFAULT_MALE_REST_DURATION_MS;

    // Fight target selection weights (NEW)
    c.fight_target_basket_weight = DEFAULT_FIGHT_TARGET_BASKET_WEIGHT;
    c.fight_target_distance_weight = DEFAULT_FIGHT_TARGET_DISTANCE_WEIGHT;

    // Ranking criteria weights
    c.rank_current_basket_weight = DEFAULT_RANK_CURRENT_BASKET_WEIGHT;
    c.rank_total_collected_weight = DEFAULT_RANK_TOTAL_COLLECTED_WEIGHT;
    c.rank_fights_weight = DEFAULT_RANK_FIGHTS_WEIGHT;

    // Baby stealing
    c.baby_steal_min = DEFAULT_BABY_STEAL_MIN;
    c.baby_steal_max = DEFAULT_BABY_STEAL_MAX;
    c.baby_steal_base_rate = DEFAULT_BABY_STEAL_BASE_RATE;
    c.baby_eat_probability = DEFAULT_BABY_EAT_PROBABILITY;

    // Termination conditions
    c.withdrawn_family_threshold = DEFAULT_WITHDRAWN_FAMILY_THRESHOLD;
    c.family_banana_threshold = DEFAULT_FAMILY_BANANA_THRESHOLD;
    c.baby_eaten_threshold = DEFAULT_BABY_EATEN_THRESHOLD;
    c.time_limit_seconds = DEFAULT_TIME_LIMIT_SECONDS;

    return c;
}

void create_default_config_file(const char* filename) {
    FILE* f = fopen(filename, "w");
    if (!f) return;

    fprintf(f,
        "# Maze Configuration\n"
        "MAZE_PATH=%s\n"
        "MAZE_SIZE=%d\n"
        "OBSTACLE_DENSITY=%g\n"
        "BANANA_DENSITY=%g\n"
        "MAX_BANANAS_PER_CELL=%d\n\n"
        "# Family Configuration\n"
        "NUM_FAMILIES=%d\n"
        "BABY_COUNT_PER_FAMILY=%d\n"
        "BANANAS_PER_TRIP=%d\n\n"
        "# Energy Parameters\n"
        "FEMALE_ENERGY_MAX=%d\n"
        "MALE_ENERGY_MAX=%d\n"
        "FEMALE_ENERGY_THRESHOLD=%d\n"
        "MALE_ENERGY_THRESHOLD=%d\n"
        "ENERGY_LOSS_MOVE=%d\n"
        "ENERGY_LOSS_FIGHT=%d\n"
        "ENERGY_GAIN_REST=%d\n"
        "ENERGY_LOSS_GUARD=%d\n\n"
        "# Fight Parameters\n"
        "FIGHT_PROBABILITY_BASE=%g\n"
        "FIGHT_BASE_DAMAGE=%d\n"
        "FIGHT_MAX_STRENGTH=%d\n"
        "FIGHT_SCALING_FACTOR=%g\n"
        "FIGHT_DAMAGE_CAP=%d\n"
        "MAX_FIGHT_ROUNDS=%d\n\n"
        "# Male Guard Parameters\n"
        "MALE_GUARD_INTERVAL=%d\n\n"
        "# Baby Stealing Parameters\n"
        "BABY_STEAL_MIN=%d\n"
        "BABY_STEAL_MAX=%d\n"
        "BABY_STEAL_BASE_RATE=%g\n"
        "BABY_EAT_PROBABILITY=%g\n\n"
        "# Movement Parameters\n"
        "SENSING_RADIUS=%d\n\n"
        "# Termination Conditions\n"
        "WITHDRAWN_FAMILY_THRESHOLD=%d\n"
        "FAMILY_BANANA_THRESHOLD=%d\n"
        "BABY_EATEN_THRESHOLD=%d\n"
        "TIME_LIMIT_SECONDS=%d\n",
        DEFAULT_MAZE_PATH,
        DEFAULT_MAZE_SIZE, DEFAULT_OBSTACLE_DENSITY, DEFAULT_BANANA_DENSITY, DEFAULT_MAX_BANANAS_PER_CELL,
        DEFAULT_NUM_FAMILIES, DEFAULT_BABY_COUNT_PER_FAMILY, DEFAULT_BANANAS_PER_TRIP,
        DEFAULT_FEMALE_ENERGY_MAX, DEFAULT_MALE_ENERGY_MAX, DEFAULT_FEMALE_ENERGY_THRESHOLD, DEFAULT_MALE_ENERGY_THRESHOLD,
        DEFAULT_ENERGY_LOSS_MOVE, DEFAULT_ENERGY_LOSS_FIGHT, DEFAULT_ENERGY_GAIN_REST, DEFAULT_ENERGY_LOSS_GUARD,
        DEFAULT_FIGHT_PROBABILITY_BASE, DEFAULT_FIGHT_BASE_DAMAGE, DEFAULT_FIGHT_MAX_STRENGTH,
        DEFAULT_FIGHT_SCALING_FACTOR, DEFAULT_FIGHT_DAMAGE_CAP, DEFAULT_MAX_FIGHT_ROUNDS,
        DEFAULT_MALE_GUARD_INTERVAL,
        DEFAULT_BABY_STEAL_MIN, DEFAULT_BABY_STEAL_MAX, DEFAULT_BABY_STEAL_BASE_RATE, DEFAULT_BABY_EAT_PROBABILITY,
        DEFAULT_SENSING_RADIUS,
        DEFAULT_WITHDRAWN_FAMILY_THRESHOLD, DEFAULT_FAMILY_BANANA_THRESHOLD, DEFAULT_BABY_EATEN_THRESHOLD, DEFAULT_TIME_LIMIT_SECONDS
    );

    fclose(f);
}

static void apply_kv(Config* c, const char* key, const char* val) {
    int iv;
    double dv;

    // Parse string values
    if (str_ieq(key, "MAZE_PATH")) {
        strncpy(c->maze_path, val, sizeof(c->maze_path) - 1);
        c->maze_path[sizeof(c->maze_path) - 1] = '\0';
    }
    
    // Parse integer values
    else if (str_ieq(key, "MAZE_SIZE") && parse_int_safe(val, &iv)) c->maze_size = iv;
    else if (str_ieq(key, "MAX_BANANAS_PER_CELL") && parse_int_safe(val, &iv)) c->max_bananas_per_cell = iv;
    else if (str_ieq(key, "NUM_FAMILIES") && parse_int_safe(val, &iv)) c->num_families = iv;
    else if (str_ieq(key, "BABY_COUNT_PER_FAMILY") && parse_int_safe(val, &iv)) c->baby_count_per_family = iv;
    else if (str_ieq(key, "BANANAS_PER_TRIP") && parse_int_safe(val, &iv)) c->bananas_per_trip = iv;

    else if (str_ieq(key, "FEMALE_ENERGY_MAX") && parse_int_safe(val, &iv)) c->female_energy_max = iv;
    else if (str_ieq(key, "MALE_ENERGY_MAX") && parse_int_safe(val, &iv)) c->male_energy_max = iv;
    else if (str_ieq(key, "FEMALE_ENERGY_THRESHOLD") && parse_int_safe(val, &iv)) c->female_energy_threshold = iv;
    else if (str_ieq(key, "MALE_ENERGY_THRESHOLD") && parse_int_safe(val, &iv)) c->male_energy_threshold = iv;
    else if (str_ieq(key, "ENERGY_LOSS_MOVE") && parse_int_safe(val, &iv)) c->energy_loss_move = iv;
    else if (str_ieq(key, "ENERGY_LOSS_FIGHT") && parse_int_safe(val, &iv)) c->energy_loss_fight = iv;
    else if (str_ieq(key, "ENERGY_GAIN_REST") && parse_int_safe(val, &iv)) c->energy_gain_rest = iv;
    else if (str_ieq(key, "ENERGY_LOSS_GUARD") && parse_int_safe(val, &iv)) c->energy_loss_guard = iv;

    else if (str_ieq(key, "FIGHT_BASE_DAMAGE") && parse_int_safe(val, &iv)) c->fight_base_damage = iv;
    else if (str_ieq(key, "FIGHT_MAX_STRENGTH") && parse_int_safe(val, &iv)) c->fight_max_strength = iv;
    else if (str_ieq(key, "FIGHT_DAMAGE_CAP") && parse_int_safe(val, &iv)) c->fight_damage_cap = iv;
    else if (str_ieq(key, "MAX_FIGHT_ROUNDS") && parse_int_safe(val, &iv)) c->max_fight_rounds = iv;

    else if (str_ieq(key, "SENSING_RADIUS") && parse_int_safe(val, &iv)) c->sensing_radius = iv;
    else if (str_ieq(key, "MALE_GUARD_INTERVAL") && parse_int_safe(val, &iv)) c->male_guard_interval = iv;
    else if (str_ieq(key, "MALE_REST_ENERGY_GAIN") && parse_int_safe(val, &iv)) c->male_rest_energy_gain = iv;
    else if (str_ieq(key, "MALE_REST_DURATION_MS") && parse_int_safe(val, &iv)) c->male_rest_duration_ms = iv;

    // Baby stealing parameters
    else if (str_ieq(key, "BABY_STEAL_MIN") && parse_int_safe(val, &iv)) c->baby_steal_min = iv;
    else if (str_ieq(key, "BABY_STEAL_MAX") && parse_int_safe(val, &iv)) c->baby_steal_max = iv;

    else if (str_ieq(key, "WITHDRAWN_FAMILY_THRESHOLD") && parse_int_safe(val, &iv)) c->withdrawn_family_threshold = iv;
    else if (str_ieq(key, "FAMILY_BANANA_THRESHOLD") && parse_int_safe(val, &iv)) c->family_banana_threshold = iv;
    else if (str_ieq(key, "BABY_EATEN_THRESHOLD") && parse_int_safe(val, &iv)) c->baby_eaten_threshold = iv;
    else if (str_ieq(key, "TIME_LIMIT_SECONDS") && parse_int_safe(val, &iv)) c->time_limit_seconds = iv;

    // Parse double values
    else if (str_ieq(key, "OBSTACLE_DENSITY") && parse_double_safe(val, &dv)) c->obstacle_density = dv;
    else if (str_ieq(key, "WALL_REMOVAL_PERCENT") && parse_double_safe(val, &dv)) c->wall_removal_percent = dv;
    else if (str_ieq(key, "BANANA_DENSITY") && parse_double_safe(val, &dv)) c->banana_density = dv;
    else if (str_ieq(key, "FIGHT_PROBABILITY_BASE") && parse_double_safe(val, &dv)) c->fight_probability_base = dv;
    else if (str_ieq(key, "FIGHT_SCALING_FACTOR") && parse_double_safe(val, &dv)) c->fight_scaling_factor = dv;
    
    // NEW: Baby stealing base rate (double)
    else if (str_ieq(key, "BABY_STEAL_BASE_RATE") && parse_double_safe(val, &dv)) c->baby_steal_base_rate = dv;
    else if (str_ieq(key, "BABY_EAT_PROBABILITY") && parse_double_safe(val, &dv)) c->baby_eat_probability = dv;
    
    // NEW: Fight target selection weights (double)
    else if (str_ieq(key, "FIGHT_TARGET_BASKET_WEIGHT") && parse_double_safe(val, &dv)) c->fight_target_basket_weight = dv;
    else if (str_ieq(key, "FIGHT_TARGET_DISTANCE_WEIGHT") && parse_double_safe(val, &dv)) c->fight_target_distance_weight = dv;
    
    // Ranking criteria weights
    else if (str_ieq(key, "RANK_CURRENT_BASKET_WEIGHT") && parse_double_safe(val, &dv)) c->rank_current_basket_weight = dv;
    else if (str_ieq(key, "RANK_TOTAL_COLLECTED_WEIGHT") && parse_double_safe(val, &dv)) c->rank_total_collected_weight = dv;
    else if (str_ieq(key, "RANK_FIGHTS_WEIGHT") && parse_double_safe(val, &dv)) c->rank_fights_weight = dv;
}

Config parse_config(const char* filename) {
    Config c = make_default_config();

    FILE* f = fopen(filename, "r");
    if (!f) {
        // File doesn't exist, create it with defaults
        create_default_config_file(filename);
        return c;
    }

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        char* s = trim_inplace(line);
        
        // Skip empty lines and comments
        if (*s == '\0' || *s == '#') continue;

        // Look for KEY=VALUE pattern
        char* eq = strchr(s, '=');
        if (!eq) continue;

        *eq = '\0';
        char* key = trim_inplace(s);
        char* val = trim_inplace(eq + 1);

        apply_kv(&c, key, val);
    }

    fclose(f);
    return c;
}

void warn_suspicious_config(const Config* c) {
    if (!c) return;

    // Check if too many families for maze size
    if (c->num_families > (c->maze_size * c->maze_size) / 10) {
        printf("Warning: Many families (%d) for maze size (%d)\n", c->num_families, c->maze_size);
    }
    
    // Check energy thresholds
    if (c->female_energy_threshold >= c->female_energy_max) {
        printf("Warning: Female energy threshold >= max!\n");
    }
    if (c->male_energy_threshold >= c->male_energy_max) {
        printf("Warning: Male energy threshold >= max!\n");
    }
    
    // Check banana collection target
    if (c->bananas_per_trip > c->maze_size * 2) {
        printf("Warning: Bananas per trip (%d) very high\n", c->bananas_per_trip);
    }

    // Check energy balance for movement cost
    int typical_trip = c->maze_size;
    int energy_needed = typical_trip * 2 * c->energy_loss_move;  // Round trip
    int energy_available = c->female_energy_max - c->female_energy_threshold;
    
    if (energy_needed > energy_available) {
        printf("Warning: Energy loss per move (%d) high for maze size (%d)\n",
               c->energy_loss_move, c->maze_size);
        printf("  Typical trip needs ~%d energy, available: %d\n",
               energy_needed, energy_available);
    }

    // Check male guard sustainability
    int guard_cycles = (c->male_energy_max - c->male_energy_threshold) / c->energy_loss_guard;
    int guard_time = guard_cycles * c->male_guard_interval;
    if (guard_time < 60) {
        printf("Warning: Male can only guard for ~%d seconds without fighting\n", guard_time);
    }
}