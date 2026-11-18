# Snake in C

A small terminal Snake game written in portable C11. No external libraries —
it uses only the C standard library and POSIX terminal APIs (already available
on Linux, macOS, and BSD).

## Build

```sh
make
```

Or compile directly:

```sh
cc -O2 -std=c11 -Wall -Wextra -Wpedantic -o snake snake.c
```

## Play

```sh
./snake
```

| Key | Action |
| --- | --- |
| Arrow keys or `WASD` | Move |
| `P` | Pause / resume |
| `R` | Restart after game over |
| `Q` or `Esc` | Quit |

Eat the `@` food to grow and score points. Hitting a wall or your own tail ends
the game. The snake speeds up as your score increases.

## Requirements

- A terminal with a minimum size of 44 columns and 26 rows
- A C11 compiler (GCC, Clang, etc.)

## Clean

```sh
make clean
```
