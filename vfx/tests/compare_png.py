import struct
import sys
import zlib


def paeth(a, b, c):
    p = a + b - c
    pa = abs(p - a)
    pb = abs(p - b)
    pc = abs(p - c)
    if pa <= pb and pa <= pc:
        return a
    if pb <= pc:
        return b
    return c


def read_png(path):
    data = open(path, 'rb').read()
    if data[:8] != b'\x89PNG\r\n\x1a\n':
        raise ValueError('not a PNG')
    offset = 8
    width = height = 0
    color_type = 0
    bit_depth = 0
    compressed = bytearray()
    while offset < len(data):
        length = struct.unpack('>I', data[offset:offset + 4])[0]
        kind = data[offset + 4:offset + 8]
        payload = data[offset + 8:offset + 8 + length]
        offset += 12 + length
        if kind == b'IHDR':
            width, height, bit_depth, color_type, compression, filtering, interlace = struct.unpack(
                '>IIBBBBB', payload)
            if bit_depth != 8 or interlace != 0 or compression != 0 or filtering != 0:
                raise ValueError('unsupported PNG layout')
        elif kind == b'IDAT':
            compressed.extend(payload)
        elif kind == b'IEND':
            break
    channels = 4 if color_type == 6 else 3 if color_type == 2 else 0
    if channels == 0:
        raise ValueError('unsupported PNG color type')
    raw = zlib.decompress(bytes(compressed))
    stride = width * channels
    rows = []
    previous = bytearray(stride)
    position = 0
    for _ in range(height):
        filter_type = raw[position]
        position += 1
        current = bytearray(raw[position:position + stride])
        position += stride
        for index in range(stride):
            left = current[index - channels] if index >= channels else 0
            up = previous[index]
            up_left = previous[index - channels] if index >= channels else 0
            if filter_type == 1:
                current[index] = (current[index] + left) & 0xff
            elif filter_type == 2:
                current[index] = (current[index] + up) & 0xff
            elif filter_type == 3:
                current[index] = (current[index] + ((left + up) >> 1)) & 0xff
            elif filter_type == 4:
                current[index] = (current[index] + paeth(left, up, up_left)) & 0xff
            elif filter_type != 0:
                raise ValueError('unsupported PNG filter')
        rows.append(current)
        previous = current
    return width, height, channels, rows


def main():
    if len(sys.argv) != 5:
        print('usage: compare_png.py expected actual max_pixels max_delta')
        return 2
    expected = read_png(sys.argv[1])
    actual = read_png(sys.argv[2])
    max_pixels = int(sys.argv[3])
    max_delta = int(sys.argv[4])
    if expected[:3] != actual[:3]:
        print('image dimensions/format differ')
        return 1
    width, height, channels, first = expected
    _, _, _, second = actual
    diff_pixels = 0
    largest_delta = 0
    first_diff = None
    for y in range(height):
        a = first[y]
        b = second[y]
        for x in range(width):
            delta = 0
            for channel in range(channels):
                index = x * channels + channel
                delta = max(delta, abs(a[index] - b[index]))
            if delta > max_delta:
                diff_pixels += 1
                largest_delta = max(largest_delta, delta)
                if first_diff is None:
                    first_diff = (x, y)
    print(f'diff_pixels={diff_pixels} max_delta={largest_delta} first={first_diff}')
    return 0 if diff_pixels <= max_pixels else 1


if __name__ == '__main__':
    sys.exit(main())
