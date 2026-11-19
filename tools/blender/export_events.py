#!/usr/bin/env python3
"""Export Blender Action markers and frame ranges as an anim_pack event sidecar.

Run inside Blender:

    blender -b character.blend --python tools/blender/export_events.py -- \
        --output build/anim/events.json

The script does not export animation data. It only writes Blender Action
frame ranges and markers; anim_pack converts each marker frame to a
normalized time using frameStart/frameEnd.
"""

import argparse
import json
import math
import os
import sys


def _finite(value, label):
    try:
        number = float(value)
    except (TypeError, ValueError):
        raise ValueError("%s must be a finite number" % label)
    if not math.isfinite(number):
        raise ValueError("%s must be a finite number" % label)
    return number


def frame_to_normalized(frame, frame_start, frame_end):
    start = _finite(frame_start, "frameStart")
    end = _finite(frame_end, "frameEnd")
    value = _finite(frame, "frame")
    if end <= start:
        raise ValueError("frameEnd must be greater than frameStart")
    normalized = (value - start) / (end - start)
    if normalized < -1e-6 or normalized > 1.0 + 1e-6:
        raise ValueError("frame %.6g is outside [frameStart, frameEnd]" % value)
    return min(1.0, max(0.0, normalized))


def _number(value):
    if float(value).is_integer():
        return int(value)
    return round(float(value), 9)


def make_action_record(name, frame_start, frame_end, events):
    if not name:
        raise ValueError("action name must not be empty")
    start = _finite(frame_start, "frameStart")
    end = _finite(frame_end, "frameEnd")
    if end <= start:
        raise ValueError("action '%s' has an empty frame range" % name)

    normalized_events = []
    seen = set()
    for event in events:
        frame = _finite(event.get("frame"), "frame")
        event_name = event.get("name")
        if not isinstance(event_name, str) or not event_name.strip():
            raise ValueError("action '%s' has an event with an empty name" % name)
        event_name = event_name.strip()
        normalized = frame_to_normalized(frame, start, end)
        key = (_number(frame), event_name)
        if key in seen:
            continue
        seen.add(key)
        normalized_events.append({
            "frame": _number(frame),
            "normalizedTime": normalized,
            "name": event_name,
        })

    normalized_events.sort(key=lambda item: (item["normalizedTime"], item["frame"],
                                              item["name"]))
    return {
        "name": name,
        "frameStart": _number(start),
        "frameEnd": _number(end),
        "events": normalized_events,
    }


def build_payload(records, fps, marker_source):
    rate = _finite(fps, "fps")
    if rate <= 0.0:
        raise ValueError("fps must be positive")
    actions = {}
    names = set()
    for record in sorted(records, key=lambda item: item["name"]):
        name = record["name"]
        if name in names:
            raise ValueError("duplicate action name '%s' after prefix stripping" % name)
        names.add(name)
        actions[name] = {
            "frameStart": _number(record["frameStart"]),
            "frameEnd": _number(record["frameEnd"]),
            "events": record["events"],
        }
    return {
        "version": 1,
        "fps": _number(rate),
        "markerSource": marker_source,
        "actions": actions,
    }


def _strip_action_name(name, prefix):
    if prefix and name.startswith(prefix):
        name = name[len(prefix):]
    if not name:
        raise ValueError("action name becomes empty after stripping '%s'" % prefix)
    return name


def _markers_for_action(action, marker_source, scene):
    if marker_source == "scene":
        return list(scene.timeline_markers)
    return list(getattr(action, "pose_markers", []))


def collect_actions(action_names, include_empty, strip_prefix, marker_source):
    import bpy

    scene = bpy.context.scene
    requested = set(action_names)
    selected = []
    for action in sorted(bpy.data.actions, key=lambda item: item.name):
        output_name = _strip_action_name(action.name, strip_prefix)
        if requested and action.name not in requested and output_name not in requested:
            continue
        if requested:
            requested.discard(action.name)
            requested.discard(output_name)
        selected.append((action, output_name))

    if requested:
        raise ValueError("action(s) not found: %s" % ", ".join(sorted(requested)))

    records = []
    for action, output_name in selected:
        markers = _markers_for_action(action, marker_source, scene)
        if not markers and not include_empty:
            continue
        frame_start, frame_end = action.frame_range
        if frame_end <= frame_start:
            if markers:
                raise ValueError("action '%s' has an empty frame range" % action.name)
            print("export_events.py: skipping empty action '%s'" % action.name,
                  file=sys.stderr)
            continue
        events = []
        for marker in markers:
            events.append({"frame": marker.frame, "name": marker.name})
        records.append(make_action_record(output_name, frame_start, frame_end, events))
    return records, scene


def write_sidecar(path, payload):
    parent = os.path.dirname(os.path.abspath(path))
    if parent:
        os.makedirs(parent, exist_ok=True)
    with open(path, "w", encoding="utf-8", newline="\n") as handle:
        json.dump(payload, handle, ensure_ascii=False, indent=2)
        handle.write("\n")


def _self_test():
    assert abs(frame_to_normalized(15, 0, 30) - 0.5) < 1e-9
    assert abs(frame_to_normalized(6, 0, 24) - 0.25) < 1e-9
    try:
        frame_to_normalized(31, 0, 30)
    except ValueError:
        pass
    else:
        raise AssertionError("out-of-range frame was accepted")

    record = make_action_record("Walk", 0, 30, [
        {"frame": 15, "name": "Footstep.Right"},
        {"frame": 0, "name": "Footstep.Left"},
    ])
    payload = build_payload([record], 24, "pose")
    events = payload["actions"]["Walk"]["events"]
    assert events[0]["name"] == "Footstep.Left"
    assert abs(events[1]["normalizedTime"] - 0.5) < 1e-9
    print("export_events.py self-test: ok")


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="Export Blender pose markers and Action frame ranges for anim_pack.")
    parser.add_argument("-o", "--output", help="output event sidecar JSON")
    parser.add_argument("--action", action="append", default=[],
                        help="export only this action (repeatable)")
    parser.add_argument("--all-actions", action="store_true",
                        help="include actions without markers")
    parser.add_argument("--strip-prefix", "--strip-action-prefix",
                        dest="strip_prefix", default="",
                        help="strip this prefix from Blender action names")
    parser.add_argument("--marker-source", choices=("pose", "scene"), default="pose",
                        help="pose markers (default) or scene timeline markers")
    parser.add_argument("--fps", type=float, help="override scene FPS")
    parser.add_argument("--self-test", action="store_true",
                        help="run pure-Python checks without Blender")
    args = parser.parse_args(argv)

    if args.self_test:
        _self_test()
        return 0

    if not args.output:
        parser.error("--output is required")

    try:
        import bpy
    except ImportError:
        print("export_events.py must run inside Blender (bpy is unavailable)",
              file=sys.stderr)
        return 2

    try:
        records, scene = collect_actions(args.action, args.all_actions,
                                         args.strip_prefix, args.marker_source)
        fps = args.fps if args.fps is not None else (
            scene.render.fps / scene.render.fps_base)
        payload = build_payload(records, fps, args.marker_source)
        write_sidecar(args.output, payload)
    except Exception as error:
        print("export_events.py: %s" % error, file=sys.stderr)
        return 1

    print("export_events.py: %d action(s), %d event(s) -> %s" % (
        len(payload["actions"]),
        sum(len(action["events"]) for action in payload["actions"].values()),
        args.output))
    return 0


def _script_arguments():
    if "--" in sys.argv:
        return sys.argv[sys.argv.index("--") + 1:]
    return sys.argv[1:]


if __name__ == "__main__":
    sys.exit(main(_script_arguments()))
