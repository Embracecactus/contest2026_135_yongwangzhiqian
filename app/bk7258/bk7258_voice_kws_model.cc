/* SPDX-License-Identifier: Apache-2.0 */

#include "bk7258_voice_kws_model.h"

#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <new>

#ifdef __NuttX__
#include <time.h>
#include <syslog.h>
#endif

#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_allocator.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/schema/schema_generated.h"

struct bkvoice_kws_model_s
{
  tflite::MicroMutableOpResolver<9> resolver;
  tflite::MicroInterpreter *interpreter = nullptr;
  TfLiteTensor *input = nullptr;
  TfLiteTensor *output = nullptr;
  bool streaming = false;
  TfLiteTensor *state_input[5] = {};
  TfLiteTensor *state_output[5] = {};
  int8_t *state = nullptr;
  size_t state_bytes = 0;
#ifdef __NuttX__
  uint64_t timing_report_ms = 0;
  uint64_t health_report_ms = 0;
  unsigned int health_windows = 0;
  float health_max_wake = 0.0f;
#endif
};

static const int g_state_rows[] = {8, 16, 32, 64, 128};

static bool bkvoice_kws_quantized(const TfLiteTensor *tensor)
{
  return tensor != nullptr && tensor->type == kTfLiteInt8 &&
         tensor->data.int8 != nullptr &&
         std::isfinite(tensor->params.scale) && tensor->params.scale > 0.0f &&
         tensor->params.zero_point >= -128 && tensor->params.zero_point <= 127;
}

int bkvoice_kws_model_open(const struct bkvoice_kws_model_spec_s *spec,
                           void *arena, size_t arena_bytes,
                           struct bkvoice_kws_model_s **model)
{
  static const char *const labels[] = {"silence", "unknown"};
  const tflite::Model *flatmodel;
  struct bkvoice_kws_model_s *instance;

  if (model == nullptr)
    {
      return -EINVAL;
    }

  *model = nullptr;
  if (spec == nullptr || spec->data == nullptr || spec->bytes < 8 ||
      spec->bytes > BKVOICE_KWS_MODEL_MAX_BYTES || arena == nullptr ||
      arena_bytes < 16 || reinterpret_cast<uintptr_t>(arena) % 16 != 0 ||
      spec->frontend == nullptr ||
      (std::strcmp(spec->frontend, BKVOICE_KWS_FRONTEND_ID) != 0 &&
       std::strcmp(spec->frontend, BKVOICE_KWS_FRONTEND_V2_ID) != 0))
    {
      return -EINVAL;
    }

  for (unsigned int i = 0; i < 2; i++)
    {
      if (spec->labels[i] == nullptr || std::strcmp(spec->labels[i], labels[i]))
        {
          return -EINVAL;
        }
    }

  /* Class 2 is the selected asset's target word. The package owns its label
   * and SHA; the tensor contract remains silence / unknown / target. */
  if (spec->labels[2] == nullptr || spec->labels[2][0] == '\0' ||
      std::strlen(spec->labels[2]) >= 32)
    return -EINVAL;

  flatbuffers::Verifier verifier(spec->data, spec->bytes);
  if (!tflite::VerifyModelBuffer(verifier))
    {
      return -EBADMSG;
    }

  flatmodel = tflite::GetModel(spec->data);
  if (flatmodel->version() != TFLITE_SCHEMA_VERSION ||
      flatmodel->subgraphs() == nullptr || flatmodel->subgraphs()->size() != 1)
    {
      return -ENOTSUP;
    }

  /* The binding lives inside the model bytes covered by the package hash.
   * Only historical unannotated models may use the legacy v1 frontend. */
  bool frontend_bound = false;
  if (flatmodel->metadata() != nullptr)
    for (const auto *metadata : *flatmodel->metadata())
      {
        if (metadata->name() == nullptr || metadata->name()->size() != 16 ||
            std::memcmp(metadata->name()->c_str(), "bkvoice.frontend", 16)) continue;
        if (frontend_bound || flatmodel->buffers() == nullptr ||
            metadata->buffer() >= flatmodel->buffers()->size()) return -EPROTO;
        const auto *binding = flatmodel->buffers()->Get(metadata->buffer())->data();
        if (binding == nullptr || binding->size() != std::strlen(spec->frontend) ||
            std::memcmp(binding->data(), spec->frontend, binding->size()))
          return -EPROTO;
        frontend_bound = true;
      }
  if (!frontend_bound && std::strcmp(spec->frontend, BKVOICE_KWS_FRONTEND_ID))
    return -EPROTO;

  const auto *graph = flatmodel->subgraphs()->Get(0);
  if (graph->inputs() == nullptr || graph->outputs() == nullptr ||
      !((graph->inputs()->size() == 1 && graph->outputs()->size() == 1) ||
        (graph->inputs()->size() == 6 && graph->outputs()->size() == 6)))
    {
      return -ENOTSUP;
    }

  /* Use bounded heap allocation and placement construction. The toolchain's
   * nothrow new still links its exception runtime in this no-exceptions
   * target; allocation failure belongs to this model adapter's error path. */
  void *storage = std::malloc(sizeof(bkvoice_kws_model_s));
  if (storage == nullptr)
    {
      return -ENOMEM;
    }
  instance = new (storage) bkvoice_kws_model_s;
  instance->streaming = graph->inputs()->size() == 6;

  auto &resolver = instance->resolver;
  if (resolver.AddConv2D(tflite::Register_CONV_2D_INT8()) != kTfLiteOk ||
      resolver.AddDepthwiseConv2D(tflite::Register_DEPTHWISE_CONV_2D_INT8()) != kTfLiteOk ||
      resolver.AddAveragePool2D(tflite::Register_AVERAGE_POOL_2D_INT8()) != kTfLiteOk ||
      resolver.AddReshape() != kTfLiteOk ||
      resolver.AddFullyConnected(tflite::Register_FULLY_CONNECTED_INT8()) != kTfLiteOk ||
      resolver.AddSoftmax(tflite::Register_SOFTMAX_INT8()) != kTfLiteOk ||
      resolver.AddConcatenation() != kTfLiteOk ||
      resolver.AddStridedSlice() != kTfLiteOk ||
      resolver.AddQuantize() != kTfLiteOk)
    {
      bkvoice_kws_model_close(instance);
      return -ENOTSUP;
    }

  /* The raw-arena constructor assumes that the allocator and planner fit
   * before AllocateTensors() can report failure. Use the official minimum
   * overhead contract instead of entering placement-new with a null buffer.
   */
  if (arena_bytes < tflite::MicroAllocator::GetDefaultTailUsage(false))
    {
      bkvoice_kws_model_close(instance);
      return -ENOMEM;
    }

  storage = std::malloc(sizeof(tflite::MicroInterpreter));
  if (storage == nullptr)
    {
      bkvoice_kws_model_close(instance);
      return -ENOMEM;
    }
  instance->interpreter = new (storage) tflite::MicroInterpreter(
    flatmodel, resolver, static_cast<uint8_t *>(arena), arena_bytes);

  if (instance->interpreter->AllocateTensors() != kTfLiteOk)
    {
      bkvoice_kws_model_close(instance);
      return -ENOTSUP;
    }

  instance->input = instance->interpreter->input(0);
  instance->output = instance->interpreter->output(0);
  if (instance->streaming)
    {
      instance->input = nullptr;
      instance->output = nullptr;
      int channels = 0;
      for (unsigned int i = 0; i < 6; i++)
        {
          auto *in = instance->interpreter->input(i);
          auto *out = instance->interpreter->output(i);
          if (!bkvoice_kws_quantized(in) || !bkvoice_kws_quantized(out) ||
              in->dims == nullptr || out->dims == nullptr)
            {
              bkvoice_kws_model_close(instance);
              return -EPROTO;
            }

          if (in->dims->size == 4 && in->dims->data[1] == 1 &&
              in->dims->data[2] == BKVOICE_KWS_BINS)
            instance->input = in;
          if (out->dims->size == 2 &&
              out->dims->data[1] == BKVOICE_KWS_CLASSES)
            instance->output = out;
          for (unsigned int j = 0; j < 5; j++)
            {
              if (in->dims->size == 4 &&
                  in->dims->data[1] == g_state_rows[j])
                instance->state_input[j] = in;
              if (out->dims->size == 4 &&
                  out->dims->data[1] == g_state_rows[j])
                instance->state_output[j] = out;
            }
        }

      for (unsigned int j = 0; j < 5; j++)
        {
          auto *in = instance->state_input[j];
          auto *out = instance->state_output[j];
          if (in == nullptr || out == nullptr || in->dims->data[0] != 1 ||
              in->dims->data[2] != 1 || in->dims->data[3] < 1 ||
              in->dims->data[3] > 64 ||
              (channels && channels != in->dims->data[3]) ||
              std::memcmp(in->dims->data, out->dims->data, 4 * sizeof(int)) ||
              in->bytes != static_cast<size_t>(g_state_rows[j] * in->dims->data[3]) ||
              out->bytes != in->bytes)
            {
              bkvoice_kws_model_close(instance);
              return -EPROTO;
            }

          channels = in->dims->data[3];
          instance->state_bytes += in->bytes;
        }

      instance->state = static_cast<int8_t *>(std::malloc(instance->state_bytes));
      if (instance->state == nullptr)
        {
          bkvoice_kws_model_close(instance);
          return -ENOMEM;
        }

      bkvoice_kws_model_reset(instance);
    }

  auto *input = instance->input;
  auto *output = instance->output;
  if (!bkvoice_kws_quantized(input) || !bkvoice_kws_quantized(output) ||
      input->dims == nullptr || input->dims->size != 4 ||
      input->dims->data[0] != 1 ||
      input->dims->data[1] != (instance->streaming ? 1 : BKVOICE_KWS_ROWS) ||
      input->dims->data[2] != BKVOICE_KWS_BINS || input->dims->data[3] != 1 ||
      input->bytes != static_cast<size_t>(instance->streaming ?
        BKVOICE_KWS_BINS : BKVOICE_KWS_FEATURES) || output->dims == nullptr ||
      output->dims->size != 2 || output->dims->data[0] != 1 ||
      output->dims->data[1] != BKVOICE_KWS_CLASSES ||
      output->bytes != BKVOICE_KWS_CLASSES ||
      output->params.zero_point != -128 || output->params.scale != 1.0f / 256)
    {
      bkvoice_kws_model_close(instance);
      return -EPROTO;
    }

  *model = instance;
  return 0;
}

static int bkvoice_kws_model_run(void *context, const float *features,
                                 float scores[BKVOICE_KWS_CLASSES],
                                 bool streaming)
{
  auto *model = static_cast<bkvoice_kws_model_s *>(context);
  if (model == nullptr || features == nullptr || scores == nullptr ||
      model->streaming != streaming)
    {
      return -EINVAL;
    }

#ifdef __NuttX__
  float feature_min = features[0];
  float feature_max = features[0];
  unsigned int clipped = 0;
#endif
  const unsigned int count = streaming ? BKVOICE_KWS_BINS : BKVOICE_KWS_FEATURES;
  for (unsigned int i = 0; i < count; i++)
    {
      if (!std::isfinite(features[i]))
        {
          return -EINVAL;
        }

      float value = features[i] / model->input->params.scale;
      value = std::round(value) + model->input->params.zero_point;
#ifdef __NuttX__
      if (features[i] < feature_min) feature_min = features[i];
      if (features[i] > feature_max) feature_max = features[i];
      if (value < -128.0f || value > 127.0f) clipped++;
#endif
      value = value < -128.0f ? -128.0f : (value > 127.0f ? 127.0f : value);
      model->input->data.int8[i] = static_cast<int8_t>(value);
    }

  if (streaming)
    {
      size_t offset = 0;
      for (unsigned int i = 0; i < 5; i++)
        {
          auto *input = model->state_input[i];
          std::memcpy(input->data.int8, model->state + offset, input->bytes);
          offset += input->bytes;
        }
    }

#ifdef __NuttX__
  struct timespec begin = {};
  struct timespec end = {};
  clock_gettime(CLOCK_MONOTONIC, &begin);
#endif
  const auto invoke_result = model->interpreter->Invoke();
#ifdef __NuttX__
  clock_gettime(CLOCK_MONOTONIC, &end);
  const uint64_t end_ms = static_cast<uint64_t>(end.tv_sec) * 1000 +
                          end.tv_nsec / 1000000;
  const int64_t elapsed_us = (static_cast<int64_t>(end.tv_sec) -
    begin.tv_sec) * 1000000 + (end.tv_nsec - begin.tv_nsec) / 1000;
  if (model->timing_report_ms == 0 ||
      (elapsed_us > BKVOICE_KWS_INFER_HOPS * 20000 &&
       end_ms - model->timing_report_ms >= 30000))
    {
      syslog(LOG_INFO, "BKVOICE KWS inference_us=%lld result=%d\n",
             static_cast<long long>(elapsed_us), static_cast<int>(invoke_result));
      model->timing_report_ms = end_ms;
    }
#endif
  if (invoke_result != kTfLiteOk)
    {
      return -EIO;
    }

  if (streaming)
    {
      /* Tensor planner lifetimes may overlap. Copy every output into the
       * bounded state cache before writing any input for the next invoke.
       * Different affine state quantizers require explicit requantization.
       */

      size_t offset = 0;
      for (unsigned int i = 0; i < 5; i++)
        {
          auto *input = model->state_input[i];
          auto *output = model->state_output[i];
          if (input->params.scale == output->params.scale &&
              input->params.zero_point == output->params.zero_point)
            {
              std::memcpy(model->state + offset, output->data.int8, input->bytes);
              offset += input->bytes;
              continue;
            }
          for (size_t j = 0; j < input->bytes; j++)
            {
              float value = (output->data.int8[j] - output->params.zero_point) *
                            output->params.scale / input->params.scale;
              value = std::round(value) + input->params.zero_point;
              value = value < -128 ? -128 : value > 127 ? 127 : value;
              model->state[offset + j] = static_cast<int8_t>(value);
            }

          offset += input->bytes;
        }
    }

  for (unsigned int i = 0; i < BKVOICE_KWS_CLASSES; i++)
    {
      scores[i] = (model->output->data.int8[i] -
                   model->output->params.zero_point) * model->output->params.scale;
    }

#ifdef __NuttX__
  /* Aggregate health, never PCM or recognized content. The wall-clock window
   * count also distinguishes a stalled input stream from a rejected hotword.
   * Scores are reported in thousandths; this does not alter the wake policy.
   */
  model->health_windows++;
  if (scores[BKVOICE_KWS_WAKE_CLASS] > model->health_max_wake)
    model->health_max_wake = scores[BKVOICE_KWS_WAKE_CLASS];
  if (model->health_report_ms == 0 ||
      end_ms - model->health_report_ms >= 10000)
    {
      syslog(LOG_INFO, "BKVOICE KWS windows=%u scores=%u/%u/%u max_wake=%u "
             "features=%d/%d clipped=%u\n",
             model->health_windows,
             static_cast<unsigned int>(scores[0] * 1000),
             static_cast<unsigned int>(scores[1] * 1000),
             static_cast<unsigned int>(scores[2] * 1000),
             static_cast<unsigned int>(model->health_max_wake * 1000),
             static_cast<int>(feature_min), static_cast<int>(feature_max),
             clipped);
      model->health_report_ms = end_ms;
      model->health_windows = 0;
      model->health_max_wake = 0.0f;
    }
#endif

  return 0;
}

bool bkvoice_kws_model_is_streaming(const struct bkvoice_kws_model_s *model)
{
  return model != nullptr && model->streaming;
}

void bkvoice_kws_model_reset(void *context)
{
  auto *model = static_cast<bkvoice_kws_model_s *>(context);
  if (model == nullptr || !model->streaming || model->state == nullptr) return;
  size_t offset = 0;
  for (unsigned int i = 0; i < 5; i++)
    {
      auto *input = model->state_input[i];
      std::memset(model->state + offset, input->params.zero_point, input->bytes);
      offset += input->bytes;
    }
}

int bkvoice_kws_model_infer(void *context, const float *features,
                           float scores[BKVOICE_KWS_CLASSES])
{
  return bkvoice_kws_model_run(context, features, scores, false);
}

int bkvoice_kws_model_step(void *context, const float *feature,
                          float scores[BKVOICE_KWS_CLASSES])
{
  return bkvoice_kws_model_run(context, feature, scores, true);
}

size_t bkvoice_kws_model_arena_used(const struct bkvoice_kws_model_s *model)
{
  return model == nullptr ? 0 : model->interpreter->arena_used_bytes();
}

void bkvoice_kws_model_close(struct bkvoice_kws_model_s *model)
{
  if (model != nullptr)
    {
      if (model->state != nullptr)
        {
          std::memset(model->state, 0, model->state_bytes);
          std::free(model->state);
        }
      if (model->interpreter != nullptr)
        {
          model->interpreter->~MicroInterpreter();
          std::free(model->interpreter);
        }
      model->~bkvoice_kws_model_s();
      std::free(model);
    }
}
