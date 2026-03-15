#ifndef TYPES_H
#define TYPES_H

#include <pthread.h>
#include <time.h>
#include <stdint.h>  // For uint8_t (wall flags)

// Simple 2D position in the maze
typedef struct {
    int x;
    int y;
} Position;

// All possible states an ape can be in
typedef enum {
    STATE_IDLE,
    STATE_SEARCHING,      // Looking for bananas
    STATE_COLLECTING,     // Collecting from current cell
    STATE_RETURNING,      // Going back home with bananas
    STATE_FIGHTING,       // Currently fighting
    STATE_RESTING,        // Recovering energy
    STATE_WITHDRAWN       // Family has withdrawn
} ApeState;

// Forward declarations to avoid circular dependencies
typedef struct female_ape FemaleApe;
typedef struct male_ape MaleApe;
typedef struct baby_ape BabyApe;
typedef struct family Family;

// Female ape collects bananas in the maze
struct female_ape {
    int ape_id;
    pthread_t thread;
    Position pos;          // Current position in maze
    Position prev_pos;     // Previous position (for movement history)
    ApeState state;

    int energy;            // Current energy (max & threshold in config)
    int collected_bananas; // Bananas currently carrying

    // Partial collection memory (Change 3)
    Position remembered_cell;  // Cell with remaining bananas
    int remembered_bananas;    // How many bananas left there
    int has_remembered_cell;   // Flag: 1 if we have a remembered cell

    // Relationships
    Family* family;
    struct simulation_state* sim;

    // Rest mechanism uses condition variable
    pthread_cond_t rest_cond;
    pthread_mutex_t rest_mutex;
    int is_resting;
};

// Male ape guards the family basket
struct male_ape {
    int ape_id;
    pthread_t thread;
    ApeState state;

    int energy;            // Current energy (max & threshold in config)
    int is_resting;        // Flag: 1 if resting after fight (can't initiate fights)

    // Relationships
    Family* family;
    Family** neighbor_families;  // Adjacent families on maze border
    int neighbor_count;
    struct simulation_state* sim;
};

// Baby ape steals bananas during fights
struct baby_ape {
    int ape_id;
    pthread_t thread;
    ApeState state;

    // Stealing statistics
    int stolen_count;      // Total bananas stolen (successfully)
    int eaten_count;       // Bananas eaten by baby
    int added_count;       // Bananas added to dad's basket
    int caught_count;      // Times caught trying to steal

    // Relationships
    Family* family;
    struct simulation_state* sim;
};

// Family unit with all members
struct family {
    int family_id;

    // Family members (direct containment, not pointers)
    MaleApe male;
    FemaleApe female;
    BabyApe* babies;       // Dynamic array
    int baby_count;

    // Family home on maze edge
    Position home_pos;

    // Shared basket protected by male
    int basket_bananas;
    int total_bananas_collected;  // Running total (never decreases, for stats)
    int fights_won;               // Number of fights won by male
    time_t withdrawal_time;       // NEW: When family withdrew (0 if still active)
    pthread_mutex_t basket_mutex;

    // Fight signaling for babies
    pthread_cond_t fight_cond;      // Signal babies when fight starts/ends
    pthread_mutex_t fight_mutex;    // Protects fighting_opponent
    Family* fighting_opponent;      // Which neighbor dad is fighting (NULL = no fight)

    // Status
    int is_withdrawn;
    pthread_cond_t withdrawal_cond;  // Signal withdrawal to all family members
};

// Simulation statistics collected during run
typedef struct {
    double total_time;
    int female_fights;
    int male_fights;
    int baby_steals;
    int total_bananas_collected;
    int winner_family_id;
    int winner_bananas;
    double avg_female_energy;
    double avg_male_energy;
} SimulationStats;

// All configurable parameters from arguments.txt
typedef struct {
    // File paths
    char maze_path[256];   // Path to maze file
    
    // Maze parameters
    int maze_size;
    double obstacle_density;      // Deprecated (kept for compatibility)
    double banana_density;
    int max_bananas_per_cell;
    double wall_removal_percent;  // NEW: Percent of walls to remove (0.0-1.0) for multiple paths

    // Family parameters
    int num_families;
    int baby_count_per_family;
    int bananas_per_trip;

    // Energy parameters
    int female_energy_max;
    int male_energy_max;
    int female_energy_threshold;
    int male_energy_threshold;
    int energy_loss_move;
    int energy_loss_fight;
    int energy_loss_guard;  // NEW: Energy loss per guard cycle
    int energy_gain_rest;

    // Fight parameters
    double fight_probability_base;
    int fight_base_damage;
    int fight_max_strength;
    int fight_damage_cap;      // NEW: Damage cap for male fights
    int max_fight_rounds;      // NEW: Max rounds before draw
    double fight_scaling_factor; // NEW: For adaptive fight probability

    // Male guarding parameters
    int male_guard_interval;   // NEW: Seconds between guard checks
    int male_rest_energy_gain; // Energy gained when resting after fight
    int male_rest_duration_ms; // How long males rest after fight (milliseconds)

    // Fight target selection weights
    double fight_target_basket_weight;   // Weight for basket size (higher = prefer richer)
    double fight_target_distance_weight; // Weight for distance (higher = prefer closer)

    // Ranking criteria weights
    double rank_current_basket_weight;   // Weight for current basket bananas
    double rank_total_collected_weight;  // Weight for total bananas collected
    double rank_fights_weight;           // Weight for fights won

    // Baby stealing parameters (NEW)
    int baby_steal_min;        // Minimum bananas to steal
    int baby_steal_max;        // Maximum bananas to steal
    double baby_steal_base_rate; // Base success rate (0.0-1.0)
    double baby_eat_probability; // Probability baby eats stolen bananas (0.0-1.0)

    // Movement parameters
    int sensing_radius;

    // Termination conditions
    int withdrawn_family_threshold;
    int family_banana_threshold;
    int baby_eaten_threshold;
    int time_limit_seconds;
} Config;

// Menu system for user interface
typedef struct {
    int id;
    char text[100];
    char function[50];
    int enabled;
} MenuItem;

typedef struct {
    MenuItem* items;
    int count;
    int capacity;
} Menu;

// Wall direction bit flags for real maze (Q1: Bit flags)
#define WALL_NORTH 0x01  // 0001 - Wall on north side
#define WALL_SOUTH 0x02  // 0010 - Wall on south side
#define WALL_EAST  0x04  // 0100 - Wall on east side
#define WALL_WEST  0x08  // 1000 - Wall on west side
#define WALL_ALL   0x0F  // 1111 - All walls present

// Single cell in the maze (REAL MAZE with walls between cells)
typedef struct {
    uint8_t walls;               // Bit flags for walls (N,S,E,W)
    int banana_count;
    FemaleApe* occupant;         // Which female is currently here (NULL if empty)
    pthread_mutex_t cell_mutex;  // Each cell has its own lock
} MazeCell;

// 2D maze grid
typedef struct {
    int width;
    int height;
    MazeCell** cells;            // Row-by-row allocation: cells[y][x]
    int total_bananas;           // NEW: Total bananas in maze (for fight probability)
} Maze;

// ============================================================
// GUI Event System - for communicating steal/eat events to GUI
// ============================================================

typedef enum {
    GUI_EVENT_NONE,
    GUI_EVENT_BABY_STEAL,      // Baby successfully stole bananas
    GUI_EVENT_BABY_EAT,        // Baby ate bananas
    GUI_EVENT_BABY_ADD,        // Baby added bananas to basket
    GUI_EVENT_BABY_CAUGHT,     // Baby was caught trying to steal
    GUI_EVENT_MALE_FIGHT_START,// Male fight initiated
    GUI_EVENT_MALE_FIGHT_END,  // Male fight ended
    GUI_EVENT_MALE_REST        // Male resting after fight
} GuiEventType;

typedef struct {
    GuiEventType type;
    
    // For baby events
    int baby_family_id;        // Which family's baby
    int baby_id;               // Which baby in the family
    int victim_family_id;      // Family being stolen from
    int amount;                // Bananas involved
    
    // For male fight events
    int initiator_family_id;   // Who started the fight
    int defender_family_id;    // Who was challenged
    int winner_family_id;      // Who won (-1 if draw or ongoing)
    
    time_t timestamp;          // When event occurred
} GuiEvent;

#define MAX_GUI_EVENTS 32

typedef struct {
    GuiEvent events[MAX_GUI_EVENTS];
    int head;                  // Next position to write
    int count;                 // Number of events in queue
    pthread_mutex_t mutex;     // Protects the queue
} GuiEventQueue;

// Main simulation state container
typedef struct simulation_state {
    Config* config;
    SimulationStats stats;

    // Maze data
    Maze* maze;
    Position* family_homes;
    Family* families;
    int num_families;

    // Global synchronization primitives
    pthread_mutex_t console_mutex;  // For thread-safe printing
    pthread_mutex_t stats_mutex;    // For updating statistics

    // GUI event queue for steal/eat notifications
    GuiEventQueue gui_events;

    // Runtime state
    volatile int simulation_running;  // Flag to stop all threads
    volatile int stats_printed;       // Flag to suppress logging after stats
    int withdrawn_count;
    time_t start_time;
} SimulationState;

#endif