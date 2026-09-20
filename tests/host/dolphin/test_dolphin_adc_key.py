#!/usr/bin/env python3

from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[3]


class KeyTest(unittest.TestCase):
    def test_gestures_tolerance_hysteresis_and_overflow(self):
        source = (ROOT / "app/dolphin/dolphin_adc_key.c").read_text()
        functions = source[
            source.index("static void dolphin_adc_key_reset") : source.index(
                "static void *dolphin_adc_key_worker"
            )
        ]
        header = r"""
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#define CONFIG_DOLPHIN_ADC_KEY_RELEASE_MIN 800
#define DOLPHIN_ADC_QUEUE_SIZE 4u
#define DOLPHIN_ADC_STABLE_SAMPLES 3u
#define DOLPHIN_ADC_STABLE_TOLERANCE 8
enum dolphin_adc_key_event_e {
  DOLPHIN_ADC_KEY_NEXT, DOLPHIN_ADC_KEY_CONFIRM,
  DOLPHIN_ADC_KEY_HOME, DOLPHIN_ADC_KEY_ERROR
};
struct dolphin_adc_key_s {
  bool ready, down, long_sent, candidate;
  int baseline, error;
  unsigned char stable, head, count;
  uint32_t down_ms;
  enum dolphin_adc_key_event_e events[DOLPHIN_ADC_QUEUE_SIZE];
};
"""
        main = r"""
int main(void)
{
  struct dolphin_adc_key_s key = {0};
  dolphin_adc_key_sample(&key, 900, 0);
  dolphin_adc_key_sample(&key, 907, 20);
  dolphin_adc_key_sample(&key, 901, 40);
  assert(key.ready);
  dolphin_adc_key_sample(&key, 100, 60);
  dolphin_adc_key_sample(&key, 100, 80);
  dolphin_adc_key_sample(&key, 100, 100);
  assert(key.down && key.count == 0);
  dolphin_adc_key_sample(&key, 900, 120);
  dolphin_adc_key_sample(&key, 900, 140);
  dolphin_adc_key_sample(&key, 900, 160);
  assert(key.count == 1 && key.events[0] == DOLPHIN_ADC_KEY_NEXT);
  key.count = 4;
  assert(!dolphin_adc_key_put(&key, DOLPHIN_ADC_KEY_CONFIRM));
  assert(key.count == 1 && key.events[0] == DOLPHIN_ADC_KEY_ERROR);
  return 0;
}
"""
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            (path / "key.c").write_text(header + functions + main)
            subprocess.run(
                [
                    "cc",
                    "-std=c11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    str(path / "key.c"),
                    "-o",
                    str(path / "key"),
                ],
                check=True,
            )
            subprocess.run([str(path / "key")], check=True)


if __name__ == "__main__":
    unittest.main()
