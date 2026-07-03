# H.264 RTP End Markers for `-vrtp`

This fork adds a small shutdown marker for the `-vrtp` video forwarding mode.
The goal is to let a downstream RTP/H.264 receiver distinguish "the AirPlay
session ended" from "RTP packets stopped arriving for an unknown reason".

## Where the marker is inserted

UxPlay builds the `-vrtp` pipeline from the internal H.264 appsrc like this:

```text
appsrc name=video_source ! queue ! h264parse ! rtph264pay <user -vrtp pipeline>
```

For example:

```text
uxplay -vrtp "config-interval=1 ! udpsink host=127.0.0.1 port=5004"
```

On a mirror-session shutdown, UxPlay now pushes two valid H.264 byte-stream NAL
units into `appsrc` before ending the GStreamer stream:

```text
00 00 00 01 0A 80   H.264 end_of_seq_rbsp
00 00 00 01 0B 80   H.264 end_of_stream_rbsp
```

These are standard H.264 NAL unit types 10 and 11. They are not an extra UDP
side channel and they do not require a proprietary RTP packet type.

## Drain behavior

After pushing the marker, UxPlay calls `gst_app_src_end_of_stream()` and waits
briefly for the GStreamer bus to report EOS or ERROR before setting the pipeline
to `GST_STATE_NULL`. The wait is intentionally short so that shutdown behavior
does not hang if a sink or downstream element does not post EOS promptly.

## Scope

The marker is only injected when all of these conditions are true:

- The active renderer is forwarding via `-vrtp`.
- The active video codec is H.264.
- The renderer has an `appsrc`.

Local video rendering is not changed. H.265 forwarding is not changed by this
patch because the injected NAL units are H.264-specific.

## Receiver guidance

A receiver that wants to observe the marker should inspect the H.264 stream
before the video decoder. In a GStreamer receiver pipeline, the practical point
is after RTP depayloading and before decode, for example:

```text
udpsrc ! application/x-rtp,... ! rtph264depay ! <detector> ! h264parse ! decoder
```

The detector should parse the H.264 byte stream or AVCC-formatted output it
receives from `rtph264depay`/`h264parse` and treat NAL unit type 10 or 11 as a
session-end hint. Many decoders ignore these NAL units, so detecting them after
decode is not reliable.

## Limitations

This is a best-effort end marker on the media stream. RTP over UDP still has no
delivery guarantee, so a receiver should keep its timeout-based handling as a
fallback. The marker is intended to improve the normal TEARDOWN/disconnect case,
not to replace network-loss detection.
