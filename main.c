#include "common.h"
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_ttf.h>
#include <libtguy.h>
#include <stdlib.h>
#include <utf8c.h>
#include <vec.h>

typedef struct {
    COMMON_CONTEXT_BODY;
    TTF_Font *font;
    SDL_Texture *trash_can_tex;
    TrashGuyState *tguy_state;
    int tguy_current_frame;
    int tguy_initial_distance;
    Uint32 tguy_frame_delta;
    int tguy_fps;
    float global_scaling_factor;
    SDL_Rect input_rect;
    vec_char_t print_buf;
    vec_char_t input_buf;
} TGuyGuiContext;

#ifdef __EMSCRIPTEN__

void enable_dom_focus(const char *selector) {
    const char code[] =
        "document.getElementById(\"%s\").focus();";
    /*
     * todo: fixme this buffer shouldn't be of hardcoded size,
     * use static if possible, otherwise allocate more
     */
    static char buf[256];
    snprintf(buf, sizeof(buf), code, selector);
    emscripten_run_script(buf);
}

void disable_dom_focus(const char *selector) {
    const char code[] =
        "document.getElementById(\"%s\").blur();";
    static char buf[256];
    snprintf(buf, sizeof(buf), code, selector);
    emscripten_run_script(buf);
}

#endif // __EMSCRIPTEN__

static void TGUY_StartTextInput(void) {
    #ifdef __EMSCRIPTEN__
    enable_dom_focus("touch_input");
    #endif
    SDL_StartTextInput();
}

static void TGUY_StopTextInput(void) {
    #ifdef __EMSCRIPTEN__
    disable_dom_focus("touch_input");
    #endif
    SDL_StopTextInput();
}

static int vec_strslice(vec_char_t *v, size_t start, size_t count) {
    if (start == v->length - count) {
        return vec_splice(v, start, count), vec_push(v, '\0');
    } else {
        return vec_splice(v, start, count);
    }
}

static int vec_strclear(vec_char_t *v) {
    vec_clear(v);
    return vec_push(v, '\0');
}

static int vec_strcat(vec_char_t *v, const char *str, size_t len) {
    int c = 0;
    if (v->length != 0) {
        vec_pop(v);
    }
    if (len == (size_t) -1) {
        while (*str) {
            if (vec_push(v, *str++) < 0) goto revert;
            c++;
        }
    } else {
        int prev_len = v->length;
        vec_pusharr(v, str, len);
        if (prev_len == v->length) goto revert;
        c += len;
    }
    if (vec_push(v, '\0') < 0) goto revert;
    return 0;
    revert:
    if (c) {
        v->length -= c;
        vec_push(v, '\0');
    }
    return -1;
}

static void cleanup_and_exit_(TGuyGuiContext *ctx, int exit_status) {
    tguy_free(ctx->tguy_state);
    vec_deinit(&ctx->input_buf);
    vec_deinit(&ctx->print_buf);
    TTF_CloseFont(ctx->font);
    if (ctx->trash_can_tex) SDL_DestroyTexture(ctx->trash_can_tex);
    if (ctx->ren) SDL_DestroyRenderer(ctx->ren);
    SDL_DestroyWindow(ctx->win);
    IMG_Quit();
    TTF_Quit();
    SDL_Quit();
    exit(exit_status);
}

static int event_filter(void *udata, SDL_Event *evt_) {
    SDL_Event evt = *evt_;
    (void) udata;
    switch (evt.type) {
        case SDL_QUIT:
        case SDL_MOUSEBUTTONDOWN:
        case SDL_FINGERDOWN:
        case SDL_WINDOWEVENT:
        case SDL_TEXTINPUT:
            return 1;
        case SDL_KEYDOWN: {
            switch (evt.key.keysym.sym) {
                case SDLK_BACKSPACE:
                case SDLK_DELETE:
                case SDLK_LEFT:
                case SDLK_RIGHT:
                case SDLK_EQUALS:
                case SDLK_MINUS:
                case SDLK_r:
                case SDLK_UP:
                case SDLK_DOWN:
                    return 1;
            }
        }
    }
    return 0;
}

#define ctrl_pressed(k_state) ((k_state)[SDL_SCANCODE_LCTRL] || (k_state)[SDL_SCANCODE_RCTRL])

static void process_events(TGuyGuiContext *ctx) {
    SDL_Event evt;
    while (SDL_PollEvent(&evt)) {
        switch (evt.type) {
            case SDL_QUIT: cleanup_and_exit(ctx, EXIT_SUCCESS);
                break;
            case SDL_WINDOWEVENT:
                switch (evt.window.event) {
                    case SDL_WINDOWEVENT_RESIZED:
                        SDL_RenderSetScale(ctx->ren, ctx->global_scaling_factor, ctx->global_scaling_factor);
                        break;
                }
                break;
            case SDL_MOUSEBUTTONDOWN:
                if (evt.button.which == SDL_TOUCH_MOUSEID) break;
            case SDL_FINGERDOWN: {
                int win_h, win_w;
                SDL_GetWindowSize(ctx->win, &win_w, &win_h);
                SDL_Point tap_p =
                    (evt.type == SDL_FINGERDOWN)
                    ? (SDL_Point) {
                        (Uint32) ((double) win_w * evt.tfinger.x),
                        (Uint32) ((double) win_h * evt.tfinger.y)
                    }
                    : (SDL_Point) {
                        evt.button.x,
                        evt.button.y
                    };

                if (!SDL_PointInRect(&tap_p, &ctx->input_rect)) continue;
                if (SDL_IsTextInputActive()) {
                    TGUY_StopTextInput();
                } else {
                    TGUY_StartTextInput();
                }
                break;
            }

            case SDL_TEXTINPUT: {
                const char *str = evt.text.text;
                if (vec_strcat(&ctx->input_buf, str, -1) < 0) {
                    cleanup_and_exit_(ctx, EXIT_FAILURE);
                }
                goto refresh;
            } // SDL_TEXTINPUT
            case SDL_KEYDOWN: {
                SDL_Keycode sym = evt.key.keysym.sym;
                const Uint8 *k_state = SDL_GetKeyboardState(NULL);
                switch (sym) {
                    case SDLK_BACKSPACE: {
                        if (ctx->input_buf.length <= 1) continue;
                        size_t str_len = ctx->input_buf.length - 1;
                        const char *end = &ctx->input_buf.data[str_len];
                        // get length of the unicode character prior to the nul terminator
                        int codepoint_len = (int) (end - utf8_prior(end, NULL));
                        // splice exactly 1 unicode character plus the nul terminator
                        vec_strslice(&ctx->input_buf, str_len - codepoint_len, codepoint_len + 1);
                        goto refresh;
                    }
                    case SDLK_DELETE:
                        vec_strclear(&ctx->input_buf);
                        goto refresh;

                    case SDLK_LEFT:
                        if (ctx->tguy_initial_distance == 0) continue;
                    case SDLK_RIGHT:
                        ctx->tguy_initial_distance += (sym == SDLK_RIGHT) ? 1 : -1;
                        goto refresh; // no need to push the nul terminator, it's already there

                    case SDLK_DOWN:
                        if (ctx->tguy_fps == 1) continue;
                    case SDLK_UP:
                        ctx->tguy_fps += (sym == SDLK_UP) ? 1 : -1;
                        continue;

                    case SDLK_MINUS:
                        if (ctx->global_scaling_factor <= 0.1) continue;
                    case SDLK_EQUALS: {
                        int win_w, win_h;

                        if (!ctrl_pressed(k_state)) continue;
                        ctx->global_scaling_factor += (sym == SDLK_EQUALS) ? 0.05f : -0.05f;
                        SDL_GetWindowSize(ctx->win, &win_w, &win_h);
                        {
                            SDL_Event winevt = {
                                .window.type = SDL_WINDOWEVENT,
                                .window.event = SDL_WINDOWEVENT_RESIZED,
                                .window.data1 = win_w,
                                .window.data2 = win_h,
                                .window.windowID = SDL_GetWindowID(ctx->win),
                            };
                            SDL_PushEvent(&winevt);
                        }
                        continue;
                    }

                    case SDLK_r:
                        if (!ctrl_pressed(k_state)) continue;
                        ctx->tguy_current_frame = 0;
                        ctx->tguy_frame_delta = 0;
                        continue;

                    default:
                        continue;
                }
                refresh:
                if (ctx->input_buf.length > 1) {
                    tguy_free(ctx->tguy_state);
                    ctx->tguy_state = tguy_init_str(ctx->input_buf.data,
                        ctx->input_buf.length - 1, ctx->tguy_initial_distance);
                    if (!ctx->tguy_state) {
                        cleanup_and_exit(ctx, EXIT_FAILURE);
                    }
                    // keep the current frame if possible
                    ctx->tguy_current_frame = (ctx->tguy_current_frame < tguy_get_frames_count(ctx->tguy_state))
                                              ? ctx->tguy_current_frame
                                              : 0;
                    // get_bsize is relatively expensive at first invocation,
                    // one might introduce wait time before updating tguy on input
                    if (vec_reserve(&ctx->print_buf, tguy_get_bsize(ctx->tguy_state)) < 0) {
                        cleanup_and_exit(ctx, EXIT_FAILURE);
                    }
                } else {
                    ctx->tguy_state = NULL;
                    ctx->tguy_current_frame = 0;
                    ctx->tguy_frame_delta = 0;
                }
            } // SDL_KEYDOWN

        }     // evt.type
    }
}

static void draw_frame(TGuyGuiContext *ctx) {
    int win_w = WINDOW_WIDTH, win_h = WINDOW_HEIGHT;
    /* draw */
    SDL_GetWindowSize(ctx->win, &win_w, &win_h);
    SDL_SetRenderDrawColor(ctx->ren, 0, 0, 0, 255);
    SDL_RenderClear(ctx->ren);

    {
        SDL_Surface *tguy_sur, *input_sur;
        input_sur = TTF_RenderUTF8_Solid(ctx->font, ctx->input_buf.data, (SDL_Color) {102, 102, 255, 255});
        if (input_sur) {
            int tex_w, tex_h;
            SDL_Texture *tex = SDL_CreateTextureFromSurface(ctx->ren, input_sur);
            if (!tex) cleanup_and_exit(ctx, EXIT_FAILURE);
            SDL_QueryTexture(tex, NULL, NULL, &tex_w, &tex_h);
            SDL_RenderCopy(ctx->ren, tex, NULL, &(SDL_Rect) {0, 0, tex_w, tex_h});
            SDL_FreeSurface(input_sur);
            SDL_DestroyTexture(tex);
        }
        if (ctx->tguy_state) {
            if (SDL_TICKS_PASSED(SDL_GetTicks(), ctx->tguy_frame_delta)) {
                int frames_count = tguy_get_frames_count(ctx->tguy_state);
                if (ctx->tguy_frame_delta != 0) {
                    ctx->tguy_current_frame++;
                    if (ctx->tguy_current_frame >= frames_count - 1) {
                        ctx->tguy_current_frame = 0;
                    }
                }
                ctx->tguy_frame_delta = SDL_GetTicks() + FPS_TICKS(ctx->tguy_fps);
            }
            tguy_from_frame(ctx->tguy_state, ctx->tguy_current_frame);
            tguy_bprint(ctx->tguy_state, ctx->print_buf.data);
            tguy_sur = TTF_RenderUTF8_Solid(ctx->font, ctx->print_buf.data, (SDL_Color) {0, 255, 0, 255});
            if (tguy_sur) {
                int tex_w, tex_h, emoji_w, emoji_h;
                SDL_Texture *tex = SDL_CreateTextureFromSurface(ctx->ren, tguy_sur);
                if (!tex) cleanup_and_exit(ctx, EXIT_FAILURE);
                SDL_QueryTexture(tex, NULL, NULL, &tex_w, &tex_h);
                SDL_RenderCopy(ctx->ren, tex, NULL, &(SDL_Rect) {0, win_h - TTF_FontHeight(ctx->font), tex_w, tex_h});
                SDL_SetRenderDrawColor(ctx->ren, 19, 221, 201, 255);
                SDL_RenderDrawLine(ctx->ren, 0, win_h - 1, tex_w, win_h - 1);
                /* I think this one should be accessible via libtguy api */
                TTF_SizeUTF8(ctx->font, "\xf0\x9f\x97\x91", &emoji_w, &emoji_h);
                SDL_RenderCopy(ctx->ren, ctx->trash_can_tex, NULL, &(SDL_Rect) {-4, win_h - TTF_FontHeight(ctx->font),
                                                                                emoji_w + 10, emoji_h});
                SDL_FreeSurface(tguy_sur);
                SDL_DestroyTexture(tex);
            }
        }
    }
    {
        SDL_Rect new_input_rect = {0, 0, win_w, TTF_FontHeight(ctx->font)};
        if (!SDL_RectEquals(&ctx->input_rect, &new_input_rect)) {
            ctx->input_rect = new_input_rect;
            SDL_SetTextInputRect(&ctx->input_rect);
        }
    }
    SDL_RenderPresent(ctx->ren);
}

static void loop_handler(void *ctx_) {
    TGuyGuiContext *ctx = ctx_;
    draw_frame(ctx);
    process_events(ctx);
}

#define start_string "Type something"

int main(int argc, char *argv[]) {
#if !defined(__EMSCRIPTEN__)
    Uint32 delta_ticks = 0;
#endif
    TGuyGuiContext ctx = {
        .win = NULL,
        .ren = NULL,
        .trash_can_tex = NULL,
        .font = NULL,
        .print_buf = {NULL, 0},
        .input_buf = {NULL, 0},
        .tguy_state = NULL,
        .tguy_initial_distance = 4,
        .tguy_fps = 10,
        .global_scaling_factor = 1.0f
    };
    (void) argc;
    (void) argv;

    if (vec_reserve(&ctx.input_buf, sizeof(start_string)) < 0) {
        cleanup_and_exit(&ctx, EXIT_FAILURE);
    }
    vec_pusharr(&ctx.input_buf, start_string, sizeof(start_string));

    ctx.tguy_state = tguy_init_str(start_string,
        ctx.input_buf.length - 1, // exclude the nul terminator
        ctx.tguy_initial_distance);

    if (vec_reserve(&ctx.print_buf, tguy_get_bsize(ctx.tguy_state)) < 0) {
        cleanup_and_exit(&ctx, EXIT_FAILURE);
    }
    if (TTF_Init() < 0) {
        SDL_Log("Can't init TTF!\n");
        cleanup_and_exit(&ctx, EXIT_FAILURE);
    }
    if (IMG_Init(0) < 0) {
        SDL_Log("Can't init IMG!\n");
        cleanup_and_exit(&ctx, EXIT_FAILURE);
    }

    if (SDL_Init(COMMON_INIT_SUBSYSTEMS) < 0) {
        SDL_Log("Can't init needed subsystems: %s\n", SDL_GetError());
        cleanup_and_exit(&ctx, EXIT_FAILURE);
    };
    SDL_SetEventFilter(event_filter, NULL);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");

    ctx.win = SDL_CreateWindow("TGuy", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, WINDOW_WIDTH, WINDOW_HEIGHT,
        SDL_WINDOW_RESIZABLE);

    if (!ctx.win) {
        cleanup_and_exit(&ctx, EXIT_FAILURE);
    }

    ctx.ren = SDL_CreateRenderer(ctx.win, -1, SDL_RENDERER_ACCELERATED);
    if (!ctx.ren) {
        cleanup_and_exit(&ctx, EXIT_FAILURE);
    }

    ctx.font = TTF_OpenFont("assets/font.ttf", 50);
    if (!ctx.font) {
        SDL_Log("Can't open font: %s\n", TTF_GetError());
        cleanup_and_exit(&ctx, EXIT_FAILURE);
    }

    SDL_Surface *trash_can_img_sur = IMG_Load("assets/trash-icon.png");
    if (!trash_can_img_sur) {
        SDL_Log("Can't load image: %s\n", IMG_GetError());
        cleanup_and_exit(&ctx, EXIT_FAILURE);
    }

    ctx.trash_can_tex = SDL_CreateTextureFromSurface(ctx.ren, trash_can_img_sur);
    if (!ctx.trash_can_tex) {
        cleanup_and_exit(&ctx, EXIT_FAILURE);
    }

    SDL_SetWindowIcon(ctx.win, trash_can_img_sur);
    SDL_FreeSurface(trash_can_img_sur);
#ifdef __EMSCRIPTEN__
    /**
    * Schedule the main loop handler to get
    * called on each animation frame
    */
    emscripten_set_main_loop_arg(loop_handler, &ctx, FPS, 1);
#else
    while (1) {
        if (SDL_TICKS_PASSED(SDL_GetTicks(), delta_ticks)) {
            Uint32 cur_ticks;
            delta_ticks = SDL_GetTicks() + EVENT_LOOP_DELTA;
            loop_handler(&ctx);
            if (!SDL_TICKS_PASSED(cur_ticks = SDL_GetTicks(), delta_ticks)) {
                SDL_Delay(delta_ticks - cur_ticks);
            }
        }
    }
#endif // __EMSCRIPTEN__
    return EXIT_SUCCESS;
}
