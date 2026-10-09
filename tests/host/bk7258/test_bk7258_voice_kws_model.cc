/* SPDX-License-Identifier: Apache-2.0 */
/* Host validation of synthetic contracts or explicitly selected candidate weights. */

#include "bk7258_voice_kws_model.h"
#include "tensorflow/lite/schema/schema_generated.h"
extern "C" {
#include "bk7258_voice_wake_package.h"
#include "bk7258_provision_store.h"
#include <mbedtls/sha256.h>
#include <nuttx/mutex.h>
}

#include <assert.h>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cerrno>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
#include <unistd.h>

extern "C" int nxmutex_lock(mutex_t *lock)
{
  return -pthread_mutex_lock(lock);
}

extern "C" int nxmutex_unlock(mutex_t *lock)
{
  return -pthread_mutex_unlock(lock);
}

static void legacy_descriptor(unsigned char *p,
  const struct bkvoice_wake_package_descriptor_s &d)
{
  std::memcpy(p, d.model_path, 160);
  std::memcpy(p + 160, d.sha256_hex, 65);
  std::memcpy(p + 225, d.label, 32);
  std::memcpy(p + 257, d.phrase, 64);
}

static std::vector<unsigned char> bind_frontend(
  const std::vector<unsigned char> &bytes, const char *frontend, bool duplicate = false)
{
  std::unique_ptr<tflite::ModelT> graph(tflite::GetModel(bytes.data())->UnPack());
  auto buffer = std::make_unique<tflite::BufferT>();
  buffer->data.assign(frontend, frontend + std::strlen(frontend));
  auto metadata = std::make_unique<tflite::MetadataT>();
  metadata->name = "bkvoice.frontend";
  metadata->buffer = graph->buffers.size();
  if (duplicate)
    graph->metadata.push_back(std::make_unique<tflite::MetadataT>(*metadata));
  graph->metadata.push_back(std::move(metadata));
  graph->buffers.push_back(std::move(buffer));
  flatbuffers::DefaultAllocator allocator;
  flatbuffers::FlatBufferBuilder builder(1024, &allocator);
  tflite::FinishModelBuffer(builder, tflite::Model::Pack(builder, graph.get()));
  return {builder.GetBufferPointer(), builder.GetBufferPointer() + builder.GetSize()};
}

static void test_package(const std::vector<unsigned char> &bytes)
{
  auto original = std::filesystem::current_path();
  char temporary[] = "/tmp/bkvoice-wkm.XXXXXX";
  assert(mkdtemp(temporary));
  assert(chdir(temporary) == 0);
  auto bound = bind_frontend(bytes, BKVOICE_KWS_FRONTEND_V2_ID);
  bkvoice_wake_package_s source = {};
  source.model = bound.data();
  source.model_size = bound.size();
  source.frontend_version = 2;
  std::strcpy(source.label, "nihao_openvela");
  std::strcpy(source.phrase, "synthetic test phrase");
  assert(mbedtls_sha256(source.model, source.model_size, source.sha256, 0) == 0);
  assert(bkvoice_wake_package_validate(&source) == 0);
  bkvoice_wake_package_descriptor_s active = {}, old = {}, loaded = {}, previous = {};
  assert(bkvoice_wake_package_stage(&source, &active) == 0);
  assert(active.frontend_version == 2);
  source.frontend_version = 1;
  source.model = bytes.data();
  source.model_size = bytes.size();
  assert(mbedtls_sha256(source.model, source.model_size, source.sha256, 0) == 0);
  assert(bkvoice_wake_package_stage(&source, &old) == 0);
  assert(old.frontend_version == 1);
  for (auto *descriptor : {&old, &active})
    {
      size_t header = bkvoice_wake_package_header_size(descriptor->frontend_version);
      const auto &payload = descriptor->frontend_version == 1 ? bytes : bound;
      std::vector<unsigned char> record(header + payload.size());
      assert(bkvoice_wake_package_encode_header(record.data(), header - 1,
                                                descriptor, payload.size()) == -ENOSPC);
      assert(bkvoice_wake_package_encode_header(record.data(), header,
                                                descriptor, payload.size()) == (int)header);
      std::memcpy(record.data() + header, payload.data(), payload.size());
      bkvoice_wake_package_s decoded = {};
      assert(bkvoice_wake_package_decode(record.data(), record.size(), &decoded) == 0);
      assert(decoded.frontend_version == descriptor->frontend_version);
      assert(decoded.model == record.data() + header && decoded.model_size == payload.size());
      assert(bkvoice_wake_package_validate(&decoded) == 0);
      decoded.frontend_version = descriptor->frontend_version == 1 ? 2 : 1;
      assert(bkvoice_wake_package_validate(&decoded) < 0);
      assert(bkvoice_wake_package_decode(record.data(), record.size() - 1, &decoded) < 0);
      record.push_back(0);
      assert(bkvoice_wake_package_decode(record.data(), record.size(), &decoded) < 0);
      record.pop_back();
      if (descriptor->frontend_version == 2)
        {
          /* An old magic-only WKM1 decoder rejects the new format outright. */
          assert(std::memcmp(record.data(), "WKM1", 4) != 0);
          for (unsigned char version : {0, 3, 255})
            {
              record[139] = version;
              assert(bkvoice_wake_package_decode(record.data(), record.size(), &decoded) < 0);
            }
          record[139] = 2;
          record[136] = 1;
          assert(bkvoice_wake_package_decode(record.data(), record.size(), &decoded) < 0);
        }
      record[3] = '3';
      assert(bkvoice_wake_package_decode(record.data(), record.size(), &decoded) < 0);
    }
  assert(!bkvoice_wake_package_header_size(0));
  assert(!bkvoice_wake_package_frontend_id(3));
  assert(bkvoice_wake_package_commit(&active, &old, 0) == 0);
  uint64_t revision;
  assert(bkvoice_wake_package_load(&loaded, &previous, &revision) == 0);
  assert(revision == 1 && loaded.frontend_version == 2 && previous.frontend_version == 1);
  assert(std::strcmp(loaded.model_path, active.model_path) == 0);
  assert(bkvoice_wake_package_commit(&old, &active, revision) == 0);
  assert(bkvoice_wake_package_load(&loaded, &previous, &revision) == 0);
  assert(revision == 2 && loaded.frontend_version == 1 && previous.frontend_version == 2);

  bkprov_store_s store;
  assert(bkprov_store_open(&store, BKVOICE_WAKE_PACKAGE_ROOT) == 0);
  std::vector<unsigned char> stored(654);
  size_t size;
  assert(bkprov_store_load(&store, stored.data(), stored.size(), &size, &revision, nullptr) == 0);
  assert(size == 654 && std::memcmp(stored.data(), "WKA2", 4) == 0);
  unsigned char transaction[16] = {'T'};
  stored[328] = 3; /* active descriptor frontend_version */
  assert(bkprov_store_commit(&store, revision++, transaction, stored.data(), stored.size()) == 0);
  assert(bkvoice_wake_package_load(&loaded, &previous, &revision) == -EBADMSG);
  stored[328] = 1;
  transaction[1]++;
  assert(bkprov_store_commit(&store, revision++, transaction, stored.data(), stored.size() - 1) == 0);
  assert(bkvoice_wake_package_load(&loaded, &previous, &revision) == -EBADMSG);

  std::vector<unsigned char> legacy(646);
  std::memcpy(legacy.data(), "WKA1", 4);
  legacy_descriptor(legacy.data() + 4, old);
  legacy_descriptor(legacy.data() + 325, old);
  transaction[1]++;
  assert(bkprov_store_commit(&store, revision, transaction, legacy.data(), legacy.size()) == 0);
  assert(bkvoice_wake_package_load(&loaded, &previous, &revision) == 0);
  assert(loaded.frontend_version == 1 && previous.frontend_version == 1);
  std::memset(legacy.data() + 325, 0, 321);
  transaction[1]++;
  assert(bkprov_store_commit(&store, revision, transaction, legacy.data(), legacy.size()) == 0);
  assert(bkvoice_wake_package_load(&loaded, &previous, &revision) == 0);
  assert(loaded.frontend_version == 1 && previous.frontend_version == 0);
  assert(bkvoice_wake_package_commit(&active, &previous, revision) == 0);
  assert(bkvoice_wake_package_load(&loaded, &previous, &revision) == 0);
  assert(loaded.frontend_version == 2 && previous.frontend_version == 0);
  active.frontend_version = 0;
  assert(bkvoice_wake_package_commit(&active, &old, revision) == -EINVAL);
  std::filesystem::current_path(original);
  std::filesystem::remove_all(temporary);
  std::puts("wake-package WKM1/v1 WKM2/v2 WKA1-upgrade WKA2-active-previous unknown-length-rejected");
}

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

/* Exercise actual trained, frontend-bound weights without rewriting their
 * metadata for the synthetic package corruption checks below.  The supplied
 * features and desktop scores share one continuous inference/reset boundary.
 * This checks native TFLM compatibility, not board timing or wake accuracy.
 */
static int check_candidate(const char *frontend, const char *model_path,
                           const char *feature_path, const char *score_path)
{
  alignas(16) unsigned char arena[512 * 1024];
  auto bytes = read_model(model_path);
  auto raw_features = read_model(feature_path);
  auto raw_scores = read_model(score_path);
  assert(!bytes.empty() && !raw_features.empty() && !raw_scores.empty());
  auto spec = spec_for(bytes.data(), bytes.size());
  spec.frontend = frontend;
  bkvoice_kws_model_s *model = nullptr;
  assert(bkvoice_kws_model_open(&spec, arena, sizeof(arena), &model) == 0);
  bool streaming = bkvoice_kws_model_is_streaming(model);
  size_t stride = streaming ? 40 : BKVOICE_KWS_FEATURES;
  assert(raw_features.size() % (stride * sizeof(float)) == 0);
  size_t count = raw_features.size() / (stride * sizeof(float));
  assert(raw_scores.size() == count * BKVOICE_KWS_CLASSES * sizeof(float));
  std::vector<float> features(raw_features.size() / sizeof(float));
  std::vector<float> expected(raw_scores.size() / sizeof(float));
  std::vector<float> first(expected.size());
  std::memcpy(features.data(), raw_features.data(), raw_features.size());
  std::memcpy(expected.data(), raw_scores.data(), raw_scores.size());
  float max_error = 0;
  for (unsigned int pass = 0; pass < 2; pass++)
    {
      bkvoice_kws_model_reset(model);
      for (size_t row = 0; row < count; row++)
        {
          float scores[BKVOICE_KWS_CLASSES];
          const float *input = features.data() + row * stride;
          int ret = streaming ? bkvoice_kws_model_step(model, input, scores) :
                                bkvoice_kws_model_infer(model, input, scores);
          assert(ret == 0);
          for (size_t i = 0; i < BKVOICE_KWS_CLASSES; i++)
            {
              size_t index = row * BKVOICE_KWS_CLASSES + i;
              assert(std::isfinite(scores[i]) && std::isfinite(expected[index]));
              float error = std::fabs(scores[i] - expected[index]);
              if (error > max_error) max_error = error;
              if (error > 2.0f / 256)
                std::fprintf(stderr, "candidate mismatch pass=%u input=%zu class=%zu "
                             "actual=%g expected=%g error=%g\n",
                             pass, row, i, scores[i], expected[index], error);
              assert(error <= 2.0f / 256);
              if (pass == 0) first[index] = scores[i];
              else assert(scores[i] == first[index]);
            }
        }
    }
  std::printf("candidate streaming=%d inputs=%zu arena_used=%zu "
              "max_tflite_score_error=%g reset=exact scope=native-host\n",
              streaming, count, bkvoice_kws_model_arena_used(model), max_error);
  bkvoice_kws_model_close(model);
  return 0;
}

int main(int argc, char **argv)
{
  if (argc == 6 && std::strcmp(argv[1], "--candidate") == 0)
    return check_candidate(argv[2], argv[3], argv[4], argv[5]);

  alignas(16) unsigned char arena[512 * 1024];
  alignas(16) unsigned char small_arena[16];
  float features[BKVOICE_KWS_FEATURES] = {};
  float scores[BKVOICE_KWS_CLASSES];
  std::vector<unsigned char> model_bytes;
  struct bkvoice_kws_model_s *model = nullptr;

  assert(argc == 2 || argc == 4);
  model_bytes = read_model(argv[1]);
  assert(!model_bytes.empty());
  struct bkvoice_kws_model_spec_s spec = spec_for(model_bytes.data(), model_bytes.size());
  assert(bkvoice_kws_model_open(&spec, arena, sizeof(arena), &model) == 0);
  assert(model != nullptr && bkvoice_kws_model_arena_used(model) > 0);
  if (bkvoice_kws_model_is_streaming(model))
    {
      assert(argc == 4);
      auto raw_features = read_model(argv[2]);
      auto raw_scores = read_model(argv[3]);
      assert(!raw_features.empty() && raw_features.size() % (40 * sizeof(float)) == 0);
      size_t rows = raw_features.size() / (40 * sizeof(float));
      assert(raw_scores.size() == rows * 3 * sizeof(float));
      std::vector<float> sequence(raw_features.size() / sizeof(float));
      std::vector<float> expected(raw_scores.size() / sizeof(float));
      std::vector<float> first(rows * 3);
      std::memcpy(sequence.data(), raw_features.data(), raw_features.size());
      std::memcpy(expected.data(), raw_scores.data(), raw_scores.size());
      float max_error = 0;
      for (unsigned int epoch = 0; epoch < 2; epoch++)
        {
          bkvoice_kws_model_reset(model);
          for (size_t row = 0; row < rows; row++)
            {
              assert(bkvoice_kws_model_step(model, sequence.data() + row * 40, scores) == 0);
              for (size_t i = 0; i < 3; i++)
                {
                  float error = std::fabs(scores[i] - expected[row * 3 + i]);
                  if (error > max_error) max_error = error;
                  assert(error <= 2.0f / 256);
                  if (epoch == 0) first[row * 3 + i] = scores[i];
                  else assert(scores[i] == first[row * 3 + i]);
                }
            }
        }
      assert(bkvoice_kws_model_infer(model, features, scores) < 0);
      std::printf("streaming rows=%zu arena=%zu max_tflite_score_error=%g reset=exact\n",
                  rows, bkvoice_kws_model_arena_used(model), max_error);
    }
  else
    {
      assert(bkvoice_kws_model_infer(model, features, scores) == 0);
      assert(bkvoice_kws_model_step(model, features, scores) < 0);
    }
  float total = 0.0f;
  for (float score : scores)
    {
      assert(std::isfinite(score) && score >= 0.0f && score <= 1.0f);
      total += score;
    }

  assert(total > 0.95f && total < 1.05f);
  bool streaming = bkvoice_kws_model_is_streaming(model);
  bkvoice_kws_model_close(model);
  test_package(model_bytes);

  if (streaming)
    {
      /* Well-formed FlatBuffers with an incompatible state or feature shape
       * must fail admission, never become an ambiguous streaming contract.
       */
      for (int wanted_rows : {8, 1})
        {
          auto corrupt = model_bytes;
          auto *graph = tflite::GetModel(corrupt.data())->subgraphs()->Get(0);
          bool changed = false;
          for (auto index : *graph->inputs())
            {
              auto *shape = graph->tensors()->Get(index)->shape();
              if (shape->size() == 4 && shape->Get(1) == wanted_rows)
                {
                  auto *dimension = const_cast<int32_t *>(shape->data());
                  dimension[wanted_rows == 1 ? 2 : 1] -= 1;
                  changed = true;
                  break;
                }
            }
          assert(changed);
          auto bad = spec_for(corrupt.data(), corrupt.size());
          assert(bkvoice_kws_model_open(&bad, arena, sizeof(arena), &model) < 0);
          assert(model == nullptr);
        }
    }

  /* The selected package owns the target label; only the two background
   * classes and the three-output tensor contract are fixed. */
  spec.labels[2] = "nihao_bingbing";
  assert(bkvoice_kws_model_open(&spec, arena, sizeof(arena), &model) == 0);
  bkvoice_kws_model_close(model);
  spec.labels[2] = "";
  assert(bkvoice_kws_model_open(&spec, arena, sizeof(arena), &model) < 0);
  spec = spec_for(model_bytes.data(), model_bytes.size());
  spec.frontend = BKVOICE_KWS_FRONTEND_V2_ID;
  assert(bkvoice_kws_model_open(&spec, arena, sizeof(arena), &model) < 0);
  for (const char *frontend : {BKVOICE_KWS_FRONTEND_ID, BKVOICE_KWS_FRONTEND_V2_ID})
    {
      auto bound = bind_frontend(model_bytes, frontend);
      spec = spec_for(bound.data(), bound.size());
      spec.frontend = frontend;
      assert(bkvoice_kws_model_open(&spec, arena, sizeof(arena), &model) == 0);
      bkvoice_kws_model_close(model);
      spec.frontend = !std::strcmp(frontend, BKVOICE_KWS_FRONTEND_ID) ?
        BKVOICE_KWS_FRONTEND_V2_ID : BKVOICE_KWS_FRONTEND_ID;
      assert(bkvoice_kws_model_open(&spec, arena, sizeof(arena), &model) < 0);
    }
  for (auto invalid : {bind_frontend(model_bytes, "unknown"),
                      bind_frontend(model_bytes, BKVOICE_KWS_FRONTEND_ID, true)})
    {
      spec = spec_for(invalid.data(), invalid.size());
      assert(bkvoice_kws_model_open(&spec, arena, sizeof(arena), &model) < 0);
    }
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
