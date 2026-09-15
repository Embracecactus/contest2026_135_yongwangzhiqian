/* SPDX-License-Identifier: Apache-2.0 */
/* Host validation with an operator-selected synthetic candidate model only. */

#include "bk7258_voice_kws_model.h"

#include <assert.h>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

static std::vector<unsigned char> read_model(const char *path)
{
  std::ifstream stream(path, std::ios::binary);
  return std::vector<unsigned char>(std::istreambuf_iterator<char>(stream),
                                    std::istreambuf_iterator<char>());
}

static struct bkvoice_kws_model_spec_s spec_for(const unsigned char *data,
                                                 size_t bytes)
{
  static const char *const labels[] = {"silence", "unknown", "nihao_openvela"};
  struct bkvoice_kws_model_spec_s spec = {};
  spec.data = data;
  spec.bytes = bytes;
  spec.frontend = BKVOICE_KWS_FRONTEND_ID;
  spec.labels[0] = labels[0];
  spec.labels[1] = labels[1];
  spec.labels[2] = labels[2];
  return spec;
}

int main(int argc, char **argv)
{
  alignas(16) unsigned char arena[512 * 1024];
  alignas(16) unsigned char small_arena[16];
  float features[BKVOICE_KWS_FEATURES] = {};
  float scores[BKVOICE_KWS_CLASSES];
  std::vector<unsigned char> model_bytes;
  struct bkvoice_kws_model_s *model = nullptr;

  assert(argc == 2);
  model_bytes = read_model(argv[1]);
  assert(!model_bytes.empty());
  struct bkvoice_kws_model_spec_s spec = spec_for(model_bytes.data(), model_bytes.size());
  assert(bkvoice_kws_model_open(&spec, arena, sizeof(arena), &model) == 0);
  assert(model != nullptr && bkvoice_kws_model_arena_used(model) > 0);
  assert(bkvoice_kws_model_infer(model, features, scores) == 0);
  float total = 0.0f;
  for (float score : scores)
    {
      assert(std::isfinite(score) && score >= 0.0f && score <= 1.0f);
      total += score;
    }

  assert(total > 0.95f && total < 1.05f);
  bkvoice_kws_model_close(model);

  /* The selected package owns the target label; only the two background
   * classes and the three-output tensor contract are fixed. */
  spec.labels[2] = "nihao_bingbing";
  assert(bkvoice_kws_model_open(&spec, arena, sizeof(arena), &model) == 0);
  bkvoice_kws_model_close(model);
  spec.labels[2] = "";
  assert(bkvoice_kws_model_open(&spec, arena, sizeof(arena), &model) < 0);
  spec = spec_for(model_bytes.data(), model_bytes.size());
  spec.frontend = "wrong";
  assert(bkvoice_kws_model_open(&spec, arena, sizeof(arena), &model) < 0);
  spec = spec_for(model_bytes.data(), model_bytes.size());
  assert(bkvoice_kws_model_open(&spec, small_arena, sizeof(small_arena), &model) < 0);
  spec = spec_for(model_bytes.data(), model_bytes.size() - 1);
  assert(bkvoice_kws_model_open(&spec, arena, sizeof(arena), &model) < 0);
  model_bytes[0] ^= 0xff;
  spec = spec_for(model_bytes.data(), model_bytes.size());
  assert(bkvoice_kws_model_open(&spec, arena, sizeof(arena), &model) < 0);
  return 0;
}
