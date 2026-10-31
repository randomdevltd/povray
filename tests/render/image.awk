# Image readers and comparisons shared by the render suites' checks (POSIX awk; runs under mawk and gawk).

# A pipe, not /dev/stderr: mawk would open that path and truncate a log that stderr is redirected to.
function fail(message) {
    printf "%s\n", message | "cat 1>&2"
    close("cat 1>&2")
    exit 1
}

function shell_quote(text) {
    gsub(/'/, "'\\''", text)
    return "'" text "'"
}

function file_bytes(path, b,    command, line, words, count, i, n) {
    split("", b)
    command = "od -An -v -t u1 " shell_quote(path)
    n = 0
    while ((command | getline line) > 0) {
        count = split(line, words, " ")
        for (i = 1; i <= count; i++) b[n++] = words[i] + 0
    }
    close(command)
    if (n == 0) fail(path ": missing or empty")
    return n
}

function is_space(byte) {
    return byte == 32 || (byte >= 9 && byte <= 13)
}

# Parses the P6 header like the old reader; returns the raster offset and sets IMAGE_W, IMAGE_H.
function ppm_header(path, b, n,    pos, end, count, token) {
    pos = 0
    count = 0
    while (count < 4) {
        while (pos < n && is_space(b[pos])) pos++
        if (pos >= n) fail(path ": truncated PPM header")
        if (b[pos] == 35) {
            while (pos < n && b[pos] != 10) pos++
            pos++
            continue
        }
        token = ""
        for (end = pos; end < n && !is_space(b[end]); end++) token = token sprintf("%c", b[end])
        IMAGE_TOKEN[++count] = token
        pos = end
    }
    if (IMAGE_TOKEN[1] != "P6" || IMAGE_TOKEN[4] + 0 != 65535) fail(path ": expected a linear 16-bit P6 image")
    IMAGE_W = IMAGE_TOKEN[2] + 0
    IMAGE_H = IMAGE_TOKEN[3] + 0
    return pos + 1
}

# Fills v[0..] with samples divided by scale (65535 for unit values, 1 for raw); returns the count.
function ppm_load(path, v, scale, strict,    b, n, start, i, k) {
    n = file_bytes(path, b)
    start = ppm_header(path, b, n)
    if (strict && n - start != IMAGE_W * IMAGE_H * 6)
        fail(path ": " (n - start) " raster bytes, expected " IMAGE_W * IMAGE_H * 6)
    split("", v)
    k = 0
    for (i = start; i < n; i += 2) v[k++] = (256 * b[i] + b[i + 1]) / scale
    return k
}

function read_ppm(path, v) {
    return ppm_load(path, v, 65535, 1)
}

# Raw 16-bit samples after the header, without checking the raster size against it.
function read_samples(path, v) {
    return ppm_load(path, v, 1, 0)
}

# Big-endian 16-bit words of a raw pixel file (a tail of a PPM); returns the count.
function read_words(path, v, expected,    b, n, i, k) {
    n = file_bytes(path, b)
    if (expected && n != 2 * expected) fail(path ": " n " bytes, expected " 2 * expected)
    split("", v)
    k = 0
    for (i = 0; i + 1 < n; i += 2) v[k++] = 256 * b[i] + b[i + 1]
    return k
}

# The last count big-endian 16-bit words of a file, whatever its header.
function read_tail_words(path, v, count,    b, n, i, k) {
    n = file_bytes(path, b)
    if (n < 2 * count) fail(path ": " n " bytes, expected at least " 2 * count)
    split("", v)
    k = 0
    for (i = n - 2 * count; i < n; i += 2) v[k++] = 256 * b[i] + b[i + 1]
    return k
}

# Decodes run-length RGBE scanlines into r, g, bl, e (one entry per pixel); sets IMAGE_W, IMAGE_H.
function read_hdr(path, r, g, bl, e,    b, n, pos, line, parts, row, c, x, count, value, k, channel) {
    n = file_bytes(path, b)
    pos = 0
    line = ""
    while (substr(line, 1, 3) != "-Y ") {
        if (pos >= n) fail(path ": missing HDR dimensions")
        line = ""
        while (pos < n && b[pos] != 10) line = line sprintf("%c", b[pos++])
        pos++
    }
    if (split(line, parts, " ") != 4 || parts[3] != "+X") fail(path ": unsupported HDR orientation")
    IMAGE_H = parts[2] + 0
    IMAGE_W = parts[4] + 0
    split("", r); split("", g); split("", bl); split("", e)
    for (row = 0; row < IMAGE_H; row++) {
        if (b[pos] != 2 || b[pos + 1] != 2 || b[pos + 2] != int(IMAGE_W / 256) || b[pos + 3] != IMAGE_W % 256)
            fail(path ": expected modern HDR scanline encoding")
        pos += 4
        for (c = 0; c < 4; c++) {
            x = 0
            while (x < IMAGE_W) {
                if (pos >= n || b[pos] == 0) fail(path ": invalid HDR run")
                count = b[pos++]
                if (count > 128) {
                    count -= 128
                    value = b[pos++]
                    for (k = 0; k < count; k++) channel[c, x + k] = value
                } else
                    for (k = 0; k < count; k++) channel[c, x + k] = b[pos++]
                x += count
                if (x > IMAGE_W || pos > n) fail(path ": HDR run crosses scanline")
            }
        }
        for (x = 0; x < IMAGE_W; x++) {
            k = row * IMAGE_W + x
            r[k] = channel[0, x]; g[k] = channel[1, x]; bl[k] = channel[2, x]; e[k] = channel[3, x]
        }
    }
    return IMAGE_W * IMAGE_H
}

function max2(a, b) {
    return a > b ? a : b
}

function abs(a) {
    return a < 0 ? -a : a
}

function sum_of(v, n,    i, s) {
    s = 0
    for (i = 0; i < n; i++) s += v[i]
    return s
}

function max_of(v, n,    i, m) {
    m = v[0]
    for (i = 1; i < n; i++) if (v[i] > m) m = v[i]
    return m
}

function min_of(v, n,    i, m) {
    m = v[0]
    for (i = 1; i < n; i++) if (v[i] < m) m = v[i]
    return m
}

function same_values(a, na, b, nb,    i) {
    if (na != nb) return 0
    for (i = 0; i < na; i++) if (a[i] != b[i]) return 0
    return 1
}

# Mean absolute and mean errors of each channel against a target, with the label in every line.
function compare(label, got, ng, expected, ne, relative,    channel, i, count, scale, error, limit, got_mean, mean_error, got_total) {
    for (channel = 0; channel < 3; channel++) {
        scale = 0; error = 0; got_total = 0; count = 0
        for (i = channel; i < ne; i += 3) {
            scale += expected[i]
            if (i < ng) error += abs(got[i] - expected[i])
            count++
        }
        scale /= count
        error /= count
        limit = max2(2 / 65535, relative * scale)
        if (error > limit)
            fail(sprintf("%s channel %d: MAE %.7g, limit %.7g, reference mean %.7g", label, channel, error, limit, scale))
        count = 0
        for (i = channel; i < ng; i += 3) { got_total += got[i]; count++ }
        got_mean = got_total / count
        mean_error = abs(got_mean - scale)
        if (mean_error > max2(2 / 65535, relative * 0.4 * scale))
            fail(sprintf("%s channel %d: mean error %.7g, reference mean %.7g", label, channel, mean_error, scale))
        printf "%s channel %d: mean %.7g, reference %.7g, MAE %.3g\n", label, channel, got_mean, scale, error
    }
}

function top_down_x(column, width, frame) {
    return frame * (0.5 - (column + 0.5) / width)
}
