#!/usr/bin/env python3
"""Generate a calibrated AnimancerCpp avatar.json from a Unity .fbx.meta.

No Unity Editor is required. The humanoid axis setup is a direct port of the
Mecanim algorithms in Unity 4.7's Runtime/mecanim sources:

  human::HumanSetupAxes / hand::HandSetupAxes
  skeleton::SetupAxes
  math::quatMatrixToQuat

The output contains local pre/post rotations, Unity's per-axis limits and the
UVW muscle binding used by the ZYRoll composition.

Usage:
  py -3 tools/unity_humanoid_axes.py \
      --meta "Assets/.../DefaultHumanoid.fbx.meta" \
      --unity-source E:/Learn/Unity4.7 \
      --out golden/DefaultHumanoid.avatar.json
"""

from __future__ import annotations

import argparse
import json
import math
import re
from pathlib import Path

BODY_NAMES = [
    "Hips", "LeftUpperLeg", "RightUpperLeg", "LeftLowerLeg", "RightLowerLeg",
    "LeftFoot", "RightFoot", "Spine", "Chest", "Neck", "Head",
    "LeftShoulder", "RightShoulder", "LeftUpperArm", "RightUpperArm",
    "LeftLowerArm", "RightLowerArm", "LeftHand", "RightHand",
    "LeftToes", "RightToes", "LeftEye", "RightEye", "Jaw",
]

BODY_ROLES = {
    "Hips": [None, None, None],
    "LeftUpperLeg": ["Left Upper Leg Front-Back", "Left Upper Leg In-Out", "Left Upper Leg Twist In-Out"],
    "RightUpperLeg": ["Right Upper Leg Front-Back", "Right Upper Leg In-Out", "Right Upper Leg Twist In-Out"],
    "LeftLowerLeg": ["Left Lower Leg Stretch", None, "Left Lower Leg Twist In-Out"],
    "RightLowerLeg": ["Right Lower Leg Stretch", None, "Right Lower Leg Twist In-Out"],
    "LeftFoot": ["Left Foot Up-Down", "Left Foot Twist In-Out", None],
    "RightFoot": ["Right Foot Up-Down", "Right Foot Twist In-Out", None],
    "Spine": ["Spine Front-Back", "Spine Left-Right", "Spine Twist Left-Right"],
    "Chest": ["Chest Front-Back", "Chest Left-Right", "Chest Twist Left-Right"],
    "Neck": ["Neck Nod Down-Up", "Neck Tilt Left-Right", "Neck Turn Left-Right"],
    "Head": ["Head Nod Down-Up", "Head Tilt Left-Right", "Head Turn Left-Right"],
    "LeftShoulder": ["Left Shoulder Down-Up", "Left Shoulder Front-Back", None],
    "RightShoulder": ["Right Shoulder Down-Up", "Right Shoulder Front-Back", None],
    "LeftUpperArm": ["Left Arm Down-Up", "Left Arm Front-Back", "Left Arm Twist In-Out"],
    "RightUpperArm": ["Right Arm Down-Up", "Right Arm Front-Back", "Right Arm Twist In-Out"],
    "LeftLowerArm": ["Left Forearm Stretch", None, "Left Forearm Twist In-Out"],
    "RightLowerArm": ["Right Forearm Stretch", None, "Right Forearm Twist In-Out"],
    "LeftHand": ["Left Hand Down-Up", "Left Hand In-Out", None],
    "RightHand": ["Right Hand Down-Up", "Right Hand In-Out", None],
    "LeftToes": ["Left Toes Up-Down", None, None],
    "RightToes": ["Right Toes Up-Down", None, None],
    "LeftEye": ["Left Eye Down-Up", "Left Eye In-Out", None],
    "RightEye": ["Right Eye Down-Up", "Right Eye In-Out", None],
    "Jaw": ["Jaw Close", "Jaw Left-Right", None],
}

FINGER_NAMES = ["Thumb", "Index", "Middle", "Ring", "Little"]
PHALANGE_NAMES = ["Proximal", "Intermediate", "Distal"]
FINGER_ROLES = {
    "Proximal": [None, None, None],  # filled from the side/finger
    "Intermediate": [None, None, None],
    "Distal": [None, None, None],
}


def parse_axes_table(path: Path, marker: str = "setupAxesInfoArray") -> list[dict]:
    text = path.read_text(encoding="utf-8", errors="ignore")
    start = text.index(marker)
    end = text.index("};", start)
    block = text[start:end]
    pattern = re.compile(
        r"\{\{([^}]*)\},\{([^}]*)\},\{([^}]*)\},\{([^}]*)\},\{([^}]*)\},math::kZYRoll,(-?\d+)\}"
    )
    rows = []
    for match in pattern.finditer(block):
        values = [
            [float(v.strip().rstrip("f")) for v in group.split(",")]
            for group in match.groups()[:-1]
        ]
        rows.append({
            "pre": values[0],
            "main": values[1],
            "min": values[2],
            "max": values[3],
            "sign": values[4],
            "force": int(match.group(6)),
        })
    if not rows:
        raise ValueError("no SetupAxesInfo rows found in " + str(path))
    return rows


def parse_meta(path: Path):
    text = path.read_text(encoding="utf-8")
    human_start = text.index("\n    human:")
    skeleton_start = text.index("\n    skeleton:", human_start)
    skeleton_end = text.index("\n    armTwist:", skeleton_start)
    human_block = text[human_start:skeleton_start]
    skeleton_block = text[skeleton_start:skeleton_end]

    human = []
    for match in re.finditer(
        r"- boneName:\s*(\S+)\s*\n\s*humanName:\s*(.+?)\s*\n", human_block
    ):
        human.append((match.group(1), match.group(2)))

    entry_pattern = re.compile(
        r"- name:[ \t]*(.*?)[ \t]*\n"
        r"[ \t]*parentName:[ \t]*(.*?)[ \t]*\n"
        r"[ \t]*position:\s*\{([^}]*)\}[ \t]*\n"
        r"[ \t]*rotation:\s*\{([^}]*)\}[ \t]*\n"
        r"[ \t]*scale:\s*\{([^}]*)\}",
        re.S,
    )

    def read_vec(value: str):
        parsed = {}
        for part in value.split(","):
            key, number = part.split(":", 1)
            parsed[key.strip()] = float(number.strip())
        return [parsed.get("x", 0.0), parsed.get("y", 0.0), parsed.get("z", 0.0)]

    def read_quat(value: str):
        parsed = {}
        for part in value.split(","):
            key, number = part.split(":", 1)
            parsed[key.strip()] = float(number.strip())
        return [
            parsed.get("x", 0.0), parsed.get("y", 0.0),
            parsed.get("z", 0.0), parsed.get("w", 1.0),
        ]

    skeleton = [
        {
            "name": match.group(1),
            "parent_name": match.group(2),
            "position": read_vec(match.group(3)),
            "rotation": read_quat(match.group(4)),
            "scale": read_vec(match.group(5)),
        }
        for match in entry_pattern.finditer(skeleton_block)
    ]
    return human, skeleton


def parse_twist_values(path: Path):
    text = path.read_text(encoding="utf-8")
    values = {}
    for key in ("armTwist", "foreArmTwist", "upperLegTwist", "legTwist"):
        match = re.search(r"\b" + key + r":\s*([-0-9.eE]+)", text)
        values[key] = float(match.group(1)) if match else 0.5
    return values


def q_mul(a, b):
    x1, y1, z1, w1 = a
    x2, y2, z2, w2 = b
    return [
        w1 * x2 + x1 * w2 + y1 * z2 - z1 * y2,
        w1 * y2 - x1 * z2 + y1 * w2 + z1 * x2,
        w1 * z2 + x1 * y2 - y1 * x2 + z1 * w2,
        w1 * w2 - x1 * x2 - y1 * y2 - z1 * z2,
    ]


def q_conj(q):
    return [-q[0], -q[1], -q[2], q[3]]


def q_norm(q):
    length = math.sqrt(sum(v * v for v in q))
    if length <= 1e-12:
        return [0.0, 0.0, 0.0, 1.0]
    return [v / length for v in q]


def v_norm(v):
    length = math.sqrt(sum(x * x for x in v[:3]))
    if length <= 1e-12:
        return [0.0, 0.0, 0.0, 0.0]
    return [v[0] / length, v[1] / length, v[2] / length, 0.0]


def cross(a, b):
    return [
        a[1] * b[2] - a[2] * b[1],
        a[2] * b[0] - a[0] * b[2],
        a[0] * b[1] - a[1] * b[0],
        0.0,
    ]


def matrix_to_quat(u, v, w):
    # Port of math::quatMatrixToQuat (Unity 4.7), with (x,y,z,w) storage.
    if u[0] >= 0.0:
        t = v[1] + w[2]
        if t >= 0.0:
            q = [v[2] - w[1], w[0] - u[2], u[1] - v[0], 1.0 + u[0] + t]
        else:
            q = [1.0 + u[0] - t, u[1] + v[0], w[0] + u[2], v[2] - w[1]]
    else:
        t = v[1] - w[2]
        if t >= 0.0:
            q = [u[1] + v[0], 1.0 - u[0] + t, v[2] + w[1], w[0] - u[2]]
        else:
            q = [w[0] + u[2], v[2] + w[1], 1.0 - u[0] - t, u[1] - v[0]]
    return q_norm(q)


def compute_global(skeleton):
    by_name = {bone["name"]: i for i, bone in enumerate(skeleton)}
    for bone in skeleton:
        bone["parent"] = by_name.get(bone["parent_name"], -1)
    global_pose = [None] * len(skeleton)

    def ensure(index):
        if global_pose[index] is not None:
            return global_pose[index]
        bone = skeleton[index]
        rotation = q_norm(bone["rotation"])
        translation = bone["position"]
        parent = bone["parent"]
        if parent < 0:
            global_pose[index] = (rotation, translation)
            return global_pose[index]
        parent_rotation, parent_translation = ensure(parent)
        rotation = q_norm(q_mul(parent_rotation, rotation))
        rotated = q_mul(q_mul(parent_rotation, translation + [0.0]), q_conj(parent_rotation))
        global_pose[index] = (
            rotation,
            [parent_translation[i] + rotated[i] for i in range(3)],
        )
        return global_pose[index]

    for i in range(len(skeleton)):
        ensure(i)
    return by_name, global_pose


def setup_axes(axes, bone_index, axis_index, left, length, skeleton, global_pose):
    pre = list(axes["pre"])
    main = axes["main"] + [0.0]
    sign = list(axes["sign"])
    if not left:
        sign = [sign[0] * -1.0, sign[1], sign[2] * -1.0, sign[3]]
        pre = [pre[0], pre[1], pre[2], pre[3] * -1.0]

    bone = global_pose[bone_index]
    u = [1.0, 0.0, 0.0, 0.0]
    w = main
    v = [0.0, 1.0, 0.0, 0.0]

    if axis_index is not None:
        child = global_pose[axis_index]
        direction = [
            (child[1][0] - bone[1][0]) * length,
            (child[1][1] - bone[1][1]) * length,
            (child[1][2] - bone[1][2]) * length,
            0.0,
        ]
        u = v_norm(direction)
        w = main
        v = v_norm(cross(w, u))
        w = cross(u, v)

    if axes["force"]:
        forced = {
            1: [1.0, 0.0, 0.0, 0.0],
            -1: [-1.0, 0.0, 0.0, 0.0],
            2: [0.0, 1.0, 0.0, 0.0],
            -2: [0.0, -1.0, 0.0, 0.0],
            3: [0.0, 0.0, 1.0, 0.0],
        }.get(axes["force"], [0.0, 0.0, -1.0, 0.0])
        u = forced
        w = main
        v = v_norm(cross(w, u))
        w = cross(u, v)

    pre_global = matrix_to_quat(u, v, w)
    parent_index = skeleton[bone_index]["parent"]
    parent_rotation = (
        global_pose[parent_index][0] if parent_index >= 0 else [0.0, 0.0, 0.0, 1.0]
    )
    zero_global = q_mul(pre, pre_global)

    local_rest = skeleton[bone_index]["rotation"]
    local_pre = q_norm(q_mul(q_conj(local_rest), q_mul(q_conj(parent_rotation), zero_global)))
    # FromAxes returns pre * q * conj(post), where Unity's raw post is
    # conj(boneQ) * frame. Our runtime uses rest * local_pre * q * local_post,
    # so local_post is conj(frame) * boneQ (zeroQ is already in local_pre).
    local_post = q_norm(q_mul(q_conj(pre_global), q_mul(parent_rotation, local_rest)))

    source_pre = q_norm(
        q_mul(q_conj(parent_rotation), q_mul(pre, pre_global))
    )
    source_post = q_norm(q_mul(q_conj(bone[0]), pre_global))
    if axis_index is not None:
        child_position = global_pose[axis_index][1]
        axis_length = math.sqrt(sum(
            (child_position[i] - bone[1][i]) ** 2 for i in range(3)
        )) * abs(length)
    else:
        axis_length = abs(length)

    source_axes = {
        "preQ": source_pre,
        "postQ": source_post,
        "sign": sign[:3],
        # SetupAxes stores limits in radians.
        "limitMin": [math.radians(v) for v in axes["min"]],
        "limitMax": [math.radians(v) for v in axes["max"]],
        "length": axis_length,
        "type": 1,  # kZYRoll for both human.cpp and hand.cpp tables.
    }
    return local_pre, local_post, sign, source_axes


def canonical_finger(side, finger, phalange):
    # AnimancerCpp HumanBone spelling, e.g. LeftThumbProximal.
    suffix = {0: "Proximal", 1: "Intermediate", 2: "Distal"}[phalange]
    return "{}{}{}".format(side, FINGER_NAMES[finger], suffix)


def meta_finger(side, finger, phalange):
    # .fbx.meta HumanDescription spelling, e.g. "Left Thumb Proximal".
    return "{} {} {}".format(
        side, FINGER_NAMES[finger], PHALANGE_NAMES[phalange]
    )


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--meta", required=True)
    parser.add_argument("--unity-source", required=True)
    parser.add_argument("--out", required=True)
    parser.add_argument("--skeleton-out", default="")
    args = parser.parse_args()

    meta = Path(args.meta)
    source = Path(args.unity_source)
    human, skeleton = parse_meta(meta)
    twists = parse_twist_values(meta)
    by_name, global_pose = compute_global(skeleton)
    human_map = {human_name: bone_name for bone_name, human_name in human}
    body_axes = parse_axes_table(source / "Runtime/mecanim/human/human.cpp")
    hand_axes = parse_axes_table(source / "Runtime/mecanim/human/hand.cpp")

    result = {
        "name": meta.name.split(".")[0],
        "hipsHeight": global_pose[by_name[human_map["Hips"]]][1][1],
        "armTwist": twists["armTwist"],
        "foreArmTwist": twists["foreArmTwist"],
        "upperLegTwist": twists["upperLegTwist"],
        "legTwist": twists["legTwist"],
        "bones": {},
        "preRot": {},
        "postRot": {},
        "uvw": {},
        "axes": {},
        "limitsOverride": {},
    }

    def add_bone(canonical, bone_name, pre, post, signs, axes, roles, source_axes):
        result["bones"][canonical] = bone_name
        result["preRot"][canonical] = pre
        result["postRot"][canonical] = post
        result["axes"][canonical] = source_axes
        slots = []
        # Human2SkeletonPose passes (DoF[2], DoF[1], DoF[0]) as UVW.
        for slot in range(3):
            role_index = 2 - slot
            muscle = roles[role_index]
            if muscle is None:
                slots.append(None)
                continue
            slots.append({
                "muscle": muscle,
                "sign": signs[slot],
                "min": axes["min"][role_index],
                "max": axes["max"][role_index],
            })
            result["limitsOverride"][muscle] = [
                axes["min"][role_index],
                axes["max"][role_index],
            ]
        result["uvw"][canonical] = slots

    for index, name in enumerate(BODY_NAMES):
        if name not in human_map:
            continue
        bone_name = human_map[name]
        bone_index = by_name[bone_name]
        child_name = None
        length = 1.0
        if name == "Head":
            child_name = human_map.get("Neck")
            length = -1.0
        elif name in ("LeftFoot", "RightFoot"):
            length = -global_pose[bone_index][1][1]
        elif name in ("LeftHand", "RightHand"):
            child_name = human_map.get("LeftLowerArm" if name == "LeftHand" else "RightLowerArm")
            length = -0.5
        elif name in ("LeftToes", "RightToes"):
            child_name = human_map.get("LeftFoot" if name == "LeftToes" else "RightFoot")
            length = 0.5
        elif name == "Hips":
            child_name = human_map.get("Spine")
        elif name == "LeftUpperLeg":
            child_name = human_map.get("LeftLowerLeg")
        elif name == "RightUpperLeg":
            child_name = human_map.get("RightLowerLeg")
        elif name == "LeftLowerLeg":
            child_name = human_map.get("LeftFoot")
        elif name == "RightLowerLeg":
            child_name = human_map.get("RightFoot")
        elif name == "Spine":
            child_name = human_map.get("Chest") or human_map.get("Neck") or human_map.get("Head")
        elif name == "Chest":
            child_name = human_map.get("Neck") or human_map.get("Head")
        elif name == "Neck":
            child_name = human_map.get("Head")
        elif name == "LeftShoulder":
            child_name = human_map.get("LeftUpperArm")
        elif name == "RightShoulder":
            child_name = human_map.get("RightUpperArm")
        elif name == "LeftUpperArm":
            child_name = human_map.get("LeftLowerArm")
        elif name == "RightUpperArm":
            child_name = human_map.get("RightLowerArm")
        elif name == "LeftLowerArm":
            child_name = human_map.get("LeftHand")
        elif name == "RightLowerArm":
            child_name = human_map.get("RightHand")

        axis_index = by_name[child_name] if child_name else None
        pre, post, signs, source_axes = setup_axes(
            body_axes[index], bone_index, axis_index, True, length, skeleton, global_pose
        )
        add_bone(name, bone_name, pre, post, signs, body_axes[index],
                 BODY_ROLES[name], source_axes)

    for side in ("Left", "Right"):
        for finger, finger_name in enumerate(FINGER_NAMES):
            for phalange, phalange_name in enumerate(PHALANGE_NAMES):
                canonical = canonical_finger(side, finger, phalange)
                meta_name = meta_finger(side, finger, phalange)
                if meta_name not in human_map:
                    continue
                bone_name = human_map[meta_name]
                bone_index = by_name[bone_name]

                next_meta = (
                    meta_finger(side, finger, phalange + 1)
                    if phalange + 1 < len(PHALANGE_NAMES) else None
                )
                prev_meta = (
                    meta_finger(side, finger, phalange - 1)
                    if phalange > 0 else None
                )
                if next_meta and next_meta in human_map:
                    axis_index = by_name[human_map[next_meta]]
                    length = 1.0
                elif prev_meta and prev_meta in human_map:
                    axis_index = by_name[human_map[prev_meta]]
                    length = -0.75
                else:
                    axis_index = None
                    length = 1.0

                if phalange == 0:
                    roles = [
                        "{}Hand.{}.1 Stretched".format(side, finger_name),
                        "{}Hand.{}.Spread".format(side, finger_name),
                        None,
                    ]
                else:
                    roles = [
                        "{}Hand.{}.{} Stretched".format(side, finger_name, phalange + 1),
                        None,
                        None,
                    ]

                table_index = finger * len(PHALANGE_NAMES) + phalange
                pre, post, signs, source_axes = setup_axes(
                    hand_axes[table_index], bone_index, axis_index,
                    side == "Left", length, skeleton, global_pose
                )
                add_bone(canonical, bone_name, pre, post, signs,
                         hand_axes[table_index], roles, source_axes)

    output = Path(args.out)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    if args.skeleton_out:
        skeleton_out = {
            "bones": [
                {
                    "name": bone["name"],
                    "parent": bone["parent"],
                    "translation": bone["position"],
                    "rotation": bone["rotation"],
                    "scale": bone["scale"],
                }
                for bone in skeleton
            ]
        }
        skeleton_path = Path(args.skeleton_out)
        skeleton_path.parent.mkdir(parents=True, exist_ok=True)
        skeleton_path.write_text(json.dumps(skeleton_out, indent=2) + "\n", encoding="utf-8")
    print("wrote", output, "bones", len(result["bones"]), "muscles", len(result["limitsOverride"]))


if __name__ == "__main__":
    main()
