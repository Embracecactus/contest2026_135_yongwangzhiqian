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
  tflite::MicroMutableOpResolver<6> resolver;
  tflite::MicroInterpreter *interpreter = nullptr;
  TfLiteTensor *input = nullptr;
  TfLiteTensor *output = nullptr;
#ifdef __NuttX__
  uint64_t timing_report_ms = 0;
  uint64_t health_report_ms = 0;
  unsigned int health_windows = 0;
  float health_max_wake = 0.0f;
#endif
};

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
      std::strcmp(spec->frontend, BKVOICE_KWS_FRONTEND_ID) != 0)
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

  const auto *graph = flatmodel->subgraphs()->Get(0);
  if (graph->inputs() == nullptr || graph->inputs()->size() != 1 ||
      graph->outputs() == nullptr || graph->outputs()->size() != 1)
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

  auto &resolver = instance->resolver;
  if (resolver.AddConv2D(tflite::Register_CONV_2D_INT8()) != kTfLiteOk ||
      resolver.AddDepthwiseConv2D(tflite::Register_DEPTHWISE_CONV_2D_INT8()) != kTfLiteOk ||
      resolver.AddAveragePool2D(tflite::Register_AVERAGE_POOL_2D_INT8()) != kTfLiteOk ||
      resolver.AddReshape() != kTfLiteOk ||
      resolver.AddFullyConnected(tflite::Register_FULLY_CONNECTED_INT8()) != kTfLiteOk ||
      resolver.AddSoftmax(tflite::Register_SOFTMAX_INT8()) != kTfLiteOk)
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
  auto *input = instance->input;
  auto *output = instance->output;
  if (!bkvoice_kws_quantized(input) || !bkvoice_kws_quantized(output) ||
      input->dims == nullptr || input->dims->size != 4 ||
      input->dims->data[0] != 1 || input->dims->data[1] != BKVOICE_KWS_ROWS ||
      input->dims->data[2] != BKVOICE_KWS_BINS || input->dims->data[3] != 1 ||
      input->bytes != BKVOICE_KWS_FEATURES || output->dims == nullptr ||
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

int bkvoice_kws_model_infer(void *context, const float *features,
                           float scores[BKVOICE_KWS_CLASSES])
{
  auto *model = static_cast<bkvoice_kws_model_s *>(context);
  if (model == nullptr || features == nullptr || scores == nullptr)
    {
      return -EINVAL;
    }

#ifdef __NuttX__
  float feature_min = features[0];
  float feature_max = features[0];
  unsigned int clipped = 0;
#endif
  for (unsigned int i = 0; i < BKVOICE_KWS_FEATURES; i++)
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

size_t bkvoice_kws_model_arena_used(const struct bkvoice_kws_model_s *model)
{
  return model == nullptr ? 0 : model->interpreter->arena_used_bytes();
}

void bkvoice_kws_model_close(struct bkvoice_kws_model_s *model)
{
  if (model != nullptr)
    {
      if (model->interpreter != nullptr)
        {
          model->interpreter->~MicroInterpreter();
          std::free(model->interpreter);
        }
      model->~bkvoice_kws_model_s();
      std::free(model);
    }
}
