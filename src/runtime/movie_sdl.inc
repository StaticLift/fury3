/* The AVI cinematics (story\*.avi), played while the game waits for MM_MCINOTIFY.
 *
 * #included into recomp_runtime.c after the waveOut code. Every movie is 320x240
 * Cinepak at 15 fps with 22 kHz 8-bit mono PCM, stored as "rec " groups in a RIFF
 * AVI. The player reads the file in order, so the audio that belongs to a frame is
 * queued just before it. The game's sound device is closed for the duration (the
 * Vita has a single background-audio port) and reopened after; everything the game
 * had queued is then reported as played. Start or Cross skips, Esc/Enter/Space on
 * a keyboard, but only after the button has been released once: the Start press
 * that began New Game is usually still held when the intro opens. */
#include "cinepak.h"

static uint32_t le32p(const uint8_t* p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint32_t le16p(const uint8_t* p) { return (uint32_t)(p[0] | p[1] << 8); }

static int movie_skip_down(void) {
    if (s_pad && (SDL_GameControllerGetButton(s_pad, SDL_CONTROLLER_BUTTON_START) ||
                  SDL_GameControllerGetButton(s_pad, SDL_CONTROLLER_BUTTON_A))) return 1;
    const Uint8* k = SDL_GetKeyboardState(NULL);
    return k[SDL_SCANCODE_ESCAPE] || k[SDL_SCANCODE_RETURN] || k[SDL_SCANCODE_SPACE];
}
/* Returns 1 if the player asked to skip (or close the app). */
static int movie_poll(int* armed) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) if (e.type == SDL_QUIT) { g_quit = 1; return 1; }
    int down = movie_skip_down();
    if (!down) *armed = 1;
    return down && *armed;
}

/* Header fields; 0 on anything but the expected layout. */
static int movie_header(const uint8_t* h, uint32_t n, uint32_t* us, int* w, int* hgt, int* rate) {
    *us = 0; *w = *hgt = *rate = 0;
    for (uint32_t i = 0; i + 8 < n; i++) {
        if (!memcmp(h + i, "avih", 4) && i + 48 <= n) {
            *us = le32p(h + i + 8); *w = (int)le32p(h + i + 40); *hgt = (int)le32p(h + i + 44);
        } else if (!memcmp(h + i, "strh", 4) && i + 12 <= n && !memcmp(h + i + 8, "auds", 4)) {
            for (uint32_t j = i; j + 16 < n; j++)
                if (!memcmp(h + j, "strf", 4)) {
                    if (le16p(h + j + 8) == 1 && le16p(h + j + 10) == 1 && le16p(h + j + 22) == 8) *rate = (int)le32p(h + j + 12);
                    break;
                }
        }
    }
    return *us && *w > 0 && *hgt > 0 && *w <= 640 && *hgt <= 480;
}

static void host_movie_play(const char* path) {
    if ((host_env("FURY3_CAMPAIGN") || host_env("FURY3_AUTOSTART")) && !host_env("FURY3_MOVIES")) return;
    if (!s_sdl_ready || !path[0]) return;
    FILE* f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "[movie] cannot open %s\n", path); return; }
    uint8_t* chunk = (uint8_t*)malloc(1 << 18);
    cvid_ctx* cv = (cvid_ctx*)calloc(1, sizeof(cvid_ctx));
    uint32_t* fb = NULL; SDL_Texture* tex = NULL; SDL_AudioDeviceID dev = 0;
    uint32_t us = 0, frames = 0, shown = 0, t0 = 0;
    int w = 0, h = 0, rate = 0, armed = 0, skipped = 0, game_audio = g_wave_dev != 0;
    uint8_t hd[12];
    const char* snap = host_env("FURY3_SNAP");
    uint32_t maxms = host_env("FURY3_MOVIE_MAXMS") ? (uint32_t)atoi(host_env("FURY3_MOVIE_MAXMS")) : 0;

    if (!chunk || !cv || fread(hd, 1, 12, f) != 12 || memcmp(hd, "RIFF", 4) || memcmp(hd + 8, "AVI ", 4)) goto done;
    /* top-level: LIST hdrl, then LIST movi */
    for (;;) {
        if (fread(hd, 1, 12, f) != 12) goto done;
        uint32_t n = le32p(hd + 4);
        if (!memcmp(hd, "LIST", 4) && !memcmp(hd + 8, "movi", 4)) break;
        if (!memcmp(hd, "LIST", 4) && !memcmp(hd + 8, "hdrl", 4) && n - 4 <= (1u << 18)) {
            if (fread(chunk, 1, n - 4, f) != n - 4) goto done;
            if (!movie_header(chunk, n - 4, &us, &w, &h, &rate)) goto done;
            continue;
        }
        fseek(f, (long)(n + (n & 1)) - 4, SEEK_CUR);
    }
    if (!us) goto done;
    fb = (uint32_t*)calloc((size_t)w * (size_t)h, 4);
    if (!fb) goto done;
#ifdef FURY3_GL
    if (!s_gl)
#endif
    {
        tex = SDL_CreateTexture(s_ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, w, h);
        if (!tex) goto done;
        SDL_SetTextureScaleMode(tex, SDL_ScaleModeLinear);
    }

    if (rate && g_wave_ever_opened()) {
        wave_device_close();
        SDL_AudioSpec want, have; SDL_zero(want);
        want.freq = rate; want.format = AUDIO_U8; want.channels = 1; want.samples = 1024;
        dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
        if (dev) SDL_PauseAudioDevice(dev, 0);
        else fprintf(stderr, "[movie] no audio: %s\n", SDL_GetError());
    }

    int ow, oh;
#ifdef FURY3_GL
    if (s_gl) gl_window_size(&ow, &oh); else
#endif
    SDL_GetRendererOutputSize(s_ren, &ow, &oh);
    int ah = oh - menu_overlay_get_menubar_height(), dw = ow, dh = ow * h / w;
    if (dh > ah) { dh = ah; dw = ah * w / h; }
    SDL_Rect dst = { (ow - dw) / 2, oh - ah + (ah - dh) / 2, dw, dh };
    fprintf(stderr, "[movie] %s: %dx%d, %u us/frame, audio %d Hz\n", path, w, h, us, rate);

    for (;;) {
        uint8_t ch[8];
        if (fread(ch, 1, 8, f) != 8) break;
        uint32_t n = le32p(ch + 4);
        if (!memcmp(ch, "LIST", 4)) { fseek(f, 4, SEEK_CUR); continue; }   /* "rec " groups */
        if (!memcmp(ch, "idx1", 4)) break;
        int video = ch[2] == 'd', audio = ch[2] == 'w';
        if ((!video && !audio) || n > (1u << 18)) { fseek(f, (long)(n + (n & 1)), SEEK_CUR); continue; }
        if (fread(chunk, 1, n + (n & 1), f) < n) break;
        if (audio) {
            if (dev) {
                float vol = menu_overlay_get_volume();
                if (vol < 1.0f) for (uint32_t i = 0; i < n; i++) chunk[i] = (uint8_t)(128 + (int)(((int)chunk[i] - 128) * vol));
                SDL_QueueAudio(dev, chunk, n);
            }
            continue;
        }
        cvid_decode(cv, chunk, n, fb, w, h);
        if (!t0) t0 = host_millis();
        uint32_t due = t0 + (uint32_t)((uint64_t)frames * us / 1000);
        frames++;
        if ((int32_t)(host_millis() - due) > (int32_t)(us / 1000)) continue;    /* late: drop this picture */
        while ((int32_t)(host_millis() - due) < 0) {
            if (movie_poll(&armed)) { skipped = 1; break; }
            SDL_Delay(1);
        }
        if (skipped || movie_poll(&armed)) { skipped = 1; break; }
#ifdef FURY3_GL
        if (s_gl) {
            gl_begin(ow, oh);
            gl_view(dst.x, dst.y, dst.w, dst.h);
            gl_rgba(fb, w, h, 0);
            gl_swap();
        } else
#endif
        {
            SDL_UpdateTexture(tex, NULL, fb, w * 4);
            SDL_SetRenderDrawColor(s_ren, 0, 0, 0, 255);
            SDL_RenderClear(s_ren);
            SDL_RenderCopy(s_ren, tex, NULL, &dst);
            SDL_RenderPresent(s_ren);
        }
        shown++;
        if (snap && frames % 45 == 0) {
            char p[300]; snprintf(p, sizeof(p), "%s/movie_%05u.bmp", snap, frames);
            SDL_Surface* s = SDL_CreateRGBSurfaceWithFormatFrom(fb, w, h, 32, w * 4, SDL_PIXELFORMAT_ARGB8888);
            if (s) { SDL_SaveBMP(s, p); SDL_FreeSurface(s); }
        }
        if (maxms && host_millis() - t0 > maxms) break;
    }
    if (dev && !skipped) {            /* let the last of the sound play out */
        uint32_t t = host_millis();
        while (SDL_GetQueuedAudioSize(dev) && host_millis() - t < 1000 && !movie_poll(&armed)) SDL_Delay(5);
    }
    if (skipped) {                    /* don't let the same press act on the next screen */
        uint32_t t = host_millis();
        while (movie_skip_down() && host_millis() - t < 1500) { SDL_Event e; while (SDL_PollEvent(&e)) { } SDL_Delay(5); }
    }
    fprintf(stderr, "[movie] done: %u frames, %u shown, %u ms%s\n", frames, shown, t0 ? host_millis() - t0 : 0, skipped ? " (skipped)" : "");
done:
    if (dev) SDL_CloseAudioDevice(dev);
    if (game_audio && !g_wave_dev) wave_device_open();
    if (tex) SDL_DestroyTexture(tex);
    free(fb); free(cv); free(chunk);
    fclose(f);
}
