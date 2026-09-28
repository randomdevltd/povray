# Render snapshots

A render that writes an output file keeps its progress in a render state file (`+C` continues from it), so the
output image does not exist until the render ends. A snapshot writes the pixels in that state file to a PNG at any
time: while the render runs, when it ends, or later from the state file alone.

## Options

| Option | Meaning |
|---|---|
| `+SN<file>` or `Snapshot_File=<file>` | PNG that receives the snapshot |
| `Snapshot_Interval=<seconds>` | also write it this often while rendering; `0`, the default, for never |
| `Snapshot_From=<state file>` | write the snapshot from this state file and exit, without parsing or rendering |

## While rendering

    povray +Iscene.pov +Oscene.png +W1920 +H1080 +SNscene-wip.png [Snapshot_Interval=60]

With `Snapshot_File` set, a render writes the snapshot:

- when the process receives `SIGUSR1` (`kill -USR1 <pid>`, or `podman kill --signal USR1 <container>` when POV-Ray is
  the container's main process);
- every `Snapshot_Interval` seconds, if set;
- once when the render finishes, fails, or is stopped with `SIGINT`, `SIGTERM` or `SIGQUIT`.

A render continued with `+C` writes snapshots the same way. The snapshot needs the state file, so it is not written
when output goes to standard output or `Create_Continue_Trace_Log=off`; the render then warns and carries on.
`SIGUSR1` without `Snapshot_File` is ignored.

## From the state file alone

    povray +W1920 +H1080 Snapshot_From=scene.pov-state +SNscene-wip.png

This reads only the state file: no scene is parsed and no render starts, and it does not open the state file for
writing, so it can run while the render that owns the file is still writing it. It exits with status 0 once the
PNG is in place, and non-zero with a message on standard error if the state file cannot be read or holds pixels
outside `+W`×`+H`. `+W` and `+H` must be the render's own, since the state file does not record them; add `+UA` if the
render used it.

The state file sits beside the output image and takes its name with `.pov-state` in place of the extension
(`+Oscene.png` gives `scene.pov-state`); without `+O` it is named after the scene file.

## The image

- 8 bits per channel RGBA, sRGB, whatever the output file type, bit depth or `File_Gamma`.
- Pixels not yet rendered have alpha 0. Rendered pixels are opaque, or with `+UA` carry the render's alpha as the
  PNG output would.
- A block-order render shows its finished blocks. A progressive render (`+PR`) shows the whole frame at the finest
  level it has reached, each sample filling its cell, as its display does.
- The snapshot is drawn from the state file by the same rules as the final image, so a snapshot taken during or at
  the end of a render and one written later from the same state file are byte for byte the same.
- Colours are converted from the working gamma: the scene's `assumed_gamma` while rendering, 1.0 when written from
  the state file alone, as scenes of version 3.7 and later assume by default.

## Safety

The PNG is written to `<file>.<random>.tmp` in the same directory and renamed over `<file>`, so a reader sees either
the previous snapshot or the new one, never a partial file. The state file is only read: a message still being
written at its end is ignored, as a continued render ignores it. Snapshots do not change the render or the state
file format.

## Cost

Each snapshot reads the whole state file and holds 4 bytes per pixel (about 33 MB at 3840×2160) while it is written.
During a render it runs on the frontend thread, which buffers the render's results meanwhile; rendering itself does
not stop.
