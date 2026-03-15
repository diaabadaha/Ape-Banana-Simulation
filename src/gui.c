#include "../include/gui.h"
#include "../include/graphics.h"
#include "../include/types.h"
#include "../include/sync.h"
#include "../include/simulation_threading.h"
#include <GL/glut.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <pthread.h>

// Window configuration - will be set to screen size
static int g_window_width = 1280;
static int g_window_height = 800;

#define WINDOW_TITLE "Ape Banana Collection Simulation"

// Layout configuration (80% maze, 20% stats) - calculated at runtime
static float MAZE_PANEL_WIDTH;
static float STATS_PANEL_WIDTH;
static float STATS_PANEL_X;

// Maze rendering - calculated at runtime based on actual maze size
static float MAZE_PADDING = 60.0f;
static float CELL_SIZE;
static float MAZE_DRAW_SIZE;  // Actual pixel size of maze
static float MAZE_OFFSET_X;   // Center maze in left panel
static float MAZE_OFFSET_Y;   // Center maze vertically

// Male positioning outside maze
#define MALE_AREA_SIZE 80.0f  // Space around maze for males

// Animation configuration
#define FPS 30
#define FRAME_TIME (1000 / FPS)

// Simulation speed multipliers
static float speed_multipliers[] = {0.5f, 1.0f, 2.0f, 5.0f, 10.0f};
static int current_speed_index = 1;  // Start at 1x

// Global state
static SimulationState* g_sim = NULL;
static int g_window_id = 0;
static int g_is_paused = 0;
static int g_show_details = 0;
static int g_selected_ape_family = -1;
static int g_selected_ape_type = -1;

// End-game display state
static int g_simulation_ended = 0;
static int g_end_stats_calculated = 0;

// End-game statistics (calculated once when simulation ends)
typedef struct {
    int family_id;
    int current_basket;    // Current basket count
    int total_collected;   // Total ever collected
    int is_active;
    int fights_won;
    int time_survived;     // Seconds survived
    double score;          // Combined weighted score
} EndGameRanking;

static EndGameRanking g_rankings[16];  // Max 16 families
static int g_bananas_in_maze = 0;
static int g_bananas_in_baskets = 0;
static int g_bananas_eaten = 0;
static int g_total_bananas = 0;

// Animation frame counter for pulsing effects
static int g_frame_counter = 0;

// Animation state for females
typedef struct {
    Position from;
    Position to;
    float progress;
    int active;
} ApeAnimation;

static ApeAnimation* g_female_anims = NULL;

// Baby animation state
typedef struct {
    float offset_x;     // Current offset from base position
    float offset_y;
    float target_x;     // Target offset (toward opponent basket)
    float target_y;
    int is_stealing;    // Currently attempting steal
    float pulse;        // Pulse animation for successful steal
} BabyAnimation;

static BabyAnimation** g_baby_anims = NULL;  // [family][baby]

// Fight/steal notifications
typedef struct {
    char message[128];
    float x, y;
    int timer;
    Color color;
} Notification;

#define MAX_NOTIFICATIONS 10
static Notification g_notifications[MAX_NOTIFICATIONS];
static int g_notification_count = 0;

// Basket steal indicator (which baskets are currently being stolen from)
#define MAX_BASKET_INDICATORS 16
typedef struct {
    int family_id;           // Family whose basket is being targeted
    int thief_family_id;     // Family doing the stealing  
    int timer;               // Frames remaining to show indicator
    int flash_state;         // For flashing effect
} BasketStealIndicator;

static BasketStealIndicator g_basket_indicators[MAX_BASKET_INDICATORS];
static int g_basket_indicator_count = 0;

// Male fight animation state
typedef struct {
    int active;                  // Is there an active fight?
    int initiator_family_id;     // Who started the fight
    int defender_family_id;      // Who is being challenged
    float initiator_offset_x;    // How far initiator has moved toward defender
    float initiator_offset_y;
    float progress;              // 0.0 to 1.0 for movement animation
    int fight_in_progress;       // 1 if they're actually fighting (not just moving)
} MaleFightAnimation;

static MaleFightAnimation g_male_fight_anim = {0};

// Mutex for GUI state
static pthread_mutex_t g_gui_mutex = PTHREAD_MUTEX_INITIALIZER;

// Forward declarations
static void display(void);
static void reshape(int w, int h);
static void keyboard(unsigned char key, int x, int y);
static void mouse(int button, int state, int x, int y);
static void timer(int value);
static void render_maze(void);
static void render_stats_panel(void);
static void calculate_layout(void);
static void get_male_position_at_home(int family_id, float* out_x, float* out_y);

// Calculate layout based on maze size
static void calculate_layout(void) {
    if (!g_sim || !g_sim->maze) return;
    
    MAZE_PANEL_WIDTH = g_window_width * 0.80f;
    STATS_PANEL_WIDTH = g_window_width * 0.20f;
    STATS_PANEL_X = MAZE_PANEL_WIDTH;
    
    int maze_size = g_sim->maze->width;
    
    // Calculate cell size to fit maze in panel with padding
    float available_width = MAZE_PANEL_WIDTH - (2 * MAZE_PADDING) - (2 * MALE_AREA_SIZE);
    float available_height = g_window_height - (2 * MAZE_PADDING) - (2 * MALE_AREA_SIZE);
    
    float cell_width = available_width / maze_size;
    float cell_height = available_height / maze_size;
    
    CELL_SIZE = (cell_width < cell_height) ? cell_width : cell_height;
    MAZE_DRAW_SIZE = CELL_SIZE * maze_size;
    
    // Center the maze in the left panel
    MAZE_OFFSET_X = (MAZE_PANEL_WIDTH - MAZE_DRAW_SIZE) / 2.0f;
    MAZE_OFFSET_Y = (g_window_height - MAZE_DRAW_SIZE) / 2.0f;
}

// Get male ape position OUTSIDE maze at their family's home position
static void get_male_position_at_home(int family_id, float* out_x, float* out_y) {
    if (!g_sim || family_id < 0 || family_id >= g_sim->num_families) {
        *out_x = 0;
        *out_y = 0;
        return;
    }
    
    Family* family = &g_sim->families[family_id];
    Position home = family->home_pos;
    
    // Calculate screen position of home cell
    float home_screen_x = MAZE_OFFSET_X + home.x * CELL_SIZE + CELL_SIZE * 0.5f;
    float home_screen_y = MAZE_OFFSET_Y + home.y * CELL_SIZE + CELL_SIZE * 0.5f;
    
    // Position male OUTSIDE the maze edge, adjacent to home cell
    float offset = CELL_SIZE * 1.2f;  // Distance outside maze
    
    // Determine which edge the home is on
    if (home.y == 0) {
        // Top edge - male above
        *out_x = home_screen_x;
        *out_y = home_screen_y - offset;
    } else if (home.y == g_sim->maze->height - 1) {
        // Bottom edge - male below
        *out_x = home_screen_x;
        *out_y = home_screen_y + offset;
    } else if (home.x == 0) {
        // Left edge - male to the left
        *out_x = home_screen_x - offset;
        *out_y = home_screen_y;
    } else if (home.x == g_sim->maze->width - 1) {
        // Right edge - male to the right
        *out_x = home_screen_x + offset;
        *out_y = home_screen_y;
    } else {
        // Not on edge (shouldn't happen) - default position
        *out_x = home_screen_x - offset;
        *out_y = home_screen_y;
    }
}

// Initialize animations
static void init_animations(void) {
    if (!g_sim) return;
    
    // Female animations
    g_female_anims = (ApeAnimation*)calloc(g_sim->num_families, sizeof(ApeAnimation));
    
    for (int i = 0; i < g_sim->num_families; i++) {
        g_female_anims[i].from = g_sim->families[i].female.pos;
        g_female_anims[i].to = g_sim->families[i].female.pos;
        g_female_anims[i].progress = 1.0f;
        g_female_anims[i].active = 0;
    }
    
    // Baby animations
    g_baby_anims = (BabyAnimation**)calloc(g_sim->num_families, sizeof(BabyAnimation*));
    for (int i = 0; i < g_sim->num_families; i++) {
        int baby_count = g_sim->families[i].baby_count;
        if (baby_count > 0) {
            g_baby_anims[i] = (BabyAnimation*)calloc(baby_count, sizeof(BabyAnimation));
        }
    }
}

// Update animations
static void update_animations(void) {
    if (!g_sim || !g_female_anims) return;
    
    g_frame_counter++;
    
    float anim_speed = 0.15f * speed_multipliers[current_speed_index];
    
    // Update female animations
    for (int i = 0; i < g_sim->num_families; i++) {
        Family* family = &g_sim->families[i];
        Position current_pos = family->female.pos;
        
        if (current_pos.x != g_female_anims[i].to.x || 
            current_pos.y != g_female_anims[i].to.y) {
            g_female_anims[i].from = g_female_anims[i].to;
            g_female_anims[i].to = current_pos;
            g_female_anims[i].progress = 0.0f;
            g_female_anims[i].active = 1;
        }
        
        if (g_female_anims[i].active) {
            g_female_anims[i].progress += anim_speed;
            if (g_female_anims[i].progress >= 1.0f) {
                g_female_anims[i].progress = 1.0f;
                g_female_anims[i].active = 0;
            }
        }
    }
    
    // Update baby animations
    for (int i = 0; i < g_sim->num_families; i++) {
        Family* family = &g_sim->families[i];
        if (family->is_withdrawn || !g_baby_anims[i]) continue;
        
        // Check if dad is fighting
        pthread_mutex_lock(&family->fight_mutex);
        Family* opponent = family->fighting_opponent;
        pthread_mutex_unlock(&family->fight_mutex);
        
        for (int j = 0; j < family->baby_count; j++) {
            BabyAnimation* anim = &g_baby_anims[i][j];
            
            if (opponent != NULL) {
                // Dad is fighting! Baby should animate toward opponent
                anim->is_stealing = 1;
                
                // Get opponent's position
                float opp_x, opp_y;
                get_male_position_at_home(opponent->family_id, &opp_x, &opp_y);
                
                // Get our position
                float our_x, our_y;
                get_male_position_at_home(i, &our_x, &our_y);
                
                // Calculate direction to opponent
                float dx = opp_x - our_x;
                float dy = opp_y - our_y;
                float dist = sqrtf(dx*dx + dy*dy);
                
                if (dist > 0) {
                    // Oscillate toward opponent and back
                    float phase = (g_frame_counter + j * 10) * 0.2f;
                    float move_amount = CELL_SIZE * 0.5f * (0.5f + 0.5f * sinf(phase));
                    
                    anim->offset_x = (dx / dist) * move_amount;
                    anim->offset_y = (dy / dist) * move_amount;
                }
                
                // Pulse effect when stealing
                anim->pulse = 0.5f + 0.5f * sinf(g_frame_counter * 0.3f + j);
            } else {
                // Not stealing - return to base position
                anim->is_stealing = 0;
                anim->offset_x *= 0.9f;  // Smooth return
                anim->offset_y *= 0.9f;
                anim->pulse = 0.0f;
            }
        }
    }
    
    // Update male fight animation - move along maze border, not diagonally
    if (g_male_fight_anim.active) {
        int init_id = g_male_fight_anim.initiator_family_id;
        int def_id = g_male_fight_anim.defender_family_id;
        
        if (init_id >= 0 && init_id < g_sim->num_families &&
            def_id >= 0 && def_id < g_sim->num_families) {
            
            float init_x, init_y, def_x, def_y;
            get_male_position_at_home(init_id, &init_x, &init_y);
            get_male_position_at_home(def_id, &def_x, &def_y);
            
            // Smooth animation progress
            g_male_fight_anim.progress += 0.03f * speed_multipliers[current_speed_index];
            if (g_male_fight_anim.progress > 1.0f) {
                g_male_fight_anim.progress = 1.0f;
                g_male_fight_anim.fight_in_progress = 1;
            }
            
            float t = g_male_fight_anim.progress;
            
            // Determine which edges initiator and defender are on
            Position init_home = g_sim->families[init_id].home_pos;
            Position def_home = g_sim->families[def_id].home_pos;
            
            int maze_w = g_sim->maze->width;
            int maze_h = g_sim->maze->height;
            
            // Edge codes: 0=top, 1=right, 2=bottom, 3=left
            int init_edge = -1, def_edge = -1;
            if (init_home.y == 0) init_edge = 0;
            else if (init_home.x == maze_w - 1) init_edge = 1;
            else if (init_home.y == maze_h - 1) init_edge = 2;
            else if (init_home.x == 0) init_edge = 3;
            
            if (def_home.y == 0) def_edge = 0;
            else if (def_home.x == maze_w - 1) def_edge = 1;
            else if (def_home.y == maze_h - 1) def_edge = 2;
            else if (def_home.x == 0) def_edge = 3;
            
            // Calculate target position (70% toward defender)
            float target_x, target_y;
            
            if (init_edge == def_edge) {
                // Same edge - direct movement along that edge
                target_x = init_x + (def_x - init_x) * 0.7f;
                target_y = init_y + (def_y - init_y) * 0.7f;
                
                g_male_fight_anim.initiator_offset_x = (target_x - init_x) * t;
                g_male_fight_anim.initiator_offset_y = (target_y - init_y) * t;
            } else {
                // Different edges - need to go around corner in L-shape
                float outside_offset = CELL_SIZE * 1.2f;
                
                // Calculate maze corners (outside positions)
                float maze_left = MAZE_OFFSET_X - outside_offset;
                float maze_right = MAZE_OFFSET_X + MAZE_DRAW_SIZE + outside_offset;
                float maze_top = MAZE_OFFSET_Y - outside_offset;
                float maze_bottom = MAZE_OFFSET_Y + MAZE_DRAW_SIZE + outside_offset;
                
                // Determine which corner to go through based on edges
                int edge_diff = (def_edge - init_edge + 4) % 4;
                
                if (edge_diff == 1 || edge_diff == 3) {
                    // Adjacent edges - go through the shared corner in L-shape
                    float corner_x, corner_y;
                    
                    if ((init_edge == 0 && def_edge == 1) || (init_edge == 1 && def_edge == 0)) {
                        corner_x = maze_right; corner_y = maze_top;  // Top-right
                    } else if ((init_edge == 1 && def_edge == 2) || (init_edge == 2 && def_edge == 1)) {
                        corner_x = maze_right; corner_y = maze_bottom;  // Bottom-right
                    } else if ((init_edge == 2 && def_edge == 3) || (init_edge == 3 && def_edge == 2)) {
                        corner_x = maze_left; corner_y = maze_bottom;  // Bottom-left
                    } else {
                        corner_x = maze_left; corner_y = maze_top;  // Top-left
                    }
                    
                    // L-SHAPE MOVEMENT: First along initiator's edge, then along defender's edge
                    // Phase 1: Move along initiator's edge toward corner (only X or only Y changes)
                    // Phase 2: Move along defender's edge toward defender (only the other axis changes)
                    
                    if (t < 0.5f) {
                        // Phase 1: Move along initiator's edge toward corner
                        float phase_t = t * 2.0f;
                        
                        // On horizontal edges (top=0, bottom=2), move X first
                        // On vertical edges (left=3, right=1), move Y first
                        if (init_edge == 0 || init_edge == 2) {
                            // Horizontal edge - move X toward corner, Y stays same
                            g_male_fight_anim.initiator_offset_x = (corner_x - init_x) * phase_t;
                            g_male_fight_anim.initiator_offset_y = 0;
                        } else {
                            // Vertical edge - move Y toward corner, X stays same
                            g_male_fight_anim.initiator_offset_x = 0;
                            g_male_fight_anim.initiator_offset_y = (corner_y - init_y) * phase_t;
                        }
                    } else {
                        // Phase 2: Move along defender's edge toward defender (70% of the way)
                        float phase_t = (t - 0.5f) * 2.0f;
                        target_x = corner_x + (def_x - corner_x) * 0.7f;
                        target_y = corner_y + (def_y - corner_y) * 0.7f;
                        
                        // Complete the first leg, then do the second leg
                        if (init_edge == 0 || init_edge == 2) {
                            // Was on horizontal edge - X is at corner, now move Y
                            g_male_fight_anim.initiator_offset_x = (corner_x - init_x);
                            g_male_fight_anim.initiator_offset_y = (target_y - init_y) * phase_t;
                        } else {
                            // Was on vertical edge - Y is at corner, now move X
                            g_male_fight_anim.initiator_offset_x = (target_x - init_x) * phase_t;
                            g_male_fight_anim.initiator_offset_y = (corner_y - init_y);
                        }
                    }
                } else {
                    // Opposite edges (edge_diff == 2) - go through two corners
                    // Always go clockwise
                    int mid_edge = (init_edge + 1) % 4;
                    float corner1_x, corner1_y, corner2_x, corner2_y;
                    
                    // First corner (between init_edge and mid_edge)
                    if (init_edge == 0) { corner1_x = maze_right; corner1_y = maze_top; }
                    else if (init_edge == 1) { corner1_x = maze_right; corner1_y = maze_bottom; }
                    else if (init_edge == 2) { corner1_x = maze_left; corner1_y = maze_bottom; }
                    else { corner1_x = maze_left; corner1_y = maze_top; }
                    
                    // Second corner (between mid_edge and def_edge)
                    if (mid_edge == 0) { corner2_x = maze_right; corner2_y = maze_top; }
                    else if (mid_edge == 1) { corner2_x = maze_right; corner2_y = maze_bottom; }
                    else if (mid_edge == 2) { corner2_x = maze_left; corner2_y = maze_bottom; }
                    else { corner2_x = maze_left; corner2_y = maze_top; }
                    
                    // Three-phase L-shape movement
                    if (t < 0.33f) {
                        // Phase 1: Along initiator's edge to corner1
                        float phase_t = t * 3.0f;
                        if (init_edge == 0 || init_edge == 2) {
                            g_male_fight_anim.initiator_offset_x = (corner1_x - init_x) * phase_t;
                            g_male_fight_anim.initiator_offset_y = 0;
                        } else {
                            g_male_fight_anim.initiator_offset_x = 0;
                            g_male_fight_anim.initiator_offset_y = (corner1_y - init_y) * phase_t;
                        }
                    } else if (t < 0.66f) {
                        // Phase 2: Along mid_edge to corner2
                        float phase_t = (t - 0.33f) * 3.0f;
                        if (init_edge == 0 || init_edge == 2) {
                            // Started horizontal, now vertical
                            g_male_fight_anim.initiator_offset_x = (corner1_x - init_x);
                            g_male_fight_anim.initiator_offset_y = (corner2_y - init_y) * phase_t;
                        } else {
                            // Started vertical, now horizontal
                            g_male_fight_anim.initiator_offset_x = (corner2_x - init_x) * phase_t;
                            g_male_fight_anim.initiator_offset_y = (corner1_y - init_y);
                        }
                    } else {
                        // Phase 3: Along defender's edge toward defender
                        float phase_t = (t - 0.66f) * 3.0f;
                        target_x = corner2_x + (def_x - corner2_x) * 0.7f;
                        target_y = corner2_y + (def_y - corner2_y) * 0.7f;
                        
                        if (init_edge == 0 || init_edge == 2) {
                            // End on horizontal edge
                            g_male_fight_anim.initiator_offset_x = (corner2_x - init_x) + (target_x - corner2_x) * phase_t;
                            g_male_fight_anim.initiator_offset_y = (corner2_y - init_y);
                        } else {
                            // End on vertical edge
                            g_male_fight_anim.initiator_offset_x = (corner2_x - init_x);
                            g_male_fight_anim.initiator_offset_y = (corner2_y - init_y) + (target_y - corner2_y) * phase_t;
                        }
                    }
                }
            }
        }
    }
}

// Update notifications
static void update_notifications(void) {
    for (int i = 0; i < g_notification_count; i++) {
        g_notifications[i].timer--;
        if (g_notifications[i].timer <= 0) {
            for (int j = i; j < g_notification_count - 1; j++) {
                g_notifications[j] = g_notifications[j + 1];
            }
            g_notification_count--;
            i--;
        }
    }
}

// Add a notification to the display
static void add_notification(const char* message, float x, float y, Color color, int duration) {
    if (g_notification_count >= MAX_NOTIFICATIONS) {
        // Remove oldest
        for (int i = 0; i < g_notification_count - 1; i++) {
            g_notifications[i] = g_notifications[i + 1];
        }
        g_notification_count--;
    }
    
    Notification* n = &g_notifications[g_notification_count];
    strncpy(n->message, message, sizeof(n->message) - 1);
    n->message[sizeof(n->message) - 1] = '\0';
    n->x = x;
    n->y = y;
    n->color = color;
    n->timer = duration;
    g_notification_count++;
}

// Add or refresh basket steal indicator
static void add_basket_indicator(int victim_family_id, int thief_family_id, int duration) {
    // Check if already exists
    for (int i = 0; i < g_basket_indicator_count; i++) {
        if (g_basket_indicators[i].family_id == victim_family_id) {
            // Refresh timer
            g_basket_indicators[i].timer = duration;
            g_basket_indicators[i].thief_family_id = thief_family_id;
            return;
        }
    }
    
    // Add new
    if (g_basket_indicator_count < MAX_BASKET_INDICATORS) {
        g_basket_indicators[g_basket_indicator_count].family_id = victim_family_id;
        g_basket_indicators[g_basket_indicator_count].thief_family_id = thief_family_id;
        g_basket_indicators[g_basket_indicator_count].timer = duration;
        g_basket_indicators[g_basket_indicator_count].flash_state = 0;
        g_basket_indicator_count++;
    }
}

// Update basket indicators
static void update_basket_indicators(void) {
    for (int i = 0; i < g_basket_indicator_count; i++) {
        g_basket_indicators[i].timer--;
        g_basket_indicators[i].flash_state = (g_basket_indicators[i].flash_state + 1) % 10;
        
        if (g_basket_indicators[i].timer <= 0) {
            // Remove
            for (int j = i; j < g_basket_indicator_count - 1; j++) {
                g_basket_indicators[j] = g_basket_indicators[j + 1];
            }
            g_basket_indicator_count--;
            i--;
        }
    }
}

// Check if a basket has an active steal indicator
static int is_basket_being_stolen(int family_id, int* flash_state) {
    for (int i = 0; i < g_basket_indicator_count; i++) {
        if (g_basket_indicators[i].family_id == family_id) {
            if (flash_state) *flash_state = g_basket_indicators[i].flash_state;
            return 1;
        }
    }
    return 0;
}

// Process GUI events from the event queue
static void process_gui_events(void) {
    if (!g_sim) return;
    
    GuiEvent event;
    while (pop_gui_event(g_sim, &event)) {
        float victim_x = 0, victim_y = 0;
        float thief_x = 0, thief_y = 0;
        float initiator_x = 0, initiator_y = 0;
        float defender_x = 0, defender_y = 0;
        char msg[128];
        Color color;
        
        // Get positions for notifications
        if (event.victim_family_id >= 0 && event.victim_family_id < g_sim->num_families) {
            get_male_position_at_home(event.victim_family_id, &victim_x, &victim_y);
        }
        if (event.baby_family_id >= 0 && event.baby_family_id < g_sim->num_families) {
            get_male_position_at_home(event.baby_family_id, &thief_x, &thief_y);
        }
        if (event.initiator_family_id >= 0 && event.initiator_family_id < g_sim->num_families) {
            get_male_position_at_home(event.initiator_family_id, &initiator_x, &initiator_y);
        }
        if (event.defender_family_id >= 0 && event.defender_family_id < g_sim->num_families) {
            get_male_position_at_home(event.defender_family_id, &defender_x, &defender_y);
        }
        
        switch (event.type) {
            case GUI_EVENT_BABY_STEAL:
                // Baby successfully stole - show on victim's basket
                snprintf(msg, sizeof(msg), "-%d STOLEN!", event.amount);
                color = (Color){1.0f, 0.3f, 0.3f};  // Red
                add_notification(msg, victim_x - 30, victim_y - 50, color, 60);
                
                // Add basket indicator
                add_basket_indicator(event.victim_family_id, event.baby_family_id, 45);
                break;
                
            case GUI_EVENT_BABY_EAT:
                // Baby ate the bananas
                snprintf(msg, sizeof(msg), "NOM! +%d", event.amount);
                color = (Color){1.0f, 0.8f, 0.0f};  // Yellow/gold
                add_notification(msg, thief_x + 30, thief_y - 30, color, 45);
                break;
                
            case GUI_EVENT_BABY_ADD:
                // Baby added to dad's basket
                snprintf(msg, sizeof(msg), "+%d ADDED", event.amount);
                color = (Color){0.3f, 1.0f, 0.3f};  // Green
                add_notification(msg, thief_x + 30, thief_y + 50, color, 45);
                break;
                
            case GUI_EVENT_BABY_CAUGHT:
                // Baby was caught
                snprintf(msg, sizeof(msg), "CAUGHT!");
                color = (Color){1.0f, 0.5f, 0.0f};  // Orange
                add_notification(msg, victim_x, victim_y - 70, color, 30);
                break;
                
            case GUI_EVENT_MALE_FIGHT_START:
                // Male fight started - animate initiator moving to defender
                g_male_fight_anim.active = 1;
                g_male_fight_anim.initiator_family_id = event.initiator_family_id;
                g_male_fight_anim.defender_family_id = event.defender_family_id;
                g_male_fight_anim.progress = 0.0f;
                g_male_fight_anim.fight_in_progress = 0;
                g_male_fight_anim.initiator_offset_x = 0;
                g_male_fight_anim.initiator_offset_y = 0;
                
                // Show notification
                snprintf(msg, sizeof(msg), "M%d attacks M%d!", 
                         event.initiator_family_id, event.defender_family_id);
                color = (Color){1.0f, 0.4f, 0.0f};  // Orange-red
                add_notification(msg, (initiator_x + defender_x) / 2, 
                               (initiator_y + defender_y) / 2 - 40, color, 90);
                break;
                
            case GUI_EVENT_MALE_FIGHT_END:
                // Male fight ended
                g_male_fight_anim.active = 0;
                g_male_fight_anim.fight_in_progress = 0;
                
                // Show winner notification
                if (event.winner_family_id >= 0) {
                    snprintf(msg, sizeof(msg), "M%d WINS!", event.winner_family_id);
                    color = (Color){0.2f, 1.0f, 0.2f};  // Green
                } else {
                    snprintf(msg, sizeof(msg), "DRAW!");
                    color = (Color){0.8f, 0.8f, 0.2f};  // Yellow
                }
                add_notification(msg, defender_x, defender_y - 60, color, 60);
                break;
                
            case GUI_EVENT_MALE_REST:
                // Male is resting after fight - show notification above their position
                snprintf(msg, sizeof(msg), "M%d RESTING +%dE", 
                         event.initiator_family_id, event.amount);
                color = (Color){0.6f, 0.8f, 1.0f};  // Light blue for rest
                add_notification(msg, initiator_x, initiator_y - 40, color, 80);
                break;
                
            default:
                break;
        }
    }
}

// Convert maze coordinates to screen coordinates
static void maze_to_screen(int maze_x, int maze_y, float* screen_x, float* screen_y) {
    *screen_x = MAZE_OFFSET_X + maze_x * CELL_SIZE + CELL_SIZE * 0.5f;
    *screen_y = MAZE_OFFSET_Y + maze_y * CELL_SIZE + CELL_SIZE * 0.5f;
}

// Convert screen coordinates to maze coordinates
static int screen_to_maze(int screen_x, int screen_y, int* maze_x, int* maze_y) {
    if (!g_sim || !g_sim->maze) return 0;
    
    float rel_x = screen_x - MAZE_OFFSET_X;
    float rel_y = screen_y - MAZE_OFFSET_Y;
    
    if (rel_x < 0 || rel_x > MAZE_DRAW_SIZE) return 0;
    if (rel_y < 0 || rel_y > MAZE_DRAW_SIZE) return 0;
    
    *maze_x = (int)(rel_x / CELL_SIZE);
    *maze_y = (int)(rel_y / CELL_SIZE);
    
    if (*maze_x < 0 || *maze_x >= g_sim->maze->width) return 0;
    if (*maze_y < 0 || *maze_y >= g_sim->maze->height) return 0;
    
    return 1;
}

// Initialize OpenGL
static void init_gl(void) {
    glClearColor(0.15f, 0.15f, 0.15f, 1.0f);  // Dark background
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluOrtho2D(0, g_window_width, g_window_height, 0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_LINE_SMOOTH);
    glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);
}

// Render a single ape
static void render_ape(float screen_x, float screen_y, int family_id, int is_male, int is_baby, 
                      int energy, int max_energy, int collected_bananas, ApeState state,
                      float scale, float extra_pulse) {
    Color family_color = get_family_color(family_id);
    
    float radius;
    if (is_baby) {
        radius = CELL_SIZE * 0.18f * scale;
    } else if (is_male) {
        radius = CELL_SIZE * 0.35f * scale;
    } else {
        radius = CELL_SIZE * 0.28f * scale;
    }
    
    // Fighting state - pulsing red glow
    if (state == STATE_FIGHTING || extra_pulse > 0.0f) {
        float pulse = 0.5f + 0.5f * sinf(g_frame_counter * 0.15f);
        if (extra_pulse > 0.0f) pulse = extra_pulse;
        Color glow_color = {1.0f, 0.2f, 0.2f};
        if (is_baby && extra_pulse > 0.0f) {
            glow_color = (Color){1.0f, 0.8f, 0.0f};  // Yellow glow for stealing babies
        }
        draw_circle_outline(screen_x, screen_y, radius + 4.0f + pulse * 5.0f, glow_color, 3.0f);
    }
    
    // Draw shape
    if (is_baby) {
        // Baby = small triangle
        draw_triangle(screen_x, screen_y, radius * 2.2f, family_color);
        // Add outline for visibility
        Color outline = {0.1f, 0.1f, 0.1f};
        glColor3f(outline.r, outline.g, outline.b);
        glLineWidth(2.0f);
        glBegin(GL_LINE_LOOP);
        float size = radius * 2.2f;
        glVertex2f(screen_x, screen_y - size * 0.5f);
        glVertex2f(screen_x - size * 0.5f, screen_y + size * 0.5f);
        glVertex2f(screen_x + size * 0.5f, screen_y + size * 0.5f);
        glEnd();
    } else if (is_male) {
        // Male = large circle
        draw_circle(screen_x, screen_y, radius, family_color);
        Color outline = {0.1f, 0.1f, 0.1f};
        draw_circle_outline(screen_x, screen_y, radius, outline, 2.5f);
    } else {
        // Female = square
        float half_size = radius;
        draw_filled_rectangle(screen_x - half_size, screen_y - half_size, 
                            half_size * 2, half_size * 2, family_color);
        Color outline = {0.1f, 0.1f, 0.1f};
        draw_rectangle_outline(screen_x - half_size, screen_y - half_size,
                             half_size * 2, half_size * 2, outline, 2.5f);
    }
    
    // Energy bar (not for babies)
    if (!is_baby) {
        float bar_width = CELL_SIZE * 0.5f;
        if (is_male) bar_width = CELL_SIZE * 0.6f;
        float bar_height = 6.0f;
        float bar_x = screen_x - bar_width * 0.5f;
        float bar_y = screen_y - radius - 15.0f;
        
        // Background
        draw_filled_rectangle(bar_x, bar_y, bar_width, bar_height, COLOR_DARK_GRAY);
        
        // Energy fill
        float fill_ratio = (float)energy / (float)max_energy;
        if (fill_ratio > 0.0f) {
            Color energy_color = get_energy_color(energy, max_energy);
            draw_filled_rectangle(bar_x, bar_y, bar_width * fill_ratio, bar_height, energy_color);
        }
        
        // Outline
        draw_rectangle_outline(bar_x, bar_y, bar_width, bar_height, COLOR_BLACK, 1.5f);
    }
    
    // Collected bananas for females
    if (!is_male && !is_baby && collected_bananas > 0) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d", collected_bananas);
        draw_text(screen_x + radius + 5, screen_y + 5, buf, COLOR_YELLOW);
    }
    
    // ID label
    char id_label[16];
    if (is_baby) {
        snprintf(id_label, sizeof(id_label), "B%d", family_id);
    } else if (is_male) {
        snprintf(id_label, sizeof(id_label), "M%d", family_id);
    } else {
        snprintf(id_label, sizeof(id_label), "F%d", family_id);
    }
    
    draw_text(screen_x - 10, screen_y + 4, id_label, COLOR_WHITE);
}

// Render males with baskets OUTSIDE maze at their home positions
static void render_males_outside_maze(void) {
    if (!g_sim) return;
    
    for (int i = 0; i < g_sim->num_families; i++) {
        Family* family = &g_sim->families[i];
        if (family->is_withdrawn) continue;
        
        float male_x, male_y;
        get_male_position_at_home(i, &male_x, &male_y);
        
        // Apply fight animation offset if this male is the initiator
        float fight_offset_x = 0, fight_offset_y = 0;
        int is_fight_initiator = 0;
        if (g_male_fight_anim.active && g_male_fight_anim.initiator_family_id == i) {
            fight_offset_x = g_male_fight_anim.initiator_offset_x;
            fight_offset_y = g_male_fight_anim.initiator_offset_y;
            is_fight_initiator = 1;
        }
        
        Color family_color = get_family_color(i);
        
        // Draw basket area (larger box) - stays at original position
        float basket_size = CELL_SIZE * 1.0f;
        draw_filled_rectangle(male_x - basket_size/2, male_y - basket_size/2 + 15, 
                            basket_size, basket_size, COLOR_DARK_GRAY);
        
        // Check if basket is being stolen from
        int flash_state = 0;
        int being_stolen = is_basket_being_stolen(i, &flash_state);
        
        if (being_stolen && (flash_state < 5)) {
            // Flashing red outline when being stolen from!
            Color steal_color = {1.0f, 0.2f, 0.2f};  // Bright red
            draw_rectangle_outline(male_x - basket_size/2 - 3, male_y - basket_size/2 + 12,
                                 basket_size + 6, basket_size + 6, steal_color, 5.0f);
        }
        
        draw_rectangle_outline(male_x - basket_size/2, male_y - basket_size/2 + 15,
                             basket_size, basket_size, family_color, 3.0f);
        
        // Draw basket count
        pthread_mutex_lock(&family->basket_mutex);
        int basket = family->basket_bananas;
        pthread_mutex_unlock(&family->basket_mutex);
        
        char buf[32];
        snprintf(buf, sizeof(buf), "%d", basket);
        Color basket_text_color = being_stolen ? COLOR_RED : family_color;
        draw_text_large(male_x - 15, male_y + basket_size/2 + 30, buf, basket_text_color);
        
        // Draw male ape - with fight animation offset if initiator
        float render_male_x = male_x + fight_offset_x;
        float render_male_y = male_y - 15 + fight_offset_y;
        
        // Extra visual effect for fighting initiator
        float extra_pulse = 0.0f;
        if (is_fight_initiator && g_male_fight_anim.fight_in_progress) {
            extra_pulse = 0.5f + 0.5f * sinf(g_frame_counter * 0.2f);
        }
        
        render_ape(render_male_x, render_male_y, i, 1, 0, 
                  family->male.energy, g_sim->config->male_energy_max, 
                  0, family->male.state, 1.0f, extra_pulse);
        
        // Draw babies around male with animation
        for (int j = 0; j < family->baby_count; j++) {
            float angle = (j * 2.0f * 3.14159f / family->baby_count) - 3.14159f/2;
            float base_x = male_x + cosf(angle) * (basket_size * 0.8f);
            float base_y = male_y + sinf(angle) * (basket_size * 0.8f);
            
            // Apply animation offset
            float anim_offset_x = 0, anim_offset_y = 0;
            float pulse = 0.0f;
            if (g_baby_anims && g_baby_anims[i]) {
                anim_offset_x = g_baby_anims[i][j].offset_x;
                anim_offset_y = g_baby_anims[i][j].offset_y;
                pulse = g_baby_anims[i][j].pulse;
            }
            
            float baby_x = base_x + anim_offset_x;
            float baby_y = base_y + anim_offset_y;
            
            BabyApe* baby = &family->babies[j];
            
            // Scale up slightly when stealing
            float scale = 1.0f;
            if (g_baby_anims && g_baby_anims[i] && g_baby_anims[i][j].is_stealing) {
                scale = 1.2f + 0.2f * sinf(g_frame_counter * 0.2f + j);
            }
            
            render_ape(baby_x, baby_y, i, 0, 1, 50, 50, 0, baby->state, scale, pulse);
            
            // Show stolen/eaten counts for babies
            if (baby->stolen_count > 0 || baby->eaten_count > 0) {
                char baby_stats[32];
                snprintf(baby_stats, sizeof(baby_stats), "S:%d E:%d", 
                        baby->stolen_count, baby->eaten_count);
                draw_text(baby_x - 20, baby_y + 20, baby_stats, COLOR_LIGHT_GRAY);
            }
        }
    }
}

// Render the maze
static void render_maze(void) {
    if (!g_sim || !g_sim->maze) return;
    
    Maze* maze = g_sim->maze;
    
    // Draw maze border
    draw_rectangle_outline(MAZE_OFFSET_X - 2, MAZE_OFFSET_Y - 2, 
                         MAZE_DRAW_SIZE + 4, MAZE_DRAW_SIZE + 4, 
                         COLOR_WHITE, 3.0f);
    
    // Draw each cell
    for (int y = 0; y < maze->height; y++) {
        for (int x = 0; x < maze->width; x++) {
            float cell_x = MAZE_OFFSET_X + x * CELL_SIZE;
            float cell_y = MAZE_OFFSET_Y + y * CELL_SIZE;
            
            MazeCell* cell = &maze->cells[y][x];
            
            // Cell background - all cells are walkable now (walls between cells)
            Color bg_color = (Color){0.5f, 0.7f, 0.5f};  // Light green ground
            draw_filled_rectangle(cell_x, cell_y, CELL_SIZE, CELL_SIZE, bg_color);
            
            // Bananas (all cells can have bananas)
            if (cell->banana_count > 0) {
                float center_x = cell_x + CELL_SIZE * 0.5f;
                float center_y = cell_y + CELL_SIZE * 0.5f;
                
                // Yellow circle for banana
                draw_circle(center_x, center_y, CELL_SIZE * 0.25f, COLOR_YELLOW);
                Color orange = {1.0f, 0.5f, 0.0f};
                draw_circle_outline(center_x, center_y, CELL_SIZE * 0.25f, orange, 2.0f);
                
                // Count
                char buf[8];
                snprintf(buf, sizeof(buf), "%d", cell->banana_count);
                draw_text(center_x - 5, center_y + 5, buf, COLOR_BLACK);
            }
        }
    }
    
    // Draw walls between cells (thick dark lines)
    Color wall_color = {0.15f, 0.15f, 0.15f};  // Very dark gray
    glLineWidth(4.0f);  // Thick lines for walls
    
    glColor3f(wall_color.r, wall_color.g, wall_color.b);
    glBegin(GL_LINES);
    
    for (int y = 0; y < maze->height; y++) {
        for (int x = 0; x < maze->width; x++) {
            float cell_x = MAZE_OFFSET_X + x * CELL_SIZE;
            float cell_y = MAZE_OFFSET_Y + y * CELL_SIZE;
            
            uint8_t walls = maze->cells[y][x].walls;
            
            // Draw north wall
            if (walls & WALL_NORTH) {
                glVertex2f(cell_x, cell_y);
                glVertex2f(cell_x + CELL_SIZE, cell_y);
            }
            
            // Draw south wall
            if (walls & WALL_SOUTH) {
                glVertex2f(cell_x, cell_y + CELL_SIZE);
                glVertex2f(cell_x + CELL_SIZE, cell_y + CELL_SIZE);
            }
            
            // Draw east wall
            if (walls & WALL_EAST) {
                glVertex2f(cell_x + CELL_SIZE, cell_y);
                glVertex2f(cell_x + CELL_SIZE, cell_y + CELL_SIZE);
            }
            
            // Draw west wall
            if (walls & WALL_WEST) {
                glVertex2f(cell_x, cell_y);
                glVertex2f(cell_x, cell_y + CELL_SIZE);
            }
        }
    }
    
    glEnd();
    glLineWidth(1.0f);  // Reset line width
    
    // Draw entry points at home positions (colored border only, NO 'H' inside maze)
    for (int i = 0; i < g_sim->num_families; i++) {
        Family* family = &g_sim->families[i];
        if (family->is_withdrawn) continue;
        
        Position home = family->home_pos;
        float cell_x = MAZE_OFFSET_X + home.x * CELL_SIZE;
        float cell_y = MAZE_OFFSET_Y + home.y * CELL_SIZE;
        
        // Draw colored border at home entry point
        Color family_color = get_family_color(i);
        draw_rectangle_outline(cell_x + 2, cell_y + 2, CELL_SIZE - 4, CELL_SIZE - 4, 
                              family_color, 4.0f);
    }
    
    // Draw females
    for (int i = 0; i < g_sim->num_families; i++) {
        Family* family = &g_sim->families[i];
        if (family->is_withdrawn) continue;
        
        // Animated position
        float female_x, female_y;
        if (g_female_anims && g_female_anims[i].active) {
            float t = g_female_anims[i].progress;
            float from_x, from_y, to_x, to_y;
            maze_to_screen(g_female_anims[i].from.x, g_female_anims[i].from.y, &from_x, &from_y);
            maze_to_screen(g_female_anims[i].to.x, g_female_anims[i].to.y, &to_x, &to_y);
            
            female_x = from_x + (to_x - from_x) * t;
            female_y = from_y + (to_y - from_y) * t;
        } else {
            maze_to_screen(family->female.pos.x, family->female.pos.y, &female_x, &female_y);
        }
        
        render_ape(female_x, female_y, i, 0, 0,
                  family->female.energy, g_sim->config->female_energy_max,
                  family->female.collected_bananas, family->female.state, 1.0f, 0.0f);
    }
    
    // Draw males and babies OUTSIDE maze
    render_males_outside_maze();
}

// Render stats panel (RIGHT 20% - includes legend now)
static void render_stats_panel(void) {
    if (!g_sim) return;
    
    float panel_x = STATS_PANEL_X;
    float panel_y = 0;
    float panel_w = STATS_PANEL_WIDTH;
    float panel_h = g_window_height;
    
    // Panel background
    Color panel_bg = {0.18f, 0.18f, 0.18f};
    draw_filled_rectangle(panel_x, panel_y, panel_w, panel_h, panel_bg);
    
    // Border
    draw_rectangle_outline(panel_x, panel_y, panel_w, panel_h, COLOR_WHITE, 2.0f);
    
    float text_x = panel_x + 15;
    float y = 30;
    float line_height = 22;
    
    // Title
    draw_text_large(text_x, y, "STATISTICS", COLOR_WHITE);
    y += line_height * 2;
    
    // Time
    time_t now = time(NULL);
    int elapsed = (int)difftime(now, g_sim->start_time);
    char buf[128];
    
    snprintf(buf, sizeof(buf), "Time: %d / %d sec", elapsed, g_sim->config->time_limit_seconds);
    draw_text(text_x, y, buf, COLOR_WHITE);
    y += line_height;
    
    // Fights
    pthread_mutex_lock(&g_sim->stats_mutex);
    snprintf(buf, sizeof(buf), "Female Fights: %d", g_sim->stats.female_fights);
    draw_text(text_x, y, buf, COLOR_WHITE);
    y += line_height;
    
    snprintf(buf, sizeof(buf), "Male Fights: %d", g_sim->stats.male_fights);
    draw_text(text_x, y, buf, COLOR_WHITE);
    y += line_height;
    
    // Baby steals
    snprintf(buf, sizeof(buf), "Baby Steals: %d", g_sim->stats.baby_steals);
    draw_text(text_x, y, buf, COLOR_WHITE);
    y += line_height;
    
    // Total bananas
    snprintf(buf, sizeof(buf), "Total Collected: %d", g_sim->stats.total_bananas_collected);
    draw_text(text_x, y, buf, COLOR_WHITE);
    y += line_height;
    
    // Withdrawn
    snprintf(buf, sizeof(buf), "Withdrawn: %d / %d", 
             g_sim->withdrawn_count, g_sim->num_families);
    draw_text(text_x, y, buf, COLOR_WHITE);
    y += line_height * 2;
    pthread_mutex_unlock(&g_sim->stats_mutex);
    
    // Family standings
    draw_text_large(text_x, y, "FAMILY STANDINGS", COLOR_WHITE);
    y += line_height * 1.5f;
    
    // Sort families
    typedef struct {
        int family_id;
        int basket_count;
        int is_withdrawn;
    } FamilyRank;
    
    FamilyRank ranks[10];
    int rank_count = 0;
    
    for (int i = 0; i < g_sim->num_families && i < 10; i++) {
        Family* family = &g_sim->families[i];
        pthread_mutex_lock(&family->basket_mutex);
        ranks[rank_count].family_id = i;
        ranks[rank_count].basket_count = family->basket_bananas;
        ranks[rank_count].is_withdrawn = family->is_withdrawn;
        pthread_mutex_unlock(&family->basket_mutex);
        rank_count++;
    }
    
    // Sort
    for (int i = 0; i < rank_count - 1; i++) {
        for (int j = 0; j < rank_count - i - 1; j++) {
            if (ranks[j].basket_count < ranks[j + 1].basket_count) {
                FamilyRank temp = ranks[j];
                ranks[j] = ranks[j + 1];
                ranks[j + 1] = temp;
            }
        }
    }
    
    // Display top families
    int display_count = (rank_count < 7) ? rank_count : 7;
    for (int i = 0; i < display_count; i++) {
        int fam_id = ranks[i].family_id;
        int basket = ranks[i].basket_count;
        int withdrawn = ranks[i].is_withdrawn;
        
        Color family_color = get_family_color(fam_id);
        
        // Rank
        if (i == 0) {
            snprintf(buf, sizeof(buf), "#1");
        } else {
            snprintf(buf, sizeof(buf), "   ");
        }
        draw_text(text_x, y, buf, COLOR_YELLOW);
        
        // Family info
        snprintf(buf, sizeof(buf), "F%d: %d", fam_id, basket);
        if (withdrawn) {
            strcat(buf, " (W)");
        }
        draw_text(text_x + 30, y, buf, family_color);
        y += line_height;
        
        // Progress bar
        float bar_x = text_x + 30;
        float bar_y = y;
        float bar_width = panel_w - 60;
        float bar_height = 10;
        
        draw_filled_rectangle(bar_x, bar_y, bar_width, bar_height, COLOR_DARK_GRAY);
        
        float fill_ratio = (float)basket / (float)g_sim->config->family_banana_threshold;
        if (fill_ratio > 1.0f) fill_ratio = 1.0f;
        if (fill_ratio > 0.0f) {
            draw_filled_rectangle(bar_x, bar_y, bar_width * fill_ratio, bar_height, family_color);
        }
        
        draw_rectangle_outline(bar_x, bar_y, bar_width, bar_height, COLOR_BLACK, 1.0f);
        y += line_height + 3;
    }
    
    y += line_height;
    
    // LEGEND
    draw_text_large(text_x, y, "LEGEND", COLOR_WHITE);
    y += line_height * 1.5f;
    
    // Family colors
    int legend_families = (g_sim->num_families < 7) ? g_sim->num_families : 7;
    for (int i = 0; i < legend_families; i++) {
        Color c = get_family_color(i);
        float square_x = text_x;
        float square_y = y - 12;
        
        draw_filled_rectangle(square_x, square_y, 18, 18, c);
        draw_rectangle_outline(square_x, square_y, 18, 18, COLOR_WHITE, 1.5f);
        
        snprintf(buf, sizeof(buf), "Family %d", i);
        draw_text(text_x + 25, y, buf, COLOR_WHITE);
        y += line_height;
    }
    
    y += line_height * 0.5f;
    
    // Ape types
    draw_text(text_x, y, "Ape Types:", COLOR_WHITE);
    y += line_height;
    
    draw_text(text_x, y, "  O  Male (circle)", COLOR_WHITE);
    y += line_height;
    draw_text(text_x, y, "  []  Female (square)", COLOR_WHITE);
    y += line_height;
    draw_text(text_x, y, "  ^  Baby (triangle)", COLOR_WHITE);
    y += line_height * 1.5f;
    
    // Controls
    draw_text(text_x, y, "CONTROLS:", COLOR_YELLOW);
    y += line_height;
    draw_text(text_x, y, "SPACE: Pause/Resume", COLOR_LIGHT_GRAY);
    y += line_height;
    draw_text(text_x, y, "+/-: Speed Control", COLOR_LIGHT_GRAY);
    y += line_height;
    draw_text(text_x, y, "Click: Select Ape", COLOR_LIGHT_GRAY);
    y += line_height;
    draw_text(text_x, y, "ESC/Q: Quit", COLOR_LIGHT_GRAY);
    y += line_height * 1.5f;
    
    // Speed
    snprintf(buf, sizeof(buf), "Speed: %.1fx", speed_multipliers[current_speed_index]);
    draw_text(text_x, y, buf, COLOR_GREEN);
    y += line_height;
    
    // Pause
    if (g_is_paused) {
        draw_text_large(text_x, y, "PAUSED", COLOR_RED);
        y += line_height * 2;
    }
    
    // Selected ape details
    if (g_selected_ape_family >= 0 && g_selected_ape_family < g_sim->num_families) {
        y += line_height;
        draw_text_large(text_x, y, "SELECTED APE", COLOR_YELLOW);
        y += line_height * 1.5f;
        
        Family* fam = &g_sim->families[g_selected_ape_family];
        
        if (g_selected_ape_type == 0) {
            // Male
            snprintf(buf, sizeof(buf), "Male %d", g_selected_ape_family);
            draw_text(text_x, y, buf, get_family_color(g_selected_ape_family));
            y += line_height;
            
            snprintf(buf, sizeof(buf), "Energy: %d/%d", fam->male.energy, g_sim->config->male_energy_max);
            draw_text(text_x, y, buf, COLOR_WHITE);
            y += line_height;
            
            const char* state = (fam->male.state == STATE_FIGHTING) ? "FIGHTING" : "GUARDING";
            snprintf(buf, sizeof(buf), "State: %s", state);
            draw_text(text_x, y, buf, COLOR_WHITE);
        } else if (g_selected_ape_type == 1) {
            // Female
            snprintf(buf, sizeof(buf), "Female %d", g_selected_ape_family);
            draw_text(text_x, y, buf, get_family_color(g_selected_ape_family));
            y += line_height;
            
            snprintf(buf, sizeof(buf), "Energy: %d/%d", fam->female.energy, g_sim->config->female_energy_max);
            draw_text(text_x, y, buf, COLOR_WHITE);
            y += line_height;
            
            snprintf(buf, sizeof(buf), "Collected: %d/%d", 
                     fam->female.collected_bananas, g_sim->config->bananas_per_trip);
            draw_text(text_x, y, buf, COLOR_WHITE);
        }
    }
}

// Calculate end-game statistics (called once when simulation ends)
static void calculate_end_game_stats(void) {
    if (g_end_stats_calculated || !g_sim) return;
    
    // Count remaining bananas in maze
    g_bananas_in_maze = 0;
    if (g_sim->maze) {
        for (int y = 0; y < g_sim->maze->height; y++) {
            for (int x = 0; x < g_sim->maze->width; x++) {
                g_bananas_in_maze += g_sim->maze->cells[y][x].banana_count;
            }
        }
    }
    
    time_t end_time = time(NULL);
    
    // Count bananas in all baskets and build rankings
    g_bananas_in_baskets = 0;
    for (int i = 0; i < g_sim->num_families && i < 16; i++) {
        Family* family = &g_sim->families[i];
        
        pthread_mutex_lock(&family->basket_mutex);
        g_rankings[i].current_basket = family->basket_bananas;
        g_rankings[i].total_collected = family->total_bananas_collected;
        pthread_mutex_unlock(&family->basket_mutex);
        
        g_rankings[i].family_id = i;
        g_rankings[i].is_active = !family->is_withdrawn;
        g_rankings[i].fights_won = family->fights_won;
        
        // Calculate time survived
        if (family->is_withdrawn && family->withdrawal_time > 0) {
            g_rankings[i].time_survived = (int)difftime(family->withdrawal_time, g_sim->start_time);
        } else {
            g_rankings[i].time_survived = (int)difftime(end_time, g_sim->start_time);
        }
        
        double current_score = g_rankings[i].current_basket * g_sim->config->rank_current_basket_weight;
        double collected_score = g_rankings[i].total_collected * g_sim->config->rank_total_collected_weight;
        double fight_score = g_rankings[i].fights_won * g_sim->config->rank_fights_weight;
        g_rankings[i].score = current_score + collected_score + fight_score;
        
        g_bananas_in_baskets += g_rankings[i].current_basket;
    }
    
    g_total_bananas = g_sim->maze ? g_sim->maze->total_bananas : 0;
    g_bananas_eaten = g_total_bananas - g_bananas_in_maze - g_bananas_in_baskets;
    if (g_bananas_eaten < 0) g_bananas_eaten = 0;
    
    // Sort with criteria and tiebreakers:
    // 1. Active families first
    // 2. Higher combined score
    // TIEBREAKERS (when scores are equal):
    // 3. Higher current basket wins
    // 4. Longer time survived wins
    // 5. Lower family_id wins (deterministic)
    for (int i = 0; i < g_sim->num_families - 1 && i < 15; i++) {
        for (int j = i + 1; j < g_sim->num_families && j < 16; j++) {
            int swap = 0;
            
            // Priority 1: Active families come before withdrawn
            if (g_rankings[j].is_active && !g_rankings[i].is_active) {
                swap = 1;
            }
            // Among same active status, compare by score and tiebreakers
            else if (g_rankings[j].is_active == g_rankings[i].is_active) {
                // Priority 2: Higher score wins
                if (g_rankings[j].score > g_rankings[i].score) {
                    swap = 1;
                }
                // TIEBREAKER CHAIN (when scores are equal)
                else if (g_rankings[j].score == g_rankings[i].score) {
                    // Tiebreaker 3: Higher current basket wins
                    if (g_rankings[j].current_basket > g_rankings[i].current_basket) {
                        swap = 1;
                    }
                    else if (g_rankings[j].current_basket == g_rankings[i].current_basket) {
                        // Tiebreaker 4: Longer time survived wins
                        if (g_rankings[j].time_survived > g_rankings[i].time_survived) {
                            swap = 1;
                        }
                        else if (g_rankings[j].time_survived == g_rankings[i].time_survived) {
                            // Tiebreaker 5: Lower family_id wins (deterministic)
                            if (g_rankings[j].family_id < g_rankings[i].family_id) {
                                swap = 1;
                            }
                        }
                    }
                }
            }
            
            if (swap) {
                EndGameRanking tmp = g_rankings[i];
                g_rankings[i] = g_rankings[j];
                g_rankings[j] = tmp;
            }
        }
    }
    
    g_end_stats_calculated = 1;
}

// Render end-game overlay
static void render_end_game_overlay(void) {
    if (!g_sim) return;
    
    // Semi-transparent dark background
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(0.0f, 0.0f, 0.0f, 0.7f);
    glBegin(GL_QUADS);
    glVertex2f(0, 0);
    glVertex2f(g_window_width, 0);
    glVertex2f(g_window_width, g_window_height);
    glVertex2f(0, g_window_height);
    glEnd();
    
    // Calculate popup dimensions
    float popup_width = 500;
    float popup_height = 450;
    float popup_x = (g_window_width - popup_width) / 2;
    float popup_y = (g_window_height - popup_height) / 2;
    
    // Popup background
    Color bg_color = {0.15f, 0.15f, 0.2f};
    draw_filled_rectangle(popup_x, popup_y, popup_width, popup_height, bg_color);
    
    // Border
    Color border_color = {0.8f, 0.7f, 0.2f};  // Gold
    draw_rectangle_outline(popup_x, popup_y, popup_width, popup_height, border_color, 4.0f);
    
    float y = popup_y + 30;
    float center_x = popup_x + popup_width / 2;
    char buf[128];
    
    // Title
    draw_text_large(center_x - 100, y, "SIMULATION ENDED", COLOR_YELLOW);
    y += 40;
    
    // Banana statistics
    draw_text(popup_x + 20, y, "BANANA STATISTICS:", COLOR_WHITE);
    y += 25;
    
    snprintf(buf, sizeof(buf), "  Total (start):     %d", g_total_bananas);
    draw_text(popup_x + 20, y, buf, COLOR_LIGHT_GRAY);
    y += 20;
    
    snprintf(buf, sizeof(buf), "  Remaining in maze: %d", g_bananas_in_maze);
    draw_text(popup_x + 20, y, buf, COLOR_LIGHT_GRAY);
    y += 20;
    
    snprintf(buf, sizeof(buf), "  In baskets:        %d", g_bananas_in_baskets);
    draw_text(popup_x + 20, y, buf, COLOR_LIGHT_GRAY);
    y += 20;
    
    snprintf(buf, sizeof(buf), "  Eaten by babies:   %d", g_bananas_eaten);
    draw_text(popup_x + 20, y, buf, COLOR_LIGHT_GRAY);
    y += 35;
    
    // Rankings
    draw_text(popup_x + 20, y, "FINAL RANKINGS:", COLOR_WHITE);
    y += 25;
    
    const char* medals[] = {"1st", "2nd", "3rd"};
    Color medal_colors[] = {
        {1.0f, 0.84f, 0.0f},   // Gold
        {0.75f, 0.75f, 0.75f}, // Silver
        {0.8f, 0.5f, 0.2f}     // Bronze
    };
    
    int num_to_show = g_sim->num_families < 8 ? g_sim->num_families : 8;
    
    for (int i = 0; i < num_to_show; i++) {
        EndGameRanking* r = &g_rankings[i];
        const char* status = r->is_active ? "SURVIVOR" : "WITHDRAWN";
        Color text_color = r->is_active ? COLOR_GREEN : COLOR_LIGHT_GRAY;
        
        // Show ranking with score breakdown (bkt=current basket, col=total collected)
        if (i < 3) {
            snprintf(buf, sizeof(buf), "  %s: F%d [%s] Score:%.0f (bkt=%d,col=%d,t=%ds,w=%d)",
                     medals[i], r->family_id, status, r->score,
                     r->current_basket, r->total_collected, r->time_survived, r->fights_won);
            draw_text(popup_x + 20, y, buf, medal_colors[i]);
        } else {
            snprintf(buf, sizeof(buf), "  %dth: F%d [%s] Score:%.0f (bkt=%d,col=%d,t=%ds,w=%d)",
                     i + 1, r->family_id, status, r->score,
                     r->current_basket, r->total_collected, r->time_survived, r->fights_won);
            draw_text(popup_x + 20, y, buf, text_color);
        }
        y += 22;
    }
    
    y += 20;
    
    // Winner announcement
    if (g_rankings[0].score > 0) {
        snprintf(buf, sizeof(buf), "WINNER: Family %d with score %.0f!",
                 g_rankings[0].family_id, g_rankings[0].score);
        draw_text_large(center_x - 150, y, buf, COLOR_YELLOW);
    } else {
        draw_text_large(center_x - 80, y, "No winner", COLOR_RED);
    }
    
    y += 40;
    
    // Instructions
    draw_text(center_x - 80, y, "Press ESC or Q to exit", COLOR_LIGHT_GRAY);
}

// Main display function
static void display(void) {
    glClear(GL_COLOR_BUFFER_BIT);
    
    pthread_mutex_lock(&g_gui_mutex);
    
    render_maze();
    render_stats_panel();
    
    // Notifications
    for (int i = 0; i < g_notification_count; i++) {
        Notification* n = &g_notifications[i];
        draw_text_large(n->x, n->y, n->message, n->color);
    }
    
    // Fight banner at top of screen when male fight is active
    if (g_male_fight_anim.active && !g_simulation_ended) {
        char fight_banner[64];
        snprintf(fight_banner, sizeof(fight_banner), "MALE %d  vs  MALE %d",
                 g_male_fight_anim.initiator_family_id,
                 g_male_fight_anim.defender_family_id);
        
        // Draw semi-transparent background bar
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glColor4f(0.2f, 0.0f, 0.0f, 0.8f);  // Dark red background
        glBegin(GL_QUADS);
        glVertex2f(0, 0);
        glVertex2f(MAZE_PANEL_WIDTH, 0);
        glVertex2f(MAZE_PANEL_WIDTH, 45);
        glVertex2f(0, 45);
        glEnd();
        glDisable(GL_BLEND);
        
        // Draw fight text centered
        Color fight_color = {1.0f, 0.8f, 0.0f};  // Yellow/gold
        float text_x = MAZE_PANEL_WIDTH / 2 - 120;
        draw_text_large(text_x, 30, fight_banner, fight_color);
    }
    
    // End-game overlay when simulation has ended
    if (g_simulation_ended) {
        calculate_end_game_stats();
        render_end_game_overlay();
    }
    
    pthread_mutex_unlock(&g_gui_mutex);
    
    glutSwapBuffers();
}

// Reshape callback
static void reshape(int w, int h) {
    g_window_width = w;
    g_window_height = h;
    
    glViewport(0, 0, w, h);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluOrtho2D(0, w, h, 0);
    glMatrixMode(GL_MODELVIEW);
    
    calculate_layout();
}

// Keyboard callback
static void keyboard(unsigned char key, int x, int y) {
    (void)x; (void)y;
    
    switch (key) {
        case 27:  // ESC
        case 'q':
        case 'Q':
            if (g_sim) {
                signal_simulation_stop(g_sim);
            }
            glutDestroyWindow(g_window_id);
            exit(0);
            break;
            
        case ' ':
            g_is_paused = !g_is_paused;
            printf("%s\n", g_is_paused ? "PAUSED" : "RESUMED");
            break;
            
        case '+':
        case '=':
            if (current_speed_index < 4) {
                current_speed_index++;
                printf("Speed: %.1fx\n", speed_multipliers[current_speed_index]);
            }
            break;
            
        case '-':
        case '_':
            if (current_speed_index > 0) {
                current_speed_index--;
                printf("Speed: %.1fx\n", speed_multipliers[current_speed_index]);
            }
            break;
            
        case 'd':
        case 'D':
            g_show_details = !g_show_details;
            break;
    }
}

// Mouse callback
static void mouse(int button, int state, int x, int y) {
    if (button == GLUT_LEFT_BUTTON && state == GLUT_DOWN) {
        int maze_x, maze_y;
        if (screen_to_maze(x, y, &maze_x, &maze_y) && g_sim) {
            for (int i = 0; i < g_sim->num_families; i++) {
                Family* family = &g_sim->families[i];
                
                if (family->female.pos.x == maze_x && family->female.pos.y == maze_y) {
                    g_selected_ape_family = i;
                    g_selected_ape_type = 1;
                    printf("Selected: Female from Family %d\n", i);
                    return;
                }
            }
        }
        
        // Check if clicked on male (outside maze)
        if (g_sim) {
            for (int i = 0; i < g_sim->num_families; i++) {
                float male_x, male_y;
                get_male_position_at_home(i, &male_x, &male_y);
                
                float dx = x - male_x;
                float dy = y - (male_y - 15);
                float dist = sqrtf(dx*dx + dy*dy);
                
                if (dist < CELL_SIZE * 0.5f) {
                    g_selected_ape_family = i;
                    g_selected_ape_type = 0;
                    printf("Selected: Male from Family %d\n", i);
                    return;
                }
            }
        }
    }
}

// Timer callback
static void timer(int value) {
    (void)value;
    
    if (!g_is_paused) {
        pthread_mutex_lock(&g_gui_mutex);
        update_animations();
        update_notifications();
        update_basket_indicators();
        process_gui_events();
        pthread_mutex_unlock(&g_gui_mutex);
        
        // CHECK TERMINATION CONDITIONS
        if (g_sim && g_sim->simulation_running) {
            const char* termination_reason = NULL;
            
            // Check withdrawn families threshold
            pthread_mutex_lock(&g_sim->stats_mutex);
            int withdrawn = g_sim->withdrawn_count;
            pthread_mutex_unlock(&g_sim->stats_mutex);
            
            if (withdrawn >= g_sim->config->withdrawn_family_threshold) {
                termination_reason = "WITHDRAWN_FAMILIES_THRESHOLD";
            }
            
            // Check if only 1 family remains (no point continuing)
            if (!termination_reason) {
                int active_families = g_sim->num_families - withdrawn;
                if (active_families <= 1) {
                    termination_reason = "ONLY_ONE_FAMILY_LEFT";
                }
            }
            
            // Check family basket threshold
            if (!termination_reason) {
                for (int i = 0; i < g_sim->num_families; i++) {
                    Family* family = &g_sim->families[i];
                    pthread_mutex_lock(&family->basket_mutex);
                    int basket = family->basket_bananas;
                    pthread_mutex_unlock(&family->basket_mutex);
                    
                    if (basket >= g_sim->config->family_banana_threshold) {
                        termination_reason = "FAMILY_BASKET_THRESHOLD";
                        break;
                    }
                }
            }
            
            // Check baby eaten threshold
            if (!termination_reason) {
                for (int i = 0; i < g_sim->num_families; i++) {
                    Family* family = &g_sim->families[i];
                    for (int j = 0; j < family->baby_count; j++) {
                        if (family->babies[j].eaten_count >= g_sim->config->baby_eaten_threshold) {
                            termination_reason = "BABY_EATEN_THRESHOLD";
                            break;
                        }
                    }
                    if (termination_reason) break;
                }
            }
            
            // Check time limit
            if (!termination_reason) {
                time_t current_time = time(NULL);
                double elapsed = difftime(current_time, g_sim->start_time);
                if (elapsed >= g_sim->config->time_limit_seconds) {
                    termination_reason = "TIME_LIMIT";
                }
            }
            
            // If termination condition met, stop simulation
            if (termination_reason) {
                log_event(g_sim, "\n🛑 TERMINATION CONDITION MET: %s\n", termination_reason);
                g_sim->simulation_running = 0;
                g_simulation_ended = 1;
                
                // Print final statistics to terminal (this also sets flag to suppress further logging)
                display_final_statistics(g_sim);
            }
        }
        
        // Also check if simulation was stopped externally (e.g., by baby eating threshold)
        if (g_sim && !g_sim->simulation_running && !g_simulation_ended) {
            g_simulation_ended = 1;
            
            // Print final statistics to terminal (this also sets flag to suppress further logging)
            display_final_statistics(g_sim);
        }
    }
    
    glutPostRedisplay();
    glutTimerFunc(FRAME_TIME, timer, 0);
}

// Cleanup animations
static void cleanup_animations(void) {
    if (g_female_anims) {
        free(g_female_anims);
        g_female_anims = NULL;
    }
    
    if (g_baby_anims && g_sim) {
        for (int i = 0; i < g_sim->num_families; i++) {
            if (g_baby_anims[i]) {
                free(g_baby_anims[i]);
            }
        }
        free(g_baby_anims);
        g_baby_anims = NULL;
    }
}

// Main entry point
void gui_show_simulation(SimulationState* sim) {
    if (!sim) {
        printf("Error: Invalid simulation state\n");
        return;
    }
    
    g_sim = sim;
    g_notification_count = 0;
    g_basket_indicator_count = 0;
    g_frame_counter = 0;
    g_simulation_ended = 0;
    g_end_stats_calculated = 0;
    
    // Initialize GLUT
    int argc = 1;
    char* argv[] = {(char*)"ape_simulation", NULL};
    glutInit(&argc, argv);
    
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGB);
    
    // Windowed mode (not fullscreen) for easier debugging with terminal
    g_window_width = 1280;
    g_window_height = 800;
    glutInitWindowSize(g_window_width, g_window_height);
    glutInitWindowPosition(50, 50);  // Position window near top-left
    g_window_id = glutCreateWindow(WINDOW_TITLE);
    
    calculate_layout();
    init_gl();
    init_animations();
    
    // Callbacks
    glutDisplayFunc(display);
    glutReshapeFunc(reshape);
    glutKeyboardFunc(keyboard);
    glutMouseFunc(mouse);
    glutTimerFunc(FRAME_TIME, timer, 0);
    
    printf("\n=== GUI STARTED ===\n");
    printf("Window: %dx%d\n", g_window_width, g_window_height);
    printf("Families: %d\n", sim->num_families);
    printf("Controls:\n");
    printf("  SPACE     - Pause/Resume\n");
    printf("  +/-       - Speed Up/Down (0.5x - 10x)\n");
    printf("  Click     - Select Ape\n");
    printf("  D         - Toggle Details\n");
    printf("  ESC/Q     - Quit\n");
    printf("\n");
    
    glutMainLoop();
    
    cleanup_animations();
}