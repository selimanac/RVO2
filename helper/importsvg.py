#!/usr/bin/env python3
import argparse
import math
import re
import sys
import xml.etree.ElementTree as ET


NUMBER = r"[-+]?(?:\d*\.\d+|\d+)(?:[eE][-+]?\d+)?"
PATH_TOKEN_RE = re.compile(rf"[MmLlHhVvZz]|{NUMBER}")
TRANSFORM_RE = re.compile(r"([a-zA-Z]+)\s*\(([^)]*)\)")


def local_name(tag):
    return tag.rsplit("}", 1)[-1]


def parse_numbers(text):
    return [float(value) for value in re.findall(NUMBER, text or "")]


def mat_mul(a, b):
    return (
        a[0] * b[0] + a[2] * b[1],
        a[1] * b[0] + a[3] * b[1],
        a[0] * b[2] + a[2] * b[3],
        a[1] * b[2] + a[3] * b[3],
        a[0] * b[4] + a[2] * b[5] + a[4],
        a[1] * b[4] + a[3] * b[5] + a[5],
    )


def transform_point(matrix, point):
    x, y = point
    a, b, c, d, e, f = matrix
    return (a * x + c * y + e, b * x + d * y + f)


def parse_transform(transform):
    matrix = (1.0, 0.0, 0.0, 1.0, 0.0, 0.0)

    for name, args in TRANSFORM_RE.findall(transform or ""):
        values = parse_numbers(args)
        name = name.lower()

        if name == "matrix" and len(values) == 6:
            next_matrix = tuple(values)
        elif name == "translate":
            tx = values[0] if values else 0.0
            ty = values[1] if len(values) > 1 else 0.0
            next_matrix = (1.0, 0.0, 0.0, 1.0, tx, ty)
        elif name == "scale":
            sx = values[0] if values else 1.0
            sy = values[1] if len(values) > 1 else sx
            next_matrix = (sx, 0.0, 0.0, sy, 0.0, 0.0)
        elif name == "rotate":
            angle = math.radians(values[0] if values else 0.0)
            cos_a = math.cos(angle)
            sin_a = math.sin(angle)
            rotation = (cos_a, sin_a, -sin_a, cos_a, 0.0, 0.0)

            if len(values) >= 3:
                cx, cy = values[1], values[2]
                next_matrix = mat_mul(
                    mat_mul((1.0, 0.0, 0.0, 1.0, cx, cy), rotation),
                    (1.0, 0.0, 0.0, 1.0, -cx, -cy),
                )
            else:
                next_matrix = rotation
        else:
            raise ValueError(f"Unsupported SVG transform: {name}({args})")

        matrix = mat_mul(matrix, next_matrix)

    return matrix


def signed_area(points):
    area = 0.0
    for i, (x1, y1) in enumerate(points):
        x2, y2 = points[(i + 1) % len(points)]
        area += x1 * y2 - x2 * y1
    return area * 0.5


def dedupe_closed(points, epsilon=1e-4):
    if len(points) > 1:
        first = points[0]
        last = points[-1]
        if abs(first[0] - last[0]) <= epsilon and abs(first[1] - last[1]) <= epsilon:
            points = points[:-1]
    return points


def parse_points_attr(points_text):
    values = parse_numbers(points_text)
    if len(values) % 2 != 0:
        raise ValueError("Odd number of values in points attribute")
    return list(zip(values[0::2], values[1::2]))


def rect_points(element):
    x = float(element.attrib.get("x", 0.0))
    y = float(element.attrib.get("y", 0.0))
    width = float(element.attrib.get("width", 0.0))
    height = float(element.attrib.get("height", 0.0))
    return [
        (x, y),
        (x + width, y),
        (x + width, y + height),
        (x, y + height),
    ]


def path_points(path_data):
    tokens = PATH_TOKEN_RE.findall(path_data or "")
    points = []
    cursor = (0.0, 0.0)
    start = None
    command = None
    i = 0

    def is_command(token):
        return len(token) == 1 and token.isalpha()

    def read_float():
        nonlocal i
        if i >= len(tokens) or is_command(tokens[i]):
            raise ValueError("Expected path number")
        value = float(tokens[i])
        i += 1
        return value

    while i < len(tokens):
        if is_command(tokens[i]):
            command = tokens[i]
            i += 1

        if command is None:
            raise ValueError("Path data starts without a command")

        absolute = command.isupper()
        op = command.upper()

        if op == "M":
            x = read_float()
            y = read_float()
            cursor = (x, y) if absolute else (cursor[0] + x, cursor[1] + y)
            start = cursor
            points.append(cursor)
            command = "L" if absolute else "l"

            while i < len(tokens) and not is_command(tokens[i]):
                x = read_float()
                y = read_float()
                cursor = (x, y) if absolute else (cursor[0] + x, cursor[1] + y)
                points.append(cursor)
        elif op == "L":
            while i < len(tokens) and not is_command(tokens[i]):
                x = read_float()
                y = read_float()
                cursor = (x, y) if absolute else (cursor[0] + x, cursor[1] + y)
                points.append(cursor)
        elif op == "H":
            while i < len(tokens) and not is_command(tokens[i]):
                x = read_float()
                cursor = (x, cursor[1]) if absolute else (cursor[0] + x, cursor[1])
                points.append(cursor)
        elif op == "V":
            while i < len(tokens) and not is_command(tokens[i]):
                y = read_float()
                cursor = (cursor[0], y) if absolute else (cursor[0], cursor[1] + y)
                points.append(cursor)
        elif op == "Z":
            if start is not None:
                points.append(start)
            command = None
        else:
            raise ValueError(
                f"Unsupported path command '{command}'. Convert curves to lines/polygons before importing."
            )

    return dedupe_closed(points)


def collect_shapes(element, parent_matrix):
    matrix = mat_mul(parent_matrix, parse_transform(element.attrib.get("transform")))
    tag = local_name(element.tag)
    shapes = []

    if tag == "rect":
        shapes.append([transform_point(matrix, point) for point in rect_points(element)])
    elif tag in ("polygon", "polyline"):
        shapes.append([transform_point(matrix, point) for point in parse_points_attr(element.attrib.get("points", ""))])
    elif tag == "path":
        shapes.append([transform_point(matrix, point) for point in path_points(element.attrib.get("d", ""))])

    for child in element:
        shapes.extend(collect_shapes(child, matrix))

    return shapes


def choose_shape(shapes, index):
    closed_shapes = [dedupe_closed(shape) for shape in shapes if len(dedupe_closed(shape)) >= 3]
    if not closed_shapes:
        raise ValueError("No polygon-like SVG shape found")

    if index is not None:
        return closed_shapes[index]

    return max(closed_shapes, key=lambda shape: abs(signed_area(shape)))


def enforce_winding(points, winding):
    # RVO2 evaluates winding in raw x/y math coordinates. For a negative
    # obstacle used as a map boundary, RVO2 wants clockwise: signed area < 0.
    area = signed_area(points)
    should_be_positive = winding == "ccw"

    if (area > 0.0) != should_be_positive:
        return list(reversed(points))

    return points


def emit_cpp(points, var_name, precision, include_obstacle, winding):
    if winding == "cw":
        print("// --- Generated RVO2 Boundary Vertices (clockwise: use for negative/map-boundary obstacle) ---")
    else:
        print("// --- Generated RVO2 Obstacle Vertices (counterclockwise: use for normal/internal obstacle) ---")
    print(f"{var_name}.clear();")

    for x, y in points:
        print(f"{var_name}.push_back(RVO::Vector2({x:.{precision}f}f, {y:.{precision}f}f));")

    if include_obstacle:
        print()
        print(f"sim->addObstacle({var_name});")
        print("sim->processObstacles();")


def main():
    parser = argparse.ArgumentParser(
        description="Convert an SVG rect/path/polygon/polyline boundary to RVO2 Vector2 vertices."
    )
    parser.add_argument("svg", help="SVG file to import")
    parser.add_argument("--shape-index", type=int, help="Use a specific polygon-like shape instead of the largest")
    parser.add_argument("--winding", choices=("cw", "ccw"), default="cw", help="Output winding in RVO2 raw x/y coordinates")
    parser.add_argument("--var", default="arena", help="C++ vector variable name")
    parser.add_argument("--precision", type=int, default=1, help="Decimal places in generated C++")
    parser.add_argument("--no-obstacle-code", action="store_true", help="Only emit push_back lines")
    args = parser.parse_args()

    root = ET.parse(args.svg).getroot()
    shapes = collect_shapes(root, (1.0, 0.0, 0.0, 1.0, 0.0, 0.0))
    points = enforce_winding(choose_shape(shapes, args.shape_index), args.winding)

    emit_cpp(points, args.var, args.precision, not args.no_obstacle_code, args.winding)
    print(
        f"\n// vertices={len(points)} winding={args.winding} signed_area={signed_area(points):.{args.precision}f}",
        file=sys.stderr,
    )


if __name__ == "__main__":
    main()
