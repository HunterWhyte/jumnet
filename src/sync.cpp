#include "sync.h"
// TODO: add debugging stats

void step(Sync* s, void* state, void* input1, void* input2);

void sync_init(Sync* s, void* initial_state, StateStep step_callback, size_t state_size,
               size_t input_size, uint8_t delay, bool flip) {
  assert(delay < MAX_DELAY);

  s->state_size = state_size;
  s->input_size = input_size;

  s->q_size = delay * 8;

  s->local_tick = 0;
  for (int i = 0; i < s->q_size; i++) {
    s->local_q[i] = calloc(1, s->input_size);
  }
  s->remote_tick = 0;
  for (int i = 0; i < s->q_size; i++) {
    s->remote_q[i] = calloc(1, s->input_size);
  }

  s->ls = malloc(s->state_size);
  s->lt = 0;

  s->cs = malloc(s->state_size);
  memcpy(s->cs, initial_state, s->state_size);
  s->ct = -delay;

  s->state_step = step_callback;

  s->flip = flip;
}

void sync_deinit(Sync* s) {
  for (int i = 0; i < s->q_size; i++) {
    free(s->local_q[i]);
  }
  s->remote_tick = 0;
  for (int i = 0; i < s->q_size; i++) {
    free(s->remote_q[i]);
  }

  free(s->ls);
  free(s->cs);
}

// TODO: // add logic to pause stepping model if we are waiting for too many inputs from remote this also will prevent us from then overrunning and overwriting our local input queue
// advance tick, state is return param
void sync_tick(Sync* s, void* state) {
  // if desync is too large, stop advancing
  if(s->remote_tick + 16 < s->ct){ // TODO: configurable value for max desync
    return;
  }

  s->ct++;

  // waits for the initial input delay
  if (s->ct < 0) {
    // copy in initial state
    memcpy(s->cs, state, s->state_size);
    memcpy(s->ls, state, s->state_size);
    return;
  }

  if (s->lt < (s->ct - 1)) {
    // rollback is active for previous frames
    while ((s->remote_tick > s->lt) && (s->lt < (s->ct - 1))) {
      // we have new inputs, can update our predictions
      s->lt++;
      step(s, s->ls, s->local_q[(s->lt) % s->q_size], s->remote_q[(s->lt) % s->q_size]);
    }

    // go forward
    memcpy(s->cs, s->ls, s->state_size);
    for (int64_t i = s->lt; i < s->ct; i++) {
      // decide if we should use actual value or predictive
      int64_t remote_tick = s->remote_tick > i ? i : s->remote_tick;
      step(s, s->cs, s->local_q[(i) % s->q_size], s->remote_q[remote_tick % s->q_size]);
    }

  } else {
    // no prediction currently active
    if (s->remote_tick >= s->ct) {
      // we have the remote input that we need. Advance as usual
      step(s, s->cs, s->local_q[s->ct % s->q_size], s->remote_q[s->ct % s->q_size]);
      s->lt = s->ct;
      // note that we don't actually stash the current state until we hit scenario where we have to predict
    } else if (s->remote_tick == (s->ct - 1)) {
      // this is first tick that we have missed, have to stash confirmed state and predict next tick
      memcpy(s->ls, s->cs, s->state_size);
      step(s, s->cs, s->local_q[s->ct % s->q_size], s->remote_q[(s->ct - 1) % s->q_size]);
    } else {
      assert(false);
    }
  }
  // todo return a pointer to state instead?
  memcpy(state, s->cs, s->state_size);
}

// inputs get copied into buffer
void sync_localInput(Sync* s, const void* input) {
  if(s->remote_tick + 16 >= s->ct){ // TODO: configurable value for max desync
      s->local_tick++; // dont advance local input while stuck waiting
  }
  // TODO: check that we aren't overrunning the current tick in ciruclar buffer
  memcpy(s->local_q[s->local_tick % s->q_size], input, s->input_size);
}

// tick should increase monotonically - TODO: see if we have to handle lost and out of order packets or if underlying network implementation handles this already
void sync_remoteInput(Sync* s, int64_t tick, const void* input) {
  // check that the remote tick should increase monotonically
  // it can also be paused in the case where remote is waiting for us to catch up
  s->remote_tick = tick;
  // TODO: check that we aren't overrunning the current tick in ciruclar buffer
  memcpy(s->remote_q[s->remote_tick % s->q_size], input, s->input_size);
}

// TODO: should really manage both inputs as if they can miss but this is a
// helper step to flip sides when needed
void step(Sync* s, void* state, void* input1, void* input2) {
  if (s->flip) {
    s->state_step(state, input2, input1);
  } else {
    s->state_step(state, input1, input2);
  }
}