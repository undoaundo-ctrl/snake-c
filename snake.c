#define _POSIX_C_SOURCE 200809L

#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/time.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#define WIDTH 40
#define HEIGHT 20
#define CAPACITY (WIDTH * HEIGHT)

#define KEY_NONE    (-1)
#define KEY_UP      1000
#define KEY_DOWN    1001
#define KEY_LEFT    1002
#define KEY_RIGHT   1003
#define KEY_QUIT    1004
#define KEY_RESTART 1005
#define KEY_PAUSE   1006

typedef struct {
    int x;
    int y;
} Point;

typedef enum {
    DIR_UP,
    DIR_DOWN,
    DIR_LEFT,
    DIR_RIGHT
} Direction;

static Point body[CAPACITY];
static Point food;
static int length;
static int score;
static Direction dir;
static Direction next_dir;
static bool paused;
static bool game_over;
static bool won;

static struct termios original_termios;
static bool terminal_saved;
static bool screen_active;

static unsigned char input_buf[16];
static size_t input_len;
static size_t input_pos;

static void restore_terminal(void)
{
    if (terminal_saved) {
        tcsetattr(STDIN_FILENO, TCSANOW, &original_termios);
        terminal_saved = false;
    }
    if (screen_active) {
        printf("\033[?25h\033[?1049l");
        fflush(stdout);
        screen_active = false;
    }
}

static void on_signal(int signo)
{
    (void)signo;
    restore_terminal();
    _exit(128);
}

static long long now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static int tick_interval(void)
{
    int interval = 140 - (score / 20) * 8;
    return interval < 60 ? 60 : interval;
}

static int next_byte(int timeout_ms)
{
    fd_set fds;
    struct timeval tv;

    if (input_pos < input_len) {
        return input_buf[input_pos++];
    }

    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    if (select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv) <= 0) {
        return KEY_NONE;
    }

    ssize_t n = read(STDIN_FILENO, input_buf, sizeof(input_buf));
    if (n <= 0) {
        return KEY_NONE;
    }

    input_len = (size_t)n;
    input_pos = 0;
    return input_buf[input_pos++];
}

static int get_key(int timeout_ms)
{
    int c = next_byte(timeout_ms);
    if (c == KEY_NONE) {
        return KEY_NONE;
    }

    if (c == 27) {
        int b1 = next_byte(20);
        if (b1 == '[' || b1 == 'O') {
            int b2 = next_byte(20);
            switch (b2) {
            case 'A': return KEY_UP;
            case 'B': return KEY_DOWN;
            case 'C': return KEY_RIGHT;
            case 'D': return KEY_LEFT;
            default: break;
            }
        }
        return KEY_QUIT;
    }

    switch (c) {
    case 'w': case 'W': return KEY_UP;
    case 's': case 'S': return KEY_DOWN;
    case 'a': case 'A': return KEY_LEFT;
    case 'd': case 'D': return KEY_RIGHT;
    case 'p': case 'P': return KEY_PAUSE;
    case 'r': case 'R': return KEY_RESTART;
    case 'q': case 'Q': return KEY_QUIT;
    default: return KEY_NONE;
    }
}

static bool occupied(int x, int y)
{
    for (int i = 0; i < length; i++) {
        if (body[i].x == x && body[i].y == y) {
            return true;
        }
    }
    return false;
}

static void spawn_food(void)
{
    for (int attempt = 0; attempt < CAPACITY * 4; attempt++) {
        Point candidate = { rand() % WIDTH, rand() % HEIGHT };
        if (!occupied(candidate.x, candidate.y)) {
            food = candidate;
            return;
        }
    }

    for (int y = 0; y < HEIGHT; y++) {
        for (int x = 0; x < WIDTH; x++) {
            if (!occupied(x, y)) {
                food = (Point){ x, y };
                return;
            }
        }
    }
}

static void reset_game(void)
{
    int start_y = HEIGHT / 2;
    int start_x = WIDTH / 4;

    length = 3;
    for (int i = 0; i < length; i++) {
        body[i] = (Point){ start_x - i, start_y };
    }

    dir = DIR_RIGHT;
    next_dir = DIR_RIGHT;
    score = 0;
    paused = false;
    game_over = false;
    won = false;
    spawn_food();
}

static void change_direction(int key)
{
    Direction wanted = next_dir;

    switch (key) {
    case KEY_UP:    wanted = DIR_UP; break;
    case KEY_DOWN:  wanted = DIR_DOWN; break;
    case KEY_LEFT:  wanted = DIR_LEFT; break;
    case KEY_RIGHT: wanted = DIR_RIGHT; break;
    default: return;
    }

    bool reversal = (wanted == DIR_UP && dir == DIR_DOWN) ||
                    (wanted == DIR_DOWN && dir == DIR_UP) ||
                    (wanted == DIR_LEFT && dir == DIR_RIGHT) ||
                    (wanted == DIR_RIGHT && dir == DIR_LEFT);
    if (!reversal) {
        next_dir = wanted;
    }
}

static void tick(void)
{
    dir = next_dir;
    Point next = body[0];

    switch (dir) {
    case DIR_UP:    next.y--; break;
    case DIR_DOWN:  next.y++; break;
    case DIR_LEFT:  next.x--; break;
    case DIR_RIGHT: next.x++; break;
    }

    if (next.x < 0 || next.x >= WIDTH || next.y < 0 || next.y >= HEIGHT) {
        game_over = true;
        return;
    }

    bool growing = (next.x == food.x && next.y == food.y);
    int limit = growing ? length : length - 1;
    for (int i = 0; i < limit; i++) {
        if (body[i].x == next.x && body[i].y == next.y) {
            game_over = true;
            return;
        }
    }

    if (growing) {
        if (length >= CAPACITY) {
            game_over = true;
            won = true;
            return;
        }
        memmove(&body[1], &body[0], (size_t)length * sizeof(body[0]));
        length++;
        score += 10;
        spawn_food();
    } else {
        memmove(&body[1], &body[0], (size_t)(length - 1) * sizeof(body[0]));
    }
    body[0] = next;
}

static void draw(void)
{
    char cells[HEIGHT][WIDTH];
    memset(cells, ' ', sizeof(cells));

    cells[food.y][food.x] = '@';
    for (int i = length - 1; i >= 0; i--) {
        cells[body[i].y][body[i].x] = (i == 0) ? 'O' : 'o';
    }

    printf("\033[H");
    printf("  SNAKE   score: %-5d length: %-4d   arrows/WASD move   P pause   Q quit\n",
           score, length);
    printf("  +");
    for (int i = 0; i < WIDTH; i++) putchar('-');
    printf("+\n");

    for (int y = 0; y < HEIGHT; y++) {
        printf("  |");
        for (int x = 0; x < WIDTH; x++) {
            putchar(cells[y][x]);
        }
        printf("|\n");
    }

    printf("  +");
    for (int i = 0; i < WIDTH; i++) putchar('-');
    printf("+\n");

    if (paused) {
        printf("  >>> PAUSED - press P to resume <<<\n");
    } else if (game_over) {
        printf("  >>> %s - press R to restart, Q to quit <<<\n",
               won ? "YOU WIN!" : "GAME OVER");
    } else {
        printf("\n");
    }
    fflush(stdout);
}

static void check_terminal_size(void)
{
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_row > 0 && ws.ws_col > 0) {
        if (ws.ws_col < WIDTH + 4 || ws.ws_row < HEIGHT + 6) {
            fprintf(stderr, "Terminal too small: need at least %d columns and %d rows.\n",
                    WIDTH + 4, HEIGHT + 6);
            exit(1);
        }
    }
}

int main(void)
{
    srand((unsigned int)time(NULL));
    reset_game();
    check_terminal_size();

    atexit(restore_terminal);
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    if (tcgetattr(STDIN_FILENO, &original_termios) == 0) {
        struct termios raw = original_termios;
        raw.c_lflag &= ~(ICANON | ECHO | ISIG);
        raw.c_iflag &= ~(IXON | ICRNL);
        raw.c_cc[VMIN] = 0;
        raw.c_cc[VTIME] = 0;
        if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) == 0) {
            terminal_saved = true;
        }
    }

    printf("\033[?1049h\033[?25l\033[2J");
    fflush(stdout);
    screen_active = true;

    long long last_tick = now_ms();

    for (;;) {
        int interval = tick_interval();
        int key = get_key(game_over ? 100 : interval);

        while (key != KEY_NONE) {
            if (key == KEY_QUIT) {
                goto done;
            }
            if (key == KEY_RESTART && game_over) {
                reset_game();
                last_tick = now_ms();
            } else if (key == KEY_PAUSE && !game_over) {
                paused = !paused;
            } else if (!game_over && !paused) {
                change_direction(key);
            }
            key = get_key(0);
        }

        if (!game_over) {
            long long current = now_ms();
            if (!paused && current - last_tick >= interval) {
                tick();
                last_tick = current;
            }
        }

        draw();
    }

done:
    restore_terminal();
    printf(" Thanks for playing! Final score: %d\n", score);
    return 0;
}
