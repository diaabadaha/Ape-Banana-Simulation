#ifndef CONFIG_H
#define CONFIG_H

#include "types.h"

// Maze defaults
#define DEFAULT_MAZE_PATH "config/maze.txt"
#define DEFAULT_MAZE_SIZE 20
#define DEFAULT_OBSTACLE_DENSITY 0.2         // Deprecated (kept for compatibility)
#define DEFAULT_BANANA_DENSITY 0.15
#define DEFAULT_MAX_BANANAS_PER_CELL 5
#define DEFAULT_WALL_REMOVAL_PERCENT 0.15    // Remove 15% of walls for multiple paths

// Family defaults
#define DEFAULT_NUM_FAMILIES 7
#define DEFAULT_BABY_COUNT_PER_FAMILY 2
#define DEFAULT_BANANAS_PER_TRIP 10

// Energy defaults
#define DEFAULT_FEMALE_ENERGY_MAX 100
#define DEFAULT_MALE_ENERGY_MAX 150
#define DEFAULT_FEMALE_ENERGY_THRESHOLD 35
#define DEFAULT_MALE_ENERGY_THRESHOLD 25
#define DEFAULT_ENERGY_LOSS_MOVE 1
#define DEFAULT_ENERGY_LOSS_FIGHT 15
#define DEFAULT_ENERGY_GAIN_REST 10
#define DEFAULT_ENERGY_LOSS_GUARD 1

// Fight defaults
#define DEFAULT_FIGHT_PROBABILITY_BASE 0.3
#define DEFAULT_FIGHT_BASE_DAMAGE 15
#define DEFAULT_FIGHT_MAX_STRENGTH 3
#define DEFAULT_FIGHT_SCALING_FACTOR 0.25
#define DEFAULT_FIGHT_DAMAGE_CAP 45
#define DEFAULT_MAX_FIGHT_ROUNDS 20

// Male guard defaults
#define DEFAULT_MALE_GUARD_INTERVAL 2
#define DEFAULT_MALE_REST_ENERGY_GAIN 20    // Energy gained after fight (rest round)
#define DEFAULT_MALE_REST_DURATION_MS 2000  // How long males rest after fight (ms)

// Fight target selection weights (NEW)
#define DEFAULT_FIGHT_TARGET_BASKET_WEIGHT 0.7   // Weight for basket size (higher = prefer richer)
#define DEFAULT_FIGHT_TARGET_DISTANCE_WEIGHT 0.3 // Weight for distance (higher = prefer closer)

// Ranking criteria weights
#define DEFAULT_RANK_CURRENT_BASKET_WEIGHT 2.0   // Weight for current basket bananas
#define DEFAULT_RANK_TOTAL_COLLECTED_WEIGHT 0.5  // Weight for total bananas collected
#define DEFAULT_RANK_FIGHTS_WEIGHT 5.0           // Weight for fights won (per win)

// Baby stealing defaults
#define DEFAULT_BABY_STEAL_MIN 1
#define DEFAULT_BABY_STEAL_MAX 10
#define DEFAULT_BABY_STEAL_BASE_RATE 0.5
#define DEFAULT_BABY_EAT_PROBABILITY 0.3  // 30% eat, 70% add to basket

// Movement defaults
#define DEFAULT_SENSING_RADIUS 5

// Termination defaults
#define DEFAULT_WITHDRAWN_FAMILY_THRESHOLD 3
#define DEFAULT_FAMILY_BANANA_THRESHOLD 200
#define DEFAULT_BABY_EATEN_THRESHOLD 50
#define DEFAULT_TIME_LIMIT_SECONDS 300

Config parse_config(const char* filename);
void create_default_config_file(const char* filename);
void warn_suspicious_config(const Config* c);

#endif