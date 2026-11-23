import argparse
import json
import math
import struct
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFont


try:
    FONT = ImageFont.truetype("arial.ttf", 16)
    LABEL_FONT = ImageFont.truetype("arial.ttf", 10)
except OSError:
    FONT = ImageFont.load_default()
    LABEL_FONT = FONT
def read_dds(path):
    data = path.read_bytes()
    if data[:4] != b"DDS ":
        raise ValueError(f"{path}: not a DDS file")
    height = struct.unpack_from("<I", data, 12)[0]
    width = struct.unpack_from("<I", data, 16)[0]
    if data[84:88] != b"DX10":
        raise ValueError(f"{path}: expected a DX10 DDS header")
    dxgi_format = struct.unpack_from("<I", data, 128)[0]
    if dxgi_format != 10:
        raise ValueError(f"{path}: expected DXGI_FORMAT_R16G16B16A16_FLOAT")
    payload = 148
    expected = payload + width * height * 8
    if len(data) != expected:
        raise ValueError(f"{path}: payload size {len(data)} != {expected}")
    pixels = np.frombuffer(data, dtype="<f2", offset=payload).reshape(height, width, 4)
    return pixels.astype(np.float32)


def write_dds_copy(source, destination, pixels=None):
    data = source.read_bytes()
    header = data[:148]
    if pixels is None:
        destination.write_bytes(data)
    else:
        destination.write_bytes(header + pixels.astype("<f2").tobytes())


def write_unity_direction_png(directory, atlas_index, quantized):
    directory.mkdir(parents=True, exist_ok=True)
    path = directory / f"lm{atlas_index}_sh1.png"
    rgba8 = np.clip(np.rint(quantized * 255.0), 0.0, 255.0).astype(np.uint8)
    Image.fromarray(np.flipud(rgba8), "RGBA").save(path, format="PNG")
    return path


def load_layers(directory, atlas_index):
    return [read_dds(directory / f"lm{atlas_index}_sh{channel}.dds") for channel in range(3)]


def srgb(value):
    value = np.clip(value, 0.0, 1.0)
    return np.where(value <= 0.0031308, value * 12.92, 1.055 * np.power(value, 1.0 / 2.4) - 0.055)


def tonemap(value):
    value = np.maximum(value, 0.0)
    return srgb(value / (1.0 + value))


def image(rgb):
    return Image.fromarray(np.round(np.clip(rgb, 0.0, 1.0) * 255.0).astype(np.uint8), "RGB")


def labeled(img, text, font=None):
    canvas = Image.new("RGB", (img.width, img.height + 24), (24, 24, 28))
    canvas.paste(img, (0, 24))
    ImageDraw.Draw(canvas).text((6, 5), text, fill=(235, 235, 235), font=font or LABEL_FONT)
    return canvas


def decode(incident, direction, normal):
    half_lambert = np.sum((direction[..., :3] - 0.5) * normal, axis=-1) + 0.5
    radiance = incident * half_lambert[..., None] / np.maximum(direction[..., 3:4], 1.0e-4)
    return np.maximum(radiance, 0.0)


def stats(a, b):
    diff = np.abs(a - b)
    mse = float((diff ** 2).mean())
    energy = float((a ** 2).mean())
    return {
        "max_abs": float(diff.max()),
        "mean_abs": float(diff.mean()),
        "rmse": float(math.sqrt(mse)),
        "psnr_db": float(10.0 * math.log10(energy / max(mse, 1.0e-30))),
        "pixels_gt_1_255_percent": float((diff.max(axis=-1) > (1.0 / 255.0)).mean() * 100.0),
    }


def summary(width, height, lines):
    panel = Image.new("RGB", (width, height), (24, 24, 28))
    draw = ImageDraw.Draw(panel)
    for index, line in enumerate(lines):
        draw.text((12, 12 + index * 22), line, fill=(225, 225, 225), font=FONT)
    return panel


def write_lightmap_figure(out, incident, direction, unity, hbg, decoded_diff, rect, normal):
    incident_rgb = tonemap(incident)
    direction_rgb = srgb(direction[..., :3])
    unity_rgb = tonemap(unity)
    hbg_rgb = tonemap(hbg)
    diff_rgb = np.clip(decoded_diff / max(float(decoded_diff.max()), 1.0e-8), 0.0, 1.0)
    diff_heat = np.stack(
        [np.ones_like(diff_rgb[:, :, 0]), 1.0 - diff_rgb[:, :, 0], 0.25 * (1.0 - diff_rgb[:, :, 0])],
        axis=-1,
    )
    diff_heat = diff_heat * 0.2 + srgb(diff_rgb) * 0.8
    panels = [
        ("Incident 16F", incident_rgb),
        ("Direction 16F", direction_rgb),
        ("Unity 8-bit dir", unity_rgb),
        ("HBG 16F dir", hbg_rgb),
        ("Diff / max", diff_heat),
    ]
    scale = 3
    images = []
    for text, rgb in panels:
        item = labeled(image(rgb), text)
        images.append(item.resize((item.width * scale, item.height * scale), Image.NEAREST))
    x, y, width, height = rect
    stats_lines = [
        f"Rect {width}x{height} at {x},{y}",
        f"Normal {normal[0]:g}, {normal[1]:g}, {normal[2]:g}",
        f"Dir max {float(np.abs(direction - np.floor(np.clip(direction, 0.0, 1.0) * 255.0) / 255.0).max()):.6f}",
        f"Decode max {float(decoded_diff.max()):.6f}",
        f"Decode mean {float(decoded_diff.mean()):.6f}",
        f"PSNR {10.0 * math.log10(float((hbg ** 2).mean()) / max(float((decoded_diff ** 2).mean()), 1.0e-30)):.2f} dB",
        "Unity direction: RGBA32",
    ]
    summary_panel = Image.new("RGB", (images[0].width, images[0].height), (24, 24, 28))
    summary_draw = ImageDraw.Draw(summary_panel)
    summary_draw.text((6, 5), "Summary", fill=(235, 235, 235), font=FONT)
    for index, line in enumerate(stats_lines):
        summary_draw.text((12, 36 + index * 24), line, fill=(225, 225, 225), font=FONT)
    images.append(summary_panel)
    sheet = Image.new("RGB", (sum(item.width for item in images[:3]), images[0].height * 2), (18, 18, 20))
    x_offset = 0
    for item in images[:3]:
        sheet.paste(item, (x_offset, 0))
        x_offset += item.width
    x_offset = 0
    for item in images[3:]:
        sheet.paste(item, (x_offset, images[0].height))
        x_offset += item.width
    sheet.save(out / "lightmap_compare.png")


def write_runtime_figure(out, path_a, path_b):
    a = np.asarray(Image.open(path_a).convert("RGB"), dtype=np.float32)
    b = np.asarray(Image.open(path_b).convert("RGB"), dtype=np.float32)
    if a.shape != b.shape:
        raise ValueError("runtime screenshots must have the same size")
    diff = np.abs(a - b)
    different_pixels = int(np.any(diff > 0.0, axis=-1).sum())
    total_pixels = int(diff.shape[0] * diff.shape[1])
    psnr = float(10.0 * math.log10(255.0 ** 2 / max(float((diff ** 2).mean()), 1.0e-30)))
    panels = [
        ("HBG RGBA16F direction", a / 255.0),
        ("Unity RGBA8 direction", b / 255.0),
    ]
    images = [labeled(image(rgb), text, FONT) for text, rgb in panels]
    diff_panel = labeled(image(np.clip(diff * 64.0 / 255.0, 0.0, 1.0)), f"diff x64 (max {float(diff.max()):.0f} LSB)")
    lines = [
        f"Different {different_pixels}/{total_pixels}",
        f"Different {100.0 * different_pixels / total_pixels:.2f}%",
        f"Max {float(diff.max()):.3f} LSB",
        f"Mean {float(diff.mean()):.4f} LSB",
        f"PSNR {psnr:.2f} dB",
    ]
    metrics = labeled(summary(images[0].width, images[0].height - 24, lines), "Runtime metrics")
    sheet = Image.new("RGB", (images[0].width * 2, images[0].height * 2), (18, 18, 20))
    sheet.paste(images[0], (0, 0))
    sheet.paste(images[1], (images[0].width, 0))
    sheet.paste(diff_panel, (0, images[0].height))
    sheet.paste(metrics, (images[0].width, images[0].height))
    sheet.save(out / "runtime_compare.png")
    return {
        "different_pixels": different_pixels,
        "total_pixels": total_pixels,
        "different_percent": float(100.0 * different_pixels / total_pixels),
        "max_abs_8bit": float(diff.max()),
        "mean_abs_8bit": float(diff.mean()),
        "pixels_gt_1_lsb": int(np.any(diff > 1.0, axis=-1).sum()),
        "psnr_db": psnr,
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--dds-dir", required=True, type=Path)
    parser.add_argument("--rect", nargs=4, required=True, type=int, metavar=("X", "Y", "W", "H"))
    parser.add_argument("--atlas-index", type=int, default=0)
    parser.add_argument("--normal", nargs=3, type=float, default=(0.0, 1.0, 0.0))
    parser.add_argument("--out", required=True, type=Path)
    parser.add_argument("--runtime-a", type=Path)
    parser.add_argument("--runtime-b", type=Path)
    parser.add_argument("--write-unity-dds", type=Path)
    parser.add_argument("--write-unity-png", type=Path)
    args = parser.parse_args()

    x, y, width, height = args.rect
    normal = np.asarray(args.normal, dtype=np.float32)
    length = float(np.linalg.norm(normal))
    if width <= 0 or height <= 0 or length <= 0.0:
        raise ValueError("rect and normal must be non-degenerate")
    if (args.runtime_a is None) != (args.runtime_b is None):
        raise ValueError("--runtime-a and --runtime-b must be supplied together")
    normal /= length

    layers = load_layers(args.dds_dir, args.atlas_index)
    atlas_height, atlas_width = layers[0].shape[:2]
    if x < 0 or y < 0 or x + width > atlas_width or y + height > atlas_height:
        raise ValueError("rect is outside the atlas")
    if any(layer.shape != layers[0].shape for layer in layers):
        raise ValueError("lightmap layers have different sizes")

    crop = (slice(y, y + height), slice(x, x + width))
    incident = layers[0][crop][..., :3]
    direction = layers[1][crop]
    quantized = np.floor(np.clip(direction, 0.0, 1.0) * 255.0) / 255.0
    unity = decode(incident, quantized, normal)
    hbg = decode(incident, direction, normal)
    encoded_diff = np.abs(direction - quantized)
    decoded_diff = np.abs(hbg - unity)

    if args.write_unity_dds is not None:
        args.write_unity_dds.mkdir(parents=True, exist_ok=True)
        quantized_atlas = np.floor(np.clip(layers[1], 0.0, 1.0) * 255.0) / 255.0
        for channel in range(3):
            source = args.dds_dir / f"lm{args.atlas_index}_sh{channel}.dds"
            destination = args.write_unity_dds / source.name
            write_dds_copy(source, destination, quantized_atlas if channel == 1 else None)

    if args.write_unity_png is not None:
        quantized_atlas = np.floor(np.clip(layers[1], 0.0, 1.0) * 255.0) / 255.0
        png_path = write_unity_direction_png(args.write_unity_png, args.atlas_index, quantized_atlas)
        print(f"wrote {png_path}")

    args.out.mkdir(parents=True, exist_ok=True)
    write_lightmap_figure(args.out, incident, direction, unity, hbg, decoded_diff, args.rect, normal)
    result = {
        "dds_dir": str(args.dds_dir),
        "atlas_index": args.atlas_index,
        "atlas_size": atlas_width,
        "rect": [x, y, width, height],
        "normal": [float(value) for value in normal],
        "atlas_layers_rgba16f": True,
        "unity_direction_format": "RGBA32 PNG -> DirectionalLightmap (textureType 12, sRGBTexture 0)",
        "direction_quantization": "floor(clamp(x, 0, 1) * 255) / 255",
        "encoded_direction": {
            "max_abs": float(encoded_diff.max()),
            "mean_abs": float(encoded_diff.mean()),
        },
        "decoded_radiance": stats(hbg, unity),
    }
    if args.runtime_a is not None and args.runtime_b is not None:
        result["runtime"] = write_runtime_figure(args.out, args.runtime_a, args.runtime_b)
    (args.out / "metrics.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()








