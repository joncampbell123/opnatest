#include <SDL.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include "font.h"

static struct {
  SDL_Window *mainwin;
  SDL_Renderer *renderer;
  SDL_Texture *fonttex;
  SDL_Texture *fonttex_neg;
  uint8_t fontbuf[8*16*(16*6)];
} g;

static void conv_font_raw(void) {
  for (int code = 0; code < 6*16; code++) {
    for (int y = 0; y < 16; y++) {
      for (int x = 0; x < 8; x++) {
        g.fontbuf[(code*16+y)*8+x] = !!(font_raw[code*16+y] & (1<<(7-x)));
      }
    }
  }
}

static bool create_fonttex(void) {
  SDL_Surface *fontsurf = SDL_CreateRGBSurfaceFrom(
    g.fontbuf,
    8, 16*16*6,
    8, 8,
    0, 0, 0, 0
  );
  if (!fontsurf) goto err;
  fontsurf->format->palette->colors[0].r = 0;
  fontsurf->format->palette->colors[0].g = 0;
  fontsurf->format->palette->colors[0].b = 0;
  fontsurf->format->palette->colors[1].r = 255;
  fontsurf->format->palette->colors[1].g = 255;
  fontsurf->format->palette->colors[1].b = 255;
  SDL_Texture *fonttex = SDL_CreateTextureFromSurface(g.renderer, fontsurf);
  if (!fonttex) goto err_surf;
  fontsurf->format->palette->colors[0].r = 255;
  fontsurf->format->palette->colors[0].g = 255;
  fontsurf->format->palette->colors[0].b = 255;
  fontsurf->format->palette->colors[1].r = 0;
  fontsurf->format->palette->colors[1].g = 0;
  fontsurf->format->palette->colors[1].b = 0;
  SDL_Texture *fonttex_neg = SDL_CreateTextureFromSurface(g.renderer, fontsurf);
  if (!fonttex_neg) goto err_fonttex;

  g.fonttex = fonttex;
  g.fonttex_neg = fonttex_neg;
  SDL_FreeSurface(fontsurf);
  return true;

err_fonttex:
  SDL_DestroyTexture(fonttex);
err_surf:
  SDL_FreeSurface(fontsurf);
err:
  return false;
}

void cmvputsr(bool color, int y, int x, const char *text) {
  SDL_Rect srcrect = {
    0, 0, 8, 16
  };
  SDL_Rect dstrect = {
    x*8, y*16, 8, 16
  };
  SDL_Texture *tex = color ? g.fonttex_neg : g.fonttex;
  while (*text) {
    srcrect.y = ((*text)-0x20)*16;
    SDL_RenderCopy(g.renderer, tex, &srcrect, &dstrect);
    dstrect.x += 8;
    text++;
  }
}

void cmvprintr(bool color, int y, int x, const char *format, ...) {
  char buf[81] = {0};
  va_list args;
  va_start(args, format);
  vsnprintf(buf, 81, format, args);
  va_end(args);
  cmvputsr(color, y, x, buf);
}

static void render() {
  SDL_RenderClear(g.renderer);
  cmvprintr(false, 0,  0, "[[[ PMD Voice Editor ver.0.1 ]]] / Programmed by T.Horikawa 2016.08.05");
  cmvprintr(false, 2,  0, "=================== Edit Area ===================  =========== Usage ===========");
  cmvprintr(false, 3,  0, "                                                   ---------");
  cmvprintr(false, 4,  0, "      NUM ALG FBL   Name: abcdefg     Octave: 000");
  cmvprintr(false, 6,  0, "       AR  DR  SR  RR  SL  TL  KS  ML DT1 DT2 AME");
  cmvprintr(false, 13, 0, "=================== Push Area ===================");
  for (int i = 0; i < 4; i++) {
    cmvprintr(false, 7+i, 0, "slot%d", i);
  }
  for (int i = 0; i < 4; i++) {
    for (int j = 0; j < 9; j++) {
      cmvprintr(true, 7+i, 6+4*j, "%03d", i*9+j);
    }
  }
  SDL_RenderPresent(g.renderer);
}

int main(int argc, char **argv) {
  if (SDL_Init(SDL_INIT_VIDEO|SDL_INIT_AUDIO) != 0) {
    return 0;
  }

  g.mainwin = SDL_CreateWindow(
    "PMD Voice Editor",
    SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
    640, 400,
    0
  );
  if (!g.mainwin) {
    SDL_ShowSimpleMessageBox(
      SDL_MESSAGEBOX_ERROR,
      "SDL_CreateWindow error",
      "Failed to create window.",
      0
    );
    goto err_sdl;
  }
  g.renderer = SDL_CreateRenderer(g.mainwin, -1, 0);
  if (!g.renderer) {
    SDL_ShowSimpleMessageBox(
      SDL_MESSAGEBOX_ERROR,
      "SDL_CreateRenderer error",
      "Failed to create renderer.",
      g.mainwin
    );
    goto err_mainwin;
  }
#if 1
  {
    SDL_RendererInfo i;
    if (SDL_GetRendererInfo(g.renderer, &i) == 0) {
      SDL_ShowSimpleMessageBox(
        SDL_MESSAGEBOX_INFORMATION,
        "RendererInfo",
        i.name,
        g.mainwin
      );
    }
  }
#endif
  conv_font_raw();
  if (!create_fonttex()) {
    SDL_ShowSimpleMessageBox(
      SDL_MESSAGEBOX_ERROR,
      "create_fonttex error",
      "Failed to create font texture.",
      g.mainwin
    );
    goto err_renderer;
  }

  SDL_SetRenderDrawColor(g.renderer, 0, 0, 0, 0);
  render();

  SDL_Event e;
  for (;;) {
    if (!SDL_WaitEvent(&e)) {
      goto err_mainwin;
    }
    switch (e.type) {
    case SDL_QUIT:
      goto err_mainwin;
    }
  }
  
err_renderer:
  SDL_DestroyRenderer(g.renderer);
err_mainwin:
  SDL_DestroyWindow(g.mainwin);
err_sdl:
  SDL_Quit();
}
