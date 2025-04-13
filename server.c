#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/wait.h>
#include "game_structs.h"
#include "print_output.h"

#define PIPE(fd) socketpair(AF_UNIX, SOCK_STREAM, PF_UNIX, fd)
#define MAX_PLAYERS 10
#define MAX_ARGS 20
#define BUFFER_SIZE 1024

// Structure to represent a player
typedef struct player {
    pid_t pid;
    char character;
    int fd[2]; // Bidirectional pipe
    char executable[BUFFER_SIZE];
    char *args[MAX_ARGS];
    int arg_count;
} player;

// Structure to represent the game state
typedef struct game_state {
    int grid_width;
    int grid_height;
    int streak_size;
    int player_count;
    char **grid; // 2D array to store the grid
    int filled_positions;
    player players[MAX_PLAYERS];
} game_state;

// Function to initialize the game grid
char** init_grid(int width, int height) {
    char **grid = (char **)malloc(height * sizeof(char *));
    for (int i = 0; i < height; i++) {
        grid[i] = (char *)malloc(width * sizeof(char));
        for (int j = 0; j < width; j++) {
            grid[i][j] = '.'; // Initialize with empty cells
        }
    }
    return grid;
}

// Function to free the game grid


// Function to check if a player has won
int check_win(game_state *state, int x, int y, char player_char) {
    // Check horizontal
    for (int i = 0; i <= state->grid_width - state->streak_size; i++) {
        int match = 1;
        for (int j = 0; j < state->streak_size; j++) {
            if (state->grid[y][i + j] != player_char) {
                match = 0;
                break;
            }
        }
        if (match) return 1;
    }

    // Check vertical
    for (int i = 0; i <= state->grid_height - state->streak_size; i++) {
        int match = 1;
        for (int j = 0; j < state->streak_size; j++) {
            if (state->grid[i + j][x] != player_char) {
                match = 0;
                break;
            }
        }
        if (match) return 1;
    }

    // Check diagonal (top-left to bottom-right)
    for (int i = 0; i <= state->grid_height - state->streak_size; i++) {
        for (int j = 0; j <= state->grid_width - state->streak_size; j++) {
            int match = 1;
            for (int k = 0; k < state->streak_size; k++) {
                if (state->grid[i + k][j + k] != player_char) {
                    match = 0;
                    break;
                }
            }
            if (match) return 1;
        }
    }

    // Check diagonal (top-right to bottom-left)
    for (int i = 0; i <= state->grid_height - state->streak_size; i++) {
        for (int j = state->streak_size - 1; j < state->grid_width; j++) {
            int match = 1;
            for (int k = 0; k < state->streak_size; k++) {
                if (state->grid[i + k][j - k] != player_char) {
                    match = 0;
                    break;
                }
            }
            if (match) return 1;
        }
    }

    return 0;
}

// Function to read game configuration from stdin
void read_game_config(game_state *state) {
    // Read grid dimensions, streak size, and player count
    scanf("%d %d %d %d", &state->grid_width, &state->grid_height, 
             &state->streak_size, &state->player_count) ;


    // Initialize grid
    state->grid = init_grid(state->grid_width, state->grid_height);
    state->filled_positions = 0;

    // Read player information
    for (int i = 0; i < state->player_count; i++) {
        // Read player character
        scanf(" %c", &state->players[i].character);
        // Read argument count
        scanf("%d", &state->players[i].arg_count);
        // Read executable path
        scanf("%s", state->players[i].executable);
        
        // Allocate memory for arguments
        state->players[i].args[0] = strdup(state->players[i].executable); // First arg is the program name

        // Read arguments (excluding the executable path)
        for (int j = 0; j < state->players[i].arg_count; j++) {
            state->players[i].args[j + 1] = (char *)malloc(BUFFER_SIZE * sizeof(char));
            scanf("%s", state->players[i].args[j + 1]);
        }
        state->players[i].args[state->players[i].arg_count + 1] = NULL; // NULL-terminate the args array
    }
}

// Function to set up communication with player processes
void setup_players(game_state *state) {
    for (int i = 0; i < state->player_count; i++) {
        // Create bidirectional pipe
        if (PIPE(state->players[i].fd) == -1) {

        }

        // Fork child process
        pid_t pid = fork();
        if (pid == -1) {

        } else if (pid == 0) {
            // Child process
            close(state->players[i].fd[0]); // Close server end of pipe

            // Redirect stdin and stdout to the pipe
            dup2(state->players[i].fd[1], STDIN_FILENO);
            dup2(state->players[i].fd[1], STDOUT_FILENO);
            
            // Execute player process
            execv(state->players[i].executable, state->players[i].args);

        } else {
            // Parent process
            close(state->players[i].fd[1]); // Close child end of pipe
            state->players[i].pid = pid;
        }
    }
}

// Function to send the current game state to a player
void send_game_state(game_state *state, int player_idx, int success) {
    sm server_msg;
    server_msg.type = RESULT;
    server_msg.success = success;
    server_msg.filled_count = state->filled_positions;

    // Write server message header
    write(state->players[player_idx].fd[0], &server_msg, sizeof(sm));

    // Prepare grid data
    gd grid_data[state->filled_positions];
    int count = 0;

    // Fill grid data with all marked positions
    for (int y = 0; y < state->grid_height; y++) {
        for (int x = 0; x < state->grid_width; x++) {
            if (state->grid[y][x] != '.') {
                grid_data[count].position.x = x;
                grid_data[count].position.y = y;
                grid_data[count].character = state->grid[y][x];
                count++;
            }
        }
    }

    // Write grid data
    write(state->players[player_idx].fd[0], grid_data, count * sizeof(gd));

    // Print output using provided function
    smp server_msg_print;
    server_msg_print.process_id = state->players[player_idx].pid;
    server_msg_print.server_message = &server_msg;

    gu grid_updates[state->filled_positions];
    for (int i = 0; i < count; i++) {
        grid_updates[i].position = grid_data[i].position;
        grid_updates[i].character = grid_data[i].character;
    }

    print_output(NULL, &server_msg_print, grid_updates, count);
}

// Function to send end of game message to all players
void send_end_message(game_state *state) {
    sm server_msg;
    server_msg.type = END;
    server_msg.success = 0;
    server_msg.filled_count = 0;

    for (int i = 0; i < state->player_count; i++) {
        // Write server message
        write(state->players[i].fd[0], &server_msg, sizeof(sm));

        // Print output using provided function
        smp server_msg_print;
        server_msg_print.process_id = state->players[i].pid;
        server_msg_print.server_message = &server_msg;

        print_output(NULL, &server_msg_print, NULL, 0);
    }
}

// Function to handle START message from a player
void handle_start_message(game_state *state, int player_idx, cm *client_msg) {
    // Create client message print structure for logging
    cmp client_msg_print;
    client_msg_print.process_id = state->players[player_idx].pid;
    client_msg_print.client_message = client_msg;

    print_output(&client_msg_print, NULL, NULL, 0);

    // Send current game state to the player
    send_game_state(state, player_idx, 0);
}

// Function to handle MARK message from a player
int handle_mark_message(game_state *state, int player_idx, cm *client_msg) {
    // Create client message print structure for logging
    cmp client_msg_print;
    client_msg_print.process_id = state->players[player_idx].pid;
    client_msg_print.client_message = client_msg;

    print_output(&client_msg_print, NULL, NULL, 0);

    // Check if position is valid
    if (client_msg->position.x < 0 || client_msg->position.x >= state->grid_width ||
        client_msg->position.y < 0 || client_msg->position.y >= state->grid_height) {
        // Invalid position
        send_game_state(state, player_idx, 0);
        return 0;
    }

    // Check if position is already marked
    if (state->grid[client_msg->position.y][client_msg->position.x] != '.') {
        // Position already marked
        send_game_state(state, player_idx, 0);
        return 0;
    }

    // Mark the position
    state->grid[client_msg->position.y][client_msg->position.x] = state->players[player_idx].character;
    state->filled_positions++;

    // Send updated game state to the player
    send_game_state(state, player_idx, 1);

    // Check if this move results in a win
    if (check_win(state, client_msg->position.x, client_msg->position.y, state->players[player_idx].character)) {
        // Player has won
        return 1;
    }

    // Check if the grid is full
    if (state->filled_positions == state->grid_width * state->grid_height) {
        // Game is a draw
        return 2;
    }

    return 0;
}

// Main function
int main() {
    game_state state;
    int game_over = 0;
    char winner_character = 0;

    // Read game configuration
    read_game_config(&state);

    // Set up player processes
    setup_players(&state);

    // Main game loop
    fd_set readfds;
    int max_fd = 0;

    while (!game_over) {
        // Set up file descriptor set for select
        FD_ZERO(&readfds);
        max_fd = 0;

        for (int i = 0; i < state.player_count; i++) {
            FD_SET(state.players[i].fd[0], &readfds);
            if (state.players[i].fd[0] > max_fd) {
                max_fd = state.players[i].fd[0];
            }
        }

        // Wait for activity on any of the file descriptors
        select(max_fd + 1, &readfds, NULL, NULL, NULL);
        // Check which file descriptor is ready
        for (int i = 0; i < state.player_count; i++) {
            if (FD_ISSET(state.players[i].fd[0], &readfds)) {
                // Read message from player
                cm client_msg;
                ssize_t bytes_read = read(state.players[i].fd[0], &client_msg, sizeof(cm));

                if (bytes_read <= 0) {
                    // Player process has terminated or error
                    continue;
                }

                // Process message
                if (client_msg.type == START) {
                    handle_start_message(&state, i, &client_msg);
                } else if (client_msg.type == MARK) {
                    int result = handle_mark_message(&state, i, &client_msg);
                    if (result == 1) {
                        // Player has won
                        game_over = 1;
                        winner_character = state.players[i].character;
                        break;
                    } else if (result == 2) {
                        // Game is a draw
                        game_over = 1;
                        break;
                    }
                }
            }
        }
    }

    // Send end message to all players
    send_end_message(&state);

    // Print final result
    if (winner_character) {
        printf("Winner: Player%c\n", winner_character);
    } else {
        printf("Draw.\n");
    }

    // Clean up resources
    for (int i = 0; i < state.player_count; i++) {
        close(state.players[i].fd[0]);
        // Free allocated memory for args
        for (int j = 0; j <= state.players[i].arg_count + 1; j++) {
            if (state.players[i].args[j] != NULL) {
                free(state.players[i].args[j]);
            }
        }
        // Wait for child process to terminate
        waitpid(state.players[i].pid, NULL, 0);
    }

    // Free grid

        for (int i = 0; i < state.grid_height; i++) {
            free(state.grid[i]);
        }
        free(state.grid);

    return 0;
}