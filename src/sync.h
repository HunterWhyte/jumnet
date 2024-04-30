#ifndef SYNC_H_
#define SYNC_H_

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define MAX_DELAY 16

typedef void (*StateStep)(void* state, void* input1, void* input2);

struct Sync {
  size_t state_size, input_size;
  StateStep state_step;  // function pointer to user defined

  uint8_t delay;

  void* remote_q[MAX_DELAY * 8];
  int64_t remote_tick;
  void* local_q[MAX_DELAY * 8];
  int64_t local_tick;
  uint8_t q_size;

  void* cs;//current_state
  int64_t ct;//current_tick

  // last model without any predictions TODO: can we get away just with storing these two states, or do we have to store every state since the last verified one - main concern here is the cancelling events Feature once skin is integrated
  void* ls;//last_confirmed_state
  int64_t lt;//last_confirmed_tick

  bool flip; // flips input sides
};

void sync_init(Sync* s, void* initial_state, StateStep step_callback, size_t state_size,
               size_t input_size, uint8_t delay, bool flip);
void sync_deinit(Sync* s);
// advance tick, state is return param
void sync_tick(Sync* s, void* state);
// tick should increase monotonically - TODO: see if we have to handle lost and out of order packets or if underlying network implementation handles this already
// inputs get copied into buffer
void sync_localInput(Sync* s, const void* input);
void sync_remoteInput(Sync* s, int64_t tick, const void* input);

#endif  // SYNC_H
