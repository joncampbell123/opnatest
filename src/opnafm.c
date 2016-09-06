#include "opnafm.h"

#include "opnatables.h"

enum {
  CH3_MODE_NORMAL = 0,
  CH3_MODE_CSM    = 1,
  CH3_MODE_SE     = 2
};

void fm_slot_reset(struct fm_slot *slot) {
  slot->phase = 0;
  slot->env = 1023;
  slot->env_count = 0;
  slot->env_state = ENV_RELEASE;
  slot->rate_shifter = 0;
  slot->rate_selector = 0;
  slot->rate_mul = 0;
  slot->tl = 0;
  slot->sl = 0;
  slot->ar = 0;
  slot->dr = 0;
  slot->sr = 0;
  slot->rr = 0;
  slot->mul = 0;
  slot->det = 0;
  slot->ks = 0;
  slot->keyon = false;
}


void fm_chan_reset(struct fm_channel *chan) {
  for (int i = 0; i < 4; i++) {
    fm_slot_reset(&chan->slot[i]);
  }

  chan->fbmem1 = 0;
  chan->fbmem2 = 0;
  chan->alg_mem = 0;

  chan->alg = 0;
  chan->fb = 0;
  chan->fnum = 0;
  chan->blk = 0;
}

void fm_opna_reset(struct fm_opna *opna) {
  for (int i = 0; i < 6; i++) {
    fm_chan_reset(&opna->channel[i]);
    opna->lselect[i] = true;
    opna->rselect[i] = true;
  }
  opna->blkfnum_h = 0;
  opna->env_div3 = 0;
//  opna->env_count = 0;
  
  opna->ch3.mode = CH3_MODE_NORMAL;
  for (int i = 0; i < 3; i++) {
    opna->ch3.fnum[i] = 0;
    opna->ch3.blk[i] = 0;
  }
}
#define LIBOPNA_ENABLE_HIRES
// maximum output: 2042<<2 = 8168
int16_t fm_slotout(struct fm_slot *slot, int16_t modulation) {
  unsigned pind = (slot->phase >> 10);
  pind += modulation >> 1;
  bool minus = pind & (1<<(LOGSINTABLEBIT+1));
  bool reverse = pind & (1<<LOGSINTABLEBIT);
  if (reverse) pind = ~pind;
  pind &= (1<<LOGSINTABLEBIT)-1;

#ifdef LIBOPNA_ENABLE_HIRES
  unsigned pind_hires = (slot->phase >> 8);
  pind_hires += modulation << 1;
  minus = pind_hires & (1<<(LOGSINTABLEHIRESBIT+1));
  reverse = pind_hires & (1<<LOGSINTABLEHIRESBIT);
  if (reverse) pind_hires = ~pind_hires;
  pind_hires &= (1<<LOGSINTABLEHIRESBIT)-1;

  int logout = logsintable_hires[pind_hires] + (slot->env << 2) + (slot->tl << 5);
#else
  int logout = logsintable[pind] + (slot->env << 2) + (slot->tl << 5);
#endif // LIBOPNA_ENABLE_HIRES

  int selector = logout & ((1<<EXPTABLEBIT)-1);
  int shifter = logout >> EXPTABLEBIT;
  if (shifter > 13) shifter = 13; 

  int16_t out = (exptable[selector] << 2) >> shifter;
  if (minus) out = -out;
  return out;
}

static unsigned blkfnum2freq(unsigned blk, unsigned fnum) {
  return (fnum << blk) >> 1;
}

#define F(n) (!!(fnum & (1 << ((n)-1))))

static unsigned blkfnum2keycode(unsigned blk, unsigned fnum) {
  unsigned keycode = blk<<2;
  keycode |= F(11) << 1;
  keycode |= (F(11) && (F(10)||F(9)||F(8))) || ((!F(11))&&F(10)&&F(9)&&F(8));
  return keycode;
}

#undef F

static void fm_slotphase(struct fm_slot *slot, unsigned freq) {
// TODO: detune
//  freq += slot->dt;
  unsigned det = dettable[slot->det & 0x3][slot->keycode];
  if (slot->det & 0x4) det = -det;
  freq += det;
  freq &= (1U<<17)-1;
  int mul = slot->mul << 1;
  if (!mul) mul = 1;
  slot->phase += ((freq * mul)>>1);
}

void fm_chanphase(struct fm_channel *chan) {
  unsigned freq = blkfnum2freq(chan->blk, chan->fnum);
  for (int i = 0; i < 4; i++) {
    fm_slotphase(&chan->slot[i], freq);
  }
}

static void fm_chanphase_se(struct fm_channel *chan, struct fm_opna *opna) {
  unsigned freq;
  freq = blkfnum2freq(opna->ch3.blk[2], opna->ch3.fnum[1]);
  fm_slotphase(&chan->slot[0], freq);
  freq = blkfnum2freq(opna->ch3.blk[0], opna->ch3.fnum[0]);
  fm_slotphase(&chan->slot[2], freq);
  freq = blkfnum2freq(opna->ch3.blk[1], opna->ch3.fnum[2]);
  fm_slotphase(&chan->slot[1], freq);
  freq = blkfnum2freq(chan->blk, chan->fnum);
  fm_slotphase(&chan->slot[3], freq);
}

int16_t fm_chanout(struct fm_channel *chan) {
  int16_t fb = chan->fbmem1 + chan->fbmem2;
  int16_t slot0 = chan->fbmem1;
  chan->fbmem1 = chan->fbmem2;
  if (!chan->fb) fb = 0;
  chan->fbmem2 = fm_slotout(&chan->slot[0], fb >> (9 - chan->fb));

  int16_t slot2;
  int16_t out = 0;

  switch (chan->alg) {
  case 0:
    slot2 = fm_slotout(&chan->slot[2], chan->alg_mem);
    chan->alg_mem = fm_slotout(&chan->slot[1], slot0);
    out = fm_slotout(&chan->slot[3], slot2);
    break;
  case 1:
    slot2 = fm_slotout(&chan->slot[2], chan->alg_mem);
    chan->alg_mem = slot0;
    chan->alg_mem += fm_slotout(&chan->slot[1], 0);
    out = fm_slotout(&chan->slot[3], slot2);
    break;
  case 2:
    slot2 = fm_slotout(&chan->slot[2], chan->alg_mem);
    chan->alg_mem = fm_slotout(&chan->slot[1], 0);
    out = fm_slotout(&chan->slot[3], slot0 + slot2);
    break;
  case 3:
    slot2 = fm_slotout(&chan->slot[2], 0);
    out = fm_slotout(&chan->slot[3], slot2 + chan->alg_mem);
    chan->alg_mem = fm_slotout(&chan->slot[1], slot0);
    break;
  case 4:
    out = fm_slotout(&chan->slot[1], slot0);
    slot2 = fm_slotout(&chan->slot[2], 0);
    out += fm_slotout(&chan->slot[3], slot2);
    break;
  case 5:
    out = fm_slotout(&chan->slot[2], chan->alg_mem);
    chan->alg_mem = slot0;
    out += fm_slotout(&chan->slot[1], slot0);
    out += fm_slotout(&chan->slot[3], slot0);
    break;
  case 6:
    out = fm_slotout(&chan->slot[1], slot0);
    out += fm_slotout(&chan->slot[2], 0);
    out += fm_slotout(&chan->slot[3], 0);
    break;
  case 7:
    out = slot0;
    out += fm_slotout(&chan->slot[1], 0);
    out += fm_slotout(&chan->slot[2], 0);
    out += fm_slotout(&chan->slot[3], 0);
    break;
  }
  
  return out;
}

static void fm_slot_setrate(struct fm_slot *slot, int status) {
  int r;
  switch (status) {
  case ENV_ATTACK:
    r = slot->ar;
    break;
  case ENV_DECAY:
    r = slot->dr;
    break;
  case ENV_SUSTAIN:
    r = slot->sr;
    break;
  case ENV_RELEASE:
    r = (slot->rr*2+1);
    break;
  case ENV_OFF:
    return;
  }

  if (!r) {
    slot->rate_selector = 0;
    slot->rate_mul = 0;
    slot->rate_shifter = 0;
    return;
  }

  int rate = 2*r + (slot->keycode >> (3 - slot->ks));

  if (rate > 63) rate = 63;
  int rate_shifter = 11 - (rate >> 2);
  if (rate_shifter < 0) {
    slot->rate_selector = (rate & ((1<<2)-1)) + 4;
    slot->rate_mul = 1<<(-rate_shifter-1);
    slot->rate_shifter = 0;
  } else {
    slot->rate_selector = rate & ((1<<2)-1);
    slot->rate_mul = 1;
    slot->rate_shifter = rate_shifter;
  }
}

static void fm_slotenv(struct fm_slot *slot) {
  slot->env_count++;
  if (!(slot->env_count & ((1<<slot->rate_shifter)-1))) {
    int rate_index = (slot->env_count >> slot->rate_shifter) & 7;
    int env_inc = rateinctable[slot->rate_selector][rate_index];
    env_inc *= slot->rate_mul;

    switch (slot->env_state) {
    int newenv;
    int sl;
    case ENV_ATTACK:
      newenv = slot->env + (((-slot->env-1) * env_inc) >> 4);
      if (newenv <= 0) {
        slot->env = 0;
        slot->env_state = ENV_DECAY;
        fm_slot_setrate(slot, ENV_DECAY);
      } else {
        slot->env = newenv;
      }
      break;
    case ENV_DECAY:
      slot->env += env_inc;
      sl = slot->sl;
      if (sl == 0xf) sl = 0x1f;
      if (slot->env >= (sl << 5)) {
        slot->env_state = ENV_SUSTAIN;
        fm_slot_setrate(slot, ENV_SUSTAIN);
      }
      break;
    case ENV_SUSTAIN:
      slot->env += env_inc;
      if (slot->env >= 1023) slot->env = 1023;
      break;
    case ENV_RELEASE:
      slot->env += env_inc;
      if (slot->env >= 1023) {
        slot->env = 1023;
        slot->env_state = ENV_OFF;
      }
      break;
    }
  }
}

void fm_slot_key(struct fm_channel *chan, int slotnum, bool keyon) {
  struct fm_slot *slot = &chan->slot[slotnum];
  if (keyon) {
    if (!slot->keyon) {
      slot->keyon = true;
      slot->env_state = ENV_ATTACK;
      slot->env_count = 0;
      slot->phase = 0;
      fm_slot_setrate(slot, ENV_ATTACK);
    }
  } else {
    if ((slot->env_state != ENV_OFF) && slot->keyon) {
      slot->keyon = false;
      slot->env_state = ENV_RELEASE;
      fm_slot_setrate(slot, ENV_RELEASE);
    }
  }
}

void fm_slot_set_det(struct fm_slot *slot, unsigned det) {
  det &= 0x7;
  slot->det = det;
}

void fm_slot_set_mul(struct fm_slot *slot, unsigned mul) {
  mul &= 0xf;
  slot->mul = mul;
}

void fm_slot_set_tl(struct fm_slot *slot, unsigned tl) {
  tl &= 0x7f;
  slot->tl = tl;
}

void fm_slot_set_ks(struct fm_slot *slot, unsigned ks) {
  ks &= 0x3;
  slot->ks = ks;
}

void fm_slot_set_ar(struct fm_slot *slot, unsigned ar) {
  ar &= 0x1f;
  slot->ar = ar;
  if (slot->env_state == ENV_ATTACK) {
    fm_slot_setrate(slot, ENV_ATTACK);
  }
}

void fm_slot_set_dr(struct fm_slot *slot, unsigned dr) {
  dr &= 0x1f;
  slot->dr = dr;
  if (slot->env_state == ENV_DECAY) {
    fm_slot_setrate(slot, ENV_DECAY);
  }
}

void fm_slot_set_sr(struct fm_slot *slot, unsigned sr) {
  sr &= 0x1f;
  slot->sr = sr;
  if (slot->env_state == ENV_SUSTAIN) {
    fm_slot_setrate(slot, ENV_SUSTAIN);
  }
}

void fm_slot_set_sl(struct fm_slot *slot, unsigned sl) {
  sl &= 0xf;
  slot->sl = sl;
}

void fm_slot_set_rr(struct fm_slot *slot, unsigned rr) {
  rr &= 0xf;
  slot->rr = rr;
  if (slot->env_state == ENV_RELEASE) {
    fm_slot_setrate(slot, ENV_RELEASE);
  }
}

void fm_chan_set_blkfnum(struct fm_channel *chan, unsigned blk, unsigned fnum) {
  blk &= 0x7;
  fnum &= 0x7ff;
  chan->blk = blk;
  chan->fnum = fnum;
  for (int i = 0; i < 4; i++) {
    chan->slot[i].keycode = blkfnum2keycode(chan->blk, chan->fnum);
    fm_slot_setrate(&chan->slot[i], chan->slot[i].env_state);
  }
}

void fm_chan_set_alg(struct fm_channel *chan, unsigned alg) {
  alg &= 0x7;
  chan->alg = alg;
}

void fm_chan_set_fb(struct fm_channel *chan, unsigned fb) {
  fb &= 0x7;
  chan->fb = fb;
}
//#include <stdio.h>
void fm_opna_fmwritereg(struct fm_opna *opna, unsigned reg, unsigned val) {
  reg &= (1<<9)-1;
  val &= (1<<8)-1;

  switch (reg & 0xff) {
  case 0x27:
    {
      unsigned mode = val >> 6;
      if (mode != opna->ch3.mode) {
//        printf("0x27\n");
//        printf("  mode = %d\n", mode);
        opna->ch3.mode = mode;
      }
    }
    return;
  case 0x28:
    {
      int c = val & 0x3;
      if (c == 3) return;
      if (val & 0x4) c += 3;
      for (int i = 0; i < 4; i++) {
        fm_slot_key(&opna->channel[c], i, (val & (1<<(4+i))));
      }
    }
    return;
  }

  int c = reg & 0x3;
  if (c == 3) return;
  if (reg & (1<<8)) c += 3;
  int s = ((reg & (1<<3)) >> 3) | ((reg & (1<<2)) >> 1);
  struct fm_channel *chan = &opna->channel[c];
  struct fm_slot *slot = &chan->slot[s];
  switch (reg & 0xf0) {
  case 0x30:
    fm_slot_set_det(slot, (val >> 4) & 0x7);
    fm_slot_set_mul(slot, val & 0xf);
    break;
  case 0x40:
    fm_slot_set_tl(slot, val & 0x7f);
    break;
  case 0x50:
    fm_slot_set_ks(slot, (val >> 6) & 0x3);
    fm_slot_set_ar(slot, val & 0x1f);
    break;
  case 0x60:
    fm_slot_set_dr(slot, val & 0x1f);
    break;
  case 0x70:
    fm_slot_set_sr(slot, val & 0x1f);
    break;
  case 0x80:
    fm_slot_set_sl(slot, (val >> 4) & 0xf);
    fm_slot_set_rr(slot, val & 0xf);
    break;
  case 0xa0:
    {
      unsigned blk = (opna->blkfnum_h >> 3) & 0x7;
      unsigned fnum = ((opna->blkfnum_h & 0x7) << 8) | (val & 0xff);
      switch (reg & 0xc) {
      case 0x0:
        fm_chan_set_blkfnum(chan, blk, fnum);
        break;
      case 0x8:
        c %= 3;
        opna->ch3.blk[c] = blk;
        opna->ch3.fnum[c] = fnum;
        break;
      case 0x4:
      case 0xc:
        opna->blkfnum_h = val & 0x3f;
        break;
      }
    }
    break;
  case 0xb0:
    switch (reg & 0xc) {
    case 0x0:
      fm_chan_set_alg(chan, val & 0x7);
      fm_chan_set_fb(chan, (val >> 3) & 0x7);
      break;
    case 0x4:
      opna->lselect[c] = val & 0x80;
      opna->rselect[c] = val & 0x40;
      break;
    }
    break;
  }
}

void fm_chanenv(struct fm_channel *chan) {
  for (int i = 0; i < 4; i++) {
    fm_slotenv(&chan->slot[i]);
  }
}

void fm_opna_fmout(struct fm_opna *opna, int32_t *lbuf, int32_t *rbuf, unsigned len) {
  for (unsigned i = 0; i < len; i++) {
    if (!opna->env_div3) {
      for (int c = 0; c < 6; c++) {
        fm_chanenv(&opna->channel[c]);
      }
      opna->env_div3 = 3;
    }
    opna->env_div3--;

    lbuf[i] = 0;
    rbuf[i] = 0;
    for (int c = 0; c < 6; c++) {
      int16_t o = fm_chanout(&opna->channel[c]);
      // TODO: CSM
      if (c == 2 && opna->ch3.mode != CH3_MODE_NORMAL) {
        fm_chanphase_se(&opna->channel[c], opna);
      } else {
        fm_chanphase(&opna->channel[c]);
      }
      if (opna->lselect[c]) lbuf[i] += o;
      if (opna->rselect[c]) rbuf[i] += o;
    }
  }
}

void fm_opna_fmout2(struct fm_opna *opna, int32_t *sbuf, unsigned samples) {
  for (unsigned i = 0; i < samples; i++) {
    if (!opna->env_div3) {
      for (int c = 0; c < 6; c++) {
        fm_chanenv(&opna->channel[c]);
      }
      opna->env_div3 = 3;
    }
    opna->env_div3--;

    sbuf[2*i+0] = 0;
    sbuf[2*i+1] = 0;

    for (int c = 0; c < 6; c++) {
      int16_t o = fm_chanout(&opna->channel[c]);
      // TODO: CSM
      if (c == 2 && opna->ch3.mode != CH3_MODE_NORMAL) {
        fm_chanphase_se(&opna->channel[c], opna);
      } else {
        fm_chanphase(&opna->channel[c]);
      }
      if (opna->lselect[c]) sbuf[2*i+0] += o;
      if (opna->rselect[c]) sbuf[2*i+1] += o;
    }
  }
}
