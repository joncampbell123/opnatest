#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include <SDL.h>
#include <SDL_stdinc.h>
#include "font.h"
#include "opnafm.h"

enum edit_state {
  STATE_DEFAULT,
  STATE_EDIT,
};

static const uint16_t fnumtable_fmp[] = {
  0x026a, // c
  0x028f, // c+
  0x02b6, // d
  0x02df, // d+
  0x030b, // e
  0x0339, // f
  0x036a, // f+
  0x039e, // g
  0x03d5, // g+
  0x0410, // a
  0x044e, // a+
  0x048f, // b
};

static struct {
  SDL_Window *mainwin;
  SDL_Renderer *renderer;
  SDL_Texture *fonttex;
  SDL_Texture *fonttex_neg;
  SDL_AudioDeviceID ad;
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
  uint8_t env_div3;

#define FM_CHAN_NUM 6
  struct {
    struct fm_channel chan;
    bool keyon;
    SDL_Scancode scancode;
  } fmchan[FM_CHAN_NUM];
  int next_chan;
  int octave;
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

static void audiocb(void *userdata, Uint8 *stream, int len) {
  (void)userdata;
  int frames = len/2;
  uint16_t *out = (uint16_t *)stream;
  for (int i = 0; i < frames; i++) {
    if (!g.env_div3) {
      for (int c = 0; c < FM_CHAN_NUM; c++) {
        fm_chanenv(&g.fmchan[c].chan);
      }
      g.env_div3 = 3;
    }
    g.env_div3--;
    int32_t sample = 0;
    for (int c = 0; c < FM_CHAN_NUM; c++) {
      sample += fm_chanout(&g.fmchan[c].chan);
      fm_chanphase(&g.fmchan[c].chan);
    }
    sample /= 2;
    if (sample > INT16_MAX) sample = INT16_MAX;
    if (sample < INT16_MIN) sample = INT16_MIN;
    out[i] = sample;
  }
}

static bool openaudio(void) {
  for (int i = 0; i < FM_CHAN_NUM; i++) {
    fm_chan_reset(&g.fmchan[i].chan);
  }

  SDL_AudioSpec as = {0};
  as.freq = 55467;
  as.format = AUDIO_S16SYS;
  as.channels = 1;
  as.samples = 1024;
  as.callback = audiocb;
  SDL_AudioDeviceID ad = SDL_OpenAudioDevice(0, 0, &as, 0, 0);
  if (!ad) return false;
  
  g.ad = ad;
  return true;
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
  SDL_LockAudioDevice(g.ad);
  if (v < 0) v = 0;
  if (g.pos.y == 0) {
    if (g.pos.x == 1) {
      R(7);
      g.param.alg = v;
      for (int i = 0; i < FM_CHAN_NUM; i++) {
        fm_chan_set_alg(&g.fmchan[i].chan, v);
      }
    }
    if (g.pos.x == 2) {
      R(7);
      g.param.fbl = v;
      for (int i = 0; i < FM_CHAN_NUM; i++) {
        fm_chan_set_fb(&g.fmchan[i].chan, v);
      }
    }
  } else {
    int slotnum = g.pos.y - 1;
    if (g.pos.x == 0) {
      R(31);
      g.param.slot[g.pos.y-1].ar = v;
      for (int i = 0; i < FM_CHAN_NUM; i++) {
        fm_slot_set_ar(&g.fmchan[i].chan.slot[slotnum], v);
      }
    }
    if (g.pos.x == 1) {
      R(31);
      g.param.slot[g.pos.y-1].dr = v;
      for (int i = 0; i < FM_CHAN_NUM; i++) {
        fm_slot_set_dr(&g.fmchan[i].chan.slot[slotnum], v);
      }
    }
    if (g.pos.x == 2) {
      R(31);
      g.param.slot[g.pos.y-1].sr = v;
      for (int i = 0; i < FM_CHAN_NUM; i++) {
        fm_slot_set_sr(&g.fmchan[i].chan.slot[slotnum], v);
      }
    }
    if (g.pos.x == 3) {
      R(15);
      g.param.slot[g.pos.y-1].rr = v;
      for (int i = 0; i < FM_CHAN_NUM; i++) {
        fm_slot_set_rr(&g.fmchan[i].chan.slot[slotnum], v);
      }
    }
    if (g.pos.x == 4) {
      R(15);
      g.param.slot[g.pos.y-1].sl = v;
      for (int i = 0; i < FM_CHAN_NUM; i++) {
        fm_slot_set_sl(&g.fmchan[i].chan.slot[slotnum], v);
      }
    }
    if (g.pos.x == 5) {
      R(127);
      g.param.slot[g.pos.y-1].tl = v;
      for (int i = 0; i < FM_CHAN_NUM; i++) {
        fm_slot_set_tl(&g.fmchan[i].chan.slot[slotnum], v);
      }
    }
    if (g.pos.x == 6) {
      R(3);
      g.param.slot[g.pos.y-1].ks = v;
      for (int i = 0; i < FM_CHAN_NUM; i++) {
        fm_slot_set_ks(&g.fmchan[i].chan.slot[slotnum], v);
      }
    }
    if (g.pos.x == 7) {
      R(15);
      g.param.slot[g.pos.y-1].ml = v;
      for (int i = 0; i < FM_CHAN_NUM; i++) {
        fm_slot_set_mul(&g.fmchan[i].chan.slot[slotnum], v);
      }
    }
    if (g.pos.x == 8) {
      R(7);
      g.param.slot[g.pos.y-1].dt = v;
      for (int i = 0; i < FM_CHAN_NUM; i++) {
        fm_slot_set_det(&g.fmchan[i].chan.slot[slotnum], v);
      }
    }
  }
  SDL_UnlockAudioDevice(g.ad);
}

#undef R

static void parse_and_set(void) {
  int v = 0;
  if (g.inputbuf[0] != ' ') v += (g.inputbuf[0]-'0')*100;
  if (g.inputbuf[1] != ' ') v += (g.inputbuf[1]-'0')*10;
  if (g.inputbuf[2] != ' ') v += (g.inputbuf[2]-'0')*1;
  setval(v);
}

static void render(void) {
  SDL_RenderClear(g.renderer);
  cmvprintr(false, 0,  0, "[[[ OPN Voice Editor ver.0.1 ]]]  2016.08.05");
  cmvprintr(false, 1,  0, "Renderer: %s", g.ri.name);
  cmvprintr(false, 2,  0, "=================== Edit Area ===================  =========== Usage ===========");
  cmvprintr(false, 3,  0, "                                                   ---------");
  cmvprintr(false, 4,  0, "      NUM ALG FBL   Name: abcdefg     Octave: %03d", g.octave);
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
        int v = 0;
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

static int allocate_channel(SDL_Scancode scancode) {
  for (int i = 0; i < FM_CHAN_NUM; i++) {
    int index = (i + g.next_chan) % FM_CHAN_NUM;
    if (g.fmchan[index].keyon) continue;
    g.fmchan[index].keyon = true;
    g.fmchan[index].scancode = scancode;
    g.next_chan = (index+1) % FM_CHAN_NUM;
    return index;
  }
  return -1;
}

static int search_channel(SDL_Scancode scancode) {
  for (int i = 0; i < FM_CHAN_NUM; i++) {
    if (!g.fmchan[i].keyon) continue;
    if (g.fmchan[i].scancode != scancode) continue;
    g.fmchan[i].keyon = false;
    return i;
  }
  return -1;
}

static void handle_key_tone(const SDL_KeyboardEvent *ke) {
  int blk = g.octave;
  unsigned fnum;
  if (ke->keysym.mod & KMOD_SHIFT) blk++;
  switch (ke->keysym.scancode) {
  case SDL_SCANCODE_Q:
    fnum = fnumtable_fmp[8];
    blk--;
    break;
  case SDL_SCANCODE_A:
    fnum = fnumtable_fmp[9];
    blk--;
    break;
  case SDL_SCANCODE_W:
    fnum = fnumtable_fmp[10];
    blk--;
    break;
  case SDL_SCANCODE_S:
    fnum = fnumtable_fmp[11];
    blk--;
    break;
  case SDL_SCANCODE_D:
    fnum = fnumtable_fmp[0];
    break;
  case SDL_SCANCODE_R:
    fnum = fnumtable_fmp[1];
    break;
  case SDL_SCANCODE_F:
    fnum = fnumtable_fmp[2];
    break;
  case SDL_SCANCODE_T:
    fnum = fnumtable_fmp[3];
    break;
  case SDL_SCANCODE_G:
    fnum = fnumtable_fmp[4];
    break;
  case SDL_SCANCODE_H:
    fnum = fnumtable_fmp[5];
    break;
  case SDL_SCANCODE_U:
    fnum = fnumtable_fmp[6];
    break;
  case SDL_SCANCODE_J:
    fnum = fnumtable_fmp[7];
    break;
  case SDL_SCANCODE_I:
    fnum = fnumtable_fmp[8];
    break;
  case SDL_SCANCODE_K:
    fnum = fnumtable_fmp[9];
    break;
  case SDL_SCANCODE_O:
    fnum = fnumtable_fmp[10];
    break;
  case SDL_SCANCODE_L:
    fnum = fnumtable_fmp[11];
    break;
  case SDL_SCANCODE_SEMICOLON:
    fnum = fnumtable_fmp[0];
    blk++;
    break;
  case SDL_SCANCODE_LEFTBRACKET:
    fnum = fnumtable_fmp[1];
    blk++;
    break;
  case SDL_SCANCODE_APOSTROPHE:
    fnum = fnumtable_fmp[2];
    blk++;
    break;
  case SDL_SCANCODE_RIGHTBRACKET:
    fnum = fnumtable_fmp[3];
    blk++;
    break;
  default:
    return;
  }
  // ignore key repeat
  if (ke->repeat) return;
  int chan;
  bool keyon = (ke->state == SDL_PRESSED);
  if (keyon) chan = allocate_channel(ke->keysym.scancode);
  else chan = search_channel(ke->keysym.scancode);
  if (chan == -1) return;

  if (blk < 0) blk = 0;
  if (blk > 7) blk = 7;

  SDL_LockAudioDevice(g.ad);
  if (keyon) {
    fm_chan_set_blkfnum(&g.fmchan[chan].chan, blk, fnum);
  }
  for (int i = 0; i < 4; i++) {
    fm_slot_key(&g.fmchan[chan].chan, i, keyon);
  }
  SDL_UnlockAudioDevice(g.ad);
}

static void handle_key(const SDL_KeyboardEvent *ke) {
  handle_key_tone(ke);
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
      case SDLK_KP_0:
      case SDLK_KP_1:
      case SDLK_KP_2:
      case SDLK_KP_3:
      case SDLK_KP_4:
      case SDLK_KP_5:
      case SDLK_KP_6:
      case SDLK_KP_7:
      case SDLK_KP_8:
      case SDLK_KP_9:
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
      case SDLK_KP_0:
      case SDLK_KP_1:
      case SDLK_KP_2:
      case SDLK_KP_3:
      case SDLK_KP_4:
      case SDLK_KP_5:
      case SDLK_KP_6:
      case SDLK_KP_7:
      case SDLK_KP_8:
      case SDLK_KP_9:
        if (g.inputbuf[0] == ' ') {
          g.inputbuf[0] = g.inputbuf[1];
          g.inputbuf[1] = g.inputbuf[2];
          switch (ke->keysym.sym) {
            case SDLK_0:
            case SDLK_KP_0:
              g.inputbuf[2] = '0';
              break;
            case SDLK_1:
            case SDLK_KP_1:
              g.inputbuf[2] = '1';
              break;
            case SDLK_2:
            case SDLK_KP_2:
              g.inputbuf[2] = '2';
              break;
            case SDLK_3:
            case SDLK_KP_3:
              g.inputbuf[2] = '3';
              break;
            case SDLK_4:
            case SDLK_KP_4:
              g.inputbuf[2] = '4';
              break;
            case SDLK_5:
            case SDLK_KP_5:
              g.inputbuf[2] = '5';
              break;
            case SDLK_6:
            case SDLK_KP_6:
              g.inputbuf[2] = '6';
              break;
            case SDLK_7:
            case SDLK_KP_7:
              g.inputbuf[2] = '7';
              break;
            case SDLK_8:
            case SDLK_KP_8:
              g.inputbuf[2] = '8';
              break;
            case SDLK_9:
            case SDLK_KP_9:
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
      case SDLK_KP_ENTER:
      case SDLK_UP:
      case SDLK_RIGHT:
      case SDLK_TAB:
      case SDLK_LEFT:
        g.state = STATE_DEFAULT;
        parse_and_set();
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
      case SDLK_KP_ENTER:
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
    case SDLK_KP_ENTER:
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
    case SDLK_END:
      g.octave++;
      if (g.octave > 7) g.octave = 7;
      render();
      break;
    case SDLK_HOME:
      g.octave--;
      if (g.octave < 0) g.octave = 0;
      render();
      break;
    default:
      break;
    }
  }
}

int main(int argc, char **argv) {
  (void)argc, (void)argv;
  g.octave = 4;

  if (SDL_Init(SDL_INIT_VIDEO|SDL_INIT_AUDIO) != 0) {
    return 0;
  }
  
  puts(SDL_GetPrefPath("tak", "opnatest"));
  
  if (!openaudio()) {
    SDL_ShowSimpleMessageBox(
      SDL_MESSAGEBOX_ERROR,
      "audio open error",
      "Failed to open audio device.",
      0
    );
    goto err_sdl;
  }
  SDL_PauseAudioDevice(g.ad, 0);
  g.mainwin = SDL_CreateWindow(
    "OPN Voice Editor",
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
