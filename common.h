#ifndef COMMON_H
#define COMMON_H

#include <SDL2/SDL.h>
#if defined(__EMSCRIPTEN__)
    #include <emscripten.h>
#endif

#define cleanup_and_exit(ctx, exit_status)                                            \
    {                                                                                 \
        if ((exit_status) == EXIT_FAILURE) {                                          \
            SDL_Log("%s(%d) in %s: failure cleanup\n", __FILE__, __LINE__, __func__); \
        } else if ((exit_status) == EXIT_SUCCESS) {                                   \
            SDL_Log("%s(%d) in %s: success cleanup\n", __FILE__, __LINE__, __func__); \
        }                                                                             \
        cleanup_and_exit_(ctx, exit_status);                                          \
    }                                                                                 \
    ((void) 0)

#define COMMON_INIT_SUBSYSTEMS (SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_TIMER)

#define FPS_TICKS(fps) (Uint32)(1000.0 / (fps))

const Uint32 FPS = 60;

#define EVENT_LOOP_DELTA FPS_TICKS(FPS);

#define WINDOW_WIDTH 1000
#define WINDOW_HEIGHT 500


typedef struct {
    #define COMMON_CONTEXT_BODY \
    SDL_Window *win;        \
    SDL_Renderer *ren

    COMMON_CONTEXT_BODY;
} CommonContext;

#endif //COMMON_H
