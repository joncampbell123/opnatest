#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include <SDL.h>
#include <SDL_stdinc.h>
#include "font.h"

enum edit_state {
  STATE_DEFAULT,
  STATE_EDIT,
};

static struct {
  SDL_Window *mainwin;
  SDL_Renderer *renderer;
  SDL_Texture *fonttex;
  SDL_Texture *fonttex_neg;
  uint8_t fontbuf[8*16*(16*6)];
  struct {
    int x;
    int y;
  } pos;
  SDL_RendererInfo ri;
  char inputbuf[3];
  enum edit_state state;
  struct {
    int alg;
    int fbl;
    struct {
      int ar;
      int dr;
      int sr;
      int rr;
      int sl;
      int tl;
      int ks;
      int ml;
      int dt;
    } slot[4];
  } param;
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

static void cmvputchr(bool color, int y, int x, char c) {
  if (c < 0x20 || c >= 0x80) return;
  SDL_Rect srcrect = {
    0, (c-0x20)*16, 8, 16
  };
  SDL_Rect dstrect = {
    x*8, y*16, 8, 16
  };
  SDL_Texture *tex = color ? g.fonttex_neg : g.fonttex;
  SDL_RenderCopy(g.renderer, tex, &srcrect, &dstrect);
}

static void cmvputsr(bool color, int y, int x, const char *text) {
  while (*text) {
    cmvputchr(color, y, x, *text);
    x++;
    text++;
  }
}

static void cmvprintr(bool color, int y, int x, const char *format, ...) {
  char buf[81] = {0};
  va_list args;
  va_start(args, format);
  SDL_vsnprintf(buf, 81, format, args);
  va_end(args);
  cmvputsr(color, y, x, buf);
}

static int getval(void) {
  if (g.pos.y == 0) {
    if (g.pos.x == 1) return g.param.alg;
    if (g.pos.x == 2) return g.param.fbl;
  } else {
    if (g.pos.x == 0) return g.param.slot[g.pos.y-1].ar;
    if (g.pos.x == 1) return g.param.slot[g.pos.y-1].dr;
    if (g.pos.x == 2) return g.param.slot[g.pos.y-1].sr;
    if (g.pos.x == 3) return g.param.slot[g.pos.y-1].rr;
    if (g.pos.x == 4) return g.param.slot[g.pos.y-1].sl;
    if (g.pos.x == 5) return g.param.slot[g.pos.y-1].tl;
    if (g.pos.x == 6) return g.param.slot[g.pos.y-1].ks;
    if (g.pos.x == 7) return g.param.slot[g.pos.y-1].ml;
    if (g.pos.x == 8) return g.param.slot[g.pos.y-1].dt;
  }
  return 0;
}

#define R(max) \
  do { \
    if (v > max) { \
      v = max; \
    } \
  } while (0)

static void setval(int v) {
  if (v < 0) v = 0;
  if (g.pos.y == 0) {
    if (g.pos.x == 1) {
      R(7);
      g.param.alg = v;
    }
    if (g.pos.x == 2) {
      R(7);
      g.param.fbl = v;
    }
  } else {
    if (g.pos.x == 0) {
      R(31);
      g.param.slot[g.pos.y-1].ar = v;
    }
    if (g.pos.x == 1) {
      R(31);
      g.param.slot[g.pos.y-1].dr = v;
    }
    if (g.pos.x == 2) {
      R(31);
      g.param.slot[g.pos.y-1].sr = v;
    }
    if (g.pos.x == 3) {
      R(15);
      g.param.slot[g.pos.y-1].rr = v;
    }
    if (g.pos.x == 4) {
      R(15);
      g.param.slot[g.pos.y-1].sl = v;
    }
    if (g.pos.x == 5) {
      R(127);
      g.param.slot[g.pos.y-1].tl = v;
    }
    if (g.pos.x == 6) {
      R(3);
      g.param.slot[g.pos.y-1].ks = v;
    }
    if (g.pos.x == 7) {
      R(15);
      g.param.slot[g.pos.y-1].ml = v;
    }
    if (g.pos.x == 8) {
      R(7);
      g.param.slot[g.pos.y-1].dt = v;
    }
  }
}

#undef R

static void parse(void) {
  int v = 0;
  if (g.inputbuf[0] != ' ') v += (g.inputbuf[0]-'0')*100;
  if (g.inputbuf[1] != ' ') v += (g.inputbuf[1]-'0')*10;
  if (g.inputbuf[2] != ' ') v += (g.inputbuf[2]-'0')*1;
  setval(v);
}

static void render(void) {
  SDL_RenderClear(g.renderer);
  cmvprintr(false, 0,  0, "[[[ PMD Voice Editor ver.0.1 ]]] / Programmed by T.Horikawa 2016.08.05");
  cmvprintr(false, 1,  0, "Renderer: %s", g.ri.name);
  cmvprintr(false, 2,  0, "=================== Edit Area ===================  =========== Usage ===========");
  cmvprintr(false, 3,  0, "                                                   ---------");
  cmvprintr(false, 4,  0, "      NUM ALG FBL   Name: abcdefg     Octave: 000");
  cmvprintr(false, 6,  0, "       AR  DR  SR  RR  SL  TL  KS  ML DT1 DT2 AME");
  cmvprintr(false, 13, 0, "=================== Push Area ===================");
  for (int i = 0; i < 4; i++) {
    cmvprintr(false, 7+i, 0, "slot%d", i);
  }
  for (int x = 0; x < 3; x++) {
    bool current = x == g.pos.x && 0 == g.pos.y;
    if (g.state == STATE_EDIT && current) {
      for (int i = 0; i < 3; i++) {
        cmvputchr(i == 2, 5, 6+4*x+i, g.inputbuf[i]);
      }
    } else {
      int v = 0;
      if (x == 1) v = g.param.alg;
      if (x == 2) v = g.param.fbl;
      cmvprintr(current, 5, 6+4*x, "%03d", v);
    }
  }
  for (int y = 0; y < 4; y++) {
    for (int x = 0; x < 9; x++) {
      bool current = x == g.pos.x && (y+1) == g.pos.y;
      if (g.state == STATE_EDIT && current) {
        for (int i = 0; i < 3; i++) {
          cmvputchr(i == 2, 7+y, 6+4*x+i, g.inputbuf[i]);
        }
      } else {
        int v;
        if (x == 0) v = g.param.slot[y].ar;
        if (x == 1) v = g.param.slot[y].dr;
        if (x == 2) v = g.param.slot[y].sr;
        if (x == 3) v = g.param.slot[y].rr;
        if (x == 4) v = g.param.slot[y].sl;
        if (x == 5) v = g.param.slot[y].tl;
        if (x == 6) v = g.param.slot[y].ks;
        if (x == 7) v = g.param.slot[y].ml;
        if (x == 8) v = g.param.slot[y].dt;
        cmvprintr(current, 7+y, 6+4*x, "%03d", v);
      }
    }
  }
  SDL_RenderPresent(g.renderer);
}

static void handle_key(const SDL_KeyboardEvent *ke) {
  if (g.state == STATE_DEFAULT) {
    if (ke->state == SDL_PRESSED) {
      switch (ke->keysym.sym) {
      case SDLK_0:
      case SDLK_1:
      case SDLK_2:
      case SDLK_3:
      case SDLK_4:
      case SDLK_5:
      case SDLK_6:
      case SDLK_7:
      case SDLK_8:
      case SDLK_9:
        g.state = STATE_EDIT;
        g.inputbuf[0] = ' ';
        g.inputbuf[1] = ' ';
        g.inputbuf[2] = ' ';
        break;
      case SDLK_ESCAPE:
        {
          SDL_Event e = {0};
          e.type = SDL_QUIT;
          SDL_PushEvent(&e);
        }
        break;
      case SDLK_PAGEUP:
        setval(getval()+1);
        render();
        break;
      case SDLK_PAGEDOWN:
        setval(getval()-1);
        render();
        break;
      default:
        break;
      }
    }
  }
  if (g.state == STATE_EDIT) {
    if (ke->state == SDL_PRESSED) {
      switch (ke->keysym.sym) {
      case SDLK_0:
      case SDLK_1:
      case SDLK_2:
      case SDLK_3:
      case SDLK_4:
      case SDLK_5:
      case SDLK_6:
      case SDLK_7:
      case SDLK_8:
      case SDLK_9:
        if (g.inputbuf[0] == ' ') {
          g.inputbuf[0] = g.inputbuf[1];
          g.inputbuf[1] = g.inputbuf[2];
          switch (ke->keysym.sym) {
            case SDLK_0:
              g.inputbuf[2] = '0';
              break;
            case SDLK_1:
              g.inputbuf[2] = '1';
              break;
            case SDLK_2:
              g.inputbuf[2] = '2';
              break;
            case SDLK_3:
              g.inputbuf[2] = '3';
              break;
            case SDLK_4:
              g.inputbuf[2] = '4';
              break;
            case SDLK_5:
              g.inputbuf[2] = '5';
              break;
            case SDLK_6:
              g.inputbuf[2] = '6';
              break;
            case SDLK_7:
              g.inputbuf[2] = '7';
              break;
            case SDLK_8:
              g.inputbuf[2] = '8';
              break;
            case SDLK_9:
              g.inputbuf[2] = '9';
              break;
          }
          render();
        }
        break;
      case SDLK_BACKSPACE:
        g.inputbuf[2] = g.inputbuf[1];
        g.inputbuf[1] = g.inputbuf[0];
        g.inputbuf[0] = ' ';
        render();
        break;
      case SDLK_DOWN:
      case SDLK_RETURN:
      case SDLK_RETURN2:
      case SDLK_UP:
      case SDLK_RIGHT:
      case SDLK_TAB:
      case SDLK_LEFT:
        g.state = STATE_DEFAULT;
        parse();
        break;
      case SDLK_ESCAPE:
        g.state = STATE_DEFAULT;
        render();
        break;
      default:
        break;
      }
    }
  }
  if (ke->state == SDL_PRESSED) {
    SDL_Keycode sym = ke->keysym.sym;
    // Reverse if shift key pressed
    if (ke->keysym.mod & KMOD_SHIFT) {
      switch (sym) {
      case SDLK_RETURN:
      case SDLK_RETURN2:
        sym = SDLK_UP;
        break;
      case SDLK_TAB:
        sym = SDLK_LEFT;
        break;
      default:
        break;
      }
    }

    switch (sym) {
    case SDLK_DOWN:
    case SDLK_RETURN:
    case SDLK_RETURN2:
      g.pos.y++;
      if (g.pos.y > 4) g.pos.y = 4;
      render();
      return;
    case SDLK_UP:
      g.pos.y--;
      if (g.pos.y == 0) {
        if (g.pos.x > 2) g.pos.x = 2;
      }
      if (g.pos.y < 0) g.pos.y = 0;
      render();
      return;
    case SDLK_RIGHT:
    case SDLK_TAB:
      g.pos.x++;
      if (g.pos.y == 0) {
        if (g.pos.x > 2) {
          g.pos.y = 1;
          g.pos.x = 0;
        }
      } else if (g.pos.y < 4) {
        if (g.pos.x > 8) {
          g.pos.y++;
          g.pos.x = 0;
        }
      } else {
        if (g.pos.x > 8) {
          g.pos.x = 8;
        }
      }
      render();
      return;
    case SDLK_LEFT:
      g.pos.x--;
      if (g.pos.x < 0) {
        if (g.pos.y == 0) {
          g.pos.x = 0;
        } else if (g.pos.y == 1) {
          g.pos.y = 0;
          g.pos.x = 2;
        } else {
          g.pos.y--;
          g.pos.x = 8;
        }
      }
      render();
      return;
    default:
      break;
    }
  }
}

int main(int argc, char **argv) {
  (void)argc, (void)argv;
  
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
  SDL_GetRendererInfo(g.renderer, &g.ri);
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
    case SDL_KEYDOWN:
    case SDL_KEYUP:
      handle_key(&e.key);
      break;
    case SDL_WINDOWEVENT:
      switch (e.window.event) {
      case SDL_WINDOWEVENT_EXPOSED:
        render();
        break;
      }
    }
  }
  
err_renderer:
  SDL_DestroyRenderer(g.renderer);
err_mainwin:
  SDL_DestroyWindow(g.mainwin);
err_sdl:
  SDL_Quit();
  return 0;
}
