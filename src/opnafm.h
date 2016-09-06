#ifndef LIBOPNA_OPNAFM_H_INCLUDED
#define LIBOPNA_OPNAFM_H_INCLUDED

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
  ENV_ATTACK,
  ENV_DECAY,
  ENV_SUSTAIN,
  ENV_RELEASE,
  ENV_OFF,
};

struct fm_slot {
  // 20bits, upper 10 bits will be the index to sine table
  uint32_t phase;
  // 10 bits
  uint16_t env;
  uint16_t env_count;
  uint8_t env_state;
  uint8_t rate_shifter;
  uint8_t rate_selector;
  uint8_t rate_mul;

  uint8_t tl;
  uint8_t sl;

  uint8_t ar;
  uint8_t dr;
  uint8_t sr;
  uint8_t rr;

  uint8_t mul;
  uint8_t det;
  uint8_t ks;

  uint8_t keycode;

  bool keyon;
};

struct fm_channel {
  struct fm_slot slot[4];

  // save 2 samples for slot 1 feedback
  uint16_t fbmem1;
  uint16_t fbmem2;
  // save sample for long (>2) chain of slots
  uint16_t alg_mem;

  uint8_t alg;
  uint8_t fb;
  uint16_t fnum;
  uint8_t blk;
};

struct fm_opna {
  struct fm_channel channel[6];

  // remember here what was written on higher byte,
  // actually write when lower byte written
  uint8_t blkfnum_h;
  // channel 3 blk, fnum
  struct {
    uint16_t fnum[3];
    uint8_t blk[3];
    uint8_t mode;
  } ch3;

  // increment counter once per 3 samples
  uint8_t env_div3;
//  uint16_t env_count;

  // pan
  bool lselect[6];
  bool rselect[6];
};

void fm_opna_reset(struct fm_opna *opna);
void fm_opna_fmout(struct fm_opna *opna, int32_t *lbuf, int32_t *rbuf, unsigned len);
void fm_opna_fmout2(struct fm_opna *opna, int32_t *sbuf, unsigned samples);
void fm_opna_fmwritereg(struct fm_opna *opna, unsigned reg, unsigned val);

//
void fm_chan_reset(struct fm_channel *chan);
void fm_chanphase(struct fm_channel *chan);
void fm_chanenv(struct fm_channel *chan);
void fm_chan_set_blkfnum(struct fm_channel *chan, unsigned blk, unsigned fnum);
int16_t fm_chanout(struct fm_channel *chan);
void fm_slot_key(struct fm_channel *chan, int slotnum, bool keyon);

void fm_chan_set_alg(struct fm_channel *chan, unsigned alg);
void fm_chan_set_fb(struct fm_channel *chan, unsigned fb);
void fm_slot_set_ar(struct fm_slot *slot, unsigned ar);
void fm_slot_set_dr(struct fm_slot *slot, unsigned dr);
void fm_slot_set_sr(struct fm_slot *slot, unsigned sr);
void fm_slot_set_rr(struct fm_slot *slot, unsigned rr);
void fm_slot_set_sl(struct fm_slot *slot, unsigned sl);
void fm_slot_set_tl(struct fm_slot *slot, unsigned tl);
void fm_slot_set_ks(struct fm_slot *slot, unsigned ks);
void fm_slot_set_mul(struct fm_slot *slot, unsigned mul);
void fm_slot_set_det(struct fm_slot *slot, unsigned det);

#ifdef __cplusplus
}
#endif

#endif /* LIBOPNA_OPNAFM_H_INCLUDED */
