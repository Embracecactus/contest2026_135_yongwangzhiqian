# SPDX-License-Identifier: Apache-2.0
"""Check real C request output with independent JSON/base64/WAV decoders."""
import base64
import io
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
import wave

ROOT = Path(__file__).resolve().parents[3]
MBEDTLS = ROOT.parent / "apps/crypto/mbedtls/mbedtls"
CJSON = ROOT.parent / "apps/netutils/cjson/cJSON"


class CloudRequestTest(unittest.TestCase):
    def test_wire_and_response_validation(self):
        with tempfile.TemporaryDirectory() as temporary:
            executable = Path(temporary) / "request"
            subprocess.run(
                [
                    "cc",
                    "-std=c11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-ffunction-sections",
                    "-Wl,--gc-sections",
                    "-I",
                    str(ROOT / "app/bk7258"),
                    "-I",
                    str(MBEDTLS / "include"),
                    "-I",
                    str(CJSON),
                    str(ROOT / "app/bk7258/bk7258_cloud_request.c"),
                    str(ROOT / "tests/host/bk7258/test_bk7258_cloud_request.c"),
                    str(CJSON / "cJSON.c"),
                    str(MBEDTLS / "library/base64.c"),
                    str(MBEDTLS / "library/constant_time.c"),
                    str(MBEDTLS / "library/platform_util.c"),
                    "-lm",
                    "-o",
                    str(executable),
                ],
                check=True,
            )
            for size in (2, 4, 6, 722, 724, 726, 728, 960000):
                with self.subTest(size=size):
                    model = "vendor/asr-generic" if size % 3 else "mimo-v2.5-asr"
                    payload = subprocess.check_output(
                        [str(executable), str(size), model, "0"]
                    )
                    for chunk in (1, 3, 127, 1024):
                        pulled = subprocess.check_output(
                            [str(executable), str(size), model, str(chunk)]
                        )
                        self.assertEqual(pulled, payload)
                    request = json.loads(payload)
                    self.assertEqual(request["model"], model)
                    self.assertIs(request["stream"], False)
                    audio = request["messages"][0]["content"][0]["input_audio"]["data"]
                    self.assertTrue(audio.startswith("data:audio/wav;base64,"))
                    raw = base64.b64decode(audio.split(",", 1)[1], validate=True)
                    with wave.open(io.BytesIO(raw), "rb") as wav:
                        self.assertEqual(
                            (
                                wav.getnchannels(),
                                wav.getsampwidth(),
                                wav.getframerate(),
                            ),
                            (1, 2, 16000),
                        )
                        self.assertEqual(wav.getnframes() * 2, size)
                    self.assertEqual(
                        wav.readframes(wav.getnframes()),
                        bytes(i % 251 for i in range(size)),
                    )

            for size in (4, 5, 722, 524288):
                with self.subTest(image_size=size):
                    payload = subprocess.check_output(
                        [str(executable), "image", str(size), "mimo-v2.5", "1"]
                    )
                    for chunk in (2, 3, 127, 1024):
                        pulled = subprocess.check_output(
                            [
                                str(executable),
                                "image",
                                str(size),
                                "mimo-v2.5",
                                str(chunk),
                            ]
                        )
                        self.assertEqual(pulled, payload)
                    request = json.loads(payload)
                    self.assertEqual(request["model"], "mimo-v2.5")
                    content = request["messages"][0]["content"]
                    self.assertEqual(content[0], {"type": "text", "text": "describe"})
                    encoded = content[1]["image_url"]["url"]
                    self.assertTrue(encoded.startswith("data:image/jpeg;base64,"))
                    image = base64.b64decode(encoded.split(",", 1)[1], validate=True)
                    self.assertEqual(len(image), size)
                    self.assertEqual(
                        (image[:2], image[-2:]), (b"\xff\xd8", b"\xff\xd9")
                    )


if __name__ == "__main__":
    unittest.main()
