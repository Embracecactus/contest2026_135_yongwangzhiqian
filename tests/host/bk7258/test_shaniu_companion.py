#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Run the production companion policy and motion detector, external I/O only."""

import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
APP = ROOT / "app/bk7258"

HARNESS = r"""
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "bk7258_motion_core.h"
#include "bk7258_health_service.h"
#include "bk7258_haptic_service.h"
#define CONFIG_BK7258_MOTION_SERVICE 1
#define CONFIG_BK7258_HAPTIC_SERVICE 1
#define CONFIG_BK7258_HEALTH_SERVICE 1
#define CONFIG_BK7258_DISPLAY_SERVICE 1
#define BATTERY_CHARGING 1
#define BATTERY_DISCHARGING 2
#define BATTERY_FULL 3
#define LOG_INFO 6
#define LOG_WARNING 4
#define syslog(...) ((void)0)
static struct bkmotion_rpc_response_s sample;
static struct bk7258_health_service_snapshot_s health;
static int sample_error, display_error, pulse_error, async_fault;
static unsigned visual, displays, pulses, stops, snapshots;
static bool polling;
static int bk7258_motion_service_poll(bool active)
{ polling=active; return 0; }
static int bk7258_motion_service_snapshot(struct bkmotion_rpc_response_s *s)
{ snapshots++; *s=sample; return sample_error; }
static int bk7258_health_service_snapshot_mock(struct bk7258_health_service_snapshot_s *h)
{ *h=health; return 0; }
#define bk7258_health_service_snapshot bk7258_health_service_snapshot_mock
static int bk7258_display_companion(unsigned v, unsigned duration)
{ assert(duration<=1000); if(v) displays++; visual=v; return display_error; }
int bkhaptic_service_product_status(struct bkhaptic_product_status_s *status)
{ memset(status,0,sizeof(*status)); status->fault=async_fault; return 0; }
int bkhaptic_service_pulse(unsigned duration)
{ assert(duration==40); pulses++; return pulse_error; }
int bkhaptic_service_stop_product(void) { stops++; return 0; }
#include "bk7258_agent_companion.inc"
static void feed(uint64_t now, int x, int z, bool admitted, bool idle, bool done)
{
  sample=(struct bkmotion_rpc_response_s){
    .magic=BKMOTION_RPC_MAGIC,.version=1,.command=BKMOTION_RPC_RESPONSE,
    .session=1,.sequence=(uint32_t)now,.flags=BKMOTION_FLAG_SAMPLE_VALID,
    .timestamp_us=now*1000,.x_mms2=x,.z_mms2=z};
  product_companion_step(now,admitted,idle,done);
}
int main(int argc,char **argv)
{
  assert(argc==2);
  health.flags=BK7258_HEALTH_SNAPSHOT_BATTERY_STATE_VALID |
               BK7258_HEALTH_SNAPSHOT_BATTERY_VOLTAGE_VALID;
  health.sequence=1; health.battery_state=BATTERY_DISCHARGING;
  health.sampled_ms=100;
  health.battery_voltage_mv=3900;
  feed(100,0,10000,true,true,false);
  if(!strcmp(argv[1],"move")) {
    feed(200,4000,10000,true,true,false);
    assert(polling && displays==1 && visual==1 && pulses==0);
    for(unsigned t=300;t<=3000;t+=100) feed(t,4000,10000,true,true,false);
    assert(displays==1); /* settle within cooldown is consumed, never queued */
  } else if(!strcmp(argv[1],"still")) {
    for(unsigned t=200;t<=10000;t+=100) feed(t,0,10000,true,true,false);
    assert(displays==0 && pulses==0 && polling);
  } else if(!strcmp(argv[1],"preempt")) {
    feed(200,4000,10000,true,true,false);
    feed(250,8000,10000,false,true,false);
    assert(!polling && visual==0 && stops>0);
    feed(300,8000,10000,true,true,false);
    assert(displays==1 && polling && visual==0);
  } else if(!strcmp(argv[1],"voice")) {
    feed(200,4000,10000,true,false,true);
    assert(!polling && displays==0 && pulses==0);
    feed(300,4000,10000,true,true,false);
    assert(displays==0 && pulses==0);
  } else if(!strcmp(argv[1],"freshness")) {
    sample.timestamp_us=1;
    product_companion_step(500,true,true,false);
    assert(displays==0);
    sample.timestamp_us=900000;
    product_companion_step(600,true,true,false);
    assert(displays==0);
    feed(700,4000,10000,true,true,false);
    assert(displays==0);
    feed(800,8000,10000,true,true,false);
    assert(displays==1);
    sample_error=-EIO;
    feed(900,0,10000,true,true,false);
    assert(visual==0 && displays==1);
  } else if(!strcmp(argv[1],"duplicate")) {
    feed(200,4000,10000,true,true,false);
    for(unsigned t=200;t<=350;t+=10) product_companion_step(t,true,true,false);
    assert(displays==1);
  } else if(!strcmp(argv[1],"completion")) {
    feed(200,0,10000,true,true,true);
    assert(displays==1 && pulses==1);
    feed(250,0,10000,true,true,true);
    assert(pulses==1); /* finite event budget, no retry queue */
    feed(300,4000,10000,true,true,false);
    assert(displays==1); /* motor/table vibration guard */
    feed(2300,0,10000,true,true,true);
    assert(pulses==2);
  } else if(!strcmp(argv[1],"low-battery")) {
    health.battery_voltage_mv=3200; health.sequence++;
    feed(200,0,10000,true,true,true);
    assert(pulses==0);
  } else if(!strcmp(argv[1],"invalid-battery")) {
    health.flags=0; health.sequence++;
    feed(200,0,10000,true,true,true);
    assert(pulses==0);
  } else if(!strcmp(argv[1],"stale-battery")) {
    feed(121000,0,10000,true,true,true);
    assert(pulses==0);
  } else if(!strcmp(argv[1],"display-rejected")) {
    display_error=-EBUSY;
    feed(200,0,10000,true,true,true);
    assert(pulses==0);
    display_error=0;
    feed(300,0,10000,true,true,false);
    assert(displays==1 && pulses==0);
  } else if(!strcmp(argv[1],"motor-error")) {
    pulse_error=-EIO;
    feed(200,0,10000,true,true,true);
    feed(2300,0,10000,true,true,true);
    assert(pulses==1 && stops>0); /* no repeated hardware fault */
  } else if(!strcmp(argv[1],"async-motor-error")) {
    feed(200,0,10000,true,true,true);
    async_fault=-EIO;
    feed(2300,0,10000,true,true,true);
    assert(pulses==1);
  } else if(!strcmp(argv[1],"battery-transition")) {
    health.battery_state=BATTERY_CHARGING;
    feed(200,0,10000,true,true,false);
    assert(displays==1 && visual==4);
    health.flags=0;
    feed(300,0,10000,true,true,false);
    health.flags=3;
    feed(400,0,10000,true,true,false);
    assert(displays==1); /* invalid/recovered sample cannot repeat an alarm */
    health.battery_state=BATTERY_DISCHARGING; health.battery_voltage_mv=3300;
    feed(500,0,10000,true,true,false);
    assert(displays==2 && visual==5);
    health.battery_voltage_mv=3500;
    feed(600,0,10000,true,true,true);
    assert(pulses==0); /* hysteresis remains low until 3600 mV */
  } else assert(0);
  puts("CONTRACT_PASS");
  return 0;
}
"""


class CompanionTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not (APP / "bk7258_agent_companion.inc").exists():
            raise RuntimeError("BLOCKED_INTERFACE: product companion wiring absent")
        cls.temp = tempfile.TemporaryDirectory(prefix="shaniu-companion-")
        cls.addClassCleanup(cls.temp.cleanup)
        source = Path(cls.temp.name) / "case.c"
        cls.binary = Path(cls.temp.name) / "case"
        source.write_text(HARNESS)
        subprocess.run(
            [
                "cc",
                "-std=c11",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-fsanitize=undefined",
                "-fno-sanitize-recover=all",
                "-I",
                str(APP),
                str(source),
                str(APP / "bk7258_motion_core.c"),
                "-o",
                str(cls.binary),
            ],
            check=True,
        )


def add_case(name):
    def run(self):
        result = subprocess.run([self.binary, name], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("CONTRACT_PASS", result.stdout)

    setattr(CompanionTest, "test_" + name.replace("-", "_"), run)


for variant in (
    "still",
    "move",
    "preempt",
    "voice",
    "freshness",
    "duplicate",
    "completion",
    "low-battery",
    "invalid-battery",
    "stale-battery",
    "display-rejected",
    "motor-error",
    "async-motor-error",
    "battery-transition",
):
    add_case(variant)


if __name__ == "__main__":
    unittest.main()
