"""Reads PNG screenshots and compares two of them.

Chrome hands a screenshot back as a PNG, and the question asked of two of them
is almost always the same one: are they the same picture, and if not, where do
they differ?  That is a small enough job to do on the standard library, and
doing it there is what keeps the harness installable with no packages at all.

The decoder covers what ``Page.captureScreenshot`` produces -- eight bits a
channel, not interlaced -- and refuses anything else rather than guessing.
"""

import struct
import zlib


class ImageError(Exception):
    """A PNG this decoder does not read."""


_CHANNELS = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}

PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"


class Image:
    """An RGBA image: ``width``, ``height``, and ``pixels`` as four bytes each."""

    __slots__ = ("width", "height", "pixels")

    def __init__(self, width, height, pixels):
        self.width = width
        self.height = height
        self.pixels = pixels

    def pixel(self, x, y):
        offset = (y * self.width + x) * 4
        return tuple(self.pixels[offset:offset + 4])


def _unfilter(raw, width, height, bytes_per_pixel):
    """Reverses the per-scanline filters, returning the raw samples."""

    stride = width * bytes_per_pixel
    out = bytearray(stride * height)
    previous = bytearray(stride)
    position = 0

    for row in range(height):
        if position >= len(raw):
            raise ImageError("the image data ends before the last scanline")

        filter_type = raw[position]
        position += 1
        line = bytearray(raw[position:position + stride])
        position += stride

        if len(line) != stride:
            raise ImageError("a scanline is short")

        if filter_type == 0:
            pass
        elif filter_type == 1:
            for index in range(bytes_per_pixel, stride):
                line[index] = (line[index] + line[index - bytes_per_pixel]) & 0xFF
        elif filter_type == 2:
            for index in range(stride):
                line[index] = (line[index] + previous[index]) & 0xFF
        elif filter_type == 3:
            for index in range(stride):
                left = line[index - bytes_per_pixel] if index >= bytes_per_pixel else 0
                line[index] = (line[index] + ((left + previous[index]) >> 1)) & 0xFF
        elif filter_type == 4:
            for index in range(stride):
                left = line[index - bytes_per_pixel] if index >= bytes_per_pixel else 0
                up = previous[index]
                upleft = previous[index - bytes_per_pixel] if index >= bytes_per_pixel else 0
                estimate = left + up - upleft
                da = abs(estimate - left)
                db = abs(estimate - up)
                dc = abs(estimate - upleft)
                if da <= db and da <= dc:
                    predictor = left
                elif db <= dc:
                    predictor = up
                else:
                    predictor = upleft
                line[index] = (line[index] + predictor) & 0xFF
        else:
            raise ImageError("scanline filter %d is not a PNG filter" % filter_type)

        out[row * stride:(row + 1) * stride] = line
        previous = line

    return out


def decode(data):
    """Decodes PNG bytes into an :class:`Image`."""

    if not data.startswith(PNG_SIGNATURE):
        raise ImageError("not a PNG")

    position = len(PNG_SIGNATURE)
    header = None
    palette = b""
    transparency = b""
    body = []

    while position + 8 <= len(data):
        length, kind = struct.unpack(">I4s", data[position:position + 8])
        payload = data[position + 8:position + 8 + length]
        position += 12 + length

        if kind == b"IHDR":
            header = struct.unpack(">IIBBBBB", payload)
        elif kind == b"PLTE":
            palette = payload
        elif kind == b"tRNS":
            transparency = payload
        elif kind == b"IDAT":
            body.append(payload)
        elif kind == b"IEND":
            break

    if header is None:
        raise ImageError("the PNG has no header chunk")

    width, height, depth, colour, compression, filtering, interlace = header

    if depth != 8:
        raise ImageError("only eight bits a channel is read, not %d" % depth)
    if interlace != 0:
        raise ImageError("an interlaced PNG is not read")
    if compression != 0 or filtering != 0:
        raise ImageError("the PNG uses a compression or filter method that is not standard")
    if colour not in _CHANNELS:
        raise ImageError("colour type %d is not a PNG colour type" % colour)

    channels = _CHANNELS[colour]
    samples = _unfilter(zlib.decompress(b"".join(body)), width, height, channels)

    pixels = bytearray(width * height * 4)

    if colour == 6:
        pixels[:] = samples
    elif colour == 2:
        for index in range(width * height):
            pixels[index * 4:index * 4 + 3] = samples[index * 3:index * 3 + 3]
            pixels[index * 4 + 3] = 255
    elif colour == 0:
        for index in range(width * height):
            grey = samples[index]
            pixels[index * 4:index * 4 + 4] = bytes((grey, grey, grey, 255))
    elif colour == 4:
        for index in range(width * height):
            grey = samples[index * 2]
            pixels[index * 4:index * 4 + 4] = bytes((grey, grey, grey, samples[index * 2 + 1]))
    else:
        if not palette:
            raise ImageError("a palette image with no palette")
        for index in range(width * height):
            entry = samples[index]
            pixels[index * 4:index * 4 + 3] = palette[entry * 3:entry * 3 + 3]
            pixels[index * 4 + 3] = transparency[entry] if entry < len(transparency) else 255

    return Image(width, height, pixels)


def read(path):
    """Decodes the PNG at *path*."""

    with open(path, "rb") as handle:
        return decode(handle.read())


def compare(first, second, threshold=0):
    """Compares two images channel by channel.

    A channel differing by *threshold* or less counts as the same, which is how
    a comparison survives a renderer that dithers.  The answer reports how many
    pixels differ, the box that encloses them, and the largest difference any
    one channel showed.
    """

    if first.width != second.width or first.height != second.height:
        return {
            "identical": False,
            "reason": "different sizes",
            "size": [first.width, first.height],
            "other_size": [second.width, second.height],
        }

    differing = 0
    worst = 0
    left = first.width
    top = first.height
    right = -1
    bottom = -1

    a = first.pixels
    b = second.pixels
    stride = first.width * 4

    if a == b:
        return {
            "identical": True,
            "size": [first.width, first.height],
            "pixels": first.width * first.height,
            "differing": 0,
            "worst_channel_difference": 0,
            "threshold": threshold,
        }

    for y in range(first.height):
        row = y * stride

        # Whole rows match far more often than not, and comparing one slice is
        # a thousand times cheaper than comparing its pixels one at a time.
        if a[row:row + stride] == b[row:row + stride]:
            continue

        for x in range(first.width):
            offset = row + x * 4
            if a[offset:offset + 4] == b[offset:offset + 4]:
                continue

            distance = max(abs(a[offset + c] - b[offset + c]) for c in range(4))
            if distance <= threshold:
                continue

            differing += 1
            if distance > worst:
                worst = distance
            if x < left:
                left = x
            if x > right:
                right = x
            if y < top:
                top = y
            if y > bottom:
                bottom = y

    answer = {
        "identical": differing == 0,
        "size": [first.width, first.height],
        "pixels": first.width * first.height,
        "differing": differing,
        "worst_channel_difference": worst,
        "threshold": threshold,
    }

    if differing:
        answer["box"] = {
            "x": left,
            "y": top,
            "width": right - left + 1,
            "height": bottom - top + 1,
        }

    return answer
