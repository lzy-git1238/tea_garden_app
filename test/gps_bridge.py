#!/usr/bin/env python3
"""GPS 桥接：订阅 PX4 VehicleGlobalPosition，输出 lat/lon/alt。
用法: python3 gps_bridge.py /fmu/out/vehicle_global_position
输出: lat:30.123\nlon:120.456\nalt:100.0\n
"""
import sys
import struct
import rclpy
from rclpy.node import Node
from rclpy.qos import qos_profile_sensor_data

# VehicleGlobalPosition CDR layout (little-endian):
#   uint64 timestamp
#   uint64 timestamp_sample
#   float64 lat        ← offset 16
#   float64 lon        ← offset 24
#   float64 alt        ← offset 32
#   float64 terrain_alt
#   float64 lat_lon_valid
#   ...

TOPIC = sys.argv[1] if len(sys.argv) > 1 else "/fmu/out/vehicle_global_position"

rclpy.init(args=sys.argv)
node = Node("gps_bridge")
gps_data = {"lat": 0.0, "lon": 0.0, "alt": 0.0}


def callback(msg):
    try:
        data = msg.tobytes() if hasattr(msg, 'tobytes') else bytes(msg)
        lat, lon, alt = struct.unpack_from("<ddd", data, offset=16)
        gps_data["lat"] = lat
        gps_data["lon"] = lon
        gps_data["alt"] = alt
    except Exception:
        pass


# Raw subscription: receive serialized CDR bytes, parse manually
from rclpy.qos import QoSProfile, ReliabilityPolicy, DurabilityPolicy

qos = QoSProfile(depth=1, reliability=ReliabilityPolicy.BEST_EFFORT,
                 durability=DurabilityPolicy.VOLATILE)

sub = node.create_subscription(
    rclpy.msg.Raw, TOPIC, callback, qos
)

try:
    rclpy.spin_once(node, timeout_sec=3.0)  # wait for first message
except Exception:
    pass

print(f"lat:{gps_data['lat']}")
print(f"lon:{gps_data['lon']}")
print(f"alt:{gps_data['alt']}")

node.destroy_node()
rclpy.shutdown()
