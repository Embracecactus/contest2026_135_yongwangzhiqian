#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0

"""Deterministic shaniu-eye-pack-v1 builder and verifier.

The pack is a product asset, not a firmware image.  It is intended to live on
the AIDK fixed block device and contains only logical display resources; no
framebuffer number, GPIO, bus or physical left/right mapping is encoded here.
"""

from __future__ import annotations

import binascii
import hashlib
import json
import os
import re
import struct
import tempfile
import zlib
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable


SOURCE_FORMAT = "shaniu-eye-source/1"
PACK_FORMAT = "shaniu-eye-pack-v1"
PACK_MAGIC = b"SHNEYE1\0"
PACK_VERSION = 1
RENDERER_API = 1
CANVAS_WIDTH = 160
CANVAS_HEIGHT = 160
MAX_ENTRIES = 64
MAX_PACK_BYTES = 32 * 1024 * 1024

KIND_PALETTE = 1
KIND_INDEXED_FRAME = 2
CODEC_RAW = 0
CODEC_RLE8 = 1
PIXEL_RGB565LE = 1
PIXEL_INDEX8 = 2
SIDE_SHARED = 0
SIDE_LEFT = 1
SIDE_RIGHT = 2
ENTRY_FLAG_MIRROR_FOR_RIGHT = 1 << 0

HEADER = struct.Struct("<8s8H6I32s32s16s")
ENTRY = struct.Struct("<32s4B4H5I")

_PACK_ID = re.compile(r"[a-z0-9][a-z0-9._-]{0,30}")
_EXPRESSION_ID = re.compile(r"[a-z][a-z0-9_-]{0,20}")
_ENTRY_NAME = re.compile(r"[a-z][a-z0-9_./-]{0,30}")
_HEX_COLOR = re.compile(r"#[0-9a-fA-F]{6}")


class EyePackError(ValueError):
    """Raised when an eye source or pack violates its bounded contract."""


@dataclass(frozen=True)
class EntryInfo:
    name: str
    kind: int
    codec: int
    pixel_format: int
    side: int
    flags: int
    width: int
    height: int
    palette_count: int
    payload_offset: int
    stored_size: int
    decoded_size: int
    crc32: int


@dataclass(frozen=True)
class EyePackReport:
    path: Path
    pack_id: str
    revision: int
    renderer_api: int
    width: int
    height: int
    entries: tuple[EntryInfo, ...]
    stored_size: int
    decoded_bytes: int
    source_sha256: str
    sha256: str


@dataclass(frozen=True)
class _BuiltEntry:
    name: str
    kind: int
    codec: int
    pixel_format: int
    side: int
    flags: int
    width: int
    height: int
    palette_count: int
    stored: bytes
    decoded: bytes


def _require_keys(
    value: object,
    *,
    required: set[str],
    optional: set[str] = frozenset(),
    label: str,
) -> dict[str, object]:
    if not isinstance(value, dict):
        raise EyePackError(f"{label} must be an object")
    keys = set(value)
    missing = required - keys
    unknown = keys - required - optional
    if missing:
        raise EyePackError(f"{label} is missing fields: {', '.join(sorted(missing))}")
    if unknown:
        raise EyePackError(f"{label} has unknown fields: {', '.join(sorted(unknown))}")
    return value


def _integer(value: object, minimum: int, maximum: int, label: str) -> int:
    if isinstance(value, bool) or not isinstance(value, int):
        raise EyePackError(f"{label} must be an integer")
    if value < minimum or value > maximum:
        raise EyePackError(f"{label} must be in {minimum}..{maximum}")
    return value


def _boolean(value: object, label: str) -> bool:
    if not isinstance(value, bool):
        raise EyePackError(f"{label} must be a boolean")
    return value


def _canonical_json(document: dict[str, object]) -> bytes:
    return (
        json.dumps(document, sort_keys=True, separators=(",", ":"), ensure_ascii=True)
        + "\n"
    ).encode("ascii")


def _unique_json_object(pairs: list[tuple[str, object]]) -> dict[str, object]:
    result: dict[str, object] = {}
    for key, value in pairs:
        if key in result:
            raise EyePackError(f"eye source has duplicate JSON field: {key}")
        result[key] = value
    return result


def _regular(path: Path, label: str) -> Path:
    try:
        status = path.lstat()
    except OSError as error:
        raise EyePackError(f"missing {label}: {path}") from error
    if path.is_symlink() or not path.is_file():
        raise EyePackError(f"{label} must be a regular non-symlink file: {path}")
    if status.st_size <= 0:
        raise EyePackError(f"{label} must not be empty: {path}")
    return path


def _decode_text(path: Path) -> dict[str, object]:
    raw = _regular(path, "eye source").read_bytes()
    if len(raw) > 1024 * 1024:
        raise EyePackError("eye source exceeds the 1 MiB authoring limit")
    try:
        document = json.loads(
            raw.decode("utf-8"), object_pairs_hook=_unique_json_object
        )
    except (UnicodeError, json.JSONDecodeError) as error:
        raise EyePackError(f"eye source is not valid UTF-8 JSON: {error}") from error
    if not isinstance(document, dict):
        raise EyePackError("eye source root must be an object")
    return document


def _side(value: object, label: str) -> int:
    mapping = {"shared": SIDE_SHARED, "left": SIDE_LEFT, "right": SIDE_RIGHT}
    if not isinstance(value, str) or value not in mapping:
        raise EyePackError(f"{label} must be shared, left or right")
    return mapping[value]


def _palette(document: dict[str, object]) -> tuple[tuple[int, int, int], ...]:
    value = document["palette"]
    if not isinstance(value, list) or not 2 <= len(value) <= 256:
        raise EyePackError("palette must contain 2..256 RGB colors")
    result: list[tuple[int, int, int]] = []
    for index, color in enumerate(value):
        if not isinstance(color, str) or _HEX_COLOR.fullmatch(color) is None:
            raise EyePackError(f"palette[{index}] must be #RRGGBB")
        result.append(tuple(int(color[offset:offset + 2], 16) for offset in (1, 3, 5)))
    return tuple(result)


def _rgb565le(palette: Iterable[tuple[int, int, int]]) -> bytes:
    output = bytearray()
    for red, green, blue in palette:
        value = ((red >> 3) << 11) | ((green >> 2) << 5) | (blue >> 3)
        output.extend(struct.pack("<H", value))
    return bytes(output)


def _color(value: object, count: int, label: str) -> int:
    return _integer(value, 0, count - 1, label)


def _coordinate(value: object, extent: int, label: str) -> int:
    return _integer(value, -extent * 2, extent * 3, label)


def _draw_rectangle(
    pixels: bytearray,
    width: int,
    height: int,
    layer: dict[str, object],
    color: int,
    label: str,
) -> None:
    x = _coordinate(layer["x"], width, f"{label}.x")
    y = _coordinate(layer["y"], height, f"{label}.y")
    span_x = _integer(layer["width"], 1, width * 3, f"{label}.width")
    span_y = _integer(layer["height"], 1, height * 3, f"{label}.height")
    x0 = max(0, x)
    y0 = max(0, y)
    x1 = min(width, x + span_x)
    y1 = min(height, y + span_y)
    if x0 >= x1 or y0 >= y1:
        return
    row = bytes([color]) * (x1 - x0)
    for line in range(y0, y1):
        start = line * width + x0
        pixels[start:start + len(row)] = row


def _draw_ellipse(
    pixels: bytearray,
    width: int,
    height: int,
    layer: dict[str, object],
    color: int,
    label: str,
) -> None:
    cx = _coordinate(layer["cx"], width, f"{label}.cx")
    cy = _coordinate(layer["cy"], height, f"{label}.cy")
    rx = _integer(layer["rx"], 1, width * 2, f"{label}.rx")
    ry = _integer(layer["ry"], 1, height * 2, f"{label}.ry")
    x0 = max(0, cx - rx)
    x1 = min(width - 1, cx + rx)
    y0 = max(0, cy - ry)
    y1 = min(height - 1, cy + ry)
    rx2 = rx * rx
    ry2 = ry * ry
    limit = 4 * rx2 * ry2
    for y in range(y0, y1 + 1):
        dy = 2 * y + 1 - 2 * cy
        dy_term = dy * dy * rx2
        row = y * width
        for x in range(x0, x1 + 1):
            dx = 2 * x + 1 - 2 * cx
            if dx * dx * ry2 + dy_term <= limit:
                pixels[row + x] = color


def _inside_polygon(px: int, py: int, points: tuple[tuple[int, int], ...]) -> bool:
    inside = False
    previous_x, previous_y = points[-1]
    for current_x, current_y in points:
        if (current_y > py) != (previous_y > py):
            dy = previous_y - current_y
            lhs = (px - current_x) * dy
            rhs = (previous_x - current_x) * (py - current_y)
            crosses = lhs < rhs if dy > 0 else lhs > rhs
            if crosses:
                inside = not inside
        previous_x, previous_y = current_x, current_y
    return inside


def _draw_polygon(
    pixels: bytearray,
    width: int,
    height: int,
    layer: dict[str, object],
    color: int,
    label: str,
) -> None:
    raw = layer["points"]
    if not isinstance(raw, list) or not 3 <= len(raw) <= 16:
        raise EyePackError(f"{label}.points must contain 3..16 points")
    points: list[tuple[int, int]] = []
    for index, point in enumerate(raw):
        if not isinstance(point, list) or len(point) != 2:
            raise EyePackError(f"{label}.points[{index}] must be [x,y]")
        x = _coordinate(point[0], width, f"{label}.points[{index}].x")
        y = _coordinate(point[1], height, f"{label}.points[{index}].y")
        points.append((x * 2, y * 2))
    frozen = tuple(points)
    x0 = max(0, min(point[0] for point in points) // 2)
    x1 = min(width - 1, max(point[0] for point in points) // 2)
    y0 = max(0, min(point[1] for point in points) // 2)
    y1 = min(height - 1, max(point[1] for point in points) // 2)
    for y in range(y0, y1 + 1):
        row = y * width
        for x in range(x0, x1 + 1):
            if _inside_polygon(2 * x + 1, 2 * y + 1, frozen):
                pixels[row + x] = color


def _render_expression(
    expression: dict[str, object],
    *,
    width: int,
    height: int,
    palette_count: int,
    label: str,
    source_dir: Path,
    palette: tuple[tuple[int, int, int], ...],
) -> bytes:
    bitmap = expression.get("bitmap")
    if bitmap is not None:
        if "background" in expression or "layers" in expression:
            raise EyePackError(f"{label} bitmap cannot be combined with background/layers")
        spec = _require_keys(bitmap, required={"path", "sha256", "crop"}, label=f"{label}.bitmap")
        path = spec["path"]
        digest = spec["sha256"]
        crop = spec["crop"]
        if not isinstance(path, str) or not path or Path(path).is_absolute():
            raise EyePackError(f"{label}.bitmap.path must be a relative path")
        if not isinstance(digest, str) or re.fullmatch(r"[0-9a-fA-F]{64}", digest) is None:
            raise EyePackError(f"{label}.bitmap.sha256 must be 64 hex characters")
        bitmap_path = (source_dir / path).resolve()
        try:
            bitmap_path.relative_to(source_dir.resolve())
        except ValueError as error:
            raise EyePackError(f"{label}.bitmap.path escapes the source directory") from error
        raw = _regular(bitmap_path, f"{label}.bitmap.path").read_bytes()
        if hashlib.sha256(raw).hexdigest().lower() != digest.lower():
            raise EyePackError(f"{label}.bitmap.sha256 does not match the PNG")
        if not isinstance(crop, list) or len(crop) != 4 or any(
            isinstance(value, bool) or not isinstance(value, int) for value in crop
        ):
            raise EyePackError(f"{label}.bitmap.crop must be [x,y,width,height]")
        try:
            from PIL import Image
            with Image.open(bitmap_path) as image:
                if image.format != "PNG":
                    raise EyePackError(f"{label}.bitmap.path must be a PNG")
                image.load()
                x, y, crop_width, crop_height = crop
                if x < 0 or y < 0 or crop_width <= 0 or crop_height <= 0 \
                        or x + crop_width > image.width or y + crop_height > image.height:
                    raise EyePackError(f"{label}.bitmap.crop is outside the PNG")
                image = image.convert("RGBA").crop((x, y, x + crop_width, y + crop_height))
                if "A" in image.getbands():
                    background = Image.new("RGBA", image.size, (*palette[0], 255))
                    image = Image.alpha_composite(background, image)
                image = image.convert("RGB")
                image = image.resize((width, height), Image.Resampling.LANCZOS)
                colors = tuple(palette)
                pixels = bytearray()
                for red, green, blue in image.getdata():
                    pixels.append(min(range(len(colors)), key=lambda i:
                        (red - colors[i][0]) ** 2 + (green - colors[i][1]) ** 2 +
                        (blue - colors[i][2]) ** 2))
                return bytes(pixels)
        except EyePackError:
            raise
        except (OSError, ValueError, RuntimeError) as error:
            raise EyePackError(f"{label}.bitmap PNG decode failed: {error}") from error
        except ImportError as error:
            raise EyePackError("bitmap input requires Pillow; install it for PNG import") from error
    if "bitmap" in expression:
        raise EyePackError(f"{label}.bitmap must be an object")
    background = _color(expression["background"], palette_count, f"{label}.background")
    pixels = bytearray([background]) * (width * height)
    layers = expression["layers"]
    if not isinstance(layers, list) or not 1 <= len(layers) <= 48:
        raise EyePackError(f"{label}.layers must contain 1..48 shapes")
    schemas = {
        "ellipse": {"type", "color", "cx", "cy", "rx", "ry"},
        "rectangle": {"type", "color", "x", "y", "width", "height"},
        "polygon": {"type", "color", "points"},
    }
    for index, value in enumerate(layers):
        layer_label = f"{label}.layers[{index}]"
        if not isinstance(value, dict) or not isinstance(value.get("type"), str):
            raise EyePackError(f"{layer_label} must be a shape object")
        kind = value["type"]
        if kind not in schemas:
            raise EyePackError(f"{layer_label}.type is unsupported: {kind!r}")
        layer = _require_keys(value, required=schemas[kind], label=layer_label)
        color = _color(layer["color"], palette_count, f"{layer_label}.color")
        if kind == "ellipse":
            _draw_ellipse(pixels, width, height, layer, color, layer_label)
        elif kind == "rectangle":
            _draw_rectangle(pixels, width, height, layer, color, layer_label)
        else:
            _draw_polygon(pixels, width, height, layer, color, layer_label)
    return bytes(pixels)


def _rle8(data: bytes) -> bytes:
    output = bytearray()
    offset = 0
    while offset < len(data):
        value = data[offset]
        count = 1
        while offset + count < len(data) and count < 255 \
                and data[offset + count] == value:
            count += 1
        output.extend((count, value))
        offset += count
    return bytes(output)


def _unrle8(data: bytes, expected: int, label: str) -> bytes:
    if len(data) % 2 != 0:
        raise EyePackError(f"{label} has a truncated RLE8 pair")
    output = bytearray()
    for offset in range(0, len(data), 2):
        count = data[offset]
        if count == 0:
            raise EyePackError(f"{label} contains a zero-length RLE8 run")
        if len(output) + count > expected:
            raise EyePackError(f"{label} RLE8 data exceeds decoded size")
        output.extend(bytes([data[offset + 1]]) * count)
    if len(output) != expected:
        raise EyePackError(f"{label} RLE8 data has the wrong decoded size")
    return bytes(output)


def _padded_ascii(value: str, size: int, label: str) -> bytes:
    try:
        encoded = value.encode("ascii")
    except UnicodeEncodeError as error:
        raise EyePackError(f"{label} must be ASCII") from error
    if not encoded or len(encoded) >= size:
        raise EyePackError(f"{label} must use 1..{size - 1} ASCII bytes")
    return encoded + bytes(size - len(encoded))


def _read_padded_ascii(value: bytes, pattern: re.Pattern[str], label: str) -> str:
    end = value.find(b"\0")
    if end <= 0 or any(value[end:]):
        raise EyePackError(f"{label} has malformed NUL padding")
    try:
        decoded = value[:end].decode("ascii")
    except UnicodeDecodeError as error:
        raise EyePackError(f"{label} is not ASCII") from error
    if pattern.fullmatch(decoded) is None:
        raise EyePackError(f"{label} has an invalid identifier")
    return decoded


def _align(value: int, alignment: int = 4) -> int:
    return (value + alignment - 1) & ~(alignment - 1)


def _source_entries(
    document: dict[str, object],
    source_dir: Path,
) -> tuple[str, int, int, bytes, tuple[tuple[int, int, int], ...], list[_BuiltEntry]]:
    root = _require_keys(
        document,
        required={
            "format", "pack_id", "revision", "renderer_api", "canvas",
            "palette", "expressions",
        },
        optional={"description"},
        label="eye source",
    )
    if root["format"] != SOURCE_FORMAT:
        raise EyePackError(f"unsupported eye source format: {root['format']!r}")
    if "description" in root:
        description = root["description"]
        if not isinstance(description, str) or not 1 <= len(description) <= 256:
            raise EyePackError("description must contain 1..256 characters")
    pack_id = root["pack_id"]
    if not isinstance(pack_id, str) or _PACK_ID.fullmatch(pack_id) is None:
        raise EyePackError("pack_id must match [a-z0-9][a-z0-9._-]{0,30}")
    revision = _integer(root["revision"], 1, 0xffffffff, "revision")
    renderer_api = _integer(root["renderer_api"], 1, 0xffff, "renderer_api")
    if renderer_api != RENDERER_API:
        raise EyePackError(f"renderer_api must be {RENDERER_API}")
    canvas = _require_keys(
        root["canvas"], required={"width", "height"}, label="canvas"
    )
    width = _integer(canvas["width"], 1, 0xffff, "canvas.width")
    height = _integer(canvas["height"], 1, 0xffff, "canvas.height")
    if (width, height) != (CANVAS_WIDTH, CANVAS_HEIGHT):
        raise EyePackError(
            f"canvas must be {CANVAS_WIDTH}x{CANVAS_HEIGHT} for the AIDK panels"
        )
    palette = _palette(root)
    palette_bytes = _rgb565le(palette)
    entries = [
        _BuiltEntry(
            name="palette/default",
            kind=KIND_PALETTE,
            codec=CODEC_RAW,
            pixel_format=PIXEL_RGB565LE,
            side=SIDE_SHARED,
            flags=0,
            width=0,
            height=0,
            palette_count=len(palette),
            stored=palette_bytes,
            decoded=palette_bytes,
        )
    ]
    expressions = root["expressions"]
    if not isinstance(expressions, list) or not 1 <= len(expressions) < MAX_ENTRIES:
        raise EyePackError(f"expressions must contain 1..{MAX_ENTRIES - 1} entries")
    seen: set[tuple[str, int]] = set()
    sides_by_expression: dict[str, set[int]] = {}
    has_neutral = False
    built_frames: list[_BuiltEntry] = []
    for index, value in enumerate(expressions):
        label = f"expressions[{index}]"
        expression = _require_keys(
            value,
            required={"id", "side", "mirror_for_right"},
            optional={"background", "layers", "bitmap"},
            label=label,
        )
        expression_id = expression["id"]
        if not isinstance(expression_id, str) \
                or _EXPRESSION_ID.fullmatch(expression_id) is None:
            raise EyePackError(f"{label}.id has an invalid identifier")
        side = _side(expression["side"], f"{label}.side")
        mirror = _boolean(expression["mirror_for_right"], f"{label}.mirror_for_right")
        if mirror and side != SIDE_SHARED:
            raise EyePackError(f"{label} can mirror only a shared expression")
        identity = (expression_id, side)
        if identity in seen:
            raise EyePackError(f"duplicate expression/side: {expression_id}")
        seen.add(identity)
        sides_by_expression.setdefault(expression_id, set()).add(side)
        has_neutral = has_neutral or expression_id == "neutral"
        has_bitmap = "bitmap" in expression
        if has_bitmap and ("background" in expression or "layers" in expression):
            raise EyePackError(f"{label} bitmap cannot be combined with background/layers")
        if not has_bitmap and ("background" not in expression or "layers" not in expression):
            raise EyePackError(f"{label} requires bitmap or background/layers")
        decoded = _render_expression(
            expression,
            width=width,
            height=height,
            palette_count=len(palette),
            label=label,
            source_dir=source_dir,
            palette=palette,
        )
        compressed = _rle8(decoded)
        if len(compressed) < len(decoded):
            codec = CODEC_RLE8
            stored = compressed
        else:
            codec = CODEC_RAW
            stored = decoded
        suffix = {SIDE_LEFT: "left", SIDE_RIGHT: "right"}.get(side)
        name = f"expression/{expression_id}"
        if suffix is not None:
            name = f"{name}/{suffix}"
        if _ENTRY_NAME.fullmatch(name) is None:
            raise EyePackError(f"derived entry name is too long or invalid: {name}")
        built_frames.append(
            _BuiltEntry(
                name=name,
                kind=KIND_INDEXED_FRAME,
                codec=codec,
                pixel_format=PIXEL_INDEX8,
                side=side,
                flags=ENTRY_FLAG_MIRROR_FOR_RIGHT if mirror else 0,
                width=width,
                height=height,
                palette_count=0,
                stored=stored,
                decoded=decoded,
            )
        )
    for expression_id, sides in sides_by_expression.items():
        if SIDE_SHARED in sides and sides != {SIDE_SHARED}:
            raise EyePackError(
                f"{expression_id} cannot mix shared and side-specific frames"
            )
        if SIDE_SHARED not in sides and sides != {SIDE_LEFT, SIDE_RIGHT}:
            raise EyePackError(
                f"{expression_id} must provide both left and right frames"
            )
    if not has_neutral:
        raise EyePackError("eye source must define a neutral expression")
    entries.extend(sorted(built_frames, key=lambda row: (row.name, row.side)))
    if len({row.name for row in entries}) != len(entries):
        raise EyePackError("derived pack entry names are not unique")
    canonical = _canonical_json(document)
    return pack_id, revision, renderer_api, canonical, palette, entries


def _assemble(
    pack_id: str,
    revision: int,
    renderer_api: int,
    canonical_source: bytes,
    entries: list[_BuiltEntry],
) -> bytes:
    toc_offset = HEADER.size
    payload_offset = _align(toc_offset + len(entries) * ENTRY.size)
    payload = bytearray()
    rows = bytearray()
    infos: list[tuple[_BuiltEntry, int]] = []
    cursor = payload_offset
    for row in entries:
        aligned = _align(cursor)
        payload.extend(bytes(aligned - cursor))
        cursor = aligned
        infos.append((row, cursor))
        payload.extend(row.stored)
        cursor += len(row.stored)
    for row, offset in infos:
        rows.extend(
            ENTRY.pack(
                _padded_ascii(row.name, 32, "entry name"),
                row.kind,
                row.codec,
                row.pixel_format,
                row.side,
                row.flags,
                row.width,
                row.height,
                row.palette_count,
                offset,
                len(row.stored),
                len(row.decoded),
                binascii.crc32(row.decoded) & 0xffffffff,
                0,
            )
        )
    toc = bytes(rows)
    prefix_padding = bytes(payload_offset - (toc_offset + len(toc)))
    total_size = payload_offset + len(payload)
    header = HEADER.pack(
        PACK_MAGIC,
        PACK_VERSION,
        HEADER.size,
        ENTRY.size,
        len(entries),
        CANVAS_WIDTH,
        CANVAS_HEIGHT,
        renderer_api,
        0,
        revision,
        toc_offset,
        payload_offset,
        total_size,
        binascii.crc32(toc) & 0xffffffff,
        binascii.crc32(payload) & 0xffffffff,
        _padded_ascii(pack_id, 32, "pack_id"),
        hashlib.sha256(canonical_source).digest(),
        bytes(16),
    )
    result = header + toc + prefix_padding + bytes(payload)
    if len(result) != total_size or len(result) > MAX_PACK_BYTES:
        raise EyePackError("assembled eye pack exceeds its declared bounds")
    return result


def _write_no_replace(path: Path, data: bytes, label: str) -> Path:
    output = path.absolute()
    output.parent.mkdir(parents=True, exist_ok=True)
    if output.exists() or output.is_symlink():
        raise EyePackError(f"{label} already exists: {output}")
    temporary: Path | None = None
    try:
        with tempfile.NamedTemporaryFile(
            prefix=f".{output.name}.", dir=output.parent, delete=False
        ) as stream:
            temporary = Path(stream.name)
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        os.chmod(temporary, 0o644)
        os.link(temporary, output)
    except FileExistsError as error:
        raise EyePackError(f"{label} already exists: {output}") from error
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)
    return output


def _png_chunk(kind: bytes, data: bytes) -> bytes:
    return (
        struct.pack(">I", len(data))
        + kind
        + data
        + struct.pack(">I", binascii.crc32(kind + data) & 0xffffffff)
    )


def _png(indexes: bytes, width: int, height: int,
         palette: tuple[tuple[int, int, int], ...]) -> bytes:
    rows = bytearray()
    for y in range(height):
        rows.append(0)
        for index in indexes[y * width:(y + 1) * width]:
            rows.extend(palette[index])
    header = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    return (
        b"\x89PNG\r\n\x1a\n"
        + _png_chunk(b"IHDR", header)
        + _png_chunk(b"IDAT", zlib.compress(bytes(rows), level=9))
        + _png_chunk(b"IEND", b"")
    )


def _mirror(indexes: bytes, width: int, height: int) -> bytes:
    output = bytearray()
    for y in range(height):
        row = indexes[y * width:(y + 1) * width]
        output.extend(reversed(row))
    return bytes(output)


def _write_previews(
    directory: Path,
    entries: list[_BuiltEntry],
    palette: tuple[tuple[int, int, int], ...],
) -> None:
    root = directory.absolute()
    if root.exists() or root.is_symlink():
        raise EyePackError(f"preview directory already exists: {root}")
    root.parent.mkdir(parents=True, exist_ok=True)
    temporary = Path(tempfile.mkdtemp(prefix=f".{root.name}.", dir=root.parent))
    try:
        for row in entries:
            if row.kind != KIND_INDEXED_FRAME:
                continue
            stem = row.name.removeprefix("expression/").replace("/", "-")
            if row.side == SIDE_SHARED:
                left = row.decoded
                right = _mirror(left, row.width, row.height) \
                    if row.flags & ENTRY_FLAG_MIRROR_FOR_RIGHT else left
                (temporary / f"{stem}-left.png").write_bytes(
                    _png(left, row.width, row.height, palette)
                )
                (temporary / f"{stem}-right.png").write_bytes(
                    _png(right, row.width, row.height, palette)
                )
            else:
                suffix = "left" if row.side == SIDE_LEFT else "right"
                (temporary / f"{stem}-{suffix}.png").write_bytes(
                    _png(row.decoded, row.width, row.height, palette)
                )
        os.rename(temporary, root)
    except OSError as error:
        raise EyePackError(f"failed to publish preview directory: {error}") from error
    finally:
        if temporary.exists():
            for child in temporary.iterdir():
                child.unlink()
            temporary.rmdir()


def build(source: Path, output: Path, preview_dir: Path | None = None) -> EyePackReport:
    selected_output = output.absolute()
    if selected_output.exists() or selected_output.is_symlink():
        raise EyePackError(f"eye pack output already exists: {selected_output}")
    if preview_dir is not None:
        selected_preview = preview_dir.absolute()
        if selected_preview.exists() or selected_preview.is_symlink():
            raise EyePackError(
                f"preview directory already exists: {selected_preview}"
            )
    document = _decode_text(source)
    pack_id, revision, renderer_api, canonical, palette, entries = _source_entries(
        document, source.parent
    )
    data = _assemble(pack_id, revision, renderer_api, canonical, entries)
    if preview_dir is not None:
        _write_previews(preview_dir, entries, palette)
    selected = _write_no_replace(output, data, "eye pack output")
    return verify(selected)


def _decode_entry(row: EntryInfo, stored: bytes) -> bytes:
    if row.codec == CODEC_RAW:
        if len(stored) != row.decoded_size:
            raise EyePackError(f"{row.name} raw payload size changed")
        return stored
    if row.codec == CODEC_RLE8:
        return _unrle8(stored, row.decoded_size, row.name)
    raise EyePackError(f"{row.name} uses unsupported codec {row.codec}")


def verify(path: Path) -> EyePackReport:
    selected = _regular(path, "eye pack")
    size = selected.stat().st_size
    if size < HEADER.size or size > MAX_PACK_BYTES:
        raise EyePackError("eye pack size is outside the supported bounds")
    data = selected.read_bytes()
    unpacked = HEADER.unpack_from(data)
    (
        magic, version, header_size, entry_size, entry_count, width, height,
        renderer_api, flags, revision, toc_offset, payload_offset, total_size,
        toc_crc32, payload_crc32, pack_id_raw, source_sha256, reserved,
    ) = unpacked
    if magic != PACK_MAGIC or version != PACK_VERSION:
        raise EyePackError("unsupported eye pack magic/version")
    if header_size != HEADER.size or entry_size != ENTRY.size:
        raise EyePackError("eye pack header/entry size is incompatible")
    if not 2 <= entry_count <= MAX_ENTRIES:
        raise EyePackError("eye pack entry count is outside supported bounds")
    if (width, height) != (CANVAS_WIDTH, CANVAS_HEIGHT):
        raise EyePackError("eye pack canvas is incompatible with the AIDK panels")
    if renderer_api != RENDERER_API or flags != 0 or revision == 0:
        raise EyePackError("eye pack renderer flags/revision are incompatible")
    if any(reserved) or source_sha256 == bytes(32):
        raise EyePackError("eye pack reserved/source identity fields are invalid")
    expected_payload = _align(HEADER.size + entry_count * ENTRY.size)
    if toc_offset != HEADER.size or payload_offset != expected_payload \
            or total_size != len(data):
        raise EyePackError("eye pack offsets or total size are inconsistent")
    toc = data[toc_offset:toc_offset + entry_count * ENTRY.size]
    if binascii.crc32(toc) & 0xffffffff != toc_crc32:
        raise EyePackError("eye pack TOC CRC changed")
    if any(data[toc_offset + len(toc):payload_offset]):
        raise EyePackError("eye pack TOC padding is not zero")
    payload = data[payload_offset:]
    if binascii.crc32(payload) & 0xffffffff != payload_crc32:
        raise EyePackError("eye pack payload CRC changed")
    pack_id = _read_padded_ascii(pack_id_raw, _PACK_ID, "pack_id")

    entries: list[EntryInfo] = []
    names: set[str] = set()
    sides_by_expression: dict[str, set[int]] = {}
    last_frame_name = ""
    cursor = payload_offset
    decoded_total = 0
    neutral = False
    for index in range(entry_count):
        values = ENTRY.unpack_from(toc, index * ENTRY.size)
        (
            name_raw, kind, codec, pixel_format, side, entry_flags,
            entry_width, entry_height, palette_count, offset, stored_size,
            decoded_size, crc32, entry_reserved,
        ) = values
        name = _read_padded_ascii(name_raw, _ENTRY_NAME, f"entry[{index}].name")
        if name in names:
            raise EyePackError(f"duplicate eye pack entry: {name}")
        names.add(name)
        if side not in {SIDE_SHARED, SIDE_LEFT, SIDE_RIGHT} \
                or entry_flags & ~ENTRY_FLAG_MIRROR_FOR_RIGHT \
                or (entry_flags and side != SIDE_SHARED):
            raise EyePackError(f"{name} has invalid side/flags")
        if entry_reserved != 0 or stored_size == 0 or decoded_size == 0:
            raise EyePackError(f"{name} has invalid size/reserved fields")
        aligned = _align(cursor)
        if offset != aligned or offset + stored_size > len(data):
            raise EyePackError(f"{name} has an invalid or overlapping payload range")
        if any(data[cursor:offset]):
            raise EyePackError(f"{name} payload alignment padding is not zero")
        stored = data[offset:offset + stored_size]
        if kind == KIND_PALETTE:
            if index != 0 or name != "palette/default" or codec != CODEC_RAW \
                    or pixel_format != PIXEL_RGB565LE or side != SIDE_SHARED \
                    or entry_flags != 0 or entry_width != 0 or entry_height != 0 \
                    or not 2 <= palette_count <= 256 \
                    or decoded_size != palette_count * 2:
                raise EyePackError("eye pack default palette contract is invalid")
        elif kind == KIND_INDEXED_FRAME:
            if codec not in {CODEC_RAW, CODEC_RLE8} \
                    or pixel_format != PIXEL_INDEX8 \
                    or (entry_width, entry_height) != (width, height) \
                    or palette_count != 0 or decoded_size != width * height \
                    or not name.startswith("expression/"):
                raise EyePackError(f"{name} indexed-frame contract is invalid")
            parts = name.split("/")
            if len(parts) not in {2, 3} \
                    or _EXPRESSION_ID.fullmatch(parts[1]) is None:
                raise EyePackError(f"{name} has an invalid expression identity")
            if side == SIDE_SHARED:
                if len(parts) != 2:
                    raise EyePackError(f"{name} shared frame has a side suffix")
            else:
                expected_side = "left" if side == SIDE_LEFT else "right"
                if len(parts) != 3 or parts[2] != expected_side:
                    raise EyePackError(f"{name} side suffix disagrees with its TOC")
            if name <= last_frame_name:
                raise EyePackError("eye pack frame entries are not canonical")
            last_frame_name = name
            sides_by_expression.setdefault(parts[1], set()).add(side)
            neutral = neutral or parts[1] == "neutral"
        else:
            raise EyePackError(f"{name} has unsupported entry kind {kind}")
        decoded = _decode_entry(
            EntryInfo(
                name, kind, codec, pixel_format, side, entry_flags,
                entry_width, entry_height, palette_count, offset, stored_size,
                decoded_size, crc32,
            ),
            stored,
        )
        if binascii.crc32(decoded) & 0xffffffff != crc32:
            raise EyePackError(f"{name} decoded CRC changed")
        if kind == KIND_INDEXED_FRAME \
                and decoded and max(decoded) >= entries[0].palette_count:
            raise EyePackError(f"{name} references a missing palette color")
        info = EntryInfo(
            name, kind, codec, pixel_format, side, entry_flags,
            entry_width, entry_height, palette_count, offset, stored_size,
            decoded_size, crc32,
        )
        entries.append(info)
        decoded_total += decoded_size
        cursor = offset + stored_size
    if cursor != len(data):
        raise EyePackError("eye pack has unreferenced trailing bytes")
    for expression_id, sides in sides_by_expression.items():
        if SIDE_SHARED in sides and sides != {SIDE_SHARED}:
            raise EyePackError(
                f"{expression_id} mixes shared and side-specific frames"
            )
        if SIDE_SHARED not in sides and sides != {SIDE_LEFT, SIDE_RIGHT}:
            raise EyePackError(
                f"{expression_id} lacks a left or right frame"
            )
    if not neutral:
        raise EyePackError("eye pack has no neutral expression")
    return EyePackReport(
        path=selected.absolute(),
        pack_id=pack_id,
        revision=revision,
        renderer_api=renderer_api,
        width=width,
        height=height,
        entries=tuple(entries),
        stored_size=len(data),
        decoded_bytes=decoded_total,
        source_sha256=source_sha256.hex(),
        sha256=hashlib.sha256(data).hexdigest(),
    )
