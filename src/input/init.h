#pragma once

namespace input {
  struct input_t;

  enum class batch_result_e {
    batched,  // This entry was batched with the source entry
    not_batchable,  // Not eligible to batch but continue attempts to batch
    terminate_batch,  // Stop trying to batch with this entry
  };
}  // namespace input
