// This is a C program for a Wii Balance Board game.
// It uses SDL2 for graphics and sound, and xwiimote for Wii hardware communication.
// This is a complete and corrected version that fixes compilation errors,
// implements correct scoring and target movement for the "Balance Hold" game mode,
// and ensures proper game state transitions.

// --- Include necessary libraries ---
#include <stdio.h>
#include <stdlib.h>

#include <unistd.h>      // For sleep // FIXED: Was <unistd.>
#include <time.h>        // For srand, time
#include <math.h>        // For fabsf, roundf, sqrtf, hypot, sin, cos
#include <fcntl.h>       // For O_NONBLOCK, fcntl
#include <errno.h>       // For errno
#include <string.h>      // For strerror, memset
#include <poll.h>        // For poll

// xwiimote and bluetooth libraries for Wii Balance Board
#include <xwiimote.h>
#include <bluetooth/bluetooth.h>

// SDL2 libraries for graphics, text, and audio
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <SDL2/SDL_mixer.h>
#include <SDL2/SDL_image.h> // Required for PNG images

// --- Game Configuration ---
#define WINDOW_WIDTH 1920
#define WINDOW_HEIGHT 1080
#define GAME_OBJECT_SIZE 150   // INCREASED SIZE for better visibility on a large TV.
#define COB_SCALE_GENERAL 0.00015    // Original working value for general modes
#define COB_SCALE_DODGE 0.00025 // Increased sensitivity for Dodge mode
#define DEAD_ZONE 400.0      // Original working value
#define TRAIL_LENGTH 60       // Longer trail for smoother curves (Increased from 20)
#define WIN_ANIMATION_DURATION 2500 // In milliseconds
#define FPS 60
#define POLL_TIMEOUT_MS 100  // Poll more frequently
#define MAX_EVENTS_PER_POLL 10  // Process multiple events per poll
#define POLL_TIMEOUT_THRESHOLD 100  // Much more forgiving timeout
#define TARGET_FPS 60
#define FRAME_TIME (1000.0f / TARGET_FPS)

// --- Debug & Performance ---
#define DEBUG_INTERVAL 60 // Print debug info every 60 frames
char debug_buffer[256]; // Buffer for formatted debug strings

// --- Dodge Mode Configuration ---
#define MAX_DODGE_BLOCKS 10
#define BLOCK_WIDTH 50
#define BLOCK_HEIGHT 100
#define BLOCK_INITIAL_SPEED 300.0f
#define BLOCK_SPEED_INCREMENT 50.0f
#define BLOCK_SPAWN_INTERVAL 2.0f
#define DODGE_SCORE_FILE "dodge_score.txt"
#define MENU_SELECT_TIME_REQUIRED 1.5 // Reduced time to make selection easier
#define TRANSITION_DURATION 1.5 // Duration of the camera shake and transition music
#define MIN_TOTAL_WEIGHT 2000.0f  // Original working value
#define INACTIVITY_TIMEOUT_SECONDS 15 // Reset to connection screen after 15 seconds of no input

// --- UI Configuration ---
#define TITLE_FONT_SIZE 60
#define TUTORIAL_FONT_SIZE 60  // Increased font size for connecting screen
#define MENU_TITLE_FONT_SIZE 72
#define MENU_DESCRIPTION_FONT_SIZE 40 // Increased for better readability
#define FONT_COLOR_R 50
#define FONT_COLOR_G 50
#define FONT_COLOR_B 50
#define TRAIL_COLOR_R 212 // Reddish-orange
#define TRAIL_COLOR_G 83 // Reddish-orange
#define TRAIL_COLOR_B 81 // Reddish-orange
#define TRAIL_THICKNESS 5

// --- Balance Hold Mode Configuration ---
#define BH_HOLD_TIME_REQUIRED 1.5 // Time in seconds to hold position
#define BH_TARGET_PULSE_SPEED 8.0f // How fast the target pulses
#define BH_BEEP_FREQUENCY 0.5f // Time in seconds between beeps
#define BH_HOLD_BAR_WIDTH 400
#define BH_HOLD_BAR_HEIGHT 40
#define BH_GRACE_ZONE_RADIUS 200 // The larger, more forgiving area around the target.
#define BH_HOLD_RADIUS 100 // NEW: Radius for the hold timer to count up
#define BH_TARGET_MOVEMENT_SPEED_EASY 0.0f
#define BH_TARGET_MOVEMENT_SPEED_MEDIUM 25.0f
#define BH_TARGET_MOVEMENT_SPEED_HARD 50.0f

// --- Coin Collector Mode Configuration ---
#define CC_COIN_SPAWN_RADIUS 600
#define COIN_SAFE_MARGIN 300    // INCREASED safety margin for the larger coins
#define STARTING_COIN_SIZE 150  // Starting size for the first coin
#define CC_COIN_TIMER 10.0f // Time in seconds to collect the next coin
#define COIN_SPAWN_MIN_DIST_PLAYER 250 // Minimum distance from player for new coin spawn

// --- Damped Spring Smoothing Configuration ---
#define SPRING_CONSTANT 10.0f
#define DAMPING_FACTOR 5.0f // Increased damping for more stability

// --- Confetti Configuration ---
#define NUM_CONFETTI 150
#define CONFETTI_LIFETIME 2.0f // In seconds
#define CONFETTI_GRAVITY 200.0f
#define CONFETTI_SPREAD 300.0f

// --- Wii Balance Board MAC Address ---
// NOTE: You must change this to your device's MAC address.
#define WII_BB_MAC_ADDRESS "XX:XX:XX:XX:XX:XX"

// --- Game States ---
typedef enum {
    CONNECTING,
    TRANSITIONING,
    PLAYER_SELECTION,
    MAIN_MENU,
    DIFFICULTY_SELECTION,
    GAME_BALANCE_HOLD,
    GAME_COIN_COLLECTOR,
    GAME_DODGE,
    WINNING
} GameState;

// --- Game Type for Menu Selection ---
typedef enum {
    NO_GAME_SELECTED,
    BALANCE_HOLD,
    COIN_COLLECTOR,
    DODGE
} GameType;

// --- Difficulty Levels ---
typedef enum {
    EASY,
    MEDIUM,
    HARD
} Difficulty;

// --- Player Configuration ---
typedef struct {
    const char* name;
    const char* image_path;
} PlayerProfile;

PlayerProfile available_players[] = {
    {"Player 1", "example1.jpg"}, 
    {"Player 2", "example2.jpg"},
    {"Player 3", "example3.jpg"}
};
int num_players = sizeof(available_players) / sizeof(available_players[0]);

// --- Data Structures for Game Objects ---
typedef struct {
    float x;
    float y;
    float velocity_x;
    float velocity_y;
} PlayerObject;

typedef struct {
    float x;
    float y;
    float velocity_x;
    float velocity_y;
} TargetObject;

typedef struct {
    float x;
    float y;
    float vx;
    float vy;
    float lifetime;
    SDL_Color color;
} ConfettiParticle;

typedef struct {
    float x;
    float y;
    int active;
} Coin;

typedef struct {
    float x, y;
    float speed;
    int active;
} DodgeBlock;

// --- Global Variables ---
struct xwii_iface *iface = NULL;
int fd = -1;
struct xwii_event event;
ConfettiParticle confetti[NUM_CONFETTI];
float lowest_time_to_win = -1.0f;
int total_wins = 0; // NEW: Global variable for total wins
// Pointers to the sound effects and music
Mix_Chunk *coin_sound = NULL;
Mix_Chunk *win_sound = NULL;
Mix_Chunk *select_sound = NULL;
Mix_Chunk *target_sound = NULL;
Mix_Chunk *reset_sound = NULL; // NEW: Sound for timer reset
Mix_Music *connection_intro_music = NULL;
Mix_Music *connection_main_music = NULL;
Mix_Music *transition_music = NULL;
Mix_Music *main_intro_music = NULL;
Mix_Music *main_loop_music = NULL;
PlayerObject trail_points[TRAIL_LENGTH];
int trail_head = 0;
SDL_Texture* boardpower_texture = NULL;
SDL_Texture* player_textures[3];
SDL_Texture* coin_texture = NULL;
int poll_timeout_count = 0;
Coin coin_collector_coins[30]; // Max coins for hard mode
float current_total_weight = 0.0f;
float coin_timer = 0.0f;

// Dodge game variables
DodgeBlock dodge_blocks[MAX_DODGE_BLOCKS];
float block_spawn_timer = 0;
float current_block_speed = BLOCK_INITIAL_SPEED;
int dodge_score = 0;
int dodge_high_score = 0;

// Add a variable to manage the dynamic spawn interval
float dynamic_block_spawn_interval = BLOCK_SPAWN_INTERVAL;

// Menu-specific global variables
float menu_select_timer = 0.0f;
GameType selected_game = NO_GAME_SELECTED;
Difficulty current_difficulty;
int difficulty_selection = 0; // 0: None, 1: Easy, 2: Medium, 3: Hard
int selected_player_index = -1;
int player_selection_choice = 0; // 1 for left, 2 for center, 3 for right

// Game-specific variables
int current_game_target = 0; // Target score for the current game
float hold_timer = 0.0;
int coins = 0;
Uint32 game_start_time = 0;
Uint32 win_message_start_time = 0;
int beeps_played = 0;
TargetObject balance_hold_target; // Specific target for Balance Hold mode

// Transition variables
Uint32 transition_start_time = 0;
float shake_intensity = 0.0f;
Uint32 connection_start_time = 0;

// Dodge game movement control
Uint32 dodge_last_input_time = 0;

// --- Function Prototypes ---
void init_dodge_game(PlayerObject *player); 
void reset_game_state();
int init_xwiimote_non_blocking();
float read_lowest_time(const char* filename);
void write_lowest_time(const char* filename, float new_score);
int read_total_wins(const char* filename);
void write_total_wins(const char* filename, int wins);
void draw_hold_timer_bar(SDL_Renderer* renderer, int x, int y, int width, int height, float progress);
void cleanup_text_cache(void);
int read_dodge_high_score(const char* filename);
void write_dodge_high_score(const char* filename, int score);
void draw_gradient_background(SDL_Renderer* renderer, SDL_Color start_color, SDL_Color end_color);
void draw_middle_grid(SDL_Renderer* renderer);
void draw_filled_circle(SDL_Renderer* renderer, int x, int y, int radius);
void draw_outlined_circle(SDL_Renderer* renderer, int x, int y, int radius, int thickness);
void draw_text(SDL_Renderer* renderer, TTF_Font* font, const char* text, int x, int y, SDL_Color color);
void draw_centered_text(SDL_Renderer* renderer, TTF_Font* font, const char* text, int y, SDL_Color color);
void draw_confetti(SDL_Renderer* renderer, ConfettiParticle particle);
void init_confetti(float x, float y);
void update_confetti(float delta_time);
void draw_thick_line(SDL_Renderer* renderer, float x1, float y1, float x2, float y2, int thickness, SDL_Color color);
void draw_line_trail(SDL_Renderer* renderer);
int read_wii_balance_board_data(float *x_cob, float *y_cob);
void init_player(PlayerObject *player);
void init_balance_hold_game(PlayerObject *player, TargetObject *target);
void init_coin_collector_game(PlayerObject *player);
void spawn_dodge_block();
int is_in_zone(PlayerObject player, TargetObject target, int zone_radius);
void music_intro_finished_callback();
void update_player_position(PlayerObject* player, float target_x, float target_y, float delta_time);


/**
 * @brief Resets all game state variables and cleans up xwiimote resources.
 */
void reset_game_state() {
    if (iface) {
        xwii_iface_close(iface, XWII_IFACE_BALANCE_BOARD);
        xwii_iface_unref(iface);
    }
    iface = NULL;
    fd = -1;
    poll_timeout_count = 0;
    menu_select_timer = 0.0f;
    selected_game = NO_GAME_SELECTED;
    difficulty_selection = 0;
    selected_player_index = -1;
    player_selection_choice = 0;
    current_game_target = 0;
    hold_timer = 0.0f;
    coins = 0;
    beeps_played = 0;
    Mix_HaltMusic(); // Stop all music
}

/**
 * @brief Initializes the xwiimote interface and connects to the Wii Balance Board.
 * @return 0 on success, -1 on failure.
 */
int init_xwiimote_non_blocking() {
    struct xwii_monitor *mon = NULL;
    char *path = NULL;
    mon = xwii_monitor_new(true, NULL);  // First try scanning for any board
    if (!mon) {
        fprintf(stderr, "Failed to create xwiimote monitor\n");
        return -1;
    }
    path = xwii_monitor_poll(mon);
    if (!path) {
        xwii_monitor_unref(mon);
        // Try again with specific MAC
        mon = xwii_monitor_new(false, WII_BB_MAC_ADDRESS);
        if (!mon) return -1;
        path = xwii_monitor_poll(mon);
    }
    xwii_monitor_unref(mon);
    if (!path) {
        fprintf(stderr, "No balance board found. Is it powered on and synced?\n");
        return -1;
    }
    printf("Found balance board at: %s\n", path);
    if (xwii_iface_new(&iface, path) < 0) {
        perror("Failed to open interface, retrying...");
        free(path);
        iface = NULL;
        return -1;
    }
    free(path);
    fd = xwii_iface_get_fd(iface);
    if (fd < 0) {
        perror("Failed to get file descriptor, retrying...");
        xwii_iface_unref(iface);
        iface = NULL;
        return -1;
    }
    if (fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK) < 0) {
        perror("Failed to set non-blocking mode on fd, retrying...");
        xwii_iface_unref(iface);
        iface = NULL;
        return -1;
    }
    int ret = xwii_iface_open(iface, XWII_IFACE_BALANCE_BOARD);
    if (ret < 0) {
        fprintf(stderr, "Cannot open interface: %d\n", ret);
        xwii_iface_unref(iface);
        iface = NULL;
        return -1;
    }

    ret = xwii_iface_watch(iface, true);
    if (ret) {
        fprintf(stderr, "Cannot initialize hotplug watch: %d\n", ret);
        xwii_iface_unref(iface);
        iface = NULL;
        return -1;
    }
    printf("Wii Balance Board connected!\n");
    return 0;
}

// --- File I/O Functions ---
// Helper function to generate profile-specific filename
char* get_profile_filename(const char* base_filename, int player_index) {
    static char filename[256];
    const char* player_name = available_players[player_index].name;
    // Convert player name to lowercase for filename
    char lowercase_name[64];
    int i = 0;
    while (player_name[i] && i < 63) {
        lowercase_name[i] = (player_name[i] >= 'A' && player_name[i] <= 'Z') ? 
                           player_name[i] + 32 : player_name[i];
        i++;
    }
    lowercase_name[i] = '\0';
    
    snprintf(filename, sizeof(filename), "%s_%s", lowercase_name, base_filename);
    return filename;
}

float read_lowest_time(const char* filename) {
    FILE* file = fopen(filename, "r");
    float score = -1.0f;
    if (file) { fscanf(file, "%f", &score); fclose(file); }
    return score;
}

void write_lowest_time(const char* filename, float new_score) {
    FILE* file = fopen(filename, "w");
    if (file) { fprintf(file, "%.2f", new_score); fclose(file); }
    else { perror("Failed to write to score.txt"); }
}

// NEW: Functions to read and write total wins
int read_total_wins(const char* filename) {
    FILE* file = fopen(filename, "r");
    int wins = 0;
    if (file) { fscanf(file, "%d", &wins); fclose(file); }
    return wins;
}

void write_total_wins(const char* filename, int wins) {
    FILE* file = fopen(filename, "w");
    if (file) { fprintf(file, "%d", wins); fclose(file); }
    else { perror("Failed to write to wins.txt"); }
}

// --- Dodge Mode High Score Functions ---

void draw_hold_timer_bar(SDL_Renderer* renderer, int x, int y, int width, int height, float progress);
void cleanup_text_cache(void);

int read_dodge_high_score(const char* filename) {
    FILE* file = fopen(filename, "r");
    if (!file) return 0;
    int score;
    fscanf(file, "%d", &score);
    fclose(file);
    return score;
}

void write_dodge_high_score(const char* filename, int score) {
    FILE* file = fopen(filename, "w");
    if (!file) return;
    fprintf(file, "%d", score);
    fclose(file);
}

// --- Drawing Helper Functions ---
void draw_gradient_background(SDL_Renderer* renderer, SDL_Color start_color, SDL_Color end_color) {
    for (int i = 0; i < WINDOW_HEIGHT; ++i) {
        float ratio = (float)i / (float)WINDOW_HEIGHT;
        Uint8 r = start_color.r + (end_color.r - start_color.r) * ratio;
        Uint8 g = start_color.g + (end_color.g - start_color.g) * ratio;
        Uint8 b = start_color.b + (end_color.b - start_color.b) * ratio;
        SDL_SetRenderDrawColor(renderer, r, g, b, 255);
        SDL_RenderDrawLine(renderer, 0, i, WINDOW_WIDTH, i);
    }
}

void draw_middle_grid(SDL_Renderer* renderer) {
    int grid_size = 600;
    int grid_x = (WINDOW_WIDTH - grid_size) / 2;
    int grid_y = (WINDOW_HEIGHT - grid_size) / 2;
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 150);
    int line_thickness = 4;
    for (int i = 0; i < line_thickness; ++i) {
        SDL_Rect outer_rect = {grid_x - i, grid_y - i, grid_size + 2 * i, grid_size + 2 * i};
        SDL_RenderDrawRect(renderer, &outer_rect);
        SDL_RenderDrawLine(renderer, grid_x, grid_y + grid_size / 2 - i, grid_x + grid_size, grid_y + grid_size / 2 - i);
        SDL_RenderDrawLine(renderer, grid_x + grid_size / 2 - i, grid_y, grid_x + grid_size / 2 - i, grid_y + grid_size);
    }
}

void draw_filled_circle(SDL_Renderer* renderer, int x, int y, int radius) {
    for (int dy = -radius; dy <= radius; dy++) {
        int dx = sqrt(radius * radius - dy * dy);
        SDL_RenderDrawLine(renderer, x - dx, y + dy, x + dx, y + dy);
    }
}

void draw_outlined_circle(SDL_Renderer* renderer, int x, int y, int radius, int thickness) {
    int dx = radius;
    int dy = 0;
    int err = 0;
    while (dx >= dy) {
        for (int i = 0; i < thickness; i++) {
            SDL_RenderDrawPoint(renderer, x + dx - i, y + dy);
            SDL_RenderDrawPoint(renderer, x + dy, y + dx - i);
            SDL_RenderDrawPoint(renderer, x - dy, y + dx - i);
            SDL_RenderDrawPoint(renderer, x - dx + i, y + dy);
            SDL_RenderDrawPoint(renderer, x - dx + i, y - dy);
            SDL_RenderDrawPoint(renderer, x - dy, y - dx + i);
            SDL_RenderDrawPoint(renderer, x + dy, y - dx + i);
            SDL_RenderDrawPoint(renderer, x + dx - i, y - dy);
        }
        if (err <= 0) {
            dy += 1;
            err += 2 * dy + 1;
        }
        if (err > 0) {
            dx -= 1;
            err -= 2 * dx + 1;
        }
    }
}

// A function to render text to the screen, centered within a given rectangle
void draw_text(SDL_Renderer* renderer, TTF_Font* font, const char* text, int x, int y, SDL_Color color) {
    SDL_Surface* textSurface = TTF_RenderText_Blended(font, text, color);
    if (textSurface) {
        SDL_Texture* textTexture = SDL_CreateTextureFromSurface(renderer, textSurface);
        SDL_Rect textRect = {x, y, textSurface->w, textSurface->h};
        SDL_RenderCopy(renderer, textTexture, NULL, &textRect);
        SDL_DestroyTexture(textTexture);
        SDL_FreeSurface(textSurface);
    }
}

void draw_centered_text(SDL_Renderer* renderer, TTF_Font* font, const char* text, int y, SDL_Color color) {
    SDL_Surface* textSurface = TTF_RenderText_Blended_Wrapped(font, text, color, WINDOW_WIDTH - 200);
    if (textSurface) {
        SDL_Texture* textTexture = SDL_CreateTextureFromSurface(renderer, textSurface);
        SDL_Rect textRect = {(WINDOW_WIDTH - textSurface->w) / 2, y, textSurface->w, textSurface->h};
        SDL_RenderCopy(renderer, textTexture, NULL, &textRect);
        SDL_DestroyTexture(textTexture);
        SDL_FreeSurface(textSurface);
    }
}

void draw_confetti(SDL_Renderer* renderer, ConfettiParticle particle) {
    SDL_SetRenderDrawColor(renderer, particle.color.r, particle.color.g, particle.color.b, 255);
    SDL_Rect rect = {roundf(particle.x), roundf(particle.y), 5, 5};
    SDL_RenderFillRect(renderer, &rect);
}

void init_confetti(float x, float y) {
    SDL_Color colors[] = { {95, 215, 11, 255}, {114, 187, 255, 255}, {166, 255, 166, 255} };
    for (int i = 0; i < NUM_CONFETTI; ++i) {
        confetti[i].x = x;
        confetti[i].y = y;
        confetti[i].vx = (float)(rand() % (int)CONFETTI_SPREAD) - (CONFETTI_SPREAD / 2.0f);
        confetti[i].vy = (float)(rand() % (int)CONFETTI_SPREAD) - (CONFETTI_SPREAD / 2.0f);
        confetti[i].lifetime = CONFETTI_LIFETIME;
        confetti[i].color = colors[rand() % (sizeof(colors) / sizeof(colors[0]))];
    }
}

void update_confetti(float delta_time) {
    for (int i = 0; i < NUM_CONFETTI; ++i) {
        if (confetti[i].lifetime > 0) {
            confetti[i].x += confetti[i].vx * delta_time;
            confetti[i].y += confetti[i].vy * delta_time;
            confetti[i].vy += CONFETTI_GRAVITY * delta_time;
            confetti[i].lifetime -= delta_time;
        }
    }
}

// MODIFIED: A function to draw a thick line between two points using SDL_RenderGeometry with SDL_Vertex
void draw_thick_line(SDL_Renderer* renderer, float x1, float y1, float x2, float y2, int thickness, SDL_Color color) {
    float dx = x2 - x1;
    float dy = y2 - y1;
    float len = hypot(dx, dy);
    if (len == 0) return; // Avoid division by zero

    float nx = -dy / len; // Normal vector x
    float ny = dx / len;  // Normal vector y

    // Calculate half-thickness offsets
    float offset_x = nx * thickness / 2.0f;
    float offset_y = ny * thickness / 2.0f;

    SDL_Vertex vertices[4];

    // Define vertices for a rectangle
    vertices[0].position.x = x1 - offset_x;
    vertices[0].position.y = y1 - offset_y;
    vertices[0].color = color;
    vertices[0].tex_coord.x = 0; vertices[0].tex_coord.y = 0;

    vertices[1].position.x = x1 + offset_x;
    vertices[1].position.y = y1 + offset_y;
    vertices[1].color = color;
    vertices[1].tex_coord.x = 0; vertices[1].tex_coord.y = 0;

    vertices[2].position.x = x2 + offset_x;
    vertices[2].position.y = y2 + offset_y;
    vertices[2].color = color;
    vertices[2].tex_coord.x = 0; vertices[2].tex_coord.y = 0;

    vertices[3].position.x = x2 - offset_x;
    vertices[3].position.y = y2 - offset_y;
    vertices[3].color = color;
    vertices[3].tex_coord.x = 0; vertices[3].tex_coord.y = 0;
    
    // Define indices for the two triangles forming the rectangle (0,1,2 and 0,2,3)
    int indices[6] = {0, 1, 2, 0, 2, 3};

    SDL_RenderGeometry(renderer, NULL, vertices, 4, indices, 6);
}


void draw_hold_timer_bar(SDL_Renderer* renderer, int x, int y, int width, int height, float progress) {
    SDL_Rect bg_rect = {x, y, width, height};
    SDL_SetRenderDrawColor(renderer, 150, 150, 150, 255);
    SDL_RenderFillRect(renderer, &bg_rect);

    int filled_width = (int)(width * progress);
    SDL_Rect fill_rect = {x, y, filled_width, height};
    
    // Interpolate color from red to green based on progress
    int r = (int)(255 * (1.0f - progress));
    int g = (int)(255 * progress);
    SDL_SetRenderDrawColor(renderer, r, g, 0, 255);
    SDL_RenderFillRect(renderer, &fill_rect);
}

// MODIFIED: This function now draws a solid, thick, fading line.
void draw_line_trail(SDL_Renderer* renderer) {
    SDL_Color trail_color = {TRAIL_COLOR_R, TRAIL_COLOR_G, TRAIL_COLOR_B, 255}; 

    // Ensure TRAIL_LENGTH is at least 2 to prevent division by zero and ensure at least one segment can be drawn
    if (TRAIL_LENGTH < 2) {
        return; 
    }

    // Iterate backwards from the newest point (trail_head - 1)
    for (int i = 0; i < TRAIL_LENGTH - 1; ++i) {
        // Calculate the indices for the current segment (newest to oldest)
        int current_point_idx = (trail_head - 1 - i + TRAIL_LENGTH) % TRAIL_LENGTH;
        int next_point_idx = (trail_head - 1 - (i + 1) + TRAIL_LENGTH) % TRAIL_LENGTH;

        // Only draw if both points are valid (not at 0,0 default init)
        // This break prevents drawing lines from the origin during initial frames
        if ((trail_points[current_point_idx].x == 0 && trail_points[current_point_idx].y == 0) ||
            (trail_points[next_point_idx].x == 0 && trail_points[next_point_idx].y == 0)) {
            break; 
        }

        float alpha_ratio = 1.0f - ((float)i / (TRAIL_LENGTH - 1)); // Fade out from full alpha to 0
        Uint8 alpha = (Uint8)(255 * alpha_ratio);
        
        SDL_Color current_trail_color = trail_color;
        current_trail_color.a = alpha;

        draw_thick_line(renderer, 
                        trail_points[current_point_idx].x, trail_points[current_point_idx].y,
                        trail_points[next_point_idx].x, trail_points[next_point_idx].y,
                        TRAIL_THICKNESS, current_trail_color);
    }
}

/**
 * @brief Reads data from the Wii Balance Board and calculates CoB.
 * @return 0 on success, -1 on disconnection.
 */
int read_wii_balance_board_data(float *x_cob, float *y_cob) {
    if (iface == NULL || fd < 0) {
        printf("No interface or invalid fd\n");
        *x_cob = 0; *y_cob = 0;
        return -1;
    }

    struct pollfd fds[1];
    fds[0].fd = fd;
    fds[0].events = POLLIN;
    fds[0].revents = 0;

    int ret = poll(fds, 1, POLL_TIMEOUT_MS);
    if (ret < 0) {
        perror("Poll failed");
        return -1;
    }
    if (ret == 0) {
        poll_timeout_count++;
        if (poll_timeout_count >= POLL_TIMEOUT_THRESHOLD) {
            printf("Board timeout\n");
            return -1;
        }
        return 0;
    }

    poll_timeout_count = 0;
    int got_data = 0;
    while (xwii_iface_dispatch(iface, &event, sizeof(event)) == 0) {
        if (event.type == XWII_EVENT_BALANCE_BOARD) {
            // Use correct mapping: TL=2, TR=0, BL=3, BR=1
            float cells[4];
            cells[0] = event.v.abs[2].x / 100.0f; // TL
            cells[1] = event.v.abs[0].x / 100.0f; // TR
            cells[2] = event.v.abs[3].x / 100.0f; // BL
            cells[3] = event.v.abs[1].x / 100.0f; // BR
            float total_weight = cells[0] + cells[1] + cells[2] + cells[3];
            current_total_weight = total_weight * 100.0f;
            printf("BB RAW: TL=%.2f TR=%.2f BL=%.2f BR=%.2f SUM=%.2f\n", cells[0], cells[1], cells[2], cells[3], total_weight);
            if (current_total_weight > MIN_TOTAL_WEIGHT) {
                *x_cob = (cells[1] + cells[3] - cells[0] - cells[2]) * 100.0f;
                *y_cob = (cells[0] + cells[1] - cells[2] - cells[3]) * 100.0f;
                if (fabsf(*x_cob) < DEAD_ZONE) *x_cob = 0;
                if (fabsf(*y_cob) < DEAD_ZONE) *y_cob = 0;
                got_data = 1;
            }
        }
    }
    if (got_data) {
        printf("BB CoB: X=%.2f Y=%.2f Weight=%.2f\n", *x_cob, *y_cob, current_total_weight);
        return 0;
    } else {
        *x_cob = 0; *y_cob = 0;
        return 0;
    }
}

// --- Game Logic Functions ---
void init_player(PlayerObject *player) {
    player->x = WINDOW_WIDTH / 2.0f;
    player->y = WINDOW_HEIGHT / 2.0f;
    player->velocity_x = 0.0f;
    player->velocity_y = 0.0f;
    for (int i = 0; i < TRAIL_LENGTH; ++i) {
        // Initialize trail points to player's starting position
        trail_points[i].x = player->x;
        trail_points[i].y = player->y;
    }
    trail_head = 0;
}

void init_balance_hold_game(PlayerObject *player, TargetObject *target) {
    init_player(player);
    target->x = (float)(rand() % (WINDOW_WIDTH - GAME_OBJECT_SIZE * 2)) + GAME_OBJECT_SIZE;
    target->y = (float)(rand() % (WINDOW_HEIGHT - GAME_OBJECT_SIZE * 2)) + GAME_OBJECT_SIZE;
    float movement_speed;
    switch(current_difficulty) {
        case EASY: movement_speed = BH_TARGET_MOVEMENT_SPEED_EASY; break;
        case MEDIUM: movement_speed = BH_TARGET_MOVEMENT_SPEED_MEDIUM; break;
        case HARD: default: movement_speed = BH_TARGET_MOVEMENT_SPEED_HARD; break;
    }
    target->velocity_x = (rand() % 2 == 0) ? movement_speed : -movement_speed;
    target->velocity_y = (rand() % 2 == 0) ? movement_speed : -movement_speed;
    game_start_time = SDL_GetTicks();
    hold_timer = 0.0f;
    beeps_played = 0;
}

void init_coin_collector_game(PlayerObject *player) {
    init_player(player);
    for (int i = 0; i < current_game_target; ++i) {
        coin_collector_coins[i].active = 0;
    }
    
    // Spawn first coin far from player and not at edges
    int spawned = 0;
    while (!spawned) {
        float new_coin_x = (float)(rand() % (WINDOW_WIDTH - COIN_SAFE_MARGIN * 2)) + COIN_SAFE_MARGIN;
        float new_coin_y = (float)(rand() % (WINDOW_HEIGHT - COIN_SAFE_MARGIN * 2)) + COIN_SAFE_MARGIN;
        
        float dist_x = new_coin_x - player->x;
        float dist_y = new_coin_y - player->y;
        float distance = hypot(dist_x, dist_y);

        if (distance > COIN_SPAWN_MIN_DIST_PLAYER) {
            coin_collector_coins[0].active = 1;
            coin_collector_coins[0].x = new_coin_x;
            coin_collector_coins[0].y = new_coin_y;
            spawned = 1;
        }
    }

    game_start_time = SDL_GetTicks();
    coins = 0;
    // Only initialize timer in hard mode
    if (current_difficulty == HARD) {
        coin_timer = CC_COIN_TIMER;
    }
}

void init_dodge_game(PlayerObject *player) {
    init_player(player);
    for (int i = 0; i < MAX_DODGE_BLOCKS; i++) {
        dodge_blocks[i].active = 0;
    }
    block_spawn_timer = 0;
    current_block_speed = BLOCK_INITIAL_SPEED;
    dodge_score = 0;
    game_start_time = SDL_GetTicks();
}

void spawn_dodge_block() {
    for (int i = 0; i < MAX_DODGE_BLOCKS; i++) {
        if (!dodge_blocks[i].active) {
            dodge_blocks[i].active = 1;
            dodge_blocks[i].x = WINDOW_WIDTH + BLOCK_WIDTH;
            dodge_blocks[i].y = (float)(rand() % (WINDOW_HEIGHT - BLOCK_HEIGHT));
            dodge_blocks[i].speed = current_block_speed;
            break;
        }
    }
}

// Checks if the player is within a specified radius of the target's center.
int is_in_zone(PlayerObject player, TargetObject target, int zone_radius) {
    float dx = player.x - target.x;
    float dy = player.y - target.y;
    float distance = hypot(dx, dy);
    return distance <= zone_radius;
}

/**
 * @brief Callback function to play main music after an intro track finishes.
 */
void music_intro_finished_callback() {
    if (!Mix_PlayingMusic() && main_loop_music) {
        if (Mix_PlayMusic(main_loop_music, -1) == -1) {
            fprintf(stderr, "Failed to play main_loop.wav after intro: %s\n", Mix_GetError());
        }
    }
}

// Update player position with physics simulation
void update_player_position(PlayerObject* player, float target_x, float target_y, float delta_time) {
    float force_x = (target_x - player->x) * SPRING_CONSTANT;
    float force_y = (target_y - player->y) * SPRING_CONSTANT;
    force_x -= player->velocity_x * DAMPING_FACTOR;
    force_y -= player->velocity_y * DAMPING_FACTOR;
    player->velocity_x += force_x * delta_time;
    player->velocity_y += force_y * delta_time;
    player->x += player->velocity_x * delta_time;
    player->y += player->velocity_y * delta_time;
    if (player->x < GAME_OBJECT_SIZE/2) { player->x = GAME_OBJECT_SIZE/2; player->velocity_x = 0; }
    if (player->x > WINDOW_WIDTH - GAME_OBJECT_SIZE/2) { player->x = WINDOW_WIDTH - GAME_OBJECT_SIZE/2; player->velocity_x = 0; }
    if (player->y < GAME_OBJECT_SIZE/2) { player->y = GAME_OBJECT_SIZE/2; player->velocity_y = 0; }
    if (player->y > WINDOW_HEIGHT - GAME_OBJECT_SIZE/2) { player->y = WINDOW_HEIGHT - GAME_OBJECT_SIZE/2; player->velocity_y = 0; }
    trail_points[trail_head] = *player;
    trail_head = (trail_head + 1) % TRAIL_LENGTH;
}

// --- Main Program ---
int main(int argc, char* argv[]) {
    SDL_Window* window = NULL;
    SDL_Renderer* renderer = NULL;
    TTF_Font* font_score = NULL;
    TTF_Font* font_tutorial = NULL;
    TTF_Font* font_menu_title = NULL;
    TTF_Font* font_menu_description = NULL;
    
    // Declaring all variables at the beginning of main() to fix the compilation error
    int quit = 0;
    SDL_Event event_sdl;
    PlayerObject player;
    TargetObject target;
    float x_cob = 0.0, y_cob = 0.0;
    GameState state = CONNECTING;
    Uint32 last_frame_time = 0;
    Uint32 last_input_time = 0;
    float pulse_timer = 0.0f;
    SDL_Color start_color, end_color, textColor;
    SDL_Rect viewport_rect;
    int text_w, text_h;

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0) {
        fprintf(stderr, "SDL could not initialize! SDL_Error: %s\n", SDL_GetError()); return 1;
    }
    if (TTF_Init() == -1) {
        fprintf(stderr, "SDL_ttf could not initialize! TTF_Error: %s\n", TTF_GetError()); SDL_Quit(); return 1;
    }
    if (Mix_Init(MIX_INIT_MP3 | MIX_INIT_OGG) != (MIX_INIT_MP3 | MIX_INIT_OGG)) {
        fprintf(stderr, "SDL_mixer could not initialize! Mix_Error: %s\n", Mix_GetError());
    }
    if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 2048) < 0) {
        fprintf(stderr, "SDL_mixer could not open audio! Mix_Error: %s\n", Mix_GetError());
    }
    if (IMG_Init(IMG_INIT_PNG | IMG_INIT_JPG) == 0) {
        fprintf(stderr, "SDL_image could not initialize! IMG_Error: %s\n", IMG_GetError());
        Mix_CloseAudio(); TTF_Quit(); SDL_Quit(); return 1;
    }

    coin_sound = Mix_LoadWAV("coin.mp3");
    win_sound = Mix_LoadWAV("win.mp3");
    select_sound = Mix_LoadWAV("select.mp3");
    target_sound = Mix_LoadWAV("target.wav");
    reset_sound = Mix_LoadWAV("reset.wav"); // NEW: Load reset sound
    connection_intro_music = Mix_LoadMUS("connection_intro.wav");
    connection_main_music = Mix_LoadMUS("connection_main.wav");
    transition_music = Mix_LoadMUS("transition.wav");
    main_intro_music = Mix_LoadMUS("main_intro.wav");
    main_loop_music = Mix_LoadMUS("main_loop.wav");

    if (!coin_sound || !win_sound || !select_sound || !target_sound || !reset_sound || !connection_intro_music || !connection_main_music || !transition_music || !main_intro_music || !main_loop_music) {
        fprintf(stderr, "One or more audio files failed to load. Please check file paths. Mix_Error: %s\n", Mix_GetError());
    }

    // Set callback for intro music ending
    Mix_HookMusicFinished(music_intro_finished_callback);

    if (connection_intro_music && Mix_PlayMusic(connection_intro_music, 0) == -1) {
        fprintf(stderr, "Failed to play connection_intro.wav: %s\n", Mix_GetError());
    }

    window = SDL_CreateWindow("Wii Fit Balance Game", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, WINDOW_WIDTH, WINDOW_HEIGHT, SDL_WINDOW_FULLSCREEN_DESKTOP | SDL_WINDOW_SHOWN);
    if (!window) { fprintf(stderr, "Window could not be created! SDL_Error: %s\n", SDL_GetError()); goto cleanup; }
    // Enable vsync for smoother rendering
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) { fprintf(stderr, "Renderer could not be created! SDL_Error: %s\n", SDL_GetError()); goto cleanup; }
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_ShowCursor(SDL_DISABLE);

    boardpower_texture = IMG_LoadTexture(renderer, "boardpower.png");
    coin_texture = IMG_LoadTexture(renderer, "coin.png");
    for (int i = 0; i < num_players; i++) {
        player_textures[i] = IMG_LoadTexture(renderer, available_players[i].image_path);
        if (!player_textures[i]) {
            fprintf(stderr, "Failed to load player texture: %s. IMG_Error: %s\n", available_players[i].image_path, IMG_GetError());
        }
    }
    if (!boardpower_texture || !coin_texture) {
        fprintf(stderr, "Failed to load one or more image textures. IMG_Error: %s\n", IMG_GetError());
    }

    font_score = TTF_OpenFont("shingom.otf", TITLE_FONT_SIZE);
    font_tutorial = TTF_OpenFont("shingom.otf", TUTORIAL_FONT_SIZE);
    font_menu_title = TTF_OpenFont("shingom.otf", MENU_TITLE_FONT_SIZE);
    font_menu_description = TTF_OpenFont("shingom.otf", MENU_DESCRIPTION_FONT_SIZE);
    if (!font_score || !font_tutorial || !font_menu_title || !font_menu_description) {
        fprintf(stderr, "Failed to load font 'shingom.otf'! TTF_Error: %s\n", TTF_GetError());
    }

    // Initialize with default player (will be updated when player is selected)
    lowest_time_to_win = -1.0f;
    total_wins = 0;
    last_frame_time = SDL_GetTicks();
    last_input_time = SDL_GetTicks();
    connection_start_time = SDL_GetTicks();
    init_player(&player);

    while (!quit) {
        Uint32 frame_start = SDL_GetTicks();
        
        Uint32 current_time = SDL_GetTicks();
        float delta_time = (float)(current_time - last_frame_time) / 1000.0f;
        last_frame_time = current_time;

        while (SDL_PollEvent(&event_sdl) != 0) {
            if (event_sdl.type == SDL_QUIT) quit = 1;
            if (event_sdl.type == SDL_KEYDOWN) {
                if (event_sdl.key.keysym.sym == SDLK_ESCAPE) quit = 1;
            }
        }

        float hold_progress = 0.0f;
        float pulse_scale = 1.0f;
        int render_offset_x = 0;
        int render_offset_y = 0;

        // --- Game Logic based on State ---
        // DEBUG: Print CoB and weight every frame
        printf("DEBUG: x_cob=%.2f y_cob=%.2f total_weight=%.2f\n", x_cob, y_cob, current_total_weight);
        if (state != CONNECTING && read_wii_balance_board_data(&x_cob, &y_cob) != 0) {
            // Disconnection detected
            reset_game_state();
            state = CONNECTING;
            connection_start_time = SDL_GetTicks(); // Reset connection timer
            if (connection_intro_music && Mix_PlayMusic(connection_intro_music, 0) == -1) {
                 fprintf(stderr, "Failed to play connection_intro.wav: %s\n", Mix_GetError());
            }
            continue;
        }

        // Handle inactivity timeout - check for minimal weight or activity
        if (state != CONNECTING && state != TRANSITIONING && current_total_weight < MIN_TOTAL_WEIGHT) {
            if (current_time - last_input_time > INACTIVITY_TIMEOUT_SECONDS * 1000) {
                printf("Inactivity timeout. Returning to connecting screen.\n");
                reset_game_state();
                state = CONNECTING;
                connection_start_time = SDL_GetTicks(); // Reset connection timer
                if (connection_intro_music && Mix_PlayMusic(connection_intro_music, 0) == -1) {
                     fprintf(stderr, "Failed to play connection_intro.wav: %s\n", Mix_GetError());
                }
            }
        } else {
            last_input_time = current_time;
        }

        switch (state) {
            case CONNECTING:
                if (Mix_PlayingMusic() == 0) {
                    if (connection_intro_music && Mix_PlayMusic(connection_intro_music, 0) == -1) {
                        fprintf(stderr, "Failed to play connection_intro.wav: %s\n", Mix_GetError());
                    }
                }
                if (init_xwiimote_non_blocking() == 0) {
                    state = TRANSITIONING;
                    Mix_HaltMusic();
                    if (transition_music && Mix_PlayMusic(transition_music, 0) == -1) {
                         fprintf(stderr, "Failed to play transition.wav: %s\n", Mix_GetError());
                    }
                    transition_start_time = SDL_GetTicks();
                }
                break;

            case TRANSITIONING:
                {
                    float elapsed = (float)(SDL_GetTicks() - transition_start_time) / 1000.0f;
                    if (elapsed >= TRANSITION_DURATION) {
                        state = PLAYER_SELECTION; // Go to player selection after transition
                        Mix_HaltMusic();
                        if (main_intro_music && Mix_PlayMusic(main_intro_music, 0) == -1) {
                            fprintf(stderr, "Failed to play main_intro.wav: %s\n", Mix_GetError());
                        }
                    } else {
                        float shake_progress = elapsed / TRANSITION_DURATION;
                        shake_intensity = (shake_progress < 0.5f) ? (shake_progress * 2.0f * 20.0f) : ((1.0f - shake_progress) * 2.0f * 20.0f);
                        render_offset_x = (rand() % (int)(shake_intensity + 1)) - (shake_intensity / 2);
                        render_offset_y = (rand() % (int)(shake_intensity + 1)) - (shake_intensity / 2);
                    }
                }
                break;

            case PLAYER_SELECTION:
                if (!Mix_PlayingMusic()) {
                    if (main_loop_music && Mix_PlayMusic(main_loop_music, -1) == -1) {
                        fprintf(stderr, "Failed to play main_loop.wav: %s\n", Mix_GetError());
                    }
                }
                int prev_player_selection_choice = player_selection_choice;
                player_selection_choice = 0;
                if (current_total_weight > MIN_TOTAL_WEIGHT) {
                    if (x_cob < -200) player_selection_choice = 1; // Left
                    else if (fabsf(x_cob) < 150) player_selection_choice = 2; // Center
                    else if (x_cob > 200) player_selection_choice = 3; // Right
                }

                if (player_selection_choice != prev_player_selection_choice) {
                    menu_select_timer = 0.0f;
                }
                if (player_selection_choice != 0) {
                    menu_select_timer += delta_time;
                }

                if (menu_select_timer >= MENU_SELECT_TIME_REQUIRED) {
                    selected_player_index = player_selection_choice - 1;
                    // Load profile-specific save data
                    lowest_time_to_win = read_lowest_time(get_profile_filename("score.txt", selected_player_index));
                    total_wins = read_total_wins(get_profile_filename("wins.txt", selected_player_index));
                    state = MAIN_MENU;
                    menu_select_timer = 0.0f;
                    Mix_PlayChannel(-1, select_sound, 0);
                }
                break;

            case MAIN_MENU:
                if (!Mix_PlayingMusic()) {
                    if (main_loop_music && Mix_PlayMusic(main_loop_music, -1) == -1) {
                        fprintf(stderr, "Failed to play main_loop.wav: %s\n", Mix_GetError());
                    }
                }

                GameType prev_selected_game = selected_game;
                selected_game = NO_GAME_SELECTED;
                if (current_total_weight > MIN_TOTAL_WEIGHT) {
                    if (x_cob < -200) selected_game = BALANCE_HOLD;
                    else if (fabsf(x_cob) < 150) selected_game = DODGE;
                    else if (x_cob > 200) selected_game = COIN_COLLECTOR;
                }

                if (selected_game != prev_selected_game) {
                    menu_select_timer = 0.0f;
                }
                if (selected_game != NO_GAME_SELECTED) {
                    menu_select_timer += delta_time;
                }

                if (menu_select_timer >= MENU_SELECT_TIME_REQUIRED) {
                    if (selected_game == DODGE) {
                        state = GAME_DODGE;
                        dodge_high_score = read_dodge_high_score(get_profile_filename("dodge_score.txt", selected_player_index));
                        init_dodge_game(&player);
                    } else {
                        state = DIFFICULTY_SELECTION;
                    }
                    menu_select_timer = 0.0f;
                    Mix_PlayChannel(-1, select_sound, 0);
                }
                break;

            case DIFFICULTY_SELECTION:
                if (!Mix_PlayingMusic()) {
                    if (main_loop_music && Mix_PlayMusic(main_loop_music, -1) == -1) {
                        fprintf(stderr, "Failed to play main_loop.wav: %s\n", Mix_GetError());
                    }
                }

                int prev_difficulty_selection = difficulty_selection;
                difficulty_selection = 0;
                if (current_total_weight > MIN_TOTAL_WEIGHT) {
                    if (x_cob < -200) difficulty_selection = 1;
                    else if (fabsf(x_cob) < 150) difficulty_selection = 2;
                    else if (x_cob > 200) difficulty_selection = 3;
                }

                if (difficulty_selection != prev_difficulty_selection) {
                    menu_select_timer = 0.0f;
                }
                if (difficulty_selection != 0) {
                    menu_select_timer += delta_time;
                }

                if (menu_select_timer >= MENU_SELECT_TIME_REQUIRED) {
                    switch(difficulty_selection) {
                        case 1: current_difficulty = EASY; break;
                        case 2: current_difficulty = MEDIUM; break;
                        case 3: current_difficulty = HARD; break;
                    }
                    if (selected_game == BALANCE_HOLD) {
                        state = GAME_BALANCE_HOLD;
                        current_game_target = (current_difficulty == EASY) ? 10 : (current_difficulty == HARD) ? 25 : 15;
                        init_balance_hold_game(&player, &balance_hold_target); // Use the new target for Balance Hold
                        coins = 0; // Reset coins for a new game
                    } else if (selected_game == COIN_COLLECTOR) {
                        state = GAME_COIN_COLLECTOR;
                        current_game_target = (current_difficulty == EASY) ? 15 : (current_difficulty == HARD) ? 30 : 20;
                        init_coin_collector_game(&player);
                        coins = 0; // Reset coins for a new game
                    } else if (selected_game == DODGE) {
                        state = GAME_DODGE;
                        init_dodge_game(&player);
                    }
                    menu_select_timer = 0.0f;
                    Mix_PlayChannel(-1, select_sound, 0);
                }
                break;

            case GAME_BALANCE_HOLD:
            case GAME_COIN_COLLECTOR:
                // Calculate target position for movement using COB_SCALE_GENERAL
                float target_x_general = (WINDOW_WIDTH / 2.0f) + x_cob * COB_SCALE_GENERAL * WINDOW_WIDTH;
                float target_y_general = (WINDOW_HEIGHT / 2.0f) + y_cob * -COB_SCALE_GENERAL * WINDOW_HEIGHT;

                if (!Mix_PlayingMusic()) {
                    if (main_loop_music && Mix_PlayMusic(main_loop_music, -1) == -1) {
                        fprintf(stderr, "Failed to play main_loop.wav: %s\n", Mix_GetError());
                    }
                }
                // Update player movement
                update_player_position(&player, target_x_general, target_y_general, delta_time);

                if (state == GAME_BALANCE_HOLD) {
                    // Update target position based on difficulty
                    balance_hold_target.x += balance_hold_target.velocity_x * delta_time;
                    balance_hold_target.y += balance_hold_target.velocity_y * delta_time;
                    // Bounce off walls
                    if (balance_hold_target.x < BH_GRACE_ZONE_RADIUS || balance_hold_target.x > WINDOW_WIDTH - BH_GRACE_ZONE_RADIUS) {
                        balance_hold_target.velocity_x *= -1;
                    }
                    if (balance_hold_target.y < BH_GRACE_ZONE_RADIUS || balance_hold_target.y > WINDOW_HEIGHT - BH_GRACE_ZONE_RADIUS) {
                        balance_hold_target.velocity_y *= -1;
                    }

                    // Score counting logic
                    if (is_in_zone(player, balance_hold_target, BH_HOLD_RADIUS)) {
                        hold_timer += delta_time;
                    } else {
                        hold_timer = 0;
                        beeps_played = 0;
                        Mix_PlayChannel(-1, reset_sound, 0);
                    }
                    
                    hold_progress = hold_timer / BH_HOLD_TIME_REQUIRED;
                    if (hold_progress > 1.0f) hold_progress = 1.0f;
                    
                    if (hold_timer >= BH_HOLD_TIME_REQUIRED) {
                        coins++;
                        if (target_sound) Mix_PlayChannel(-1, target_sound, 0);
                        if (coins >= current_game_target) {
                            state = WINNING;
                        } else {
                            init_balance_hold_game(&player, &balance_hold_target);
                        }
                    }

                    pulse_timer += delta_time;
                    pulse_scale = 1.0f + 0.3f * sinf(pulse_timer * BH_TARGET_PULSE_SPEED);
                } else { // GAME_COIN_COLLECTOR
                    // Only check timer in hard mode
                    if (current_difficulty == HARD) {
                        coin_timer -= delta_time;
                        if (coin_timer <= 0) {
                            // Game over, return to menu
                            printf("Time's up! Returning to menu.\n");
                            reset_game_state();
                            state = MAIN_MENU;
                            continue;
                        }
                    }

                    for (int i = 0; i < current_game_target; ++i) {
                        if (coin_collector_coins[i].active) {
                             // Adjust coin hitbox size to make it easier to collect
                             if (is_in_zone(player, (TargetObject){coin_collector_coins[i].x, coin_collector_coins[i].y, 0, 0}, STARTING_COIN_SIZE * 1.2)) {
                                coin_collector_coins[i].active = 0;
                                coins++;
                                Mix_PlayChannel(-1, coin_sound, 0);
                                if (coins < current_game_target) {
                                    int next_coin_index = (i + 1);
                                    // Spawn next coin far from player and not at edges
                                    int spawned_next_coin = 0;
                                    while (!spawned_next_coin) {
                                        float new_coin_x = (float)(rand() % (WINDOW_WIDTH - COIN_SAFE_MARGIN * 2)) + COIN_SAFE_MARGIN;
                                        float new_coin_y = (float)(rand() % (WINDOW_HEIGHT - COIN_SAFE_MARGIN * 2)) + COIN_SAFE_MARGIN;
                                        
                                        float dist_x = new_coin_x - player.x;
                                        float dist_y = new_coin_y - player.y;
                                        float distance = hypot(dist_x, dist_y);

                                        if (distance > COIN_SPAWN_MIN_DIST_PLAYER) {
                                            coin_collector_coins[next_coin_index].active = 1;
                                            coin_collector_coins[next_coin_index].x = new_coin_x;
                                            coin_collector_coins[next_coin_index].y = new_coin_y;
                                            spawned_next_coin = 1;
                                        }
                                    }

                                    if (current_difficulty == HARD) {
                                        coin_timer = CC_COIN_TIMER; // Reset timer only in hard mode
                                    }
                                } else {
                                    state = WINNING;
                                }
                            }
                        }
                    }
                }

                if (state == WINNING) {
                    Mix_HaltChannel(-1);
                    Mix_PlayChannel(-1, win_sound, 0);
                    win_message_start_time = SDL_GetTicks();
                    float win_time = (float)(win_message_start_time - game_start_time) / 1000.0f;
                    if (lowest_time_to_win == -1.0f || win_time < lowest_time_to_win) {
                        lowest_time_to_win = win_time;
                        write_lowest_time(get_profile_filename("score.txt", selected_player_index), lowest_time_to_win);
                    }
                    total_wins++; // NEW: Increment total wins
                    write_total_wins(get_profile_filename("wins.txt", selected_player_index), total_wins); // NEW: Save total wins
                    init_confetti(player.x, player.y);
                }
                break;

            case GAME_DODGE:
                if (state != GAME_DODGE) break;
                
                // Calculate target position for movement using COB_SCALE_DODGE
                float target_x_dodge = (WINDOW_WIDTH / 2.0f) + x_cob * COB_SCALE_DODGE * WINDOW_WIDTH;
                float target_y_dodge = (WINDOW_HEIGHT / 2.0f) + y_cob * -COB_SCALE_DODGE * WINDOW_HEIGHT;

                // Only update player movement if there's actual input
                if (current_total_weight > MIN_TOTAL_WEIGHT && (fabsf(x_cob) > DEAD_ZONE || fabsf(y_cob) > DEAD_ZONE)) {
                    update_player_position(&player, target_x_dodge, target_y_dodge, delta_time);
                }
                
                // Enhanced Dodge mode logic
                for (int i = 0; i < MAX_DODGE_BLOCKS; i++) {
                    if (dodge_blocks[i].active) {
                        dodge_blocks[i].x -= dodge_blocks[i].speed * delta_time;
                        if (dodge_blocks[i].x + BLOCK_WIDTH < 0) {
                            dodge_blocks[i].active = 0;
                            dodge_score++;
                            if (dodge_score > dodge_high_score) {
                                dodge_high_score = dodge_score;
                                write_dodge_high_score(get_profile_filename("dodge_score.txt", selected_player_index), dodge_high_score);
                            }
                        }
                        SDL_Rect block_rect = {
                            (int)dodge_blocks[i].x,
                            (int)dodge_blocks[i].y,
                            BLOCK_WIDTH,
                            BLOCK_HEIGHT
                        };
                        SDL_Rect player_rect = {
                            (int)(player.x - GAME_OBJECT_SIZE / 2),
                            (int)(player.y - GAME_OBJECT_SIZE / 2),
                            GAME_OBJECT_SIZE,
                            GAME_OBJECT_SIZE
                        };
                        if (SDL_HasIntersection(&block_rect, &player_rect)) {
                            state = WINNING;
                            Mix_PlayChannel(-1, reset_sound, 0);
                            break;
                        }
                    }
                }
                block_spawn_timer += delta_time;
                if (block_spawn_timer >= dynamic_block_spawn_interval) {
                    spawn_dodge_block();
                    block_spawn_timer = 0;
                }
                current_block_speed += BLOCK_SPEED_INCREMENT * delta_time;
                dynamic_block_spawn_interval = fmax(0.5f, dynamic_block_spawn_interval - (0.01f * delta_time));
                break;

            case WINNING:
                update_confetti(delta_time);
                
                if (SDL_GetTicks() - win_message_start_time > WIN_ANIMATION_DURATION) {
                    // Handle game-specific cleanup
                    if (dodge_score > 0) {
                        block_spawn_timer = 0;
                        current_block_speed = BLOCK_INITIAL_SPEED;
                        dynamic_block_spawn_interval = BLOCK_SPAWN_INTERVAL;
                        for (int i = 0; i < MAX_DODGE_BLOCKS; i++) {
                            dodge_blocks[i].active = 0;
                        }
                        dodge_score = 0;
                    }
                    
                    reset_game_state();
                    state = PLAYER_SELECTION;
                    init_player(&player);
                }
                break;
        }

        // --- Performance Optimizations ---
        // Optimized debug output
        static int debug_frame_counter = 0;
        if (++debug_frame_counter >= DEBUG_INTERVAL) {
            snprintf(debug_buffer, sizeof(debug_buffer),
                    "DEBUG: x_cob=%.2f y_cob=%.2f weight=%.2f fps=%.1f\n",
                    x_cob, y_cob, current_total_weight, 1000.0f / delta_time);
            fputs(debug_buffer, stdout);
            debug_frame_counter = 0;
        }

        // Render only if the window is visible
        if (!(SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED)) {
            SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
            SDL_RenderClear(renderer);

            if (state == GAME_BALANCE_HOLD) { start_color = (SDL_Color){200, 255, 200, 255}; end_color = (SDL_Color){100, 200, 100, 255}; } // Green Gradient
            else if (state == GAME_COIN_COLLECTOR) { start_color = (SDL_Color){255, 255, 255, 255}; end_color = (SDL_Color){173, 216, 230, 255}; } // Blue/White Gradient
            else if (state == GAME_DODGE) { start_color = (SDL_Color){40, 40, 40, 255}; end_color = (SDL_Color){0, 0, 0, 255}; } // Black Gradient
            else { start_color = (SDL_Color){240, 240, 240, 255}; end_color = (SDL_Color){200, 200, 200, 255}; } // Default gray gradient
            draw_gradient_background(renderer, start_color, end_color);

            viewport_rect = (state == TRANSITIONING) ? (SDL_Rect){render_offset_x, render_offset_y, WINDOW_WIDTH, WINDOW_HEIGHT} : (SDL_Rect){0, 0, WINDOW_WIDTH, WINDOW_HEIGHT};
            SDL_RenderSetViewport(renderer, &viewport_rect);

            switch(state) {
                case CONNECTING:
                    textColor = (SDL_Color){FONT_COLOR_R, FONT_COLOR_G, FONT_COLOR_B, 255};
                    // Adjust connecting text and image positions
                    if (boardpower_texture) {
                        int img_width = 846;
                        int img_height = 462;
                        int img_x = (WINDOW_WIDTH - img_width) / 2;
                        int img_y = (WINDOW_HEIGHT / 2) - 250; // Updated y position for image
                        SDL_Rect rect = {img_x, img_y, img_width, img_height};
                        SDL_RenderCopy(renderer, boardpower_texture, NULL, &rect);
                    }

                    // Draw connecting text below the image
                    draw_centered_text(renderer, font_tutorial, "Connecting to Wii Balance Board...", (WINDOW_HEIGHT / 2) + 250, textColor);
                    break;
                case TRANSITIONING:
                    // Background shake is handled by viewport
                    break;
                case PLAYER_SELECTION:
                    textColor = (SDL_Color){FONT_COLOR_R, FONT_COLOR_G, FONT_COLOR_B, 255};
                    draw_centered_text(renderer, font_menu_title, "Select Player", 150, textColor);

                    int base_y = WINDOW_HEIGHT / 2 + 100;
                    int positions[] = {WINDOW_WIDTH / 4, WINDOW_WIDTH / 2, WINDOW_WIDTH * 3 / 4};
                    const char* instructions[] = {"Lean Left", "Stay Centered", "Lean Right"};

                    for (int i = 0; i < num_players; i++) {
                        int text_width, text_height;

                        if (player_textures[i]) {
                            SDL_Rect img_rect = {positions[i] - 75, base_y - 200, 150, 150};
                            SDL_RenderCopy(renderer, player_textures[i], NULL, &img_rect);
                        }

                        TTF_SizeText(font_menu_title, available_players[i].name, &text_width, &text_height);
                        int x_pos = positions[i] - (text_width / 2);

                        if (player_selection_choice == (i + 1)) {
                            textColor = (SDL_Color){255, 255, 255, (Uint8)(128 + 127 * sin(menu_select_timer * 10))};
                        } else {
                            textColor = (SDL_Color){FONT_COLOR_R, FONT_COLOR_G, FONT_COLOR_B, 255};
                        }
                        draw_text(renderer, font_menu_title, available_players[i].name, x_pos, base_y, textColor);

                        TTF_SizeText(font_menu_description, instructions[i], &text_w, &text_h);
                        x_pos = positions[i] - (text_w / 2);
                        draw_text(renderer, font_menu_description, instructions[i], x_pos, base_y + 80, (SDL_Color){FONT_COLOR_R, FONT_COLOR_G, FONT_COLOR_B, 255});
                    }
                    break;
                case MAIN_MENU:
                    textColor = (SDL_Color){FONT_COLOR_R, FONT_COLOR_G, FONT_COLOR_B, 255};
                    draw_centered_text(renderer, font_menu_title, "Select Game", 150, textColor);

                    int menu_base_y = WINDOW_HEIGHT / 2;
                    int menu_x_left = WINDOW_WIDTH / 4;
                    int menu_x_center = WINDOW_WIDTH / 2;
                    int menu_x_right = WINDOW_WIDTH * 3 / 4;
                    
                    // Balance Hold Option
                    TTF_SizeText(font_menu_title, "Balance Hold", &text_w, &text_h);
                    textColor = (selected_game == BALANCE_HOLD) ? (SDL_Color){255, 255, 255, (Uint8)(128 + 127 * sin(menu_select_timer * 10))} : (SDL_Color){FONT_COLOR_R, FONT_COLOR_G, FONT_COLOR_B, 255};
                    draw_text(renderer, font_menu_title, "Balance Hold", menu_x_left - text_w/2, menu_base_y, textColor);
                    TTF_SizeText(font_menu_description, "Lean left to select.", &text_w, &text_h);
                    draw_text(renderer, font_menu_description, "Lean left to select.", menu_x_left - text_w/2, menu_base_y + 80, (SDL_Color){FONT_COLOR_R, FONT_COLOR_G, FONT_COLOR_B, 255});

                    // Dodge Option
                    TTF_SizeText(font_menu_title, "Dodge", &text_w, &text_h);
                    textColor = (selected_game == DODGE) ? (SDL_Color){255, 255, 255, (Uint8)(128 + 127 * sin(menu_select_timer * 10))} : (SDL_Color){FONT_COLOR_R, FONT_COLOR_G, FONT_COLOR_B, 255};
                    draw_text(renderer, font_menu_title, "Dodge", menu_x_center - text_w/2, menu_base_y, textColor);
                    TTF_SizeText(font_menu_description, "Stay centered to select.", &text_w, &text_h);
                    draw_text(renderer, font_menu_description, "Stay centered to select.", menu_x_center - text_w/2, menu_base_y + 80, (SDL_Color){FONT_COLOR_R, FONT_COLOR_G, FONT_COLOR_B, 255});

                    // Coin Collector Option
                    TTF_SizeText(font_menu_title, "Coin Collector", &text_w, &text_h);
                    textColor = (selected_game == COIN_COLLECTOR) ? (SDL_Color){255, 255, 255, (Uint8)(128 + 127 * sin(menu_select_timer * 10))} : (SDL_Color){FONT_COLOR_R, FONT_COLOR_G, FONT_COLOR_B, 255};
                    draw_text(renderer, font_menu_title, "Coin Collector", menu_x_right - text_w/2, menu_base_y, textColor);
                    TTF_SizeText(font_menu_description, "Lean right to select.", &text_w, &text_h);
                    draw_text(renderer, font_menu_description, "Lean right to select.", menu_x_right - text_w/2, menu_base_y + 80, (SDL_Color){FONT_COLOR_R, FONT_COLOR_G, FONT_COLOR_B, 255});

                    // NEW: Display total wins
                    char total_wins_text[50];
                    snprintf(total_wins_text, 50, "Total Wins: %d", total_wins);
                    draw_centered_text(renderer, font_menu_description, total_wins_text, menu_base_y + 200, (SDL_Color){FONT_COLOR_R, FONT_COLOR_G, FONT_COLOR_B, 255});
                    break;
                case DIFFICULTY_SELECTION:
                    textColor = (SDL_Color){FONT_COLOR_R, FONT_COLOR_G, FONT_COLOR_B, 255};
                    draw_centered_text(renderer, font_menu_title, "Select Difficulty", 150, textColor);

                    int diff_base_y = WINDOW_HEIGHT / 2;
                    int diff_positions[] = {WINDOW_WIDTH / 4, WINDOW_WIDTH / 2, WINDOW_WIDTH * 3 / 4};
                    const char* difficulties[] = {"Easy", "Medium", "Hard"};
                    const char* diff_instructions[] = {"Lean Left", "Stay Centered", "Lean Right"};

                    for (int i = 0; i < 3; i++) {
                        int text_width, text_height;
                        TTF_SizeText(font_menu_title, difficulties[i], &text_width, &text_height);
                        int x_pos = diff_positions[i] - (text_width / 2);
                        
                        textColor = (difficulty_selection == (i + 1)) ? (SDL_Color){255, 255, 255, (Uint8)(128 + 127 * sin(menu_select_timer * 10))} : (SDL_Color){FONT_COLOR_R, FONT_COLOR_G, FONT_COLOR_B, 255};
                        
                        // Draw the difficulty text centered at its position
                        draw_text(renderer, font_menu_title, difficulties[i], x_pos, diff_base_y, textColor);
                        
                        // Draw the instruction text below, also centered
                        TTF_SizeText(font_menu_description, diff_instructions[i], &text_width, &text_height);
                        x_pos = diff_positions[i] - (text_w / 2);
                        draw_text(renderer, font_menu_description, diff_instructions[i], x_pos, diff_base_y + 80, (SDL_Color){FONT_COLOR_R, FONT_COLOR_G, FONT_COLOR_B, 255});
                    }
                    break;
                case GAME_BALANCE_HOLD:
                case GAME_COIN_COLLECTOR:
                    draw_line_trail(renderer); // MODIFIED: Call the new line trail function
                    draw_middle_grid(renderer);

                    if (state == GAME_BALANCE_HOLD) {
                        // Draw the larger, semi-transparent "grace zone" circle
                        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 50); // Semi-transparent white
                        draw_filled_circle(renderer, roundf(balance_hold_target.x), roundf(balance_hold_target.y), BH_GRACE_ZONE_RADIUS);

                        // Draw the solid inner target circle
                        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
                        draw_filled_circle(renderer, roundf(balance_hold_target.x), roundf(balance_hold_target.y), roundf(BH_HOLD_RADIUS * pulse_scale));

                        // Draw a pulsating outline that shows hold progress
                        SDL_SetRenderDrawColor(renderer, 95, 215, 11, 255); // Changed to solid green
                        draw_outlined_circle(renderer, roundf(balance_hold_target.x), roundf(balance_hold_target.y), roundf(BH_HOLD_RADIUS * (1.0f + 0.5f * hold_progress)), 5);


                    } else { // GAME_COIN_COLLECTOR
                        for (int i = 0; i < current_game_target; ++i) {
                            if (coin_collector_coins[i].active) {
                                 SDL_SetRenderDrawColor(renderer, 255, 215, 0, 255); // Gold color for coins
                                 SDL_Rect coin_rect = {roundf(coin_collector_coins[i].x - STARTING_COIN_SIZE/2), roundf(coin_collector_coins[i].y - STARTING_COIN_SIZE/2), STARTING_COIN_SIZE, STARTING_COIN_SIZE};
                                 if (coin_texture) {
                                     SDL_RenderCopy(renderer, coin_texture, NULL, &coin_rect);
                                 } else {
                                     SDL_SetRenderDrawColor(renderer, 255, 215, 0, 255);
                                     draw_filled_circle(renderer, roundf(coin_collector_coins[i].x), roundf(coin_collector_coins[i].y), STARTING_COIN_SIZE/2);
                                 }
                            }
                        }
                    }

                    // Draw Player
                    if (selected_player_index != -1 && player_textures[selected_player_index]) {
                        SDL_Rect player_rect = {roundf(player.x - GAME_OBJECT_SIZE / 2.0f), roundf(player.y - GAME_OBJECT_SIZE / 2.0f), GAME_OBJECT_SIZE, GAME_OBJECT_SIZE};
                        SDL_RenderCopy(renderer, player_textures[selected_player_index], NULL, &player_rect);
                    } else {
                        // MODIFIED: Draw player with the new color #D45351
                        SDL_SetRenderDrawColor(renderer, TRAIL_COLOR_R, TRAIL_COLOR_G, TRAIL_COLOR_B, 255);
                        draw_filled_circle(renderer, roundf(player.x), roundf(player.y), GAME_OBJECT_SIZE / 2);
                    }

                    char score_text[50];
                    snprintf(score_text, 50, state == GAME_BALANCE_HOLD ? "Targets: %d/%d" : "Coins: %d/%d", coins, current_game_target);
                    textColor = (SDL_Color){FONT_COLOR_R, FONT_COLOR_G, FONT_COLOR_B, 255};
                    draw_text(renderer, font_score, score_text, 50, 50, textColor);

                    if (state == GAME_BALANCE_HOLD) {
                        draw_hold_timer_bar(renderer, (WINDOW_WIDTH - BH_HOLD_BAR_WIDTH) / 2, 50, BH_HOLD_BAR_WIDTH, BH_HOLD_BAR_HEIGHT, hold_progress);
                    } else if (state == GAME_COIN_COLLECTOR) {
                        // Only show timer in hard mode
                        if (current_difficulty == HARD) {
                            char timer_text[50];
                            snprintf(timer_text, 50, "Time Left: %.1f", coin_timer > 0 ? coin_timer : 0);
                            draw_centered_text(renderer, font_score, timer_text, 100, textColor);
                        }
                    }
                    break;
                case WINNING:
                    for (int i = 0; i < NUM_CONFETTI; ++i) {
                        if (confetti[i].lifetime > 0) draw_confetti(renderer, confetti[i]);
                    }
                    textColor = (SDL_Color){FONT_COLOR_R, FONT_COLOR_G, FONT_COLOR_B, 255};
                    draw_centered_text(renderer, font_menu_title, "You Win!", WINDOW_HEIGHT / 2 - 100, textColor);
                    break;
            }

            // Draw game-specific elements
            if (state == GAME_DODGE) {
                draw_line_trail(renderer); // Add trail rendering
                
                // Draw background
                SDL_Color start_color = {50, 50, 50, 255}; // Dark gray
                SDL_Color end_color = {20, 20, 20, 255};   // Darker gray
                draw_gradient_background(renderer, start_color, end_color);

                // Draw dodge blocks
                SDL_SetRenderDrawColor(renderer, 255, 0, 0, 255); // Red blocks
                for (int i = 0; i < MAX_DODGE_BLOCKS; i++) {
                    if (dodge_blocks[i].active) {
                        SDL_Rect block = {
                            (int)dodge_blocks[i].x,
                            (int)dodge_blocks[i].y,
                            BLOCK_WIDTH,
                            BLOCK_HEIGHT
                        };
                        SDL_RenderFillRect(renderer, &block);
                    }
                }

                // Draw player
                SDL_Rect player_rect = {
                    (int)(player.x - GAME_OBJECT_SIZE / 2),
                    (int)(player.y - GAME_OBJECT_SIZE / 2),
                    GAME_OBJECT_SIZE,
                    GAME_OBJECT_SIZE
                };
                SDL_SetRenderDrawColor(renderer, 0, 255, 0, 255); // Green player
                SDL_RenderFillRect(renderer, &player_rect);

                // Draw score
                char score_text[50];
                snprintf(score_text, 50, "Score: %d  High Score: %d", dodge_score, dodge_high_score);
                SDL_Color textColor = {255, 255, 255, 255}; // White text
                draw_text(renderer, font_score, score_text, 50, 50, textColor);
            }

            // Draw player name in game modes
            if ((state == GAME_BALANCE_HOLD || state == GAME_COIN_COLLECTOR) && selected_player_index != -1) {
                char player_text[100];
                snprintf(player_text, 100, "Player: %s", available_players[selected_player_index].name);
                textColor = (SDL_Color){FONT_COLOR_R, FONT_COLOR_G, FONT_COLOR_B, 255};
                int text_w, text_h;
                TTF_SizeText(font_score, player_text, &text_w, &text_h);
                draw_text(renderer, font_score, player_text, WINDOW_WIDTH - text_w - 50, 50, textColor);
            }

            SDL_RenderPresent(renderer);
        }

        // Frame rate limiting
        Uint32 frame_time = SDL_GetTicks() - frame_start;
        if (frame_time < FRAME_TIME) {
            SDL_Delay(FRAME_TIME - frame_time);
        }
    }

cleanup_iface:
    if (iface) {
        xwii_iface_close(iface, XWII_IFACE_BALANCE_BOARD);
        xwii_iface_unref(iface);
    }

cleanup:
    cleanup_text_cache();
    if (coin_sound) Mix_FreeChunk(coin_sound);
    if (win_sound) Mix_FreeChunk(win_sound);
    if (select_sound) Mix_FreeChunk(select_sound);
    if (target_sound) Mix_FreeChunk(target_sound);
    if (reset_sound) Mix_FreeChunk(reset_sound);
    if (connection_intro_music) Mix_FreeMusic(connection_intro_music);
    if (connection_main_music) Mix_FreeMusic(connection_main_music);
    if (transition_music) Mix_FreeMusic(transition_music);
    if (main_intro_music) Mix_FreeMusic(main_intro_music);
    if (main_loop_music) Mix_FreeMusic(main_loop_music);
    if (boardpower_texture) SDL_DestroyTexture(boardpower_texture);
    for (int i = 0; i < num_players; i++) {
        if (player_textures[i]) {
            SDL_DestroyTexture(player_textures[i]);
        }
    }
    if (coin_texture) SDL_DestroyTexture(coin_texture);
    if (font_score) TTF_CloseFont(font_score);
    if (font_tutorial) TTF_CloseFont(font_tutorial);
    if (font_menu_title) TTF_CloseFont(font_menu_title);
    if (font_menu_description) TTF_CloseFont(font_menu_description);
    if (renderer) SDL_DestroyRenderer(renderer);
    if (window) SDL_DestroyWindow(window);
    Mix_Quit();
    IMG_Quit();
    TTF_Quit();
    SDL_Quit();

    return 0;
}

// Add texture caching for text
typedef struct {
    char* text;
    SDL_Texture* texture;
    int w, h;
} CachedText;

#define MAX_CACHED_TEXTS 32
CachedText text_cache[MAX_CACHED_TEXTS];
int cached_text_count = 0;

SDL_Texture* get_cached_text(SDL_Renderer* renderer, TTF_Font* font, const char* text, SDL_Color color) {
    // Check cache first
    for (int i = 0; i < cached_text_count; i++) {
        if (strcmp(text_cache[i].text, text) == 0) {
            return text_cache[i].texture;
        }
    }
    
    // Create new texture if not found
    if (cached_text_count < MAX_CACHED_TEXTS) {
        SDL_Surface* surface = TTF_RenderText_Blended(font, text, color);
        if (surface) {
            SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
            if (texture) {
                text_cache[cached_text_count].text = strdup(text);
                text_cache[cached_text_count].texture = texture;
                text_cache[cached_text_count].w = surface->w;
                text_cache[cached_text_count].h = surface->h;
                cached_text_count++;
                SDL_FreeSurface(surface);
                return texture;
            }
            SDL_FreeSurface(surface);
        }
    }
    return NULL;
}

void cleanup_text_cache() {
    for (int i = 0; i < cached_text_count; i++) {
        free(text_cache[i].text);
        SDL_DestroyTexture(text_cache[i].texture);
    }
    cached_text_count = 0;
}
